/* Copyright (C) All Rights Reserved
** Written by Gottem <support@gottem.nl>
** Website: https://gottem.nl/unreal
** License: https://gottem.nl/unreal/license
*/

/*** <<<MODULE MANAGER START>>>
module {
	documentation "https://gottem.nl/unreal/man/extwarn";
	troubleshooting "In case of problems, check the FAQ at https://gottem.nl/unreal/halp or e-mail me at support@gottem.nl";
	min-unrealircd-version "6.*";
	//max-unrealircd-version "6.*";
	post-install-text {
		"The module is installed, now all you need to do is add a 'loadmodule' line to your config file:";
		"loadmodule \"third/extwarn\";";
		"Then /rehash the IRCd.";
		"For usage information, refer to the module's documentation found at: https://gottem.nl/unreal/man/extwarn";
	}
}
*** <<<MODULE MANAGER END>>>
*/

// One include for all cross-platform compatibility thangs
#include "unrealircd.h"

#define CheckAPIError(apistr, apiobj) \
	do { \
		if(!(apiobj)) { \
			config_error("A critical error occurred on %s for %s: %s", (apistr), MOD_HEADER.name, ModuleGetErrorStr(modinfo->handle)); \
			return MOD_FAILED; \
		} \
	} while(0)

// Quality fowod declarations
EVENT(extwarn_event);

// Dat dere module header
ModuleHeader MOD_HEADER = {
	"third/extwarn", // Module name
	"2.1.0", // Version
	"Enables additional configuration error checking", // Description
	"Gottem", // Author
	"unrealircd-6", // Modversion
};

// Initialisation routine (register hooks, commands and modes or create structs etc)
MOD_INIT() {
	// Delay event by 10 seconds so the config is fully available ;];]];
	CheckAPIError("EventAdd(extwarn_event)", EventAdd(modinfo->handle, "extwarn_event", extwarn_event, NULL, 10000, 1));

	MARK_AS_GLOBAL_MODULE(modinfo);
	return MOD_SUCCESS;
}

// Actually load the module here (also command overrides as they may not exist in MOD_INIT yet)
MOD_LOAD() {
	return MOD_SUCCESS;
}

// Called on unload/rehash obv
MOD_UNLOAD() {
	return MOD_SUCCESS; // We good
}

EVENT(extwarn_event) {
	// Check for missing operclasses lol
	ConfigItem_oper *oper;
	ConfigItem_operclass *operclass;
	for(oper = conf_oper; oper; oper = (ConfigItem_oper *)oper->next) { // Checkem configured opers
		if(!(operclass = find_operclass(oper->operclass))) // None found, throw warning yo
			config_warn("[extwarn] Unknown operclass '%s' found in oper block for '%s'", oper->operclass, oper->name);
	}
}
