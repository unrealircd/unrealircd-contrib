/*
  This module provides command /CHGSWHOIS which does what it says on the tin

  This module changes a users "special whois" line to the string you specify

  Syntax: CHGSWHOIS username is a bastardó

  This module needs to be loaded on all servers on the network

  License: GPLv3

  Copyright Ⓒ 2022 Valerie Pond
*/
/*** <<<MODULE MANAGER START>>>
module {
	documentation "https://github.com/ValwareIRC/valware-unrealircd-mods/blob/main/chgswhois/README.md";
	troubleshooting "In case of problems, check the README or e-mail me at v.a.pond@outlook.com";
	min-unrealircd-version "6.*";
	
	post-install-text {
		"The module is installed, now all you need to do is add a 'loadmodule' line to your config file:";
		"loadmodule \"third/chgswhois\";";
		"Then /rehash the IRCd.";
		"This module have no configuration";
	}
}
*** <<<MODULE MANAGER END>>>
*/

#include "unrealircd.h"

// deFIIIINEs
#define CHGCMD "CHGSWHOIS"
#define DELCMD "DELSWHOIS"
#define CheckAPIError(apistr, apiobj) \
	do { \
		if(!(apiobj)) { \
			config_error("A critical error occurred on %s for %s: %s", (apistr), MOD_HEADER.name, ModuleGetErrorStr(modinfo->handle)); \
			return MOD_FAILED; \
		} \
	} while(0)
		
	
CMD_FUNC(CHGSWHOIS);
CMD_FUNC(DELSWHOIS);

ModuleHeader MOD_HEADER = {
	"third/chgswhois",
	"1.0",
	"Provides command /CHGSWHOIS and /DELSWHOIS for priviledged IRCops to change a users \"special whois\" line.",
	"Valware",
	"unrealircd-6",
};

MOD_INIT() {
	CheckAPIError("CommandAdd(CHGSWHOIS)", CommandAdd(modinfo->handle, CHGCMD, CHGSWHOIS, 2, CMD_USER));
	CheckAPIError("CommandAdd(DELSWHOIS)", CommandAdd(modinfo->handle, DELCMD, DELSWHOIS, 1, CMD_USER));
	MARK_AS_GLOBAL_MODULE(modinfo);
	return MOD_SUCCESS;
}

MOD_LOAD() {
	return MOD_SUCCESS;
}
MOD_UNLOAD() {
	return MOD_SUCCESS;
}

CMD_FUNC(CHGSWHOIS) {
	
	Client *splooge;
	char tag[HOSTLEN+1];
	char swhois[SWHOISLEN+1];
	*tag = *swhois = '\0';
	// we're somewhere in there!
	int priority = 5;
	
	if (!ValidatePermissionsForPath("client:set:name",client,NULL,NULL,NULL)) {
		sendnumeric(client, ERR_NOPRIVILEGES);
		return;
	}

	else if ((parc < 3) || !*parv[2]) {
		sendnumeric(client, ERR_NEEDMOREPARAMS, CHGCMD);
		return;
	}

	else if (strlen(parv[2]) > (SWHOISLEN)) {
		sendnotice(client, "*** CHGSWHOIS rejected: Too long");
		return;
	}

	else if (!(splooge = find_user(parv[1], NULL))) {
		sendnumeric(client, ERR_NOSUCHNICK, parv[1]);
		return;
	}
	strlcpy(tag, client->name, sizeof(tag));
	strlcpy(swhois, parv[2], sizeof(swhois));


	if (!IsULine(client))
		unreal_log(ULOG_INFO, "chgcmds", "CHGSWHOIS_COMMAND", client,
		           "CHGSWHOIS: $client changed the special whois of $target to be $new_swhois",
		           log_data_string("change_type", "swhois"),
		           log_data_client("target", splooge),
		           log_data_string("new_swhois", parv[2]));
	
	// Find and delete old swhois line
	
	swhois_delete(splooge, "chgswhoislmao", "*", &me, NULL);
	
	// Add our new one!
	swhois_add(splooge, "chgswhoislmao", priority, swhois, &me, NULL);
	return;
	
}

// copypasta ninja copies from abooove~~~
CMD_FUNC(DELSWHOIS) {
	
	Client *splooge;
	char tag[HOSTLEN+1];
	
	if (!ValidatePermissionsForPath("client:set:name",client,NULL,NULL,NULL)) {
		sendnumeric(client, ERR_NOPRIVILEGES);
		return;
	}

	else if ((parc < 2) || !*parv[1]) {
		sendnumeric(client, ERR_NEEDMOREPARAMS, CHGCMD);
		return;
	}

	else if (!(splooge = find_user(parv[1], NULL))) {
		sendnumeric(client, ERR_NOSUCHNICK, parv[1]);
		return;
	}

	if (!IsULine(client))
                unreal_log(ULOG_INFO, "chgcmds", "DELSWHOIS_COMMAND", client,
                           "CHGSWHOIS: $client deleted the special whois of $target.details",
                           log_data_string("change_type", "swhois"),
                           log_data_client("target", splooge));

	// Find and delete old swhois line
	swhois_delete(splooge, "chgswhoislmao", "*", &me, NULL);
	
	return;
	
}
		
