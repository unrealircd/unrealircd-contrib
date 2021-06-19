/* Copyright (C) All Rights Reserved
** Written by Gottem <support@gottem.nl>
** Website: https://gottem.nl/unreal
** License: https://gottem.nl/unreal/license
*/

/*** <<<MODULE MANAGER START>>>
module {
	documentation "https://gottem.nl/unreal/man/autovhost";
	troubleshooting "In case of problems, check the FAQ at https://gottem.nl/unreal/halp or e-mail me at support@gottem.nl";
	min-unrealircd-version "5.*";
	//max-unrealircd-version "5.*";
	post-install-text {
		"The module is installed, now all you need to do is add a 'loadmodule' line to your config file:";
		"loadmodule \"third/autovhost\";";
		"Then /rehash the IRCd.";
		"For usage information, refer to the module's documentation found at: https://gottem.nl/unreal/man/autovhost";
	}
}
*** <<<MODULE MANAGER END>>>
*/

// One include for all cross-platform compatibility thangs
#include "unrealircd.h"

#define MYCONF "autovhost"

// Big hecks go here
typedef struct t_vhostEntry vhostEntry;
struct t_vhostEntry {
	char *mask;
	char *vhost;
	vhostEntry *next;
};

// Quality fowod declarations
char *replaceem(char *str, char *search, char *replace);
int autovhost_configtest(ConfigFile *cf, ConfigEntry *ce, int type, int *errs);
int autovhost_configposttest(int *errs);
int autovhost_configrun(ConfigFile *cf, ConfigEntry *ce, int type);
int autovhost_connect(Client *client);

vhostEntry *vhostList = NULL; // Stores the mask <-> vhost pairs
int vhostCount = 0;

// Dat dere module header
ModuleHeader MOD_HEADER = {
	"third/autovhost", // Module name
	"2.0", // Version
	"Apply vhosts at connect time based on users' raw nick formats or IPs", // Description
	"Gottem", // Author
	"unrealircd-5", // Modversion
};

// Configuration testing-related hewks go in testing phase obv
// This function is entirely optional
MOD_TEST() {
	// We have our own config block so we need to checkem config obv m9
	// Priorities don't really matter here
	HookAdd(modinfo->handle, HOOKTYPE_CONFIGTEST, 0, autovhost_configtest);
	HookAdd(modinfo->handle, HOOKTYPE_CONFIGPOSTTEST, 0, autovhost_configposttest);
	return MOD_SUCCESS;
}

// Initialisation routine (register hooks, commands and modes or create structs etc)
MOD_INIT() {
	MARK_AS_GLOBAL_MODULE(modinfo);

	HookAdd(modinfo->handle, HOOKTYPE_CONFIGRUN, 0, autovhost_configrun);
	HookAdd(modinfo->handle, HOOKTYPE_LOCAL_CONNECT, 0, autovhost_connect);
	return MOD_SUCCESS;
}

MOD_LOAD() {
	return MOD_SUCCESS; // We good
}

// Called on unload/rehash obv
MOD_UNLOAD() {
	if(vhostList) {
		// This shit is a bit convoluted to prevent memory issues obv famalmalmalmlmalm
		vhostEntry *vEntry;
		while((vEntry = vhostList) != NULL) {
			vhostList = vhostList->next;
			safe_free(vEntry->mask);
			safe_free(vEntry->vhost);
			safe_free(vEntry);
		}
		vhostList = NULL;
	}
	vhostCount = 0; // Just to maek shur
	return MOD_SUCCESS; // We good
}

