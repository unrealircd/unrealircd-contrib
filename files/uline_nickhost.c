/* Copyright (C) All Rights Reserved
** Written by Gottem <support@gottem.nl>
** Website: https://gottem.nl/unreal
** License: https://gottem.nl/unreal/license
*/

/*** <<<MODULE MANAGER START>>>
module {
	documentation "https://gottem.nl/unreal/man/uline_nickhost";
	troubleshooting "In case of problems, check the FAQ at https://gottem.nl/unreal/halp or e-mail me at support@gottem.nl";
	min-unrealircd-version "5.*";
	//max-unrealircd-version "5.*";
	post-install-text {
		"The module is installed, now all you need to do is add a 'loadmodule' line to your config file:";
		"loadmodule \"third/uline_nickhost\";";
		"Then /rehash the IRCd.";
		"For usage information, refer to the module's documentation found at: https://gottem.nl/unreal/man/uline_nickhost";
	}
}
*** <<<MODULE MANAGER END>>>
*/

// One include for all cross-platform compatibility thangs
#include "unrealircd.h"

// Commands to override
#define OVR_PRIVMSG "PRIVMSG"
#define OVR_NOTICE "NOTICE"

#define CheckAPIError(apistr, apiobj) \
	do { \
		if(!(apiobj)) { \
			config_error("A critical error occurred on %s for %s: %s", (apistr), MOD_HEADER.name, ModuleGetErrorStr(modinfo->handle)); \
			return MOD_FAILED; \
		} \
	} while(0)

// Quality fowod declarations
CMD_OVERRIDE_FUNC(uline_nickhost_override);

// Dat dere module header
ModuleHeader MOD_HEADER = {
	"third/uline_nickhost", // Module name
	"2.0", // Version
	"Requires people to address services like NickServ@services.my.net", // Description
	"Gottem", // Author
	"unrealircd-5", // Modversion
};

// Initialisation routine (register hooks, commands and modes or create structs etc)
MOD_INIT() {
	MARK_AS_GLOBAL_MODULE(modinfo);
	return MOD_SUCCESS;
}

// Actually load the module here (also command overrides as they may not exist in MOD_INIT yet)
MOD_LOAD() {
	CheckAPIError("CommandOverrideAdd(PRIVMSG)", CommandOverrideAdd(modinfo->handle, OVR_PRIVMSG, uline_nickhost_override));
	CheckAPIError("CommandOverrideAdd(NOTICE)", CommandOverrideAdd(modinfo->handle, OVR_NOTICE, uline_nickhost_override));
	return MOD_SUCCESS; // We good
}

// Called on unload/rehash obv
MOD_UNLOAD() {
	return MOD_SUCCESS; // We good
}

// Now for the actual override
CMD_OVERRIDE_FUNC(uline_nickhost_override) {
	// Gets args: CommandOverride *ovr, Client *client, MessageTag *recv_mtags, int parc, char *parv[]
	Client *acptr; // Pointer to target client
	char nickhost[NICKLEN + HOSTLEN + 2]; // Full nick@server mask thingy, HOSTLEN is the limit for server names anyways so ;]

	// Check argument sanity and see if we can find a target pointer (and if that's a U-Line as well)
	if(BadPtr(parv[1]) || !(acptr = find_person(parv[1], NULL)) || !IsULine(acptr)) {
		CallCommandOverride(ovr, client, recv_mtags, parc, parv); // Run original function yo
		return;
	}

	ircsnprintf(nickhost, sizeof(nickhost), "%s@%s", acptr->name, acptr->srvptr->name);
	if(strcasecmp(parv[1], nickhost)) { // Check if exact match (case-insensitivity is a go)
		sendnotice(client, "*** Please use %s when addressing services bots", nickhost); // Notice lol
		return;
	}
	CallCommandOverride(ovr, client, recv_mtags, parc, parv); // Run original function yo
}
