/*
  Licence: GPLv3
  NickLock
  
  Changes the user's nick to the new nick, and forces
  it to remain as such for the remainder of the session.
  
  Syntax: /nicklock <nick> <new nick>
  Taken from/inspircd by the module on InspIRCd

  "inspircd had it and I wanted it so here you go"
*/
/*** <<<MODULE MANAGER START>>>
module
{
        documentation "https://github.com/ValwareIRC/valware-unrealircd-mods/blob/main/nicklock/README.md";
	troubleshooting "In case of problems, documentation or e-mail me at v.a.pond@outlook.com";
        min-unrealircd-version "6.*";
        max-unrealircd-version "6.*";
        post-install-text {
                "The module is installed. Now all you need to do is add a loadmodule line:";
                "loadmodule \"third/nicklock\";";
                "And /REHASH the IRCd.";
                "The module does not need any other configuration.";
        }
}
*** <<<MODULE MANAGER END>>>
*/

#include "unrealircd.h"

ModuleHeader MOD_HEADER = {
	"third/nicklock",
	"1.0",
	"Adds the /NICKLOCK command which allows server operators to prevent a user from changing their nick during their session.",
	"Valware",
	"unrealircd-6",
};


#define IsNickLock(x)			(moddata_client(x, nicklock_md).i)
#define SetNickLock(x)		do { moddata_client(x, nicklock_md).i = 1; } while(0)
#define ClearNickLock(x)		do { moddata_client(x, nicklock_md).i = 0; } while(0)

#define NLOCK "NICKLOCK"
#define RNLOCK "NICKUNLOCK"
#define OVRCMD "NICK"

CMD_FUNC(NICKLOCK);
CMD_FUNC(NICKUNLOCK);
CMD_OVERRIDE_FUNC(nick_override);

void nicklock_free(ModData *m);
const char *nicklock_serialize(ModData *m);
void nicklock_unserialize(const char *str, ModData *m);

ModDataInfo *nicklock_md;

MOD_INIT() {
	ModDataInfo mreq;

	MARK_AS_GLOBAL_MODULE(modinfo);
	
	memset(&mreq, 0, sizeof(mreq));
	mreq.name = "nicklock";
	mreq.free = nicklock_free;
	mreq.serialize = nicklock_serialize;
	mreq.unserialize = nicklock_unserialize;
	mreq.sync = 1;
	mreq.type = MODDATATYPE_CLIENT;
	nicklock_md = ModDataAdd(modinfo->handle, mreq);
	if (!nicklock_md)
		abort();
	
	CommandAdd(modinfo->handle, NLOCK, NICKLOCK, 2, CMD_OPER);
	CommandAdd(modinfo->handle, RNLOCK, NICKUNLOCK, 1, CMD_OPER);
	
	return MOD_SUCCESS;
}
/** Called upon module load */
MOD_LOAD()
{
	CommandOverrideAdd(modinfo->handle, OVRCMD, 0, nick_override);
	return MOD_SUCCESS;
}

/** Called upon unload */
MOD_UNLOAD()
{
	return MOD_SUCCESS;
}
const char *nicklock_serialize(ModData *m)
{
	static char buf[32];
	if (m->i == 0)
		return NULL; /* not set */
	snprintf(buf, sizeof(buf), "%d", m->i);
	return buf;
}
void nicklock_free(ModData *m)
{
    m->i = 0;
}
void nicklock_unserialize(const char *str, ModData *m)
{
    m->i = atoi(str);
}

CMD_FUNC(NICKLOCK)
{
	Client *target;
	Client *newnick;
	char nickname[NICKLEN+1];
	char oldnickname[NICKLEN+1];
	MessageTag *mtags = NULL;
	
	if (!IsOper(client))
	{
		sendnumeric(client, ERR_NOPRIVILEGES);
		return;	
	}

	else if (parc < 2) {
		sendnumeric(client, ERR_NEEDMOREPARAMS, NLOCK);
		return;
	}
	sendnotice(client,"%s",parv[1]);
	if (!(target = find_user(parv[1], NULL))) {
		sendnumeric(client, ERR_NOSUCHNICK, parv[1]);
		return;
	}
	if (IsOper(target) && client != target)
	{
		sendnumeric(client, ERR_NOPRIVILEGES);
		return;	
	}
	if (IsNickLock(target))
	{
		sendnotice(client,"%s is already nicklocked.",target->name);
		return;
	}
	
	/* actually do the nick lock */
	if (!parv[2])
		parv[2] = target->name;
		
	strlcpy(nickname, parv[2], sizeof(nickname));
	if (do_nick_name(nickname) == 0)
		return;

	if (strcmp(target->name,nickname) && (newnick = find_user(nickname, NULL)))
	{
		sendnotice(client,"%s is already an existing nick!", nickname);
		return;
	}
	strlcpy(oldnickname, target->name, sizeof(oldnickname));

	if (strcmp(target->name,nickname))
	{
		target->lastnick = TStime();
		new_message(target, NULL, &mtags);
		RunHook(HOOKTYPE_LOCAL_NICKCHANGE, target, mtags, nickname);
		sendto_local_common_channels(target, target, 0, mtags, ":%s NICK :%s", target->name, nickname);
		sendto_one(target, mtags, ":%s NICK :%s", target->name, nickname);
		sendto_server(NULL, 0, 0, mtags, ":%s NICK %s :%lld", target->id, nickname, (long long)target->lastnick);

		add_history(target, 1);
		del_from_client_hash_table(target->name, target);

		strlcpy(target->name, nickname, sizeof target->name);
		add_to_client_hash_table(nickname, target);
		RunHook(HOOKTYPE_POST_LOCAL_NICKCHANGE, target, mtags, oldnickname);
		free_message_tags(mtags);
	}
	
	sendnotice(client,"%s is now locked to the nick %s", oldnickname, nickname);
	unreal_log(ULOG_INFO, "nick", "NICK_IS_LOCKED", target,
		"$old_nick_name has been locked to the nick $new_nick_name - Set by $our_nick",
	           	log_data_string("new_nick_name", nickname),
			log_data_string("old_nick_name", oldnickname),
			log_data_string("our_nick", client->name));

	SetNickLock(target);
	return;
}

CMD_FUNC(NICKUNLOCK)
{
	Client *target;
	

	if (!IsOper(client))
	{
		sendnumeric(client, ERR_NOPRIVILEGES);
		return;	
	}
	else if (parc < 2) {
		sendnumeric(client, ERR_NEEDMOREPARAMS, RNLOCK);
		return;
	}
	else if (!(target = find_user(parv[1], NULL))) {
		sendnumeric(client, ERR_NOSUCHNICK, parv[1]);
		return;
	}
	if (IsOper(target) && client != target)
	{
		sendnumeric(client, ERR_NOPRIVILEGES);
		return;	
	}
	if (!IsNickLock(target))
	{
		sendnotice(client,"%s was not nicklocked anyway.",target->name);
		return;
	}
	ClearNickLock(target);
	sendnotice(client,"%s is no longer nicklocked.",target->name);
	unreal_log(ULOG_INFO, "nick", "NICK_IS_LOCKED", target,
		"$nick is no longer nicklocked - Removed by $our_nick",
	           	log_data_string("nick", target->name),
			log_data_string("our_nick", client->name));
	return;
}

CMD_OVERRIDE_FUNC(nick_override)
{
	if (!IsNickLock(client))
		CallCommandOverride(ovr, client, recv_mtags, parc, parv);
	return;
}
