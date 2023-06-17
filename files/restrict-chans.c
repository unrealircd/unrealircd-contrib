/** 
 * LICENSE: GPLv3 or later
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
int isreg_can_join(Client *client, Channel *channel, const char *key);
int isreg_check_join(Client *client, Channel *channel, const char *key, char **errmsg);
ModuleHeader MOD_HEADER =
{
	"third/restrict-chans",
	"1.2",
	"Restrict channel creation to logged-in users",
	"Valware",
	"unrealircd-6",
};
MOD_INIT()
{
	MARK_AS_GLOBAL_MODULE(modinfo);
	
	HookAdd(modinfo->handle, HOOKTYPE_CAN_JOIN, 0, isreg_check_join);
	HookAdd(modinfo->handle, HOOKTYPE_PRE_LOCAL_JOIN, 0, isreg_can_join);
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

int isreg_check_join(Client *client, Channel *channel, const char *key, char **errmsg)
{
	if (has_channel_mode(channel, 'P')) // it's permanent, continue;
		return HOOK_CONTINUE;
	if (channel->users == 0)
	{
		if (IsLoggedIn(client))
			return HOOK_CONTINUE;
			
		*errmsg = "%s :You must be logged in to create new channels", channel->name;
		return ERR_CANNOTDOCOMMAND;
	}
	return HOOK_CONTINUE;
	
}

int isreg_can_join(Client *client, Channel *channel, const char *key)
{
	if (has_channel_mode(channel, 'P')) // it's permanent, continue;
		return HOOK_CONTINUE;
	/* allow people to join permanent empty channels and allow opers to create new channels */
	if (channel->users == 0)
	{
		if (IsLoggedIn(client))
			return HOOK_CONTINUE;
			
		sendnumeric(client, ERR_CANNOTDOCOMMAND, channel->name, "You must be logged in to create new channels");
		return HOOK_DENY;
	}
	return HOOK_CONTINUE;
}
