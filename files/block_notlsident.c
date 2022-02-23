/* Copyright (C) All Rights Reserved
** Written by Gottem <support@gottem.nl>
** Website: https://gottem.nl/unreal
** License: https://gottem.nl/unreal/license
*/

/*** <<<MODULE MANAGER START>>>
module {
	documentation "https://gottem.nl/unreal/man/block_notlsident";
	troubleshooting "In case of problems, check the FAQ at https://gottem.nl/unreal/halp or e-mail me at support@gottem.nl";
	min-unrealircd-version "6.*";
	//max-unrealircd-version "6.*";
	post-install-text {
		"The module is installed, now all you need to do is add a 'loadmodule' line to your config file:";
		"loadmodule \"third/block_notlsident\";";
		"Then /rehash the IRCd.";
		"For usage information, refer to the module's documentation found at: https://gottem.nl/unreal/man/block_notlsident";
	}
}
*** <<<MODULE MANAGER END>>>
*/

// One include for all cross-platform compatibility thangs
#include "unrealircd.h"

// Config bl0ck
#define MYCONF "block_notlsident"

// Quality fowod declarations
int find_bident(char *ident);
int block_notlsident_configtest(ConfigFile *cf, ConfigEntry *ce, int type, int *errs);
int block_notlsident_configposttest(int *errs); // You may not need this
int block_notlsident_configrun(ConfigFile *cf, ConfigEntry *ce, int type);
int block_notlsident_prelocalconnect(Client *client);

// Config vars
char bicount = 0;
char **blockedIdents; // Dynamic array ;]

// Dat dere module header
ModuleHeader MOD_HEADER = {
	"third/block_notlsident", // Module name
	"2.1.0", // Version
	"Restrict certain idents to SSL/TLS connections only", // Description
	"Gottem", // Author
	"unrealircd-6", // Modversion
};

// Configuration testing-related hewks go in testing phase obv
MOD_TEST() {
	// We have our own config block so we need to checkem config obv m9
	// Priorities don't really matter here
	HookAdd(modinfo->handle, HOOKTYPE_CONFIGTEST, 0, block_notlsident_configtest);
	HookAdd(modinfo->handle, HOOKTYPE_CONFIGPOSTTEST, 0, block_notlsident_configposttest);
	return MOD_SUCCESS;
}

// Initialisation routine (register hooks, commands and modes or create structs etc)
MOD_INIT() {
	MARK_AS_GLOBAL_MODULE(modinfo);

	HookAdd(modinfo->handle, HOOKTYPE_CONFIGRUN, 0, block_notlsident_configrun);
	HookAdd(modinfo->handle, HOOKTYPE_PRE_LOCAL_CONNECT, 0, block_notlsident_prelocalconnect);
	return MOD_SUCCESS;
}

MOD_LOAD() {
	return MOD_SUCCESS; // We good
}

// Called on unload/rehash obv
MOD_UNLOAD() {
	int i;
	for(i = 0; i < bicount && blockedIdents[i]; i++) // Iter8 em
		safe_free(blockedIdents[i]); // Kbye
	if(bicount)
		safe_free(blockedIdents);
	bicount = 0;
	return MOD_SUCCESS; // We good
}

int find_bident(char *ident) {
	int i;
	for(i = 0; i < bicount; i++) { // Iterate em lol
		if(match_simple(blockedIdents[i], ident))
			return 1; // We gottem
	}
	return 0; // Nope
}

int block_notlsident_configtest(ConfigFile *cf, ConfigEntry *ce, int type, int *errs) {
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
		// Do we even have a valid entry l0l?
		if(!cep->name || !cep->value || !strlen(cep->value)) {
			config_error("%s:%i: blank %s item", cep->file->filename, cep->line_number, MYCONF); // Rep0t error
			errors++; // Increment err0r count fam
			continue; // Next iteration imo tbh
		}

		if(!strcmp(cep->name, "ident")) {
			bicount++;
			continue;
		}

		config_warn("%s:%i: unknown item %s::%s", cep->file->filename, cep->line_number, MYCONF, cep->name); // Rep0t warn if unknown directive =]
	}

	*errs = errors;
	return errors ? -1 : 1; // Returning 1 means "all good", -1 means we shat our panties
}

int block_notlsident_configposttest(int *errs) {
	if(!bicount)
		config_warn("Module %s was loaded but the %s { } block contains no (valid) idents", MOD_HEADER.name, MYCONF);
	return 1;
}

// "Run" the config (everything should be valid at this point)
int block_notlsident_configrun(ConfigFile *cf, ConfigEntry *ce, int type) {
	ConfigEntry *cep; // To store the current variable/value pair etc, nested
	int i; // Array index =]

	if(!bicount) // No valid config blocks found, so gtfo early
		return 0; // Returning 0 means idgaf bout dis

	// Since we'll add a top-level block to unrealircd.conf, need to filter on CONFIG_MAIN lmao
	if(type != CONFIG_MAIN)
		return 0;

	// Check for valid config entries first
	if(!ce || !ce->name)
		return 0;

	// If it isn't block_notlsident, idc
	if(strcmp(ce->name, MYCONF))
		return 0;

	// Loop dat shyte fam
	i = 0;
	blockedIdents = safe_alloc(bicount * sizeof(char *));
	for(cep = ce->items; cep; cep = cep->next) {
		// Do we even have a valid entry l0l?
		if(!cep->name || !cep->value)
			continue; // Next iteration imo tbh

		if(!strcmp(cep->name, "ident")) {
			blockedIdents[i++] = strdup(cep->value);
			continue;
		}
	}

	return 1; // We good
}

int block_notlsident_prelocalconnect(Client *client) {
	if(!(client->local->listener->options & LISTENER_TLS) && find_bident(client->user->username)) {
		unreal_log(ULOG_INFO, "kill", "KILL_COMMAND", client, "Client killed: $client.details ([block_notlsident] tried to connect without SSL/TLS)");
		exit_client(client, NULL, "Illegal ident"); // Kbye
		return HOOK_DENY;
	}
	return HOOK_CONTINUE;
}
