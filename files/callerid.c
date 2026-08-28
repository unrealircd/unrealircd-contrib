/*** <<<MODULE MANAGER START>>>
module {
	documentation "https://git.0bin.xyz/pegasus/unrealircd-modules/src/branch/main/callerid";
	troubleshooting "In case of problems, check the module's README or open an issue on Forgejo";
	min-unrealircd-version "6.*";
	post-install-text {
		"The module is installed, now all you need to do is add a 'loadmodule' line to your config file:";
		"loadmodule \"third/callerid\";";
		"Then /rehash the IRCd.";
		"Optionally add a 'set { callerid { ... }; };' block - see the top of the module source for details.";
	}
}
*** <<<MODULE MANAGER END>>>
*/

/*
 * User mode +g (CALLERID) - server-side whitelist for private messages/notices.
 * Modeled after InspIRCd's m_callerid.cpp and Solanum/ircd-hybrid's um_callerid.c.
 *
 * ==[ MODULE LOADING AND CONFIGURATION ]==
 * loadmodule "third/callerid";
 * set {
 *         callerid {
 *                 maxaccepts 30;
 *                 cooldown 60;
 *                 operoverride no;
 *         };
 * };
 * All three directives are optional - if the block (or any directive) is
 * absent, the defaults shown above are used. operoverride controls whether
 * any oper can bypass +g; it defaults to "no", so nobody bypasses +g
 * unless the network explicitly opts in here.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 1, or (at your option)
 * any later version.
 */

#include "unrealircd.h"

/* None of these exist in Unreal's numeric.h, so they're defined here.
 * Values are verified byte-identical between InspIRCd's m_callerid.cpp and
 * Solanum's include/numeric.h (checked directly against both source trees),
 * and confirmed collision-free against Unreal's own numeric.h and every
 * bundled module. WeeChat's irc-protocol.c additionally has explicit
 * handling for ERR_TARGUMODEG/RPL_TARGNOTIFY (716/717) today - routed to
 * an error-styled, target-buffer-aware callback - versus the generic,
 * unstyled fallback a plain RPL_TEXT (304) line gets, which is what
 * motivated switching to these from an earlier RPL_TEXT-based version of
 * this module. ERR_OWNMODE (494) is Solanum's own numeric for the "your
 * own accept list is full, so they can't reply" case (its actual text
 * format wasn't available to check, so ours is custom for this module,
 * but the numeric itself matches Solanum's real precedent for this exact
 * situation in add_callerid_accept_for_source()).
 *
 * We send these ourselves rather than relying on Unreal's automatic
 * ERR_CANTSENDTOUSER (531) fallback for HOOKTYPE_CAN_SEND_TO_USER denials
 * (message.c would otherwise send that whenever *errmsg is set on a
 * HOOK_DENY) - so *errmsg is deliberately left unset on every HOOK_DENY
 * path below, to avoid sending both.
 */
#define RPL_ACCEPTLIST   281
#define RPL_ENDOFACCEPT  282
#define ERR_ACCEPTFULL   456
#define ERR_ACCEPTEXIST  457
#define ERR_ACCEPTNOT    458
#define ERR_OWNMODE      494
#define ERR_TARGUMODEG   716
#define RPL_TARGNOTIFY   717
#define RPL_UMODEGMSG    718

/* Config defaults, used whenever set::callerid (or a directive within it)
 * is absent from the conf.
 */
#define CALLERID_DEFAULT_MAXACCEPT   30     /* max entries on a user's accept list */
#define CALLERID_DEFAULT_COOLDOWN    60     /* seconds between "X is messaging you" notices */
#define CALLERID_DEFAULT_OPEROVERRIDE 0     /* opers don't bypass +g unless the network opts in */

struct {
	int maxaccepts;
	long cooldown;
	int operoverride;
} cfg;

ModuleHeader MOD_HEADER = {
	"third/callerid",
	"1.0",
	"User Mode +g (CALLERID / server-side whitelist)",
	"PeGaSuS",
	"unrealircd-6",
};

