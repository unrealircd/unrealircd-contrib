/* Copyright (C) All Rights Reserved
** Written by Gottem <support@gottem.nl>
** Website: https://gottem.nl/unreal
** License: https://gottem.nl/unreal/license
*/

/*** <<<MODULE MANAGER START>>>
module {
	documentation "https://gottem.nl/unreal/man/sacmds";
	troubleshooting "In case of problems, check the FAQ at https://gottem.nl/unreal/halp or e-mail me at support@gottem.nl";
	min-unrealircd-version "5.*";
	//max-unrealircd-version "5.*";
	post-install-text {
		"The module is installed, now all you need to do is add a 'loadmodule' line to your config file:";
		"loadmodule \"third/sacmds\";";
		"Then /rehash the IRCd.";
		"For usage information, refer to the module's documentation found at: https://gottem.nl/unreal/man/sacmds";
	}
}
*** <<<MODULE MANAGER END>>>
*/

// One include for all cross-platform compatibility thangs
#include "unrealircd.h"

// Prior to v5.2.0 we didn't have message tags for nickchange events :>
#undef BACKPORT_NICKCHANGE_NO_MTAGS
#if (UNREAL_VERSION_TIME < 202115)
	#define BACKPORT_NICKCHANGE_NO_MTAGS
#endif

#define SNOMASK_SACMD 'A'

#define MSG_SANICK "SANICK"
#define MSG_SAUMODE "SAUMODE"

#define UMODE_DENY 0
#define UMODE_ALLOW 1

// Dem macros yo
#define CheckAPIError(apistr, apiobj) \
	do { \
		if(!(apiobj)) { \
			config_error("A critical error occurred on %s for %s: %s", (apistr), MOD_HEADER.name, ModuleGetErrorStr(modinfo->handle)); \
			return MOD_FAILED; \
		} \
	} while(0)

CMD_FUNC(cmd_sanick);
CMD_FUNC(cmd_saumode);

// Quality fowod declarations
int HasSAPriv(Client *client);
static void dumpit(Client *client, char **p);
int sacmds_check_snomask(Client *client, int what);

long SNO_SACMD = 0L;

// Help strings in case someone does just /SA<CMD>
/* Special characters:
** \002 = bold -- \x02
** \037 = underlined -- \x1F
*/
static char *sanickhelp[] = {
	"*** \002Help on /SANICK\002 ***",
	"Forces a user to take a different nick.",
	" ",
	"Syntax:",
	"    \002/SANICK\002 \037orig\037 \037new\037",
	" ",
	"Examples:",
	"    \002/SANICK test test2\002",
	"        Changes the nick \037test\037 to \037test2\037.",
	NULL
};

static char *saumodehelp[] = {
	"*** \002Help on /SAUMODE\002 ***",
	"Forcibly changes someone's u(ser)modes",
	" ",
	"Syntax:",
	"    \002/SAUMODE\002 \037nick\037 \037modes\037",
	" ",
	"Examples:",
	"    \002/SAUMODE someone +Z\002",
	"        Enables secureonlymsg on \037someone\037.",
	"    \002/SAUMODE someone -x\002",
	"        Remove \037someone\037's cloaked host.",
	"    \002/SAUMODE someone +Z-x\002",
	"        Combination of the two.",
	NULL
};

// Dat dere module header
ModuleHeader MOD_HEADER = {
	"third/sacmds", // Module name
	"2.1", // Version
	"Implements SA* commands for privileged opers", // Description
	"Gottem", // Author
	"unrealircd-5", // Modversion
};

MOD_INIT() {
	CheckAPIError("SnomaskAdd(SNO_SACMD)", SnomaskAdd(modinfo->handle, SNOMASK_SACMD, sacmds_check_snomask, &SNO_SACMD));
	CheckAPIError("CommandAdd(SANICK)", CommandAdd(modinfo->handle, MSG_SANICK, cmd_sanick, 2, CMD_USER));
	CheckAPIError("CommandAdd(SAUMODE)", CommandAdd(modinfo->handle, MSG_SAUMODE, cmd_saumode, 2, CMD_USER));

	MARK_AS_GLOBAL_MODULE(modinfo);
	return MOD_SUCCESS;
}

