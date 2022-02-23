/* Copyright (C) All Rights Reserved
** Written by Gottem <support@gottem.nl>
** Website: https://gottem.nl/unreal
** License: https://gottem.nl/unreal/license
*/

/*** <<<MODULE MANAGER START>>>
module {
	documentation "https://gottem.nl/unreal/man/plainusers";
	troubleshooting "In case of problems, check the FAQ at https://gottem.nl/unreal/halp or e-mail me at support@gottem.nl";
	min-unrealircd-version "6.*";
	//max-unrealircd-version "6.*";
	post-install-text {
		"The module is installed, now all you need to do is add a 'loadmodule' line to your config file:";
		"loadmodule \"third/plainusers\";";
		"Then /rehash the IRCd.";
		"For usage information, refer to the module's documentation found at: https://gottem.nl/unreal/man/plainusers";
	}
}
*** <<<MODULE MANAGER END>>>
*/

// One include for all cross-platform compatibility thangs
#include "unrealircd.h"

// Command strings
#define MSG_PLAINUSERS "PLAINUSERS"
#define MSG_PLAINUSERS_SHORT "PUSERS"

// Dem macros yo
#define CheckAPIError(apistr, apiobj) \
	do { \
		if(!(apiobj)) { \
			config_error("A critical error occurred on %s for %s: %s", (apistr), MOD_HEADER.name, ModuleGetErrorStr(modinfo->handle)); \
			return MOD_FAILED; \
		} \
	} while(0)

CMD_FUNC(plainusers); // Register command function

// Dat dere module header
ModuleHeader MOD_HEADER = {
	"third/plainusers", // Module name
	"2.1.0", // Version
	"Allows opers to list all users NOT connected over SSL/TLS", // Description
	"Gottem", // Author
	"unrealircd-6", // Modversion
};

// Initialisation routine (register hooks, commands and modes or create structs etc)
MOD_INIT() {
	CheckAPIError("CommandAdd(PLAINUSERS)", CommandAdd(modinfo->handle, MSG_PLAINUSERS, plainusers, 0, CMD_USER));
	CheckAPIError("CommandAdd(PUSERS)", CommandAdd(modinfo->handle, MSG_PLAINUSERS_SHORT, plainusers, 0, CMD_USER));

	MARK_AS_GLOBAL_MODULE(modinfo);
	return MOD_SUCCESS;
}

// Actually load the module here (also command overrides as they may not exist in MOD_INIT yet)
MOD_LOAD() {
	return MOD_SUCCESS; // We good
}

// Called on unload/rehash obv
MOD_UNLOAD() {
	return MOD_SUCCESS; // We good
}

CMD_FUNC(plainusers) {
	// Gets args: Client *client, MessageTag *recv_mtags, int parc, char *parv[]
	char ubuf[224]; // For sending multiple nicks at once instead of spamming the fuck out of people
	Client *acptr; // For iteration lol
	int count; // Count em too yo
	if(!IsUser(client) || !IsOper(client)) {
		sendnumeric(client, ERR_NOPRIVILEGES); // lmao no privlelgdges
		return; // rip
	}

	memset(ubuf, '\0', sizeof(ubuf)); // Init 'em
	count = 0;
	list_for_each_entry(acptr, &client_list, client_node) { // Get clients lol
		if(!IsUser(acptr) || IsULine(acptr)) // Check if it's a person and NOT a U-Line (as they usually connect w/o SSL)
			continue;

		if((acptr->umodes & UMODE_SECURE)) // Check for umode +z (indic8s ur connected wit TLS etc)
			continue;

		count++; // Gottem

		if(!ubuf[0]) // First nick in this set
			strlcpy(ubuf, acptr->name, sizeof(ubuf)); // Need cpy instead of cat ;]
		else {
			strlcat(ubuf, ", ", sizeof(ubuf)); // Dat separator lol
			strlcat(ubuf, acptr->name, sizeof(ubuf)); // Now append non-first nikk =]
		}

		if(strlen(ubuf) > (sizeof(ubuf) - NICKLEN - 3)) { // If another nick won't fit (-3 cuz ", " and nullbyet)
			sendnotice(client, "[plainusers] Found: %s", ubuf); // Send what we have
			memset(ubuf, 0, sizeof(ubuf)); // And reset buffer lmoa
		}
	}
	if(ubuf[0]) // If we still have some nicks (i.e. we didn't exceed buf's size for the last set)
		sendnotice(client, "[plainusers] Found: %s", ubuf); // Dump whatever's left

	if(!count)
		sendnotice(client, "[plainusers] No non-SSL/TLS users found");
}
