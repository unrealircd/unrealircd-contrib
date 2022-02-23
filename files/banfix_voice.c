/* Copyright (C) All Rights Reserved
** Written by Gottem <support@gottem.nl>
** Website: https://gottem.nl/unreal
** License: https://gottem.nl/unreal/license
*/

/*** <<<MODULE MANAGER START>>>
module {
	documentation "https://gottem.nl/unreal/man/banfix_voice";
	troubleshooting "In case of problems, check the FAQ at https://gottem.nl/unreal/halp or e-mail me at support@gottem.nl";
	min-unrealircd-version "6.*";
	//max-unrealircd-version "6.*";
	post-install-text {
		"The module is installed, now all you need to do is add a 'loadmodule' line to your config file:";
		"loadmodule \"third/banfix_voice\";";
		"Then /rehash the IRCd.";
		"For usage information, refer to the module's documentation found at: https://gottem.nl/unreal/man/banfix_voice";
	}
}
*** <<<MODULE MANAGER END>>>
*/

#include "unrealircd.h"

#define CheckAPIError(apistr, apiobj) \
	do { \
		if(!(apiobj)) { \
			config_error("A critical error occurred on %s for %s: %s", (apistr), MOD_HEADER.name, ModuleGetErrorStr(modinfo->handle)); \
			return MOD_FAILED; \
		} \
	} while(0)

CMD_OVERRIDE_FUNC(check_banned_butvoiced);

// Mod header obv fam =]
ModuleHeader MOD_HEADER = {
	"third/banfix_voice",
	"2.1.0", // Version
	"Correct some odd behaviour in regards to banned-but-voiced users",
	"Gottem", // Author
	"unrealircd-6", // Modversion
};

// Initialise that shit
MOD_INIT() {
	MARK_AS_GLOBAL_MODULE(modinfo);
	return MOD_SUCCESS; // We good
}

// Here we actually load the m0d
MOD_LOAD() {
	CheckAPIError("CommandOverrideAdd(PRIVMSG)", CommandOverrideAdd(modinfo->handle, "PRIVMSG", 0, check_banned_butvoiced));
	CheckAPIError("CommandOverrideAdd(NOTICE)", CommandOverrideAdd(modinfo->handle, "NOTICE", 0, check_banned_butvoiced));
	return MOD_SUCCESS; // WE GOOD
}

MOD_UNLOAD() {
	return MOD_SUCCESS; // What can go wrong?
}

// Now for the actual override
CMD_OVERRIDE_FUNC(check_banned_butvoiced) {
	const char *target; // First argument is the target
	int v; // Has voice
	int noticed; // To store if this was a notice, also if b&
	Channel *channel; // Channel pointer obv

	// In case of U-Lines and opers, let's just pass it back to the core function
	if(MyUser(client) && !IsULine(client) && !IsOper(client) && !BadPtr(parv[1])) {
		target = parv[1];
		if(target[0] != '#') { // If first character of target isn't even #, bans don't apply at all, so...
			CallCommandOverride(ovr, client, recv_mtags, parc, parv); // Run original function yo
			return;
		}

		// is_banned() for BANCHK_MSG returns a true value for quiet bans nowadays, so we only need to check if the user is *just* voiced [[=[=[=[=[==[
		if((channel = find_channel(target)) && is_banned(client, channel, BANCHK_MSG, NULL, NULL)) {
			noticed = (strcmp(ovr->command->cmd, "NOTICE") == 0); // Was it an attempt to send a notice?
			v = check_channel_access(client, channel, "v"); // User has voice?

			// Apparently when you're banned (not necessarily ~quiet) and try to /notice #chan, you won't see a "you are banned" message, so let's fix dat too =]
			if((noticed || (v && !noticed)) && !check_channel_access(client, channel, "hoaq")) { // If not at least hop
				sendnumeric(client, ERR_CANNOTSENDTOCHAN, channel->name, "[BF] You are banned", target); // Send error
				return; // Stop processing
			}
		}
	}

	CallCommandOverride(ovr, client, recv_mtags, parc, parv); // Run original function yo
}
