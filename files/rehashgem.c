/* Copyright (C) All Rights Reserved
** Written by Gottem <support@gottem.nl>
** Website: https://gottem.nl/unreal
** License: https://gottem.nl/unreal/license
*/

/*** <<<MODULE MANAGER START>>>
module {
	documentation "https://gottem.nl/unreal/man/rehashgem";
	troubleshooting "In case of problems, check the FAQ at https://gottem.nl/unreal/halp or e-mail me at support@gottem.nl";
	min-unrealircd-version "5.*";
	//max-unrealircd-version "5.*";
	post-install-text {
		"The module is installed, now all you need to do is add a 'loadmodule' line to your config file:";
		"loadmodule \"third/rehashgem\";";
		"Then /rehash the IRCd.";
		"For usage information, refer to the module's documentation found at: https://gottem.nl/unreal/man/rehashgem";
	}
}
*** <<<MODULE MANAGER END>>>
*/

// One include for all cross-platform compatibility thangs
#include "unrealircd.h"

// Quality fowod declarations
int rehashgem_rehashflag(Client *client, char *flag);

// Dat dere module header
ModuleHeader MOD_HEADER = {
	"third/rehashgem", // Module name
	"2.0", // Version
	"Implements an additional rehash flag -gem", // Description
	"Gottem", // Author
	"unrealircd-5", // Modversion
};

// Initialisation routine (register hooks, commands and modes or create structs etc)
MOD_INIT() {
	MARK_AS_GLOBAL_MODULE(modinfo);

	// Add a hook with priority 0 (i.e. normal) that returns an int
	HookAdd(modinfo->handle, HOOKTYPE_REHASHFLAG, 0, rehashgem_rehashflag);
	return MOD_SUCCESS;
}

// Actually load the module here
MOD_LOAD() {
	return MOD_SUCCESS; // We good
}

// Called on unload/rehash obv
MOD_UNLOAD() {
	return MOD_SUCCESS; // We good
}

// Actual hewk function m8
int rehashgem_rehashflag(Client *client, char *flag) {
	Client *acptr; // For iterating em servers
	char *sflag; // For grabbing suffixes to "-gem" like "-gemssl"
	unsigned int scount;
	if(match_simple("-gem*", flag)) { // Got em match?
		scount = 0;
		sflag = (strlen(flag) > 4 ? (flag + 4) : NULL); // If the flag is longer than -gem, get the rest (dirty lil hack to make -gemssl etc work lel) =]
		sendto_snomask_global(SNO_SNOTICE, "*** [rehashgem] %s is broadcasting REHASH -%s to all other servers", client->name, (sflag ? sflag : "all"));

		// Rehash reports messages like "oper is remotely rehashing", "loading IRCd config" and "config loaded without problems" for every server, to _their local opers only_
		// Some messages also go to GLOBOPS (I think MOTD related shit) =]
		sendto_server(NULL, 0, 0, NULL, ":%s REHASH -global -%s", client->name, (sflag ? sflag : "all"));
		list_for_each_entry(acptr, &global_server_list, client_node) { // Still need to count em servers kek
			if(acptr == &me)
				continue;
			scount++;
		}
		sendto_snomask_global(SNO_SNOTICE, "*** [rehashgem] Done broadcasting to %d servers (might include services)", scount);
	}
	return HOOK_CONTINUE;
}
