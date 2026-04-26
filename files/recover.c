/*
License: GPLv3 or later
Name: third/recover
Author: Valware
*/


/*** <<<MODULE MANAGER START>>>
module
{
		documentation "https://github.com/ValwareIRC/valware-unrealircd-mods/blob/main/recover/recover.md";
		troubleshooting "In case of problems, please file a bug report at https://github.com/ValwareIRC/valware-unrealircd-mods/issues/new?template=bug_report.md";
		min-unrealircd-version "6.*";
		max-unrealircd-version "6.*";
		post-install-text {
				"The module is installed. Now all you need to do is add a loadmodule line:";
				"loadmodule \"third/recover\";";
				"to your configuration, and then rehash your server.";
				"The module does not need any other configuration.";
		}
}
*** <<<MODULE MANAGER END>>>
*/

#include "unrealircd.h"

ModuleHeader MOD_HEADER
  = {
	"third/recover",
	"1.1",
	"Adds command /RECOVER",
	"Valware",
	"unrealircd-6",
};

	
CMD_FUNC(CMD_RECOVER);
CMD_FUNC(CMD_GHOST);
CMD_FUNC(CMD_SARECOVER);
int do_recovery(Client *from, Client *to);

MOD_INIT()
{
	/*  Mark this as a global module. All this means is that UnrealIRCd will
		complain about it if you didn't load it on all servers on your
		network, because clients would not function correctly unless it were.
	*/
	MARK_AS_GLOBAL_MODULE(modinfo);

	/*  Add our command "RECOVER" to the IRCd to expect 1 parameter(s)
		and to be allowed to be used by regular users.
        "SARECOVER" requires that the oper using the command has
        operclass permission "recover"
	*/
	CommandAdd(modinfo->handle, "RECOVER", CMD_RECOVER, 1, CMD_USER);
	CommandAdd(modinfo->handle, "SARECOVER", CMD_SARECOVER, 2, CMD_OPER);

    // we don't have 'SAGHOST' because why not just use kill or something ¯\_(ツ)_/¯ 
    CommandAdd(modinfo->handle, "GHOST", CMD_GHOST, 1, CMD_USER);
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

/** The command functionality (What happens when a user types "/RECOVER" or "/RECOVER something".)
	The following is boilerplate code and doesn't do anything and is there for example purposes.
	The return value is void.
*/
CMD_FUNC(CMD_RECOVER)
{
    Client *target;

	if (parc < 1)
	{
		sendnumeric(client, ERR_NEEDMOREPARAMS, "RECOVER");
		return;
	}
    else
    {
        target = find_user(parv[1], NULL);
    }
	if (!IsLoggedIn(client))
	{
		sendnumeric(client, ERR_CANNOTDOCOMMAND, "RECOVER", "You must be logged into an account.");
		return;
	}

    if (!target || IsULine(target)) // don't fuck with ulines even if you manage to somehow register a ulined nick
    {
        sendnumeric(client, ERR_NOSUCHNICK, parv[1]);
        return;
    }

    if (!IsLoggedIn(target) || strcasecmp(client->user->account, target->user->account) != 0)
    {
        sendnumeric(client, ERR_CANNOTDOCOMMAND, "RECOVER", "You may not recover that nick.");
		return;
    }

    unreal_log(ULOG_INFO, "recover", "NICK_RECOVERY", client, "$nick used \'RECOVER\' on $target",
                    log_data_string("nick", client->name), log_data_string("target", target->name));

    sendnotice(target, "You were killed due to RECOVER command used by '%s'", client->name);
    do_recovery(client, target);
}

CMD_FUNC(CMD_SARECOVER)
{
    Client *from, *to;

    if (!ValidatePermissionsForPath("recover", client, NULL, NULL, NULL))
    {
        sendnumeric(client, ERR_NOPRIVILEGES);
        return;
    }

	if (parc < 2)
	{
		sendnumeric(client, ERR_NEEDMOREPARAMS, "SARECOVER");
        sendnotice(client, "Syntax: /SARECOVER <from nick> <to nick>");
		return;
	}
    else
    {
        from = find_user(parv[1], NULL);
        to = find_user(parv[2], NULL);
    }
    if (!from || !to || IsULine(to))
    {
        sendnumeric(client, ERR_NOSUCHNICK, !from ? parv[1] : parv[2]);
        return;
    }

    unreal_log(ULOG_INFO, "recover", "FORCE_NICK_RECOVERY", client, "$nick used \'RECOVER\' on $target to recover $targ",
                    log_data_string("nick", client->name), log_data_string("target", from->name), log_data_string("targ", to->name));

    do_recovery(from, to);
    
}

int do_recovery(Client *from, Client *to)
{
    if (!from || !to)
        return 0;

    const char *newnick = strdup(to->name);
    const char *args[4];
    args[0] = NULL;
    args[1] = to->name;
    args[2] = "Nickname has been recovered";
    args[3] = NULL;
	do_cmd(&me, NULL, "KILL", 3, args);

    const char *args2[3];
    args2[0] = NULL;
    args2[1] = newnick;
    args2[2] = NULL;
    do_cmd(from, NULL, "NICK", 2, args2);
    
    return !strcasecmp(from->name,newnick) ? 1 : 0;
}

CMD_FUNC(CMD_GHOST)
{
    Client *target;

	if (parc < 1)
	{
		sendnumeric(client, ERR_NEEDMOREPARAMS, "GHOST");
		return;
	}
    else
    {
        target = find_user(parv[1], NULL);
    }
	if (!IsLoggedIn(client))
	{
		sendnumeric(client, ERR_CANNOTDOCOMMAND, "GHOST", "You must be logged into an account.");
		return;
	}

    if (!target || IsULine(target)) // don't fuck with ulines even if you manage to somehow register a ulined nick
    {
        sendnumeric(client, ERR_NOSUCHNICK, parv[1]);
        return;
    }

    if (!IsLoggedIn(target) || strcasecmp(client->user->account, target->user->account) != 0)
    {
        sendnumeric(client, ERR_CANNOTDOCOMMAND, "GHOST", "You may not 'GHOST' that nick.");
		return;
    }

    unreal_log(ULOG_INFO, "recover", "GHOST_KILL", client, "$nick used \'GHOST\' on $target",
                    log_data_string("nick", client->name), log_data_string("target", target->name));

    const char *args[4];
    args[0] = NULL;
    args[1] = target->name;
    args[2] = "Ghost user";
    args[3] = NULL;
    do_cmd(&me, NULL, "KILL", 3, args);
}
