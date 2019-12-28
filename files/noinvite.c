/* Copyright (C) All Rights Reserved
** Written by Gottem <support@gottem.nl>
** Website: https://gottem.nl/unreal
** License: https://gottem.nl/unreal/license
*/

/*** <<<MODULE MANAGER START>>>
module {
	documentation "https://gottem.nl/unreal/man/noinvite";
	troubleshooting "In case of problems, check the FAQ at https://gottem.nl/unreal/halp or e-mail me at support@gottem.nl";
	min-unrealircd-version "5.*";
	//max-unrealircd-version "5.*";
	post-install-text {
		"The module is installed, now all you need to do is add a 'loadmodule' line to your config file:";
		"loadmodule \"third/noinvite\";";
		"Then /rehash the IRCd.";
		"For usage information, refer to the module's documentation found at: https://gottem.nl/unreal/man/noinvite";
	}
}
*** <<<MODULE MANAGER END>>>
*/

// One include for all cross-platform compatibility thangs
#include "unrealircd.h"

#define UMODE_FLAG 'N' // No invite

// Commands to override
#define OVR_INVITE "INVITE"

#define CheckAPIError(apistr, apiobj) \
	do { \
		if(!(apiobj)) { \
			config_error("A critical error occurred on %s for %s: %s", (apistr), MOD_HEADER.name, ModuleGetErrorStr(modinfo->handle)); \
			return MOD_FAILED; \
		} \
	} while(0)

// Quality fowod declarations
int noinvite_hook(Client *client, Client *target, Channel *channel, int *override);

long extumode_noinvite = 0L; // Store bitwise value latur

// Dat dere module header
ModuleHeader MOD_HEADER = {
	"third/noinvite", // Module name
	"2.0", // Version
	"Adds umode +N to block invites", // Description
	"Gottem", // Author
	"unrealircd-5", // Modversion
};

// Initialisation routine (register hooks, commands and modes or create structs etc)
MOD_INIT() {
	// Add a global umode (i.e. propagate to all servers), allow anyone to set/remove it on themselves
	CheckAPIError("UmodeAdd(extumode_noinvite)", UmodeAdd(modinfo->handle, UMODE_FLAG, UMODE_GLOBAL, 0, NULL, &extumode_noinvite));

	MARK_AS_GLOBAL_MODULE(modinfo);

	HookAdd(modinfo->handle, HOOKTYPE_PRE_INVITE, 0, noinvite_hook);
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

int noinvite_hook(Client *client, Client *target, Channel *channel, int *override) {
	if(!IsULine(client) && !IsOper(client) && (target->umodes & extumode_noinvite)) {
		sendto_one(client, NULL, ":%s NOTICE %s :%s has blocked all invites", me.name, channel->chname, target->name);
		return HOOK_DENY;
	}
	return HOOK_CONTINUE;
}
