/* Copyright (C) All Rights Reserved
** Written by Gottem <support@gottem.nl>
** Website: https://gottem.nl/unreal
** License: https://gottem.nl/unreal/license
*/

/*** <<<MODULE MANAGER START>>>
module {
	documentation "https://gottem.nl/unreal/man/block_no_tls";
	troubleshooting "In case of problems, check the FAQ at https://gottem.nl/unreal/halp or e-mail me at support@gottem.nl";
	min-unrealircd-version "6.*";
	//max-unrealircd-version "6.*";
	post-install-text {
		"The module is installed, now all you need to do is add a 'loadmodule' line to your config file:";
		"loadmodule \"third/block_no_tls\";";
		"Then /rehash the IRCd.";
		"For usage information, refer to the module's documentation found at: https://gottem.nl/unreal/man/block_no_tls";
	}
}
*** <<<MODULE MANAGER END>>>
*/

// One include for all cross-platform compatibility thangs
#include "unrealircd.h"

// Command strings
#define MSG_BLOCK_NOTLS "BLOCKNOTLS"
#define MSG_UNBLOCK_NOTLS "UNBLOCKNOTLS"

// Dem macros yo
#define CheckAPIError(apistr, apiobj) \
	do { \
		if(!(apiobj)) { \
			config_error("A critical error occurred on %s for %s: %s", (apistr), MOD_HEADER.name, ModuleGetErrorStr(modinfo->handle)); \
			return MOD_FAILED; \
		} \
	} while(0)

CMD_FUNC(cmd_block_notls); // Register blocking command function
CMD_FUNC(cmd_unblock_notls); // Register unblocking command function

// Quality fowod declarations
int block_notls_hook_prelocalconnect(Client *client);

int blockem = 0;

// Dat dere module header
ModuleHeader MOD_HEADER = {
	"third/block_no_tls", // Module name
	"2.1.0", // Version
	"Allows privileged opers to temporarily block new, non-TLS (SSL) user connections", // Description
	"Gottem", // Author
	"unrealircd-6", // Modversion
};

// Initialisation routine (register hooks, commands and modes or create structs etc)
MOD_INIT() {
	MARK_AS_GLOBAL_MODULE(modinfo);

	HookAdd(modinfo->handle, HOOKTYPE_PRE_LOCAL_CONNECT, 0, block_notls_hook_prelocalconnect);
	CheckAPIError("CommandAdd(BLOCKNOTLS)", CommandAdd(modinfo->handle, MSG_BLOCK_NOTLS, cmd_block_notls, 0, CMD_USER));
	CheckAPIError("CommandAdd(UNBLOCKNOTLS)", CommandAdd(modinfo->handle, MSG_UNBLOCK_NOTLS, cmd_unblock_notls, 0, CMD_USER));
	return MOD_SUCCESS;
}

MOD_LOAD() {
	return MOD_SUCCESS; // We good
}

// Called on unload/rehash obv
MOD_UNLOAD() {
	blockem = 0;
	return MOD_SUCCESS; // We good
}

int block_notls_hook_prelocalconnect(Client *client) {
	if(blockem && !(client->local->listener->options & LISTENER_TLS)) {
		unreal_log(ULOG_INFO, "kill", "KILL_COMMAND", client, "Client killed: $client.details ([block_no_tls] tried to connect without SSL/TLS)");
		exit_client(client, NULL, "Non-TLS (SSL) connections are currently not allowed"); // Kbye
		return HOOK_DENY;
	}
	return HOOK_CONTINUE; // We good
}

CMD_FUNC(cmd_block_notls) {
	// Gets args: Client *client, MessageTag *recv_mtags, int parc, char *parv[]
	if(!IsUser(client)) // Double check imo
		return;

	if(!ValidatePermissionsForPath("blocknotls", client, NULL, NULL, NULL)) {
		sendnumeric(client, ERR_NOPRIVILEGES);
		return;
	}

	if(blockem) {
		sendnotice(client, "** [block_no_tls] Already blocking all new, non-TLS (SSL) \002user\002 connections");
		return;
	}

	// Local server notice, also broadcast em
	unreal_log(ULOG_INFO, "block_no_tls", "BLOCK_NO_TLS_CHANGED", client, "$client.details just enabled the blocking of all new, non-TLS (SSL) \002user\002 connections");
	sendto_server(client, 0, 0, NULL, ":%s %s", client->name, MSG_BLOCK_NOTLS);
	blockem = 1;
}

CMD_FUNC(cmd_unblock_notls) {
	if(!IsUser(client)) // Double check imo
		return;

	if(!ValidatePermissionsForPath("blocknotls", client, NULL, NULL, NULL)) {
		sendnumeric(client, ERR_NOPRIVILEGES);
		return;
	}

	if(!blockem) {
		sendnotice(client, "** [block_no_tls] Not currently blocking non-TLS (SSL) connections");
		return;
	}

	// Local server notice, also broadcast em
	unreal_log(ULOG_INFO, "block_no_tls", "BLOCK_NO_TLS_CHANGED", client, "$client.details just disabled the blocking of non-TLS (SSL) connections");
	sendto_server(client, 0, 0, NULL, ":%s %s", client->name, MSG_UNBLOCK_NOTLS);
	blockem = 0;
}