/* A node on a client's accept list / "who lists me" list. */
typedef struct CallerIDEntry CallerIDEntry;
struct CallerIDEntry {
	CallerIDEntry *prev, *next;
	Client *client;
};

/* Per-client moddata payload. */
typedef struct CallerIDData CallerIDData;
struct CallerIDData {
	Client *owner;              /* the client this data belongs to - lets the
	                             * ModData destructor reach the same two-sided
	                             * unlink routine the quit hooks use */
	CallerIDEntry *accepting;   /* people I accept messages from */
	CallerIDEntry *wholistsme;  /* people who have me on their accept list */
	time_t last_notify;         /* last time we sent this client the "X is messaging you" notice */
};

#define CALLERIDDATA(acptr) ((CallerIDData *)moddata_client(acptr, callerid_md).ptr)

long UMODE_CALLERID = 0L;
ModDataInfo *callerid_md = NULL;
static ISupport *callerid_isupport_accept = NULL;

CMD_FUNC(cmd_accept);
int callerid_can_send_to_user(Client *client, Client *target, const char **text, const char **errmsg, SendType sendtype, ClientContext *clictx);
int callerid_local_quit(Client *client, MessageTag *mtags, const char *comment);
int callerid_remote_quit(Client *client, MessageTag *mtags, const char *comment);
void callerid_md_free(ModData *md);
int callerid_config_test(ConfigFile *cf, ConfigEntry *ce, int type, int *errs);
int callerid_config_run(ConfigFile *cf, ConfigEntry *ce, int type);

static CallerIDData *callerid_get(Client *acptr, int create);
static void callerid_remove_from_all_accepts(Client *who);
static int callerid_on_accept_list(Client *source, Client *target);
static void callerid_list_add(Client *client, Client *target);
static void init_config(void);

MOD_TEST()
{
	HookAdd(modinfo->handle, HOOKTYPE_CONFIGTEST, 0, callerid_config_test);
	return MOD_SUCCESS;
}

MOD_INIT()
{
	ModDataInfo mreq;
	char accept_str[16];

	init_config();

	memset(&mreq, 0, sizeof(mreq));
	mreq.name = "callerid";
	mreq.type = MODDATATYPE_CLIENT;
	mreq.free = callerid_md_free;
	callerid_md = ModDataAdd(modinfo->handle, mreq);
	if (!callerid_md)
	{
		config_error("could not register callerid moddata");
		return MOD_FAILED;
	}

	UmodeAdd(modinfo->handle, 'g', UMODE_GLOBAL, 0, umode_allow_all, &UMODE_CALLERID);

	CommandAdd(modinfo->handle, "ACCEPT", cmd_accept, MAXPARA, CMD_USER);

	/* +g is a UMODE_GLOBAL mode (propagated network-wide), so every server
	 * on the network needs this module loaded for consistent behavior -
	 * mark it as such so Unreal can warn on mismatched loads.
	 */
	MARK_AS_GLOBAL_MODULE(modinfo);

	/* ISUPPORT: CALLERID=g tells clients which mode letter gates messages,
	 * ACCEPT=<n> tells them the accept-list size limit. ACCEPT's value is
	 * re-published from callerid_config_run() once set::callerid is parsed,
	 * since MOD_INIT runs before config directives are applied.
	 */
	ISupportAdd(modinfo->handle, "CALLERID", "g");
	ircsnprintf(accept_str, sizeof(accept_str), "%d", cfg.maxaccepts);
	callerid_isupport_accept = ISupportAdd(modinfo->handle, "ACCEPT", accept_str);

	HookAdd(modinfo->handle, HOOKTYPE_CAN_SEND_TO_USER, 0, callerid_can_send_to_user);
	HookAdd(modinfo->handle, HOOKTYPE_LOCAL_QUIT, 0, callerid_local_quit);
	HookAdd(modinfo->handle, HOOKTYPE_REMOTE_QUIT, 0, callerid_remote_quit);
	HookAdd(modinfo->handle, HOOKTYPE_CONFIGRUN, 0, callerid_config_run);

	return MOD_SUCCESS;
}