MOD_LOAD() {
	return MOD_SUCCESS;
}

// Called on unload/rehash lol
MOD_UNLOAD() {
	return MOD_SUCCESS;
}

int HasSAPriv(Client *client) {
	if(!IsUser(client) || IsULine(client) || ValidatePermissionsForPath("sanick", client, NULL, NULL, NULL) || ValidatePermissionsForPath("saumode", client, NULL, NULL, NULL))
		return 1;
	return 0;
}

// Dump a NULL-terminated array of strings to the user (taken from DarkFire IRCd)
static void dumpit(Client *client, char **p) {
	if(IsServer(client)) // Bail out early and silently if it's a server =]
		return;

	// Using sendto_one() instead of sendnumericfmt() because the latter strips indentation and stuff ;]
	for(; *p != NULL; p++)
		sendto_one(client, NULL, ":%s %03d %s :%s", me.name, RPL_TEXT, client->name, *p);

	// Let user take 8 seconds to read it
	client->local->since += 8;
}

int sacmds_check_snomask(Client *client, int what) {
	// No need to check remote clients
	return ((!MyUser(client) || HasSAPriv(client)) ? UMODE_ALLOW : UMODE_DENY);
}

CMD_FUNC(cmd_sanick) {
	// Gets args: Client *client, MessageTag *recv_mtags, int parc, char *parv[]
	char *oldnick, *newnick;
	Client *acptr; // "Orig" check
	Client *ocptr; // "New" check
	long tiem; // Nickchange timestamp etc
	MessageTag *mtags;

	// Prevent non-privileged opers from using this command
	if(!ValidatePermissionsForPath("sanick", client, NULL, NULL, NULL) && !IsULine(client)) {
		sendnumeric(client, ERR_NOPRIVILEGES); // Send error lol
		return;
	}

	if(BadPtr(parv[1])) { // If first argument is a bad pointer, don't proceed
		dumpit(client, sanickhelp); // Return help string instead
		return;
	}

	if(parc < 3) { // Need at least 3 params yo
		sendnumeric(client, ERR_NEEDMOREPARAMS, MSG_SANICK); // Send error lol
		return;
	}

	oldnick = parv[1];
	newnick = parv[2];

	if(!strcasecmp(oldnick, newnick)) // If orig and new are the same, gtfo =]
		return;

	if(!(acptr = find_person(oldnick, NULL))) { // Ensure that the target nick is actually in use
		sendnumeric(client, ERR_NOSUCHNICK, oldnick); // Send error lol
		return;
	}

	if(IsULine(acptr)) {
		sendnotice(client, "[sacmd/nick] You can't change nicknames of U-Lined users");
		return;
	}

	if(strlen(newnick) > NICKLEN) {
		sendnotice(client, "*** [sacmd/nick] Target nick can not exceed NICKLEN (max. %d chars)", NICKLEN);
		return;
	}

	if(do_nick_name(newnick) == 0) { // Validate new nick
		sendnotice(client, "*** [sacmd/nick] Invalid target nick: %s", newnick);
		return;
	}

	if((ocptr = find_client(newnick, NULL))) { // Prevent nick collisions (no kill)
		sendnotice(client, "*** [sacmd/nick] Changing the nick %s to %s would cause a collision, aborting", oldnick, newnick);
		return;
	}

	// If the target user is on another server, forward the command to that server (doing this only now to save a tiny bit of bandwidth)
	if(!MyUser(acptr)) {
		sendto_one(acptr->direction, NULL, ":%s %s %s %s", client->id, MSG_SANICK, oldnick, newnick);
		return; // Forwarded
	}

	// Enact the nick change
	tiem = TStime(); // Get timestamp
	acptr->umodes &= ~UMODE_REGNICK; // Remove +r umode (registered nick)
	acptr->lastnick = tiem; // Set the timestamp of the last nick change for the target user to the current time.

	mtags = NULL;
	new_message(acptr, NULL, &mtags);
	sendto_local_common_channels(acptr, NULL, 0, mtags, ":%s NICK :%s", acptr->name, newnick); // Send to other local users in common channels
	sendto_server(NULL, 0, 0, mtags, ":%s NICK %s :%ld", acptr->id, newnick, acptr->lastnick); // And the rest of el netw0rkerin0 ;]
	free_message_tags(mtags); // Also sets mtags back to NULL ;]

	add_history(acptr, 1); // Add nick history for whowas etc
	(void)del_from_client_hash_table(acptr->name, acptr); // Remove old name from lclient_list
	hash_check_watch(acptr, RPL_LOGOFF);

	if(client->user) {
		// Just in case a server calls this shit, send notice to all 0pers ;]
		sendto_snomask_global(SNO_SACMD, "*** %s (%s@%s) used %s to change %s to %s", client->name, client->user->username, client->user->realhost, MSG_SANICK, acptr->name, newnick);
	}

	// Run hooks lol
	#ifdef BACKPORT_NICKCHANGE_NO_MTAGS
		RunHook2(HOOKTYPE_LOCAL_NICKCHANGE, acptr, newnick);
	#else
		new_message(acptr, NULL, &mtags);
		RunHook3(HOOKTYPE_LOCAL_NICKCHANGE, acptr, mtags, newnick);
		free_message_tags(mtags);
	#endif

	strlcpy(acptr->name, newnick, sizeof(acptr->name)); // Actually change the nick the pointer is using here
	add_to_client_hash_table(newnick, acptr); // Re-add to lclient_list
	hash_check_watch(acptr, RPL_LOGON);
}

