/** 
 * LICENSE: GPLv3-or-later
 * Copyright Ⓒ 2022 Valerie Pond
 * 
*/

/*** <<<MODULE MANAGER START>>>
module
{
		documentation "https://github.com/ValwareIRC/valware-unrealircd-mods/blob/main/mute/README.md";
		troubleshooting "In case of problems, please check the README or email me at v.a.pond@outlook.com";
		min-unrealircd-version "6.*";
		max-unrealircd-version "6.*";
		post-install-text {
				"The module is installed. Now all you need to do is add a loadmodule line:";
				"loadmodule \"third/mute\";";
				"And then /rehash";
		}
}
*** <<<MODULE MANAGER END>>>
*/

#include "unrealircd.h"

ModuleHeader MOD_HEADER
  = {
	"third/mute",
	"1.2",
	"Globally mute a user", 
	"Valware",
	"unrealircd-6",
};

#define IsMuted(x)			(moddata_client(x, mute_md).i)
#define Mute(x)		do { moddata_client_set(x, "mute", "1"); } while(0)
#define Unmute(x)		do { moddata_client_set(x, "mute", "0"); } while(0)


#define MSG_MUTE "MUTE"
#define MSG_UNMUTE "UNMUTE"
#define UCONF "mute"

CMD_FUNC(CMD_MUTE);
CMD_FUNC(CMD_UNMUTE);

typedef struct
{
	int show_reason;
	char *reason;
	unsigned int lagby;

	unsigned short int show_reason_exists;
	unsigned short int reason_exists;
	unsigned short int lagby_exists;
} muteconf;

static muteconf ourconf;
static void send_to_client_lol(Client *client, char **p);

void mute_free(ModData *m);
const char *mute_serialize(ModData *m);
void mute_unserialize(const char *str, ModData *m);
void setconf(void);
void freeconf(void);
int mutecheck_chmsg(Client *client, Channel *channel, Membership *member, const char **text, const char **errmsg, SendType sendtype);
int mutecheck_usermsg(Client *client, Client *target, const char **text, const char **errmsg, SendType sendtype);
int mute_configtest(ConfigFile *cf, ConfigEntry *ce, int type, int *errs);
int mute_configrun(ConfigFile *cf, ConfigEntry *ce, int type);
int who_the_hell_be_muted_lol(Client *client, Client *target, NameValuePrioList **list);

ModDataInfo *mute_md;
MOD_TEST()
{
	memset(&ourconf, 0, sizeof(ourconf));

	HookAdd(modinfo->handle, HOOKTYPE_CONFIGTEST, 0, mute_configtest);
	return MOD_SUCCESS;
}
MOD_INIT() {
	ModDataInfo mreq;

	MARK_AS_GLOBAL_MODULE(modinfo);
	
	setconf();
	memset(&mreq, 0, sizeof(mreq));
	mreq.name = "mute";
	mreq.free = mute_free;
	mreq.serialize = mute_serialize;
	mreq.unserialize = mute_unserialize;
	mreq.sync = 1;
	mreq.remote_write = 1;
	mreq.type = MODDATATYPE_CLIENT;
	mute_md = ModDataAdd(modinfo->handle, mreq);
	if (!mute_md)
		abort();
	
	CommandAdd(modinfo->handle, MSG_MUTE, CMD_MUTE, 2, CMD_OPER);
	CommandAdd(modinfo->handle, MSG_UNMUTE, CMD_UNMUTE, 2, CMD_OPER);
	HookAdd(modinfo->handle, HOOKTYPE_CAN_SEND_TO_CHANNEL, -1, mutecheck_chmsg);
	HookAdd(modinfo->handle, HOOKTYPE_CAN_SEND_TO_USER, -1, mutecheck_usermsg);
	HookAdd(modinfo->handle, HOOKTYPE_CONFIGRUN, 0, mute_configrun);
	HookAdd(modinfo->handle, HOOKTYPE_WHOIS, -100, who_the_hell_be_muted_lol);
	return MOD_SUCCESS;
}
/** Called upon module load */
MOD_LOAD()
{
	return MOD_SUCCESS;
}