char *replaceem(char *str, char *search, char *replace) {
	char *tok = NULL;
	char *newstr = NULL;
	char *oldstr = NULL;
	char *head = NULL;

	if(search == NULL || replace == NULL)
		return str;

	newstr = strdup(str);
	head = newstr;
	while((tok = strstr(head, search))) {
		oldstr = newstr;
		newstr = malloc(strlen(oldstr) - strlen(search) + strlen(replace) + 1);
		if(newstr == NULL) {
			free(oldstr);
			return str;
		}
		memcpy(newstr, oldstr, tok - oldstr);
		memcpy(newstr + (tok - oldstr), replace, strlen(replace));
		memcpy(newstr + (tok - oldstr) + strlen(replace), tok + strlen(search), strlen(oldstr) - strlen(search) - (tok - oldstr));
		memset(newstr + strlen(oldstr) - strlen(search) + strlen(replace), 0, 1);
		head = newstr + (tok - oldstr) + strlen(replace);
		free(oldstr);
	}
	return newstr;
}

int autovhost_configtest(ConfigFile *cf, ConfigEntry *ce, int type, int *errs) {
	int errors = 0; // Error count
	ConfigEntry *cep; // To store the current variable/value pair etc

	// Since we'll add a top-level block to unrealircd.conf, need to filter on CONFIG_MAIN lmao
	if(type != CONFIG_MAIN)
		return 0; // Returning 0 means idgaf bout dis

	// Check for valid config entries first
	if(!ce || !ce->ce_varname)
		return 0;

	// If it isn't our block, idc
	if(strcmp(ce->ce_varname, MYCONF))
		return 0;

	// Loop dat shyte fam
	for(cep = ce->ce_entries; cep; cep = cep->ce_next) {
		// Do we even have a valid name l0l?
		if(!cep->ce_varname) {
			config_error("%s:%i: blank %s item", cep->ce_fileptr->cf_filename, cep->ce_varlinenum, MYCONF); // Rep0t error
			errors++; // Increment err0r count fam
			continue; // Next iteration imo tbh
		}

		if(!cep->ce_vardata) {
			config_error("%s:%i: blank %s value", cep->ce_fileptr->cf_filename, cep->ce_varlinenum, MYCONF); // Rep0t error
			errors++; // Increment err0r count fam
			continue; // Next iteration imo tbh
		}

		if(strlen(cep->ce_vardata) < 3) {
			config_error("%s:%i: vhost should be at least 3 characters (%s)", cep->ce_fileptr->cf_filename, cep->ce_varlinenum, cep->ce_vardata); // Rep0t error
			errors++; // Increment err0r count fam
			continue; // Next iteration imo tbh
		}

		if(strchr(cep->ce_vardata, '@')) {
			config_error("%s:%i: should only use the hostname part for vhosts (%s)", cep->ce_fileptr->cf_filename, cep->ce_varlinenum, cep->ce_vardata); // Rep0t error
			errors++; // Increment err0r count fam
			continue; // Next iteration imo tbh
		}

		if(strchr(cep->ce_vardata, '!')) {
			config_error("%s:%i: vhosts can't contain a nick or ident part (%s)", cep->ce_fileptr->cf_filename, cep->ce_varlinenum, cep->ce_vardata); // Rep0t error
			errors++; // Increment err0r count fam
			continue; // Next iteration imo tbh
		}

		vhostCount++;
	}

	*errs = errors;
	// Returning 1 means "all good", -1 means we shat our panties
	return errors ? -1 : 1;
}

int autovhost_configposttest(int *errs) {
	if(!vhostCount)
		config_warn("%s was loaded but there aren't any configured vhost entries (autovhost {} block)", MOD_HEADER.name);
	return 1;
}

