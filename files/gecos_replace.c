/* Copyright (C) All Rights Reserved
** Written by Gottem <support@gottem.nl>
** Website: https://gottem.nl/unreal
** License: https://gottem.nl/unreal/license
*/

/*** <<<MODULE MANAGER START>>>
module {
	documentation "https://gottem.nl/unreal/man/gecos_replace";
	troubleshooting "In case of problems, check the FAQ at https://gottem.nl/unreal/halp or e-mail me at support@gottem.nl";
	min-unrealircd-version "5.*";
	//max-unrealircd-version "5.*";
	post-install-text {
		"The module is installed, now all you need to do is add a 'loadmodule' line to your config file:";
		"loadmodule \"third/gecos_replace\";";
		"Then /rehash the IRCd.";
		"For usage information, refer to the module's documentation found at: https://gottem.nl/unreal/man/gecos_replace";
	}
}
*** <<<MODULE MANAGER END>>>
*/

// One include for all cross-platform compatibility thangs
#include "unrealircd.h"

// Config block
#define MYCONF "gecos-replace"

// Hooktype to use
#define MYHEWK HOOKTYPE_PRE_LOCAL_CONNECT

// Big hecks go here
typedef struct t_gecos Gecos;
struct t_gecos {
	Gecos *prev, *next;
	char *match;
	char *replace;
};

// Quality fowod declarations
char *replaceem(char *str, char *search, char *replace);
int gecos_replace_hook_prelocalconnect(Client *client);
int gecos_replace_configtest(ConfigFile *cf, ConfigEntry *ce, int type, int *errs);
int gecos_replace_configposttest(int *errs); // You may not need this
int gecos_replace_configrun(ConfigFile *cf, ConfigEntry *ce, int type);

// Muh globals
Gecos *gecosList = NULL; // Premium linked list
int gecosCount = 0; // Counter yo

// Dat dere module header
ModuleHeader MOD_HEADER = {
	"third/gecos_replace", // Module name
	"1.0", // Version
	"Enables replacing text in the gecos field on-connect", // Description
	"Gottem", // Author
	"unrealircd-5", // Modversion
};

// Configuration testing-related hewks go in testing phase obv
MOD_TEST() {
	// We have our own config block so we need to checkem config obv m9
	// Priorities don't really matter here
	HookAdd(modinfo->handle, HOOKTYPE_CONFIGTEST, 0, gecos_replace_configtest);
	HookAdd(modinfo->handle, HOOKTYPE_CONFIGPOSTTEST, 0, gecos_replace_configposttest);
	return MOD_SUCCESS;
}

// Initialisation routine (register hooks, commands and modes or create structs etc)
MOD_INIT() {
	MARK_AS_GLOBAL_MODULE(modinfo);

	// Run very late so other access control stuff checks the original gecos ;]
	HookAdd(modinfo->handle, MYHEWK, 999, gecos_replace_hook_prelocalconnect);

	HookAdd(modinfo->handle, HOOKTYPE_CONFIGRUN, 0, gecos_replace_configrun);
	return MOD_SUCCESS;
}

MOD_LOAD() {
	return MOD_SUCCESS; // We good
}

// Called on unload/rehash obv
MOD_UNLOAD() {
	// Clean up any structs and other shit here
	Gecos *gecosEntry, *next;
	for(gecosEntry = gecosList; gecosEntry; gecosEntry = next) {
		next = gecosEntry->next;
		DelListItem(gecosEntry, gecosList);
		safe_free(gecosEntry->match);
		safe_free(gecosEntry->replace);
		safe_free(gecosEntry);
	}
	gecosList = NULL;
	gecosCount = 0;
	return MOD_SUCCESS; // We good
}

// Here you'll find some black magic to recursively/reentrantly (yes I think that's a word) replace shit in strings
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

// Actual hewk functions m8
int gecos_replace_hook_prelocalconnect(Client *client) {
	// Using a *pre* connect hewk cuz the user isn't fully online yet, so the new gecos should be displayed properly ;]
	char *newgecos;
	size_t newlen;

	// Let's not touch services ;]
	if(IsULine(client))
		return HOOK_CONTINUE;

	if(!client || !client->info) {
		sendto_realops_and_log("[BUG?] [%s] %s == NULL", MOD_HEADER.name, (client ? "Gecos" : "Client"));
		return HOOK_CONTINUE;
	}

	// Gecos is required afaik, but let's check it just in case :>
	if(!strlen(client->info))
		return HOOK_CONTINUE;

	Gecos *gecosEntry;
	for(gecosEntry = gecosList; gecosEntry; gecosEntry = gecosEntry->next) {
		if(!strcasecmp(gecosEntry->match, client->info)) {
			newgecos = replaceem(client->info, gecosEntry->match, gecosEntry->replace);
			if(!newgecos)
				continue;

			newlen = strlen(newgecos);
			if(newlen == 0) { // In case of a now-empty gecos, just set it to the current nick to avoid any issues with something expecting a non-NULL value ;]
				// REALLEN normally is higher than NICKLEN, but you never know what people might do :DD
				if(strlen(client->name) > REALLEN)
					sendto_realops_and_log("[%s] Resulting gecos for '%s' is too long ('%s' exceeds %d characters), it will be truncated", MOD_HEADER.name, client->name, newgecos, REALLEN);
				strlcpy(client->info, client->name, sizeof(client->info));
			}
			else {
				if(newlen > REALLEN)
					sendto_realops_and_log("[%s] Resulting gecos for '%s' is too long ('%s' exceeds %d characters), it will be truncated", MOD_HEADER.name, client->info, newgecos, REALLEN);
				strlcpy(client->info, newgecos, sizeof(client->info));
				safe_free(newgecos);
			}
			break;
		}
	}
	return HOOK_CONTINUE;
}