MOD_LOAD()
{
	return MOD_SUCCESS;
}

MOD_UNLOAD()
{
	return MOD_SUCCESS;
}

/** Reset config to compile-time defaults. Called at MOD_INIT so a rehash
 * that removes the set::callerid block reverts to defaults rather than
 * keeping stale values around.
 */
static void init_config(void)
{
	memset(&cfg, 0, sizeof(cfg));
	cfg.maxaccepts = CALLERID_DEFAULT_MAXACCEPT;
	cfg.cooldown = CALLERID_DEFAULT_COOLDOWN;
	cfg.operoverride = CALLERID_DEFAULT_OPEROVERRIDE;
}

/** HOOKTYPE_CONFIGTEST: validate set::callerid before it's applied. */
int callerid_config_test(ConfigFile *cf, ConfigEntry *ce, int type, int *errs)
{
	int errors = 0;
	ConfigEntry *cep;
	int has_maxaccepts = 0, has_cooldown = 0, has_operoverride = 0;

	if (type != CONFIG_SET)
		return 0;

	if (!ce || !ce->name || strcmp(ce->name, "callerid"))
		return 0;

	for (cep = ce->items; cep; cep = cep->next)
	{
		if (!cep->value)
		{
			config_error("%s:%i: set::callerid::%s with no value",
			             cep->file->filename, cep->line_number, cep->name);
			errors++;
		} else if (!strcmp(cep->name, "maxaccepts"))
		{
			const char *p;
			int valid = (cep->value[0] != '\0');

			config_detect_duplicate(&has_maxaccepts, cep, &errors);

			/* Full-string digit check - atoi() alone would silently
			 * accept garbage like '30abc' as 30.
			 */
			for (p = cep->value; *p; p++)
			{
				if (!isdigit(*p))
				{
					valid = 0;
					break;
				}
			}

			if (!valid)
			{
				config_error("%s:%i: set::callerid::maxaccepts must be a positive integer (got: '%s')",
				             cep->file->filename, cep->line_number, cep->value);
				errors++;
			} else if (atoi(cep->value) < 1)
			{
				config_error("%s:%i: set::callerid::maxaccepts must be at least 1 (got: %s)",
				             cep->file->filename, cep->line_number, cep->value);
				errors++;
			}
		} else if (!strcmp(cep->name, "cooldown"))
		{
			long v;

			config_detect_duplicate(&has_cooldown, cep, &errors);

			/* config_checkval() with CFG_TIME is a unit-conversion utility,
			 * not a policy check - it happily converts a negative duration,
			 * so the range still needs checking here.
			 */
			v = config_checkval(cep->value, CFG_TIME);
			if (v < 0)
			{
				config_error("%s:%i: set::callerid::cooldown must be 0 or a positive duration (got: '%s')",
				             cep->file->filename, cep->line_number, cep->value);
				errors++;
			}
		} else if (!strcmp(cep->name, "operoverride"))
		{
			config_detect_duplicate(&has_operoverride, cep, &errors);
		} else
		{
			config_error("%s:%i: unknown directive set::callerid::%s",
			             cep->file->filename, cep->line_number, cep->name);
			errors++;
		}
	}

	*errs = errors;
	return errors ? -1 : 1;
}

/** HOOKTYPE_CONFIGRUN: apply set::callerid. */
int callerid_config_run(ConfigFile *cf, ConfigEntry *ce, int type)
{
	ConfigEntry *cep;

	if (type != CONFIG_SET)
		return 0;

	if (!ce || !ce->name || strcmp(ce->name, "callerid"))
		return 0;

	for (cep = ce->items; cep; cep = cep->next)
	{
		if (!strcmp(cep->name, "maxaccepts"))
		{
			char accept_str[16];

			cfg.maxaccepts = atoi(cep->value);
			ircsnprintf(accept_str, sizeof(accept_str), "%d", cfg.maxaccepts);
			if (callerid_isupport_accept)
				ISupportSetValue(callerid_isupport_accept, accept_str);
		} else if (!strcmp(cep->name, "cooldown"))
		{
			cfg.cooldown = config_checkval(cep->value, CFG_TIME);
		} else if (!strcmp(cep->name, "operoverride"))
		{
			cfg.operoverride = config_checkval(cep->value, CFG_YESNO);
		}
	}

	return 1;
}

