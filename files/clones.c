/* Copyright (C) All Rights Reserved
** Written by Gottem <support@gottem.nl>
** Website: https://gottem.nl/unreal
** License: https://gottem.nl/unreal/license
*/

/*** <<<MODULE MANAGER START>>>
module {
	documentation "https://gottem.nl/unreal/man/clones";
	troubleshooting "In case of problems, check the FAQ at https://gottem.nl/unreal/halp or e-mail me at support@gottem.nl";
	min-unrealircd-version "6.*";
	//max-unrealircd-version "6.*";
	post-install-text {
		"The module is installed, now all you need to do is add a 'loadmodule' line to your config file:";
		"loadmodule \"third/clones\";";
		"Then /rehash the IRCd.";
		"For usage information, refer to the module's documentation found at: https://gottem.nl/unreal/man/clones";
	}
}
*** <<<MODULE MANAGER END>>>
*/

// One include for all cross-platform compatibility thangs
#include "unrealircd.h"

#define MSG_CLONES "CLONES"

#define IsParam(x) (parc > (x) && !BadPtr(parv[(x)]))
#define IsNotParam(x) (parc <= (x) || BadPtr(parv[(x)]))

#define CheckAPIError(apistr, apiobj) \
	do { \
		if(!(apiobj)) { \
			config_error("A critical error occurred on %s for %s: %s", (apistr), MOD_HEADER.name, ModuleGetErrorStr(modinfo->handle)); \
			return MOD_FAILED; \
		} \
	} while(0)

static void dumpit(Client *client, char **p);
CMD_FUNC(clones);

ModuleHeader MOD_HEADER = {
	"third/clones",
	"2.1.0", // Version
	"Adds a command /CLONES to list all users having the same IP address matching the given options",
	"Gottem", // Author
	"unrealircd-6", // Modversion
};

MOD_INIT() {
	CheckAPIError("CommandAdd(CLONES)", CommandAdd(modinfo->handle, MSG_CLONES, clones, 3, CMD_USER));

	MARK_AS_GLOBAL_MODULE(modinfo);
	return MOD_SUCCESS;
}

MOD_LOAD() {
	return MOD_SUCCESS;
}

MOD_UNLOAD() {
	return MOD_SUCCESS;
}

// Dump a NULL-terminated array of strings to the user (taken from DarkFire IRCd)
static void dumpit(Client *client, char **p) {
	if(IsServer(client)) // Bail out early and silently if it's a server =]
		return;

	// Using sendto_one() instead of sendnumericfmt() because the latter strips indentation and stuff ;]
	for(; *p != NULL; p++)
		sendto_one(client, NULL, ":%s %03d %s :%s", me.name, RPL_TEXT, client->name, *p);
}

static char *clones_halp[] = {
	"*** \002Help on /clones\002 ***",
	"Gives you a list of clones based on the specified options.",
	"Clones are listed by a nickname or by a minimal number of",
	"concurrent sessions connecting from the local or the given",
	"server.",
	" ",
	"Syntax:",
	"CLONES <\037min-num-of-sessions|nickname\037> [\037server\037]",
	" ",
	"Examples:",
	"  /clones 2",
	"    Lists local clones having two or more sessions",
	"  /clones Loser",
	"    Lists local clones of Loser",
	"  /clones 3 hub.test.com",
	"    Lists all clones with at least 3 sessions, which are",
	"    connecting from hub.test.com",
	NULL
};

CMD_FUNC(clones) {
	Client *acptr, *acptr2;
	u_int min, count, found = 0;

	if(!IsUser(client))
		return;

	if(!IsOper(client)) {
		sendnumeric(client, ERR_NOPRIVILEGES);
		return;
	}

	if(!IsParam(1)) {
		dumpit(client, clones_halp);
		return;
	}

	if(IsParam(2)) {
		if(hunt_server(client, NULL, "CLONES", 2, parc, parv) != HUNTED_ISME)
			return;
	}

	if(isdigit(*parv[1])) {
		if((min = atoi(parv[1])) < 2) {
			sendnotice(client, "*** Invalid minimum clone count number (%s)", parv[1]);
			return;
		}

		list_for_each_entry(acptr, &client_list, client_node) {
			if(!IsUser(acptr) || !acptr->local)
				continue;

			count = 1; // Always include acptr in the count =]

			list_for_each_entry(acptr2, &client_list, client_node) {
				if(!IsUser(acptr2) || acptr == acptr2 || !acptr2->local)
					continue;

				if(!strcmp(acptr->ip, acptr2->ip))
					count++;
			}

			if(count >= min) {
				found++;
				sendnotice(client, "%s (%s!%s@%s)", acptr->ip, acptr->name, acptr->user->username, acptr->user->realhost);
			}
		}
	}
	else {
		if(!(acptr = find_user(parv[1], NULL))) {
			sendnumeric(client, ERR_NOSUCHNICK, parv[1]);
			return;
		}

		if(!MyConnect(acptr)) {
			sendnotice(client, "*** %s is not my client", acptr->name);
			return;
		}

		found = 0;
		list_for_each_entry(acptr2, &client_list, client_node) {
			if(!IsUser(acptr2) || acptr == acptr2 || !acptr2->local)
				continue;

			if(!strcmp(acptr->ip, acptr2->ip)) {
				found++;
				sendnotice(client, "%s (%s!%s@%s)", acptr2->ip, acptr2->name, acptr2->user->username, acptr2->user->realhost);
			}
		}
	}

	sendnotice(client, "%d clone%s found", found, (!found || found > 1) ? "s" : "");
}
