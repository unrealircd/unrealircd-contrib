/*
  Licence: GPLv3 or later
  Copyright Ⓒ 2022 Valerie Pond
  LockServ
  
  Locks a server (stops incoming connections)
*/
/*** <<<MODULE MANAGER START>>>
module
{
		documentation "https://github.com/ValwareIRC/valware-unrealircd-mods/blob/main/lockserv/README.md";
		troubleshooting "In case of problems, documentation or e-mail me at v.a.pond@outlook.com";
		min-unrealircd-version "6.*";
		max-unrealircd-version "6.*";
		post-install-text {
				"The module is installed. Now all you need to do is add a loadmodule line:";
				"loadmodule \"third/lockserv\";";
				"And /REHASH the IRCd.";
				"The module does not need any other configuration.";
		}
}
*** <<<MODULE MANAGER END>>>
*/

#include "unrealircd.h"



ModuleHeader MOD_HEADER = {
	"third/lockserv",
	"1.0",
	"Adds the /lockserv command which allows privileged server operators to prevent connections to a particular server.",
	"Valware",
	"unrealircd-6",
};


#define IsServerLocked(x)			(moddata_client(x, lockserv_md).i)
#define LockReason(x)				(moddata_client(x, lockserv_reason_md).str)
#define LockedBy(x)					(moddata_client(x, lockserv_lockedby_md).str)

#define MSG_LOCKSERV "LOCKSERV"
#define MSG_UNLOCKSERV "UNLOCKSERV"

CMD_FUNC(cmd_lockserv);
CMD_FUNC(cmd_unlockserv);
CMD_OVERRIDE_FUNC(lockserv_cap_ovr);

void lockserv_free(ModData *m);
const char *lockserv_serialize(ModData *m);
void lockserv_unserialize(const char *str, ModData *m);

void lockserv_reason_free(ModData *m);
const char *lockserv_reason_serialize(ModData *m);
void lockserv_reason_unserialize(const char *str, ModData *m);

void lockserv_lockedby_free(ModData *m);
const char *lockserv_lockedby_serialize(ModData *m);
void lockserv_lockedby_unserialize(const char *str, ModData *m);

int lockserv_connect(Client *client);
void lockserv_list(Client *client);

static void dumpit(Client *client, char **p);

static char *lockserv_help[] = {
	"***** Lockserv *****",
	"-",
	"Lock a server; prohibit new connections to a particular server.",
	"Note: Unlike a G-Line, this will prohibit use of SASL in addition.",
	"Requires operclass permission 'lockserv'.",
	"-",
	"Syntax:",
	"    /LOCKSERV <server|nick|-list> [<reason>]",
	"    /UNLOCKSERV <server>",
	"-",
	"Examples:",
	"-",
	"List locked servers:",
	"    /LOCKSERV -list",
	"-",
	"View this output:",
	"    /LOCKSERV -help",
	"-",
	"Lock a server called 'lol.valware.uk':",
	"    /LOCKSERV lol.valware.uk This server is closed! Please connect to irc.valware.uk instead",
	"-",
	"Lock the server that user 'Valware' is on:",
	"    /LOCKSERV Valware Server closed for spam.",
	"-",
	"Unlock a server to resume allowing connections:",
	"    /UNLOCKSERV lol.valware.uk",
	"-",
	NULL
};

ModDataInfo *lockserv_md;
ModDataInfo *lockserv_reason_md;
ModDataInfo *lockserv_lockedby_md;