// "Run" the config (everything should be valid at this point)
int autovhost_configrun(ConfigFile *cf, ConfigEntry *ce, int type) {
	ConfigEntry *cep; // To store the current variable/value pair etc
	vhostEntry *last = NULL; // Initialise to NULL so the loop requires minimal l0gic
	vhostEntry **vEntry = &vhostList; // Hecks so the ->next chain stays intact

	// Since we'll add a top-level block to unrealircd.conf, need to filter on CONFIG_MAIN lmao
	if(type != CONFIG_MAIN)
		return 0; // Returning 0 means idgaf bout dis

	// Check for valid config entries first
	if(!ce || !ce->ce_varname)
		return 0;

	// If it isn't autovhost, idc
	if(strcmp(ce->ce_varname, MYCONF))
		return 0;

		// Loop dat shyte fam
	for(cep = ce->ce_entries; cep; cep = cep->ce_next) {
		// Do we even have a valid name l0l?
		if(!cep->ce_varname || !cep->ce_vardata)
			continue; // Next iteration imo tbh

		// Lengths to alloc8 the struct vars with in a bit
		size_t masklen = sizeof(char) * (strlen(cep->ce_varname) + 1);
		size_t vhostlen = sizeof(char) * (strlen(cep->ce_vardata) + 1);

		// Allocate mem0ry for the current entry
		*vEntry = safe_alloc(sizeof(vhostEntry));

		// Allocate/initialise shit here
		(*vEntry)->mask = safe_alloc(masklen);
		(*vEntry)->vhost = safe_alloc(vhostlen);

		// Copy that shit fam
		strncpy((*vEntry)->mask, cep->ce_varname, masklen);
		strncpy((*vEntry)->vhost, cep->ce_vardata, vhostlen);

		// Premium linked list fam
		if(last)
			last->next = *vEntry;

		last = *vEntry;
		vEntry = &(*vEntry)->next;
	}

	return 1; // We good
}

int autovhost_connect(Client *client) {
	if(!client || !client->user)
		return HOOK_CONTINUE;

	vhostEntry *vEntry;
	char *newhost_nick, *newhost_ident, newhost[HOSTLEN];
	int doident;
	for(vEntry = vhostList; vEntry; vEntry = vEntry->next) {
		// Check if the mask matches the user's full nick mask (with REAL host) or IP
		if(match_simple(vEntry->mask, make_nick_user_host(client->name, client->user->username, client->user->realhost)) || match_simple(vEntry->mask, GetIP(client))) {
			snprintf(newhost, sizeof(newhost), "%s", vEntry->vhost);
			doident = (strstr(newhost, "$ident") ? 1 : 0);
			if(strstr(newhost, "$nick")) {
				newhost_nick = replaceem(newhost, "$nick", client->name);
				if(!doident && !valid_host(newhost_nick)) { // Can't really do this earlier because of $nick etc ;];]
					sendto_snomask_global(SNO_EYES, "[autovhost] Invalid result vhost: %s => %s", vEntry->vhost, newhost_nick);
					safe_free(newhost_nick);
					break;
				}
				snprintf(newhost, sizeof(newhost), "%s", newhost_nick);
				safe_free(newhost_nick);
			}

			if(doident) {
				newhost_ident = replaceem(newhost, "$ident", ((client->user && client->user->username) ? client->user->username : "unknown"));
				if(!valid_host(newhost_ident)) { // Can't really do this earlier because of $ident etc ;];]
					sendto_snomask_global(SNO_EYES, "[autovhost] Invalid result vhost: %s => %s", vEntry->vhost, newhost_ident);
					safe_free(newhost_ident);
					break;
				}
				snprintf(newhost, sizeof(newhost), "%s", newhost_ident);
				safe_free(newhost_ident);
			}

			if(strlen(newhost) > HOSTLEN)
				newhost[HOSTLEN - 1] = '\0';

			userhost_save_current(client); // Need to do this to take care of CAP capable clients etc

			// Actually set the new one here
			sendnotice(client, "*** Setting your cloaked host to %s", newhost);
			safe_strdup(client->user->virthost, newhost);
			sendto_server(NULL, 0, 0, NULL, ":%s SETHOST %s", client->name, newhost); // Broadcast to other servers too ;]
			userhost_changed(client); // m0ar CAP shit
			break;
		}
	}
	return HOOK_CONTINUE;
}
