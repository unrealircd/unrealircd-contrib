/* Copyright (C) All Rights Reserved
** Written by Gottem <support@gottem.nl>
** Website: https://gottem.nl/unreal
** License: https://gottem.nl/unreal/license
*/

/*** <<<MODULE MANAGER START>>>
module {
	documentation "https://gottem.nl/unreal/man/operoverride_ext";
	troubleshooting "In case of problems, check the FAQ at https://gottem.nl/unreal/halp or e-mail me at support@gottem.nl";
	min-unrealircd-version "6.*";
	//max-unrealircd-version "6.*";
	post-install-text {
		"The module is installed, now all you need to do is add a 'loadmodule' line to your config file:";
		"loadmodule \"third/operoverride_ext\";";
		"Then /rehash the IRCd.";
		"For usage information, refer to the module's documentation found at: https://gottem.nl/unreal/man/operoverride_ext";
	}
}
*** <<<MODULE MANAGER END>>>
*/

#include "unrealircd.h"

int operoverride_ext_hook_prelocaljoin(Client *client, Channel *channel, const char *key);

ModuleHeader MOD_HEADER = {
	"third/operoverride_ext",
	"2.1.0", // Version
	"Additional OperOverride functionality",
	"Gottem", // Author
	"unrealircd-6", // Modversion
};

MOD_TEST() {
	return MOD_SUCCESS;
}

MOD_INIT() {
	MARK_AS_GLOBAL_MODULE(modinfo);

	HookAdd(modinfo->handle, HOOKTYPE_PRE_LOCAL_JOIN, 0, operoverride_ext_hook_prelocaljoin);
	return MOD_SUCCESS;
}

MOD_LOAD() {
	return MOD_SUCCESS;
}

MOD_UNLOAD() {
	return MOD_FAILED;
}

int operoverride_ext_hook_prelocaljoin(Client *client, Channel *channel, const char *key) {
	int i;
	int canjoin[3];
	char oomsg[512];
	const char *log_event;
	char *errmsg = NULL; // Actually won't be used but is necessary so can_join() won't break lol

	// can_join() actually returns 0 if we *can* join a channel, so we don't need to bother checking any further conditions
	// i.e. when can_join() == 0 then there is nothing to override ;]
	if(!can_join(client, channel, key, &errmsg))
		return HOOK_CONTINUE;

	if(!IsOper(client) && IsULine(client))
		return HOOK_CONTINUE;

	oomsg[0] = '\0';
	canjoin[0] = 1;
	canjoin[1] = 1;
	canjoin[2] = 1;
	log_event = "OPEROVERRIDE_EXT_UNKNOWN"; // Can't really happen but let's set something anyways =]

	if(is_banned(client, channel, BANCHK_JOIN, NULL, NULL)) {
		canjoin[0] = 0;
		if(ValidatePermissionsForPath("channel:override:message:ban", client, NULL, channel, NULL)) {
			canjoin[0] = 1;
			sprintf(oomsg, "joined %s through ban", channel->name);
			log_event = "OPEROVERRIDE_EXT_BAN";
		}
	}

	if(has_channel_mode(channel, 'R') && !IsRegNick(client)) {
		canjoin[1] = 0;
		if(ValidatePermissionsForPath("channel:override:message:regonly", client, NULL, channel, NULL)) {
			canjoin[1] = 1;
			sprintf(oomsg, "joined +R channel %s without invite", channel->name);
			log_event = "OPEROVERRIDE_EXT_REGONLY";
		}
	}

	if(has_channel_mode(channel, 'i') && !find_invex(channel, client)) {
		canjoin[2] = 0;
		if(ValidatePermissionsForPath("channel:override:invite:self", client, NULL, channel, NULL)) {
			canjoin[2] = 1;
			sprintf(oomsg, "joined +i channel %s without invite", channel->name);
			log_event = "OPEROVERRIDE_EXT_INVITE";
		}
	}

	for(i = 0; i < (sizeof(canjoin) / sizeof(canjoin[0])); i++) {
		if(canjoin[i] == 0)
			return HOOK_CONTINUE;
	}

	if(oomsg[0]) {
		if(!IsULine(client))
			unreal_log(ULOG_INFO, "operoverride_ext", log_event, client, "OperOverride -- $client.details $oomsg", log_data_string("oomsg", oomsg));
		return HOOK_ALLOW;
	}
	return HOOK_CONTINUE;
}