MOD_INIT() {
	ModDataInfo mreq;

	MARK_AS_GLOBAL_MODULE(modinfo);
	
	memset(&mreq, 0, sizeof(mreq));
	mreq.name = "lockserv";
	mreq.free = lockserv_free;
	mreq.serialize = lockserv_serialize;
	mreq.unserialize = lockserv_unserialize;
	mreq.sync = MODDATA_SYNC_EARLY;
	mreq.remote_write = 1;
	mreq.type = MODDATATYPE_CLIENT;
	lockserv_md = ModDataAdd(modinfo->handle, mreq);
	if (!lockserv_md)
	{
		config_error("could not register lockserv moddata");
		return MOD_FAILED;
	}

	memset(&mreq, 0, sizeof(mreq));
	mreq.name = "lockserv_reason";
	mreq.free = lockserv_reason_free;
	mreq.serialize = lockserv_reason_serialize;
	mreq.unserialize = lockserv_reason_unserialize;
	mreq.sync = MODDATA_SYNC_EARLY;
	mreq.remote_write = 1;
	mreq.type = MODDATATYPE_CLIENT;
	lockserv_reason_md = ModDataAdd(modinfo->handle, mreq);
	if (!lockserv_reason_md)
	{
		config_error("could not register lockserv moddata");
		return MOD_FAILED;
	}
	memset(&mreq, 0, sizeof(mreq));
	mreq.name = "lockserv_lockedby";
	mreq.free = lockserv_lockedby_free;
	mreq.serialize = lockserv_lockedby_serialize;
	mreq.unserialize = lockserv_lockedby_unserialize;
	mreq.sync = MODDATA_SYNC_EARLY;
	mreq.remote_write = 1;
	mreq.type = MODDATATYPE_CLIENT;
	lockserv_lockedby_md = ModDataAdd(modinfo->handle, mreq);
	if (!lockserv_lockedby_md)
	{
		config_error("could not register lockserv moddata");
		return MOD_FAILED;
	}

	HookAdd(modinfo->handle, HOOKTYPE_LOCAL_CONNECT, 0, lockserv_connect);
	CommandAdd(modinfo->handle, MSG_LOCKSERV, cmd_lockserv, MAXPARA, CMD_OPER | CMD_SERVER);
	CommandAdd(modinfo->handle, MSG_UNLOCKSERV, cmd_unlockserv, 1, CMD_OPER | CMD_SERVER);
	CommandOverrideAdd(modinfo->handle, "CAP", 0, lockserv_cap_ovr);


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
	return MOD_SUCCESS;
}

/* Lockserv Status (1/0) */
const char *lockserv_serialize(ModData *m)
{
	static char tmp[32];
	if (m->i == 0)
		return NULL; /* not set */
	snprintf(tmp, sizeof(tmp), "%d", m->i);
	return tmp;
}
void lockserv_free(ModData *m)
{
	m->i = 0;
}
void lockserv_unserialize(const char *str, ModData *m)
{
	m->i = atoi(str);
}

/* Lockserv Reason */
const char *lockserv_reason_serialize(ModData *m)
{
	static char tmp[32];
	if (m->str == NULL)
		return NULL; /* not set */
	snprintf(tmp, sizeof(tmp), "%s", m->str);
	return tmp;
}
void lockserv_reason_free(ModData *m)
{
	m->str = 0;
}
void lockserv_reason_unserialize(const char *str, ModData *m)
{
	safe_strdup(m->str, str);
}


/* Lockserv set by */
const char *lockserv_lockedby_serialize(ModData *m)
{
	static char tmp[32];
	if (m->str == NULL)
		return NULL; /* not set */
	snprintf(tmp, sizeof(tmp), "%s", m->str);
	return tmp;
}
void lockserv_lockedby_free(ModData *m)
{
	m->str = 0;
}
void lockserv_lockedby_unserialize(const char *str, ModData *m)
{
	safe_strdup(m->str, str);
}

static void dumpit(Client *client, char **p) {
	if(IsServer(client))
		return;
	for(; *p != NULL; p++)
		sendto_one(client, NULL, ":%s %03d %s :%s", me.name, RPL_TEXT, client->name, *p);
}

CMD_FUNC(cmd_lockserv)
{
	Client *target;

	if (!IsOper(client) || !ValidatePermissionsForPath("lockserv:can_lock", client, NULL, NULL, NULL))
	{
		sendnumeric(client, ERR_NOPRIVILEGES);
		return;	
	}
	else if (parc < 2) {
		sendnumeric(client, ERR_NEEDMOREPARAMS, MSG_LOCKSERV);
		return;
	}
	if (BadPtr(parv[1]) || !strcasecmp(parv[1],"-help"))
	{
		dumpit(client,lockserv_help);
		return;
	}
	if (!strcasecmp(parv[1],"-list"))
	{
		lockserv_list(client);
		return;
	}
	target = find_server(parv[1], NULL);
	if (!target)
	{
		/* look it up by user as fallback? */
		target = find_user(parv[1], NULL);
		if (target && IsUser(target))
			target = find_server(target->uplink->name, NULL);

		else // still did not find anyone though
		{
			sendto_one(client, NULL, "FAIL %s NOT_A_SERVER :Server not found.", MSG_LOCKSERV);
			return;
		}
	}
	if (!IsServer(target) && !IsMe(target))
	{
		sendto_one(client, NULL, "FAIL %s NOT_A_SERVER :Not a server: '%s'", MSG_LOCKSERV, target->name);
		return;
	}
	
	if (IsServerLocked(target))
	{
		sendto_one(client, NULL, "FAIL %s SERVER_ALREADY_LOCKED :That server '%s' is already locked.", MSG_LOCKSERV, target->name);
		return;
	}
	char p[254] = "\0";
	if (!BadPtr(parv[2]))
	{
		int i;
		for (i = 2; i < parc && !BadPtr(parv[i]); i++)
		{
			strlcat(p, parv[i], sizeof(p));
			if (!BadPtr(parv[i + 1]))
				strlcat(p, " ", sizeof(p));
		}
	}
	const char *reason = (strlen(p)) ? p : "This server is closed to incoming connections.";
	moddata_client_set(target, "lockserv", "1");
	moddata_client_set(target, "lockserv_reason", reason);

	char s[300] = "\0";
	ircsnprintf(s, sizeof(s), "%s!%s@%s [Oper: %s [%s]]", client->name, client->ident, client->ip, moddata_client_get(client, "operlogin"),moddata_client_get(client, "operclass"));
	const char *lockedby = s;
	moddata_client_set(target, "lockserv_lockedby", lockedby);

	sendnotice(client,"Server '%s' is now locked.",target->name);
	unreal_log(ULOG_INFO, "lockserv", "SERVER_LOCKED", NULL,
		"$serv has been locked from incoming connections [by $setby] [Reason: $reason] ",
	  	log_data_string("serv", target->name),
		log_data_string("reason", reason),
		log_data_string("setby", lockedby));
	return;
}