CMD_FUNC(cmd_saumode) {
	Client *acptr; // Target nick
	char *m; // For checkin em mode string
	int what; // Direction flag
	long lastflags; // Current umodes
	int i; // iter8or lol
	char mbuf[128]; // For storing the required mode string to pass along to the MODE command

	// Prevent non-privileged opers from using this command
	if(!ValidatePermissionsForPath("saumode", client, NULL, NULL, NULL) && !IsULine(client)) {
		sendnumeric(client, ERR_NOPRIVILEGES); // Send error lol
		return;
	}

	if(BadPtr(parv[1])) { // If first argument is a bad pointer, don't proceed
		dumpit(client, saumodehelp); // Return help string instead
		return;
	}

	if(parc < 3) { // Need at least 3 params yo
		sendnumeric(client, ERR_NEEDMOREPARAMS, MSG_SAUMODE); // Send error lol
		return;
	}

	if(!(acptr = find_person(parv[1], NULL))) { // Ensure that the target nick is actually in use
		sendnumeric(client, ERR_NOSUCHNICK, parv[1]); // Send error lol
		return;
	}

	if(IsULine(acptr)) {
		sendnotice(client, "[sacmd/umode] You can't change usermodes for U-Lined users");
		return;
	}

	// If the target user is on another server, forward the command to that server (doing this only now to save a tiny bit of bandwidth)
	if(!MyUser(acptr)) {
		sendto_one(acptr->direction, NULL, ":%s %s %s %s", client->id, MSG_SAUMODE, parv[1], parv[2]);
		return;
	}

	userhost_save_current(acptr);

	what = MODE_ADD;
	lastflags = 0L;
	for(i = 0; i <= Usermode_highest; i++) {
		if(Usermode_Table[i].flag && (acptr->umodes & Usermode_Table[i].mode))
			lastflags |= Usermode_Table[i].mode;
	}

	// Ayy thx Syzop ;];]
	for(m = parv[2]; *m; m++) {
		switch (*m) {
			case '+':
				what = MODE_ADD;
				break;
			case '-':
				what = MODE_DEL;
				break;

			// We may not get these, but they shouldnt be in default =]
			case ' ':
			case '\n':
			case '\r':
			case '\t':
				break;

			case 'i':
				if((what == MODE_ADD) && !(acptr->umodes & UMODE_INVISIBLE))
					irccounts.invisible++;
				if((what == MODE_DEL) && (acptr->umodes & UMODE_INVISIBLE))
					irccounts.invisible--;
				goto setmodex;
			case 'o':
				if((what == MODE_ADD) && !(acptr->umodes & UMODE_OPER)) {
					if(!IsOper(acptr) && MyUser(acptr))
						list_add(&acptr->special_node, &oper_list);
					irccounts.operators++;
				}
				if((what == MODE_DEL) && (acptr->umodes & UMODE_OPER)) {
					if(acptr->umodes & UMODE_HIDEOPER)
						acptr->umodes &= ~UMODE_HIDEOPER; // clear 'H' too, and opercount stays the same
					else
						irccounts.operators--;

					if(MyUser(acptr) && !list_empty(&acptr->special_node))
						list_del(&acptr->special_node);

					// User is no longer oper (after the goto below), so remove all oper-only modes and snomasks.
					remove_oper_privileges(acptr, 0);
				}
				goto setmodex;
			case 'H':
				if(what == MODE_ADD && !(acptr->umodes & UMODE_HIDEOPER)) {
					if(!IsOper(acptr) && !strchr(parv[2], 'o')) // Yes, this strchr() is flawed xd
						break; // Isn't an oper and would not become one either, gtfo
					irccounts.operators--;
				}
				if(what == MODE_DEL && (acptr->umodes & UMODE_HIDEOPER))
					irccounts.operators++;
				goto setmodex;
			case 'd':
				goto setmodex;
			case 'x':
				if(what == MODE_DEL) {
					if(acptr->user->virthost) // If user has a virtual host and we got -x...
						safe_strdup(acptr->user->virthost, acptr->user->cloakedhost); // ...change it to the cloaked host ;]
				}
				else {
					if(!acptr->user->virthost)
						safe_strdup(acptr->user->virthost, acptr->user->cloakedhost);

					if(MyUser(acptr) && !strcasecmp(acptr->user->virthost, acptr->user->cloakedhost))
						sendto_server(NULL, PROTO_VHP, 0, NULL, ":%s SETHOST :%s", acptr->id, acptr->user->virthost);
				}
				goto setmodex;
			case 't':
				// We support -t nowadays, which means we remove the vhost and set the cloaked host (note that +t is a NOOP, that code is in +x)
				if(what == MODE_DEL) {
					if(acptr->user->virthost && *acptr->user->cloakedhost && strcasecmp(acptr->user->cloakedhost, GetHost(acptr))) {
						safe_strdup(acptr->user->virthost, acptr->user->cloakedhost);

						if(MyUser(acptr))
							sendto_server(NULL, PROTO_VHP, 0, NULL, ":%s SETHOST :%s", acptr->id, acptr->user->virthost);
					}
				goto setmodex;
			}
			case 'r': // Let's not fuck with registration status lel
			case 'z': // Setting and unsetting user mode 'z' is not supported
				break;
			default:
				setmodex: // Actually gonna change the mode here =]
				for(i = 0; i <= Usermode_highest; i++) {
					if(!Usermode_Table[i].flag)
						continue;
					if(*m == Usermode_Table[i].flag) {
						if(what == MODE_ADD)
							acptr->umodes |= Usermode_Table[i].mode;
						else
							acptr->umodes &= ~Usermode_Table[i].mode;
						break;
					}
				}
				break;
		}
	}

	if(lastflags != acptr->umodes) { // If the flags end up different ;]
		RunHook3(HOOKTYPE_UMODE_CHANGE, client, lastflags, acptr->umodes); // Let's runnem hewks lel
		build_umode_string(acptr, lastflags, ALL_UMODES, mbuf); // Figure out the resulting mode string ;]
		if(MyUser(acptr) && *mbuf)
			sendto_one(acptr, NULL, ":%s MODE %s :%s", client->name, acptr->name, mbuf);

		if(client->user) {
			// Just in case a server calls this shit, send notice to all 0pers ;]
			sendto_snomask_global(SNO_SACMD, "*** %s (%s@%s) used %s %s to change %s's umodes", client->name, client->user->username, client->user->realhost, MSG_SAUMODE, mbuf, acptr->name);
		}
	}

	userhost_changed(acptr); // We can safely call this, even if nothing changed
	VERIFY_OPERCOUNT(acptr, "saumode");
}