int gecos_replace_configtest(ConfigFile *cf, ConfigEntry *ce, int type, int *errs) {
	int errors = 0; // Error count
	ConfigEntry *cep; // To store the current variable/value pair etc
	int got_match, got_replace; // Required variables

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
	got_match = got_replace = 0;
	for(cep = ce->ce_entries; cep; cep = cep->ce_next) {
		// Do we even have a valid name l0l?
		// This should already be checked by Unreal's core functions but there's no harm in having it here too =]
		if(!cep->ce_varname) {
			config_error("%s:%i: blank %s item", cep->ce_fileptr->cf_filename, cep->ce_varlinenum, MYCONF); // Rep0t error
			errors++; // Increment err0r count fam
			continue; // Next iteration imo tbh
		}

		if(!cep->ce_vardata) {
			config_error("%s:%i: blank %s value", cep->ce_fileptr->cf_filename, cep->ce_varlinenum, MYCONF); // Rep0t error
			errors++;
			continue;
		}

		if(!strcmp(cep->ce_varname, "match")) {
			if(got_match) {
				config_error("%s:%i: duplicate %s::%s directive", cep->ce_fileptr->cf_filename, cep->ce_varlinenum, MYCONF, cep->ce_varname);
				errors++;
				continue;
			}

			got_match = 1;
			if(!strlen(cep->ce_vardata)) {
				config_error("%s:%i: %s::%s must be non-empty fam", cep->ce_fileptr->cf_filename, cep->ce_varlinenum, MYCONF, cep->ce_varname);
				errors++;
			}
			continue;
		}

		if(!strcmp(cep->ce_varname, "replace")) {
			if(got_replace) {
				config_error("%s:%i: duplicate %s::%s directive", cep->ce_fileptr->cf_filename, cep->ce_varlinenum, MYCONF, cep->ce_varname);
				errors++;
				continue;
			}

			got_replace = 1;
			continue;
		}

		// Anything else is unknown to us =]
		config_warn("%s:%i: unknown item %s::%s", cep->ce_fileptr->cf_filename, cep->ce_varlinenum, MYCONF, cep->ce_varname); // So display just a warning
	}

	if(!got_match) {
		config_error("%s:%i: missing %s::match directive", ce->ce_fileptr->cf_filename, ce->ce_varlinenum, MYCONF);
		errors++;
	}
	if(!got_replace) {
		config_error("%s:%i: missing %s::replace directive", ce->ce_fileptr->cf_filename, ce->ce_varlinenum, MYCONF);
		errors++;
	}

	if(got_match && got_replace)
		gecosCount++;

	*errs = errors;
	return errors ? -1 : 1; // Returning 1 means "all good", -1 means we shat our panties
}

// Post test, check for missing shit here
int gecos_replace_configposttest(int *errs) {
	if(!gecosCount)
		config_warn("Module %s was loaded but there are no (valid) %s { } blocks", MOD_HEADER.name, MYCONF);
	return 1;
}

// "Run" the config (everything should be valid at this point)
int gecos_replace_configrun(ConfigFile *cf, ConfigEntry *ce, int type) {
	ConfigEntry *cep; // To store the current variable/value pair etc
	Gecos *gecosEntry;

	// Since we'll add a top-level block to unrealircd.conf, need to filter on CONFIG_MAIN lmao
	if(type != CONFIG_MAIN)
		return 0; // Returning 0 means idgaf bout dis

	// Check for valid config entries first
	if(!ce || !ce->ce_varname)
		return 0;

	// If it isn't gecos_replace, idc
	if(strcmp(ce->ce_varname, MYCONF))
		return 0;

	// Loop dat shyte fam
	gecosEntry = safe_alloc(sizeof(Gecos));
	for(cep = ce->ce_entries; cep; cep = cep->ce_next) {
		// Do we even have a valid name l0l?
		if(!cep->ce_varname || !cep->ce_vardata)
			continue; // Next iteration imo tbh

		if(!strcmp(cep->ce_varname, "match")) {
			safe_strdup(gecosEntry->match, cep->ce_vardata);
			continue;
		}

		if(!strcmp(cep->ce_varname, "replace")) {
			safe_strdup(gecosEntry->replace, cep->ce_vardata);
			continue;
		}
	}
	AddListItem(gecosEntry, gecosList);

	return 1; // We good
}
