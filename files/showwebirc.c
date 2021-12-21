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
        min-unrealircd-version "6.*";
        post-install-text {
			"The module is installed. Now all you need to do is add a loadmodule line:";
			"loadmodule \"third/showwebirc\";";
			"Configure, who can see the webirc and websocket info (default is NOBODY!):";
			"set { whois-details { webirc { everyone none; self full; oper full; }; websocket { everyone none; self full; oper full; } } }";
			"And /REHASH the IRCd.";
			"Please note that you need to use the '/WHOIS nick nick' command to see websocket info";
			"for remote users.";
        }
}
*** <<<MODULE MANAGER END>>>
*/

#include "unrealircd.h"

ModuleHeader MOD_HEADER = {
	"third/showwebirc",   /* Name of module */
	"6.0", /* Version */
	"Add whois info for WEBIRC and websocket users", /* Short description of module */
	"k4be",
	"unrealircd-6"
};

int showwebirc_whois(Client *client, Client *target, NameValuePrioList **list);

MOD_INIT() {
	HookAdd(modinfo->handle, HOOKTYPE_WHOIS, 0, showwebirc_whois);

	return MOD_SUCCESS;
}

MOD_LOAD() {
	return MOD_SUCCESS;
}

MOD_UNLOAD() {
	return MOD_SUCCESS;
}

int showwebirc_whois(Client *client, Client *target, NameValuePrioList **list){
	int policy;
	ModDataInfo *moddata;
	
	/* WEBIRC */
	moddata = findmoddata_byname("webirc", MODDATATYPE_CLIENT);
	if(moddata != NULL){
		policy = whois_get_policy(client, target, "webirc");
		if(moddata_client(target, moddata).l && policy > WHOIS_CONFIG_DETAILS_NONE){
			add_nvplist_numeric_fmt(list, 0, "webirc", client, RPL_WHOISSPECIAL, "%s :is connecting via WEBIRC", target->name);
		}
	}
	
	/* websocket */
	if(!MyUser(target))
		return 0; /* this is not known for remote users */

	moddata = findmoddata_byname("websocket", MODDATATYPE_CLIENT);
	if(moddata != NULL){
		policy = whois_get_policy(client, target, "websocket");
		if(moddata_client(target, moddata).l && policy > WHOIS_CONFIG_DETAILS_NONE){
			add_nvplist_numeric_fmt(list, 0, "websocket", client, RPL_WHOISSPECIAL, "%s :is connecting via websocket", target->name);
		}
	}
	
	return 0;
}

