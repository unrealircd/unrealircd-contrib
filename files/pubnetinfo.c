/* Copyright (C) All Rights Reserved
** Written by Gottem <support@gottem.nl>
** Website: https://gottem.nl/unreal
** License: https://gottem.nl/unreal/license
*/

/*** <<<MODULE MANAGER START>>>
module {
	documentation "https://gottem.nl/unreal/man/pubnetinfo";
	troubleshooting "In case of problems, check the FAQ at https://gottem.nl/unreal/halp or e-mail me at support@gottem.nl";
	min-unrealircd-version "6.*";
	//max-unrealircd-version "6.*";
	post-install-text {
		"The module is installed, now all you need to do is add a 'loadmodule' line to your config file:";
		"loadmodule \"third/pubnetinfo\";";
		"Then /rehash the IRCd.";
		"For usage information, refer to the module's documentation found at: https://gottem.nl/unreal/man/pubnetinfo";
	}
}
*** <<<MODULE MANAGER END>>>
*/

// One include for all cross-platform compatibility thangs
#include "unrealircd.h"

// Command string
#define MSG_PUBNETINFO "PUBNETINFO"

// Dem macros yo
#define CheckAPIError(apistr, apiobj) \
	do { \
		if(!(apiobj)) { \
			config_error("A critical error occurred on %s for %s: %s", (apistr), MOD_HEADER.name, ModuleGetErrorStr(modinfo->handle)); \
			return MOD_FAILED; \
		} \
	} while(0)

CMD_FUNC(pubnetinfo); // Register command function

int is_loopback_ip(char *ip);

// Dat dere module header
ModuleHeader MOD_HEADER = {
	"third/pubnetinfo", // Module name
	"2.1.0", // Version
	"Display public network/server information such as SSL/TLS links", // Description
	"Gottem", // Author
	"unrealircd-6", // Modversion
};

// Initialisation routine (register hooks, commands and modes or create structs etc)
MOD_INIT() {
	CheckAPIError("CommandAdd(PUBNETINFO)", CommandAdd(modinfo->handle, MSG_PUBNETINFO, pubnetinfo, 2, CMD_USER | CMD_SERVER));

	MARK_AS_GLOBAL_MODULE(modinfo);
	return MOD_SUCCESS;
}

// Actually load the module here (also command overrides as they may not exist in MOD_INIT yet)
MOD_LOAD() {
	return MOD_SUCCESS; // We good
}

// Called on unload/rehash obv
MOD_UNLOAD() {
	// Clean up any structs and other shit
	return MOD_SUCCESS; // We good
}

// Nicked from src/socket.c =]]]]
int is_loopback_ip(char *ip) {
	ConfigItem_listen *e;
	if(!strcmp(ip, "127.0.0.1") || !strcmp(ip, "0:0:0:0:0:0:0:1") || !strcmp(ip, "0:0:0:0:0:ffff:127.0.0.1"))
		return 1;
	for(e = conf_listen; e; e = e->next) {
		if((e->options & LISTENER_BOUND) && !strcmp(ip, e->ip))
			return 1;
	}
	return 0;
}

CMD_FUNC(pubnetinfo) {
	// Gets args: Client *client, MessageTag *recv_mtags, int parc, char *parv[]
	Client *acptr, *from;
	int tls, localhost;
	const char *serv;

	serv = NULL;
	from = (IsUser(client) ? client : NULL); // Respond to client executing command by default

	// In case of forwarding, first arg is user to respond to and second is the server we gettin info fer
	if(IsServer(client) && !BadPtr(parv[1]) && !BadPtr(parv[2])) {
		from = find_user(parv[1], NULL);
		serv = parv[2];
	}

	// Return silently if client executing command is not a person or wasn't found, or was a server forward without enough args
	if(!from)
		return;

	// Checkem lol
	list_for_each_entry(acptr, &global_server_list, client_node) {
		// Skip ourselves obv, also if we got a request for a specific server and this isn't the one ;]
		if(IsMe(acptr) || (serv && strcasecmp(acptr->name, serv)))
			continue;

		// Can only get proper status if the server is directly linked to us =]
		if(acptr->uplink != &me) {
			// Only forward if the user requesting this shit is local to us imo
			if(MyUser(from))
				sendto_one(acptr->uplink, NULL, ":%s %s %s :%s", me.id, MSG_PUBNETINFO, from->name, acptr->name);
			continue;
		}

		// Initialise to unknown (fallback) ;]
		tls = -1;
		localhost = -1;

		// Checkem link config
		if(acptr->server->conf)
			tls = ((acptr->server->conf->outgoing.options & CONNECT_TLS) ? 1 : 0);

		// Checkem IP
		if(acptr->ip)
			localhost = is_loopback_ip(acptr->ip);

		// Shit out status string
		sendnotice(from, "[pubnetinfo] Link %s: localhost connection = %s -- SSL/TLS = %s",
			acptr->name,
			(localhost == 1 ? "yes" : (localhost == 0 ? "no" : "unknown")),
			(tls == 1 ? "yes" : (tls == 0 ? "no" : "unknown"))
		);
	}
}