/** Called upon unload */
MOD_UNLOAD()
{
	freeconf();
	return MOD_SUCCESS;
}
const char *mute_serialize(ModData *m)
{
	static char buf[32];
	if (m->i == 0)
		return NULL; /* not set */
	snprintf(buf, sizeof(buf), "%d", m->i);
	return buf;
}
void mute_free(ModData *m)
{
    m->i = 0;
}
void mute_unserialize(const char *str, ModData *m)
{
    m->i = atoi(str);
}

/* dump strings to the client */
static void send_to_client_lol(Client *client, char **p)
{
	if(IsServer(client))
		return;
	for(; *p != NULL; p++)
		sendto_one(client, NULL, ":%s %03d %s :%s", me.name, RPL_TEXT, client->name, *p);
}

static char *help_mute[] = {
	"***** /MUTE *****",
	"-",
	"Globally mute a user for the rest of their session (until they disconnect).",
	"They will still be able to talk in your designated help channel (usually #Help lol)",
	"as well as directly message IRCops.",
	"-",
	"Syntax:",
	"	/MUTE -list|-help|<nick>",
	" ",
	"Examples:",
	"-",
	"List everyone who is muted (requires oper):",
	"	/MUTE -list",
	"-",
	"View this help output that you are currently reading:",
	"	/MUTE -help",
	"-",
	"Mute a user:",
	"	/MUTE Lamer32",
	"-",
	"For help on unmuting someone (/UNMUTE), try /unmute -help",
	"-",
	NULL
};

static char *help_unmute[] = {
	"***** /UNMUTE *****",
	"-",
	"Removes someone's mute",
	"-",
	"Syntax:",
	"	/UNMUTE -help|<nick>",
	"-",
	"Examples:",
	"-",
	"View this help output that you are currently reading:",
	"	/UNMUTE -help",
	"-",
	"Unmute a user:",
	"	/UNMUTE Lamer32",
	"-",
	"For help on muting someone (/MUTE), try /mute -help",
	"-",
	NULL
};

CMD_FUNC(CMD_MUTE)
{
	Client *target;

	if (parc < 2)
	{
		sendnumeric(client, ERR_NEEDMOREPARAMS, "MUTE");
		return;
	}

	if (!strcasecmp(parv[1],"-help"))
	{
		send_to_client_lol(client, help_mute);
		return;
	}
	if (!strcasecmp(parv[1],"-settings"))
	{
		sendnotice(client, "Settings for MUTE:");
		sendnotice(client,"Showing reason: %s", (ourconf.show_reason) ? "yes" : "no");
		sendnotice(client,"Reason: %s", (ourconf.reason) ? ourconf.reason : "No reason provided");
		sendnotice(client,"Lag time: %i", ourconf.lagby);
		return;
	}
	else if (!strcasecmp(parv[1],"-list"))
	{
		int found = 0, listnum = 1;
		sendnotice(client, "\2Listing all muted users:");
		list_for_each_entry(target, &client_list, client_node)
			if (IsMuted(target))
			{
				sendnotice(client,"%i. [address: %s!%s@%s] [gecos: %s] [account: %s] [oper: %s] [channels: %d]", listnum, target->name, target->user->username,
							 target->user->realhost, target->info, (IsLoggedIn(target)) ? target->user->account : "<none>", (IsOper(target)) ? target->user->operlogin : "<none>", target->user->joined);
			found = 1;
			listnum++;
			}
		if (!found)
			sendnotice(client,"Nobody is muted at this time.");
		return;
	}
	else if (!(target = find_user(parv[1], NULL)))
	{
		sendnumeric(client, ERR_NOSUCHNICK, parv[1]);
		return;
	}
	if (IsOper(target) && client != target) // let them mute themselves why not
	{
		sendnumeric(client, ERR_CANNOTDOCOMMAND, "MUTE", "Permission denied!");
		return;
	}
	if (IsMuted(target))
	{
		sendnumeric(client, ERR_CANNOTDOCOMMAND, "MUTE", "That user is already muted");
		return;
	}
	Mute(target);
	sendnotice(client, "You have muted %s", target->name);
	unreal_log(ULOG_INFO, "mute", "MUTE_COMMAND", client,
					"$client muted $target.",
					log_data_string("change_type", "mute"),
					log_data_client("target", target));
}

