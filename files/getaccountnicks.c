/* Copyright (C) All Rights Reserved
** Written by westor <westor7@gmail.com>
** License: GPLv3 https://www.gnu.org/licenses/gpl-3.0.html
** Thanks to: k4be for helping me out, also thanks other modules from unrealircd/unrealircd-contrib that i rent their codes for this module.
*/

/*** <<<MODULE MANAGER START>>>
module
{
        documentation "https://gist.github.com/westor7/da301c54f2b466c35cb13f505f5652ab";
        troubleshooting "In case of problems, contact westor on irc.chathub.org network.";
        min-unrealircd-version "5.*";
        post-install-text {
                "The module is installed. Now you need to add a loadmodule line:";
                "loadmodule \"third/getaccountnicks\";";
  				"And /REHASH the IRCd.";
  				"The module does not need any other configuration.";
        }
}
*** <<<MODULE MANAGER END>>>
*/

#include "unrealircd.h"

#define MSG_GETACCOUNTNICKS "GETACCOUNTNICKS"

// The following defines are part of https://github.com/unrealircd/unrealircd/pull/129
#define _IsLoggedInWithAccount(x) (x->user && (*x->user->svid != '*') && !isdigit(*x->user->svid))
#define _IsLoggedIn(x)	(IsRegNick(x) || _IsLoggedInWithAccount(x))

CMD_FUNC(getaccountnicks);

ModuleHeader MOD_HEADER = {
	"third/getaccountnicks",
	"1.0",
	"Command /GETACCOUNTNICKS (Allows you to see all logged in nicknames on the specified account).",
	"westor",
	"unrealircd-5",
};

MOD_INIT() {
	CommandAdd(modinfo->handle, MSG_GETACCOUNTNICKS, getaccountnicks, MAXPARA, CMD_USER);

	return MOD_SUCCESS;
}

MOD_LOAD() { return MOD_SUCCESS; }
MOD_UNLOAD() { return MOD_SUCCESS; }

CMD_FUNC(getaccountnicks) {
	Client *acptr;

	if (!IsUser(client) || !IsOper(client)) {
		sendnumeric(client, ERR_NOPRIVILEGES);

		return;
	}
	
	if (BadPtr(parv[1])) {
		sendnumeric(client, ERR_NEEDMOREPARAMS, MSG_GETACCOUNTNICKS);

		return;
	}
	
	char ubuf[224] = "";

	list_for_each_entry(acptr, &client_list, client_node) {
		if (!IsUser(acptr) || IsULine(acptr) || !_IsLoggedIn(acptr) || strcmp(acptr->user->svid, parv[1])) { continue; }

		if (!ubuf[0]) { strlcpy(ubuf, acptr->name, sizeof(ubuf)); }
		else {
			strlcat(ubuf, ", ", sizeof(ubuf));
			strlcat(ubuf, acptr->name, sizeof(ubuf));
		}

	}

	if (ubuf[0]) { sendnotice(client, "[getaccountnicks]: %s = %s", parv[1], ubuf); }
	else { sendnotice(client, "[getaccountnicks]: No results."); }
}
