/* Copyright (C) All Rights Reserved
** Written by k4be
** Website: https://github.com/pirc-pl/unrealircd-modules/
** License: GPLv3 https://www.gnu.org/licenses/gpl-3.0.html
*/

/*** <<<MODULE MANAGER START>>>
module
{
        documentation "https://github.com/pirc-pl/unrealircd-modules/blob/master/README.md";
        troubleshooting "In case of problems, contact k4be on irc.pirc.pl.";
        min-unrealircd-version "5.*";
        post-install-text {
                "The module is installed. Now all you need to do is add a loadmodule line:";
                "loadmodule \"third/showwebirc\";";
  				"And /REHASH the IRCd.";
				"The module does not need any other configuration.";
        }
}
*** <<<MODULE MANAGER END>>>
*/

#include "unrealircd.h"

ModuleHeader MOD_HEADER = {
	"third/showwebirc",   /* Name of module */
	"5.0", /* Version */
	"Add SWHOIS info for WEBIRC users", /* Short description of module */
	"k4be@PIRC",
	"unrealircd-5"
};

static int showwebirc_userconnect(Client *cptr);

MOD_INIT() {
	if(!HookAdd(modinfo->handle, HOOKTYPE_LOCAL_CONNECT, 0, showwebirc_userconnect)) return MOD_FAILED;
	return MOD_SUCCESS;
}

MOD_LOAD() {
	Client *acptr;
	list_for_each_entry(acptr, &client_list, client_node){
		if(!IsUser(acptr) || !MyUser(acptr)) continue;
		showwebirc_userconnect(acptr); // add info for all users upon module loading
	}
	return MOD_SUCCESS;
}

MOD_UNLOAD() {
	Client *acptr;
	list_for_each_entry(acptr, &client_list, client_node){
		if(!IsUser(acptr) || !MyUser(acptr)) continue;
		swhois_delete(acptr, "webirc", "*", &me, NULL); // delete info when unloading 
	}
	return MOD_SUCCESS;
}

static int showwebirc_userconnect(Client *cptr) {
	ModDataInfo *moddata;
	moddata = findmoddata_byname("webirc", MODDATATYPE_CLIENT);
	if(moddata == NULL) return HOOK_CONTINUE;
	if(moddata_client(cptr, moddata).l){
		swhois_add(cptr, "webirc", 0, "is connecting via WEBIRC", &me, NULL);
	}
	return HOOK_CONTINUE;
}