CMD_FUNC(CMD_UNMUTE)
{
	Client *target;

	if (parc < 2)
	{
		sendnumeric(client, ERR_NEEDMOREPARAMS, "UMUTE");
		return;
	}

	if (!strcasecmp(parv[1],"-help"))
	{
		send_to_client_lol(client, help_unmute);
		return;
	}
	if (!(target = find_user(parv[1], NULL)))
	{
		sendnumeric(client, ERR_NOSUCHNICK, parv[1]);
		return;
	}
	if (IsOper(target) && target != client)
	{
		sendnumeric(client, ERR_CANNOTDOCOMMAND, "UNMUTE", "Permission denied!");
		return;
	}
	if (!IsMuted(target))
	{
		sendnumeric(client, ERR_CANNOTDOCOMMAND, "UNMUTE", "That user was not muted");
		return;
	}
	Unmute(target);
	sendnotice(client, "You have unmuted %s", target->name);
	unreal_log(ULOG_INFO, "unmute", "MUTE_COMMAND", client,
					"$client unmuted $target.",
					log_data_string("change_type", "mute"),
					log_data_client("target", target));
}

int mutecheck_chmsg(Client *client, Channel *channel, Membership *member, const char **text, const char **errmsg, SendType sendtype)
{
	if (IsMuted(client) && strcasecmp(iConf.helpchan, channel->name))
	{
		if (ourconf.show_reason && ourconf.reason)
		{
			*errmsg = ourconf.reason;
			return HOOK_DENY;
		}
		add_fake_lag(client, ourconf.lagby); // lag themfor proteckshun
		*text = "";
	}
	return HOOK_CONTINUE;
}
int mutecheck_usermsg(Client *client, Client *target, const char **text, const char **errmsg, SendType sendtype)
{
	if (IsMuted(client) && (!IsOper(target) || IsULine(target)))
	{
		if (ourconf.show_reason && ourconf.reason)
		{
			*errmsg = ourconf.reason;
			return HOOK_DENY;
		}
		add_fake_lag(client, ourconf.lagby); // lag them for proteckshun
		*text = "";
	}
	return HOOK_CONTINUE;
}


// default settings, yes show them the reason
void setconf(void)
{
	ourconf.show_reason = 1;
	safe_strdup(ourconf.reason, "You cannot send or receive messages except for staff and the support channel.");
	ourconf.lagby = 500;
}

void freeconf(void)
{
	safe_free(ourconf.reason);
	ourconf.show_reason_exists = 0;
	ourconf.show_reason = 0;
	ourconf.reason_exists = 0;
	ourconf.lagby = 0;
}

