/** 
 * LICENSE: GPLv3
 * Copyright Ⓒ 2023 Valerie Pond
 * 
 * Restricts channels to registered users
 * Requested by Chris[A]
 * 
*/
/*** <<<MODULE MANAGER START>>>
module
{
		documentation "https://github.com/ValwareIRC/valware-unrealircd-mods/blob/main/restrict-chans/README.md";
		troubleshooting "In case of problems, documentation or e-mail me at v.a.pond@outlook.com";
		min-unrealircd-version "6.*";
		max-unrealircd-version "6.*";
		post-install-text {
				"The module is installed. Now all you need to do is add a loadmodule line:";
				"loadmodule \"third/restrict-chans\";";
				"And /REHASH the IRCd.";
				"The module does not need any other configuration.";
		}
}
*** <<<MODULE MANAGER END>>>
*/
#include "unrealircd.h"
int isreg_can_join(Client *client, Channel *channel, const char *key, char **errmsg);

ModuleHeader MOD_HEADER =
{
	"third/restrict-chans",
	"1.1",
	"Restrict channel creation to logged-in users",
	"Valware",
	"unrealircd-6",
};
MOD_INIT()
{
	MARK_AS_GLOBAL_MODULE(modinfo);
	
	HookAdd(modinfo->handle, HOOKTYPE_CAN_JOIN, 0, isreg_can_join);
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

MOD_TEST()
{
	return MOD_SUCCESS;
}

int isreg_can_join(Client *client, Channel *channel, const char *key, char **errmsg)
{
	/* allow people to join permanent empty channels and allow opers to create new channels */
	if (!channel->users && !IsLoggedIn(client) && !has_channel_mode(channel, 'P') && !IsOper(client))
	{
		/* there aren't actually any users in the channel but sub1_from_channel()
			will destroy the channel best without duplicating code */
		sub1_from_channel(channel);
		*errmsg = "%s :You must be logged in to create new channels", channel->name;
		return ERR_CANNOTDOCOMMAND;
	}
	return 0;
}