CMD_FUNC(cmd_unlockserv)
{
	Client *target;
	if (!IsOper(client) || !ValidatePermissionsForPath("lockserv:can_unlock", client, NULL, NULL, NULL))
	{
		sendnumeric(client, ERR_NOPRIVILEGES);
		return;	
	}
	else if (parc < 2) {
		sendnumeric(client, ERR_NEEDMOREPARAMS, MSG_UNLOCKSERV);
		return;
	}
	if (BadPtr(parv[1]) || !strcasecmp(parv[1],"-help"))
	{
		dumpit(client,lockserv_help);
		return;
	}
	target = find_server(parv[1], NULL);
	if (!target)
	{
		sendto_one(client, NULL, "FAIL %s NOT_A_SERVER :Server not found.", MSG_LOCKSERV);
		return;
	}
	if (!IsServer(target) && !IsMe(target))
	{
		sendto_one(client, NULL, "FAIL %s NOT_A_SERVER :Not a server: '%s'", MSG_LOCKSERV, target->name);
		return;
	}
	if (!IsServerLocked(target))
	{
		sendto_one(client, NULL, "FAIL %s SERVER_ALREADY_LOCKED :That server '%s' is already open.", MSG_UNLOCKSERV, target->name);
		return;
	}
	moddata_client_set(target, "lockserv", "0");
	moddata_client_set(target, "lockserv_reason", "0");
	moddata_client_set(target, "lockserv_lockedby", "0");
	sendnotice(client,"Server '%s' is now locked.",target->name);
	unreal_log(ULOG_INFO, "lockserv", "SERVER_LOCKED", NULL,
		"$serv has been unlocked - now open to incoming connections ($client)",
	  	log_data_string("serv", target->name),
		log_data_string("client", client->name));
	return;
}

int lockserv_connect(Client *client)
{
	if (IsServerLocked(client->uplink) && !find_tkl_exception(TKL_ZAP, client) && IsUser(client)) // allow servers/rpc/everything else to connect still :D
	{
		if (!MyConnect(client))
			sendto_server(NULL, 0, 0, NULL, ":%s KILL %s :%s", me.name, client->id, LockReason(client->uplink));
		exit_client(client, NULL, LockReason(client->uplink));
		return HOOK_DENY;
	}
	return HOOK_CONTINUE;
}

void lockserv_list(Client *client)
{
	Client *server;
	int i = 1;
	int found = 0;

	sendnotice(client,"*** Listing locked servers.");
	list_for_each_entry(server, &global_server_list, client_node)
	{
		if (IsServerLocked(server))
		{
			char info[200];
			sendnotice(client, "%d) %s   - Reason: %s - Set by: %s", i, server->name, LockReason(server), LockedBy(server));
			i++;
			found = 1;
		}
	}
	if (!found)
		sendnotice(client,"*** No locked servers were found");
	else
		sendnotice(client,"*** End of /LOCKSERV -list");
}

/* if for some reason we didn't catch them because they connected without caps (is it even possible?)
 * not to worry we'll catch them a little later */
CMD_OVERRIDE_FUNC(lockserv_cap_ovr)
{
	Client *server = find_server(me.name, NULL);

	if (IsServerLocked(server) && !find_tkl_exception(TKL_ZAP, client) && !IsRegistered(client))
	{
		exit_client(client, NULL, LockReason(server));
		return;
	}

	CallCommandOverride(ovr, client, recv_mtags, parc, parv);
}