int mute_configtest(ConfigFile *cf, ConfigEntry *ce, int type, int *errs)
{
	int errors = 0;
	int i;
	ConfigEntry *cep, *cep2;

	if(type != CONFIG_MAIN)
		return 0;

	if(!ce || !ce->name)
		return 0;

	if(strcmp(ce->name, UCONF))
		return 0;

	for(cep = ce->items; cep; cep = cep->next)
	{
		if(!cep->name)
		{
			config_error("%s:%i: blank %s item.", cep->file->filename, cep->line_number, UCONF);
			errors++;
			continue;
		}

		if(!strcmp(cep->name, "show-reason"))
		{
			if(ourconf.show_reason_exists)
			{
				config_error("%s:%i: duplicate %s::%s directive.", cep->file->filename, cep->line_number, UCONF, cep->name);
				errors++;
				continue;
			}
			if (strcasecmp(cep->value,"yes") && strcasecmp(cep->value,"no"))
			{
				config_error("%s:%i: %s::%s must be \"yes\" or \"no\"", cep->file->filename, cep->line_number, UCONF, cep->name);
				errors++;
				continue;
			}
			ourconf.show_reason_exists = 1;
			continue;
		}
		if(!strcmp(cep->name, "reason")) // The reason displayed to the user as to why they cannot message that channel
		{
			if(ourconf.reason_exists)
			{
				config_error("%s:%i: duplicate %s::%s directive.", cep->file->filename, cep->line_number, UCONF, cep->name);
				errors++;
				continue;
			}
			if (BadPtr(cep->value))
			{
				config_error("%s:%i: %s::%s exists but has no value.", cep->file->filename, cep->line_number, UCONF, cep->name);
				errors++;
				continue;
			}
			ourconf.reason_exists = 1;
			continue;
		}
		if(!strcmp(cep->name, "lag-time")) // The reason displayed to the user as to why they cannot message that channel
		{
			if(ourconf.lagby_exists)
			{
				config_error("%s:%i: duplicate %s::%s directive.", cep->file->filename, cep->line_number, UCONF, cep->name);
				errors++;
				continue;
			}
			if (BadPtr(cep->value))
			{
				config_error("%s:%i: %s::%s exists but has no value.", cep->file->filename, cep->line_number, UCONF, cep->name);
				errors++;
				continue;
			}
			for(int i = 0; cep->value[i]; i++)
				if(!isdigit(cep->value[i]))
				{
					config_error("%s:%i: %s::%s must be a number representing milliseconds", cep->file->filename, cep->line_number, UCONF, cep->name);
					errors++;
					break;
				}
			ourconf.lagby_exists = 1;
			continue;
		}

		config_warn("%s:%i: unknown item %s::%s", cep->file->filename, cep->line_number, UCONF, cep->name); 
	}
	if (!ourconf.reason_exists && !ourconf.show_reason_exists)
		config_warn("third/mute: No config settings found. Using default options: mute { show-reason \"yes\"; reason \"You cannot send or receive messages except for staff and the support channel.\"; }");
	*errs = errors;
	return errors ? -1 : 1;
}

int mute_configposttest(int *errs)
{
	return 1; // is no problem 4 me
}

int mute_configrun(ConfigFile *cf, ConfigEntry *ce, int type)
{
	ConfigEntry *cep;

	if(type != CONFIG_MAIN) // we're not checking the main config (set {})
		return 0;

	if(!ce || !ce->name)
		return 0;

	if(strcmp(ce->name, UCONF))
		return 0;

	for(cep = ce->items; cep; cep = cep->next) // 1t's 4lw4ys gr8 2 iter8 m8
	{
		if(!cep->name)
			continue;
		if(!strcmp(cep->name, "show-reason"))
		{
			ourconf.show_reason = config_checkval(cep->value, CFG_YESNO);
			continue;
		}
		if (!strcmp(cep->name, "reason"))
		{
			safe_strdup(ourconf.reason, cep->value);
			continue;
		}
		if (!strcmp(cep->name,"lag-time"))
		{
			ourconf.lagby = atoi(cep->value);
			continue;
		}
	}

	return 1; // all is well =]
}

int who_the_hell_be_muted_lol(Client *client, Client *target, NameValuePrioList **list)
{
	if (IsMuted(target) && (IsOper(client) || (target == client && ourconf.show_reason)))
		add_nvplist_numeric(list, 0, "muted", client, RPL_WHOISSPECIAL, target->name, "has been muted");

	return HOOK_CONTINUE;
}
