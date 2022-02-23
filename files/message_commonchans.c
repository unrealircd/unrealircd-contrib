/* Copyright (C) All Rights Reserved
** Written by Gottem <support@gottem.nl>
** Website: https://gottem.nl/unreal
** License: https://gottem.nl/unreal/license
*/

/*** <<<MODULE MANAGER START>>>
module {
	documentation "https://gottem.nl/unreal/man/message_commonchans";
	troubleshooting "In case of problems, check the FAQ at https://gottem.nl/unreal/halp or e-mail me at support@gottem.nl";
	min-unrealircd-version "6.*";
	//max-unrealircd-version "6.*";
	post-install-text {
		"The module is installed, now all you need to do is add a 'loadmodule' line to your config file:";
		"loadmodule \"third/message_commonchans\";";
		"Then /rehash the IRCd.";
		"For usage information, refer to the module's documentation found at: https://gottem.nl/unreal/man/message_commonchans";
	}
}
*** <<<MODULE MANAGER END>>>
*/

// One include for all cross-platform compatibility thangs
#include "unrealircd.h"

#define UMODE_FLAG 'c'

#define CheckAPIError(apistr, apiobj) \
	do { \
		if(!(apiobj)) { \
			config_error("A critical error occurred on %s for %s: %s", (apistr), MOD_HEADER.name, ModuleGetErrorStr(modinfo->handle)); \
			return MOD_FAILED; \
		} \
	} while(0)

// Quality fowod declarations
int commchans_hook_cansend_user(Client *client, Client *to, const char **text, const char **errmsg, SendType sendtype);

long extumode_commonchans = 0; // Store bitwise value latur

// Dat dere module header
ModuleHeader MOD_HEADER = {
	"third/message_commonchans", // Module name
	"2.1.0", // Version
	"Adds umode +c to prevent people who aren't sharing a channel with you from messaging you", // Description
	"Gottem", // Author
	"unrealircd-6", // Modversion
};

// Initialisation routine (register hooks, commands and modes or create structs etc)
MOD_INIT() {
	// Add a global umode (i.e. propagate to all servers), allow anyone to set/remove it on themselves
	CheckAPIError("UmodeAdd(extumode_commonchans)", UmodeAdd(modinfo->handle, UMODE_FLAG, UMODE_GLOBAL, 0, NULL, &extumode_commonchans));

	MARK_AS_GLOBAL_MODULE(modinfo);

	HookAdd(modinfo->handle, HOOKTYPE_CAN_SEND_TO_USER, -100, commchans_hook_cansend_user); // High priority lol
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

// Actual hewk function m8
int commchans_hook_cansend_user(Client *client, Client *to, const char **text, const char **errmsg, SendType sendtype) {
	if(sendtype != SEND_TYPE_PRIVMSG && sendtype != SEND_TYPE_NOTICE)
		return HOOK_CONTINUE;
	if(!text || !*text)
		return HOOK_CONTINUE;

	if(!MyUser(client) || client == to || IsULine(client) || IsULine(to) || IsOper(client) || !(to->umodes & extumode_commonchans) || has_common_channels(client, to))
		return HOOK_CONTINUE;

	sendnumeric(client, ERR_CANTSENDTOUSER, to->name, "You need to be on a common channel in order to privately message them");
	*text = NULL;

	// Can't return HOOK_DENY here cuz Unreal will abort() in that case :D
	return HOOK_CONTINUE;
}
