/* Copyright (C) All Rights Reserved
** Written by Gottem <support@gottem.nl>
** Website: https://gottem.nl/unreal
** License: https://gottem.nl/unreal/license
*/

/*** <<<MODULE MANAGER START>>>
module {
	documentation "https://gottem.nl/unreal/man/getlegitusers";
	troubleshooting "In case of problems, check the FAQ at https://gottem.nl/unreal/halp or e-mail me at support@gottem.nl";
	min-unrealircd-version "6.*";
	//max-unrealircd-version "6.*";
	post-install-text {
		"The module is installed, now all you need to do is add a 'loadmodule' line to your config file:";
		"loadmodule \"third/getlegitusers\";";
		"Then /rehash the IRCd.";
		"For usage information, refer to the module's documentation found at: https://gottem.nl/unreal/man/getlegitusers";
	}
}
*** <<<MODULE MANAGER END>>>
*/

#include "unrealircd.h"

#define MSG_GETLEGITUSERS "GETLEGITUSERS" // Actual command name

#define CheckAPIError(apistr, apiobj) \
	do { \
		if(!(apiobj)) { \
			config_error("A critical error occurred on %s for %s: %s", (apistr), MOD_HEADER.name, ModuleGetErrorStr(modinfo->handle)); \
			return MOD_FAILED; \
		} \
	} while(0)

CMD_FUNC(getlegitusers); // Register that shit

// Quality module header yo
ModuleHeader MOD_HEADER = {
	"third/getlegitusers",
	"2.1.0", // Version
	"Command /getlegitusers to show user/bot count across the network",
	"Gottem", // Author
	"unrealircd-6", // Modversion
};

// Module initialisation
MOD_INIT() {
	CheckAPIError("CommandAdd(GETLEGITUSERS)", CommandAdd(modinfo->handle, MSG_GETLEGITUSERS, getlegitusers, 1, CMD_USER));

	MARK_AS_GLOBAL_MODULE(modinfo);
	return MOD_SUCCESS;
}

MOD_LOAD() {
	return MOD_SUCCESS; // We good fam
}

// Called on mod unload
MOD_UNLOAD() {
	return MOD_SUCCESS;
}

CMD_FUNC(getlegitusers) {
	long total, legit, bots; // Just some counters lol
	Client *acptr; // Client pointer for the iteration of the client list

	if(!IsUser(client) || !IsOper(client)) { // Is the thing executing the command even a user and opered up?
		sendnumeric(client, ERR_NOPRIVILEGES); // Lolnope
		return;
	}

	bots = total = legit = 0; // Initialise dem counters

	// Iterate over all known clients
	list_for_each_entry(acptr, &client_list, client_node) {
		if(acptr->user) { // Sanity check
			total++; // Increment total count since this IS a user
			if(IsULine(acptr)) {
				bots++; // Surely this must be a bot then
				continue;
			}
			if(acptr->user->joined > 0) // But are they joined to more than one chan ?
				legit++; // Increment legitimate user count
			else
				sendnotice(client, "*** [getlegitusers] Found unknown user %s!%s@%s", acptr->name, acptr->user->username, acptr->user->realhost);
		}
	}

	// Server notice to the executing oper
	sendnotice(client, "*** [getlegitusers] %ld clients are on at least one channel and %ld are not present on any channel. The other %ld are services agents.", legit, total - (legit + bots), bots);
}
