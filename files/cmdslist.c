/* 
	LICENSE: GPLv3-or-later
  	Copyright Ⓒ 2023 Valerie Pond

*/

/*** <<<MODULE MANAGER START>>>
module
{
		documentation "https://github.com/ValwareIRC/valware-unrealircd-mods/blob/main/cmdslist/README.md";
		troubleshooting "In case of problems, documentation or e-mail me at v.a.pond@outlook.com";
		min-unrealircd-version "6.*";
		max-unrealircd-version "6.*";
		post-install-text {
				"The module is installed. Now all you need to do is add a loadmodule line:";
				"loadmodule \"third/cmdslist\";";
				"And /REHASH the IRCd.";
				"The module does not need any other configuration.";
		}
}
*** <<<MODULE MANAGER END>>>
*/
#include "unrealircd.h"

ModuleHeader MOD_HEADER
  = {
	"third/cmdslist",
	"1.0",
	"Request and subscribe to commands", 
	"Valware",
	"unrealircd-6",
};

#define CMDSLIST_CAP "valware.uk/cmdslist"

#define CMD_CMDSLIST "CMDSLIST"
	
CMD_FUNC(CMDLIST);

/* Variables */
long CAP_CMDSLIST = 0L;

/* I don't find something else like this in the source so *shrugs* */
static int user_can_do_command(RealCommand *c, Client *client)
{
	if (c->flags & CMD_UNREGISTERED && (!IsUser(client) && MyConnect(client)))
		return 1;
	if (c->flags & CMD_USER && IsUser(client))
		return 1;
	if (c->flags & CMD_OPER && IsOper(client))
		return 1;
	return 0;
}

int operhewk(Client *client, int add, const char *oper_block, const char *operclass) 
{
	RealCommand *c;
	int i, type;
	if (!HasCapabilityFast(client, CAP_CMDSLIST))
		return 0;
	for (i=0; i < 256; i++)
	{
		for (c = CommandHash[i]; c; c = c->next)
		{
			if (c->flags ^ CMD_OPER)
				continue;

			sendto_one(client, NULL, ":%s CMDSLIST %s%s", me.name, (add) ? "+" : "-", c->cmd);
		}
	}
	return 0;
}

/* send list of new commands the user can use from being a user who is connected */
int send_cmds_list_connect(Client *client)
{
	RealCommand *c;
	int i, type;

	if (!HasCapabilityFast(client, CAP_CMDSLIST) || !MyUser(client))
		return 0;

	for (i=0; i < 256; i++)
	{
		for (c = CommandHash[i]; c; c = c->next)
		{
			if (c->flags & CMD_USER)
			{
				if (c->flags & CMD_UNREGISTERED)
					continue;

				if (user_can_do_command(c, client))
					sendto_one(client, NULL, ":%s CMDSLIST +%s", me.name, c->cmd);
			}

			// if they can't do it anymore because they're now fully connected
			else if (c->flags & CMD_UNREGISTERED && c->flags ^ CMD_USER)
				sendto_one(client, NULL, ":%s CMDSLIST -%s", me.name, c->cmd);
		}
	}
	return 0;
}

int send_cmds_list(Client *client)
{
	RealCommand *c;
	int i, type;

	if (!HasCapabilityFast(client, CAP_CMDSLIST))
		return 0;

	add_fake_lag(client, 2000);
	for (i=0; i < 256; i++)
	{
		for (c = CommandHash[i]; c; c = c->next)
			if (user_can_do_command(c, client))
				sendto_one(client, NULL, ":%s CMDSLIST +%s", me.name, c->cmd);
	}
	return 0;
}



MOD_INIT()
{
	ClientCapabilityInfo cap;

	HookAdd(modinfo->handle, HOOKTYPE_LOCAL_CONNECT, 0, send_cmds_list_connect);
	HookAdd(modinfo->handle, HOOKTYPE_LOCAL_OPER, 0, operhewk);
	memset(&cap, 0, sizeof(cap));
	cap.name = CMDSLIST_CAP;
	ClientCapabilityAdd(modinfo->handle, &cap, &CAP_CMDSLIST);
	CommandAdd(modinfo->handle, CMD_CMDSLIST, CMDLIST, 0, CMD_USER | CMD_UNREGISTERED);
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

CMD_FUNC(CMDLIST)
{
	if (!HasCapabilityFast(client, CAP_CMDSLIST))
		return;
	send_cmds_list(client);
}