/** Get (and optionally create) the CallerIDData for a client. */
static CallerIDData *callerid_get(Client *acptr, int create)
{
	CallerIDData *dat = CALLERIDDATA(acptr);
	if (!dat && create)
	{
		dat = safe_alloc(sizeof(CallerIDData));
		dat->owner = acptr;
		moddata_client(acptr, callerid_md).ptr = dat;
	}
	return dat;
}

/** Is 'source' present on 'target's accept list? */
static int callerid_on_accept_list(Client *source, Client *target)
{
	CallerIDData *dat = callerid_get(target, 0);
	CallerIDEntry *e;

	if (!dat)
		return 0;

	for (e = dat->accepting; e; e = e->next)
	{
		if (e->client == source)
			return 1;
	}
	return 0;
}

/** Add 'target' to 'client's accept list, and register 'client' in
 * 'target's wholistsme list. Does not check for duplicates or list size -
 * callers that need those checks (the ACCEPT command) should check first.
 */
static void callerid_list_add(Client *client, Client *target)
{
	CallerIDData *dat = callerid_get(client, 1);
	CallerIDData *tdat = callerid_get(target, 1);
	CallerIDEntry *e, *te;

	e = safe_alloc(sizeof(CallerIDEntry));
	e->client = target;
	AddListItemUnchecked(e, dat->accepting);

	te = safe_alloc(sizeof(CallerIDEntry));
	te->client = client;
	AddListItemUnchecked(te, tdat->wholistsme);
}

/** Remove 'who' from every accept list that references it, and free its
 * own accept/wholistsme state. Called on quit (local and remote).
 */
static void callerid_remove_from_all_accepts(Client *who)
{
	CallerIDData *dat = callerid_get(who, 0);
	CallerIDEntry *e, *e_next;

	if (!dat)
		return;

	/* Walk the list of people who accept 'who', and strip 'who' out of
	 * their 'accepting' list. This mirrors InspIRCd's wholistsme walk -
	 * it's what makes cleanup O(list length) instead of O(all users).
	 */
	for (e = dat->wholistsme; e; e = e_next)
	{
		CallerIDData *otherdat = callerid_get(e->client, 0);
		CallerIDEntry *oe, *oe_next;

		e_next = e->next;

		if (otherdat)
		{
			for (oe = otherdat->accepting; oe; oe = oe_next)
			{
				oe_next = oe->next;
				if (oe->client == who)
				{
					DelListItemUnchecked(oe, otherdat->accepting);
					safe_free(oe);
					break;
				}
			}
		}
		safe_free(e);
	}
	dat->wholistsme = NULL;

	/* Also remove 'who' from the wholistsme of everyone it was accepting,
	 * so those clients' state doesn't reference a freed entry.
	 */
	for (e = dat->accepting; e; e = e_next)
	{
		CallerIDData *otherdat = callerid_get(e->client, 0);
		CallerIDEntry *oe, *oe_next;

		e_next = e->next;

		if (otherdat)
		{
			for (oe = otherdat->wholistsme; oe; oe = oe_next)
			{
				oe_next = oe->next;
				if (oe->client == who)
				{
					DelListItemUnchecked(oe, otherdat->wholistsme);
					safe_free(oe);
					break;
				}
			}
		}
		safe_free(e);
	}
	dat->accepting = NULL;
}

/** Called whenever this moddata is actually destroyed. Normally the quit
 * hooks (callerid_local_quit/callerid_remote_quit) have already run first
 * and emptied both lists via callerid_remove_from_all_accepts(), making
 * this a no-op walk over empty lists. But if this destructor is ever
 * exercised without the quit hook having run first (eg some client-freeing
 * path that doesn't route through HOOKTYPE_LOCAL_QUIT/REMOTE_QUIT), calling
 * the same two-sided unlink routine here - instead of only freeing this
 * client's own nodes - is what stops other clients' CallerIDData from being
 * left holding a CallerIDEntry.client pointer into memory we're about to
 * free out from under them.
 */
