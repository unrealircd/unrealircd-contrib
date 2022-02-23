/* Copyright (C) All Rights Reserved
** Written by Gottem <support@gottem.nl>
** Website: https://gottem.nl/unreal
** License: https://gottem.nl/unreal/license
*/

/*** <<<MODULE MANAGER START>>>
module {
	documentation "https://gottem.nl/unreal/man/autovhost";
	troubleshooting "In case of problems, check the FAQ at https://gottem.nl/unreal/halp or e-mail me at support@gottem.nl";
	min-unrealircd-version "6.*";
	//max-unrealircd-version "6.*";
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
	"2.1.0", // Version
	"Apply vhosts at connect time based on users' raw nick formats or IPs", // Description
	"Gottem", // Author
	"unrealircd-6", // Modversion
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
	if(!ce || !ce->name)
		return 0;

	// If it isn't our block, idc
	if(strcmp(ce->name, MYCONF))
		return 0;

	// Loop dat shyte fam
	for(cep = ce->items; cep; cep = cep->next) {
		// Do we even have a valid name l0l?
		if(!cep->name) {
			config_error("%s:%i: blank %s item", cep->file->filename, cep->line_number, MYCONF); // Rep0t error
			errors++; // Increment err0r count fam
			continue; // Next iteration imo tbh
		}

		if(!cep->value) {
			config_error("%s:%i: blank %s value", cep->file->filename, cep->line_number, MYCONF); // Rep0t error
			errors++; // Increment err0r count fam
			continue; // Next iteration imo tbh
		}

		if(strlen(cep->value) <= 5) {
			config_error("%s:%i: vhost should be at least 5 characters (%s)", cep->file->filename, cep->line_number, cep->value); // Rep0t error
			errors++; // Increment err0r count fam
			continue; // Next iteration imo tbh
		}

		if(strchr(cep->value, '@')) {
			config_error("%s:%i: should only use the hostname part for vhosts (%s)", cep->file->filename, cep->line_number, cep->value); // Rep0t error
			errors++; // Increment err0r count fam
			continue; // Next iteration imo tbh
		}

		if(strchr(cep->value, '!')) {
			config_error("%s:%i: vhosts can't contain a nick or ident part (%s)", cep->file->filename, cep->line_number, cep->value); // Rep0t error
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
	if(!ce || !ce->name)
		return 0;

	// If it isn't autovhost, idc
	if(strcmp(ce->name, MYCONF))
		return 0;

		// Loop dat shyte fam
	for(cep = ce->items; cep; cep = cep->next) {
		// Do we even have a valid name l0l?
		if(!cep->name || !cep->value)
			continue; // Next iteration imo tbh

		// Lengths to alloc8 the struct vars with in a bit
		size_t masklen = sizeof(char) * (strlen(cep->name) + 1);
		size_t vhostlen = sizeof(char) * (strlen(cep->value) + 1);

		// Allocate mem0ry for the current entry
		*vEntry = safe_alloc(sizeof(vhostEntry));

		// Allocate/initialise shit here
		(*vEntry)->mask = safe_alloc(masklen);
		(*vEntry)->vhost = safe_alloc(vhostlen);

		// Copy that shit fam
		strncpy((*vEntry)->mask, cep->name, masklen);
		strncpy((*vEntry)->vhost, cep->value, vhostlen);

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
				if(!doident && !valid_host(newhost_nick, 0)) { // Can't really do this earlier because of $nick etc ;];]
					unreal_log(ULOG_ERROR, "autovhost", "AUTOVHOST_INVALID", client, "Invalid result vhost: $mask => $result",
						log_data_string("mask", vEntry->vhost),
						log_data_string("result", newhost_nick)
					);
					safe_free(newhost_nick);
					break;
				}
				snprintf(newhost, sizeof(newhost), "%s", newhost_nick);
				safe_free(newhost_nick);
			}

			if(doident) {
				newhost_ident = replaceem(newhost, "$ident", ((client->user && client->user->username) ? client->user->username : "unknown"));
				if(!valid_host(newhost_ident, 0)) { // Can't really do this earlier because of $ident etc ;];]
					unreal_log(ULOG_ERROR, "autovhost", "AUTOVHOST_INVALID", client, "Invalid result vhost: $mask => $result",
						log_data_string("mask", vEntry->vhost),
						log_data_string("result", newhost_ident)
					);
					safe_free(newhost_nick);
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
			safe_strdup(client->user->virthost, newhost);
			client->umodes |= UMODE_HIDE;
			client->umodes |= UMODE_SETHOST;
			sendto_server(client, 0, 0, NULL, ":%s SETHOST %s", client->id, newhost);
			sendto_one(client, NULL, ":%s MODE %s :+tx", client->name, client->name);
			sendnotice(client, "*** Your vhost has been changed to %s", newhost);
			userhost_changed(client); // m0ar CAP shit
			break;
		}
	}
	return HOOK_CONTINUE;
}
