/*
 * tracetklbug - Temporary module for tracing SHUN bug from
 * https://bugs.unrealircd.org/view.php?id=5566
 * (C) Copyright 2020 Bram Matthys (Syzop) and the UnrealIRCd team.
 * License: GPLv2
 */

#include "unrealircd.h"

ModuleHeader MOD_HEADER
  = {
	"third/tracetklbug",
	"1.0.0",
	"Trace TKL bug",
	"UnrealIRCd Team",
	"unrealircd-5",
    };

/* Externs */
extern int match_tkls(Client *client);

/* Forward declarions */
CMD_FUNC(tracetklbug_cmd);

MOD_INIT()
{
	CommandAdd(modinfo->handle, "TRACETKLBUG", tracetklbug_cmd, MAXPARA, CMD_USER);
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

void verifyevents(Client *client)
{
	Event *e;

	e = EventFind("check_pings");
	if (!e)
	{
		sendnotice(client, "[BUG] The check_pings event is missing???");
		return;
	}

	if (e->every_msec != 1000)
		sendnotice(client, "[BUG] The check_pings should run every 1000 msec but instead runs every %lld msec", (long long)e->every_msec);

	if (e->count != 0)
		sendnotice(client, "[BUG] The check_pings should run with count=0 but insteads count=%lld", (long long)e->count);

	if (loop.do_bancheck)
	{
		sendnotice(client, "Warning: loop.do_bancheck is still pending. Did you wait a few seconds after placing the SHUN before running TRACETKLBUG? "
		                   "Otherwise this command may produce false results.");
	}
}

CMD_FUNC(tracetklbug_cmd)
{
	Client *target = NULL;
	int was_shunned = 0;

	if (!IsOper(client))
	{
		sendnumeric(client, ERR_NOPRIVILEGES);
		return;
	}

	verifyevents(client);

	if ((parc < 2) || BadPtr(parv[1]))
	{
		sendnumeric(client, ERR_NEEDMOREPARAMS, "TRACETKLBUG");
		return;
	}

	target = find_person(parv[1], NULL);
	if (!target)
	{
		sendnumeric(client, ERR_NOSUCHNICK, parv[1]);
		return;
	}

	if (!MyUser(target))
	{
		sendnotice(client, "Forwarding your request to %s's server...", target->name);
		sendto_one(target, NULL, ":%s TRACETKLBUG :%s", client->name, target->name);
		return;
	}

	if (IsShunned(target))
		was_shunned = 1;

	match_tkls(target);

	if (!was_shunned && IsShunned(target))
	{
		sendnotice(client, "User was not shunned before. User got shunned now once you issued /TRACETKLBUG. %s.",
			loop.do_bancheck ? "Ban check was still pending, could be a race condition." : "loop.do_bancheck was zero, so this should not have happened!");
		return;
	}

	if (IsShunned(target))
	{
		sendnotice(client, "User was already shunned (TRACETKLBUG did not change this)");
	} else {
		sendnotice(client, "User is NOT shunned. TKL system thinks it should not shun this user.");
		if (find_tkl_exception(TKL_SHUN, target))
		{
			sendnotice(client, "User matches a TKL exception, check '/STATS except'");
		}
		else
		{
			sendnotice(client, "User does not match a TKL exception, this means there is simply no SHUN matching this user. "
			                   "If you don't agree with this then check '/STATS shun' and WHOIS the user and copy-paste "
			                   "this proof back to us.");
		}
	}
}

/*** <<<MODULE MANAGER START>>>
module
{
	documentation "https://bugs.unrealircd.org/view.php?id=5566";

	// This is displayed in './unrealircd module info ..' and also if compilation of the module fails:
	troubleshooting "Contact syzop@unrealircd.org if this module fails to compile";

	// Minimum version necessary for this module to work:
	min-unrealircd-version "5.*";

	post-install-text {
		"The module is installed. Now all you need to do is add a loadmodule line:";
		"loadmodule \"third/tracetklbug\";";
		"And /REHASH the IRCd.";
		"The module does not need any other configuration.";
	}
}
*** <<<MODULE MANAGER END>>>
*/