void callerid_md_free(ModData *md)
{
	CallerIDData *dat = md->ptr;

	if (!dat)
		return;

	callerid_remove_from_all_accepts(dat->owner);

	safe_free(dat);
	md->ptr = NULL;
}

int callerid_local_quit(Client *client, MessageTag *mtags, const char *comment)
{
	callerid_remove_from_all_accepts(client);
	return 0;
}

int callerid_remote_quit(Client *client, MessageTag *mtags, const char *comment)
{
	callerid_remove_from_all_accepts(client);
	return 0;
}

/** HOOKTYPE_CAN_SEND_TO_USER handler: gate delivery for +g targets. */
int callerid_can_send_to_user(Client *client, Client *target, const char **text, const char **errmsg, SendType sendtype, ClientContext *clictx)
{
	CallerIDData *dat;
	time_t now;

	if (client == target)
		return HOOK_CONTINUE;

	if (IsServer(client) || IsULine(client))
		return HOOK_CONTINUE;

	/* Auto-accept: if I reply to someone who messaged me, I clearly want
	 * to hear back from them - so if I (the sender here) have +g set, and
	 * the person I'm sending to isn't on my accept list yet, add them.
	 * This mirrors Solanum's add_callerid_accept_for_source(): sending a
	 * message implies consent to be replied to. Notices are excluded so
	 * this can't be used to silently harvest accepts via bots/services.
	 *
	 * This must run regardless of whether the TARGET has +g set - it's
	 * about the sender's own accept list, not the target's. Gating it on
	 * the target's +g state (as an earlier version of this hook did) means
	 * auto-accept only ever fires between two +g users, which misses the
	 * common case entirely: a +g user messaging an ordinary, non-+g user
	 * who later replies would still get blocked by the sender's own +g,
	 * with no accept entry ever having been created for them.
	 */
	if (MyUser(client) && (client->umodes & UMODE_CALLERID) &&
	    sendtype != SEND_TYPE_NOTICE && !callerid_on_accept_list(target, client))
	{
		CallerIDData *mydat = callerid_get(client, 0);
		unsigned int count = 0;
		CallerIDEntry *e;

		if (mydat)
			for (e = mydat->accepting; e; e = e->next)
				count++;

		if (count < cfg.maxaccepts)
		{
			callerid_list_add(client, target);
		} else
		{
			/* Matches Solanum's add_callerid_accept_for_source(): if we
			 * can't guarantee the reply path because our own list is
			 * full, reject the outgoing message instead of silently
			 * creating a one-way conversation the target can never
			 * answer. (InspIRCd and ircd-hybrid don't auto-accept on
			 * send at all, so this specific case has no precedent from
			 * them either way - Solanum is the only one of the three
			 * reference implementations that has this feature.)
			 */
			sendnumericfmt(client, ERR_OWNMODE,
				"%s :your accept list is full, message not sent; use /ACCEPT -nick to remove a nick to make room",
				target->name);
			return HOOK_DENY;
		}
	}

	/* Only the target's own server enforces the target's +g - if the
	 * target isn't local here, or doesn't have +g set, there's nothing
	 * left for this hook to block.
	 */
	if (!MyUser(target) || !(target->umodes & UMODE_CALLERID))
		return HOOK_CONTINUE;

	if (callerid_on_accept_list(client, target))
		return HOOK_CONTINUE;

	if (cfg.operoverride && IsOper(client))
		return HOOK_CONTINUE;

	if (sendtype != SEND_TYPE_NOTICE)
		sendnumericfmt(client, ERR_TARGUMODEG, "%s :is in +g mode (server-side ignore).", target->name);

	dat = callerid_get(target, 1);
	now = TStime();
	if (now > (dat->last_notify + cfg.cooldown))
	{
		if (sendtype != SEND_TYPE_NOTICE)
			sendnumericfmt(client, RPL_TARGNOTIFY, "%s :has been informed that you messaged them.", target->name);

		sendnumericfmt(target, RPL_UMODEGMSG,
			"%s %s@%s :is messaging you, and you have user mode +g set. Use /ACCEPT +%s to allow.",
			client->name, client->user->username, GetHost(client), client->name);

		dat->last_notify = now;
	}

	/* We've already sent ERR_TARGUMODEG ourselves above - leave *errmsg
	 * unset so message.c doesn't also send ERR_CANTSENDTOUSER.
	 */
	return HOOK_DENY;
}

