/* Copyright (C) All Rights Reserved
** Written by Gottem <support@gottem.nl>
** Website: https://gottem.nl/unreal
** License: https://gottem.nl/unreal/license
*/

/*** <<<MODULE MANAGER START>>>
module {
	documentation "https://gottem.nl/unreal/man/operoverride_ext";
	troubleshooting "In case of problems, check the FAQ at https://gottem.nl/unreal/halp or e-mail me at support@gottem.nl";
	min-unrealircd-version "5.*";
	//max-unrealircd-version "5.*";
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

int operoverride_ext_hook_prelocaljoin(Client *client, Channel *channel, char *parv[]);

ModuleHeader MOD_HEADER = {
	"third/operoverride_ext",
	"2.0.1",
	"Additional OperOverride functionality",
	"Gottem", // Author
	"unrealircd-5", // Modversion
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

int operoverride_ext_hook_prelocaljoin(Client *client, Channel *channel, char *parv[]) {
	int i;
	char canjoin[3];
	char oomsg[512];

	// Don't even bother if other restrictions apply
	if(!can_join(client, channel, NULL, parv))
		return HOOK_CONTINUE;

	if(!IsOper(client) && IsULine(client))
		return HOOK_CONTINUE;

	oomsg[0] = '\0';
	canjoin[0] = '1';
	canjoin[1] = '1';
	canjoin[2] = '1';

	if(is_banned(client, channel, BANCHK_JOIN, NULL, NULL)) {
		canjoin[0] = '0';
		if(ValidatePermissionsForPath("channel:override:message:ban", client, NULL, channel, NULL)) {
			canjoin[0] = '1';
			sprintf(oomsg, "joined %s through ban", channel->chname);
		}
	}

	if(has_channel_mode(channel, 'R') && !IsRegNick(client)) {
		canjoin[1] = '0';
		if(ValidatePermissionsForPath("channel:override:message:regonly", client, NULL, channel, NULL)) {
			canjoin[1] = '1';
			sprintf(oomsg, "joined +R channel %s without invite", channel->chname);
		}
	}

	if((channel->mode.mode & MODE_INVITEONLY) && !find_invex(channel, client)) {
		canjoin[2] = '0';
		if(ValidatePermissionsForPath("channel:override:invite:self", client, NULL, channel, NULL)) {
			canjoin[2] = '1';
			sprintf(oomsg, "joined +i channel %s without invite", channel->chname);
		}
	}

	for(i = 0; i < sizeof(canjoin); i++) {
		if(canjoin[i] == '0')
			return HOOK_CONTINUE;
	}

	if(oomsg[0]) {
		if(!IsULine(client)) {
			sendto_snomask(SNO_EYES, "*** OperOverride -- %s (%s@%s) %s", client->name, client->user->username, client->user->realhost, oomsg);
			ircd_log(LOG_OVERRIDE, "OVERRIDE: %s (%s@%s) %s", client->name, client->user->username, client->user->realhost, oomsg);
		}
		return HOOK_ALLOW;
	}
	return HOOK_CONTINUE;
}