/** The /ACCEPT command - manage your own callerid whitelist.
 * Syntax:
 *   ACCEPT nick          add nick to your accept list
 *   ACCEPT +nick         (same as above)
 *   ACCEPT -nick         remove nick from your accept list
 *   ACCEPT *             list your current accept list
 *   ACCEPT +n1,-n2,...   comma-separated batch of the above
 */
CMD_FUNC(cmd_accept)
{
	CallerIDData *dat;
	CallerIDEntry *e;
	char buf[512];
	char *p, *tok, *save = NULL;

	if (!MyUser(client))
		return;

	if (parc < 2 || BadPtr(parv[1]))
	{
		sendnumericfmt(client, ERR_NEEDMOREPARAMS, ":ACCEPT :Not enough parameters");
		return;
	}

	if (!strcmp(parv[1], "*"))
	{
		dat = callerid_get(client, 0);
		if (dat)
			for (e = dat->accepting; e; e = e->next)
				sendnumericfmt(client, RPL_ACCEPTLIST, "%s", e->client->name);
		sendnumericfmt(client, RPL_ENDOFACCEPT, ":End of ACCEPT list");
		return;
	}

	strlcpy(buf, parv[1], sizeof(buf));
	for (tok = strtok_r(buf, ",", &save); tok; tok = strtok_r(NULL, ",", &save))
	{
		int remove = 0;
		Client *target;

		p = tok;
		if (*p == '-')
		{
			remove = 1;
			p++;
		} else if (*p == '+')
		{
			p++;
		}

		target = find_user(p, NULL);
		if (!target)
		{
			sendnumericfmt(client, ERR_NOSUCHNICK, "%s :No such nick", p);
			continue;
		}

		if (target == client)
			continue; /* accepting yourself is a no-op, not an error */

		if (remove)
		{
			CallerIDData *mydat = callerid_get(client, 0);
			CallerIDEntry *oe, *oe_next;
			int found = 0;

			if (mydat)
			{
				for (oe = mydat->accepting; oe; oe = oe_next)
				{
					oe_next = oe->next;
					if (oe->client == target)
					{
						CallerIDData *tdat = callerid_get(target, 0);
						CallerIDEntry *te, *te_next;

						DelListItemUnchecked(oe, mydat->accepting);
						safe_free(oe);

						if (tdat)
						{
							for (te = tdat->wholistsme; te; te = te_next)
							{
								te_next = te->next;
								if (te->client == client)
								{
									DelListItemUnchecked(te, tdat->wholistsme);
									safe_free(te);
									break;
								}
							}
						}
						found = 1;
						break;
					}
				}
			}

			if (!found)
			{
				sendnumericfmt(client, ERR_ACCEPTNOT, "%s :is not on your accept list", target->name);
				continue;
			}

			sendnotice(client, "%s is no longer on your accept list", target->name);
		} else
		{
			unsigned int count = 0;

			if (callerid_on_accept_list(target, client))
			{
				sendnumericfmt(client, ERR_ACCEPTEXIST, "%s :is already on your accept list", target->name);
				continue;
			}

			dat = callerid_get(client, 0);
			if (dat)
				for (e = dat->accepting; e; e = e->next)
					count++;

			if (count >= cfg.maxaccepts)
			{
				sendnumericfmt(client, ERR_ACCEPTFULL, ":Accept list is full (limit is %d)", cfg.maxaccepts);
				continue;
			}

			callerid_list_add(client, target);
			sendnotice(client, "%s is now on your accept list", target->name);
		}
	}
}
