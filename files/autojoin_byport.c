/* Copyright (C) All Rights Reserved
** Written by Gottem <support@gottem.nl>
** Website: https://gottem.nl/unreal
** License: https://gottem.nl/unreal/license
*/

/*** <<<MODULE MANAGER START>>>
module {
	documentation "https://gottem.nl/unreal/man/autojoin_byport";
	troubleshooting "In case of problems, check the FAQ at https://gottem.nl/unreal/halp or e-mail me at support@gottem.nl";
	min-unrealircd-version "5.*";
	//max-unrealircd-version "5.*";
	post-install-text {
		"The module is installed, now all you need to do is add a 'loadmodule' line to your config file:";
		"loadmodule \"third/autojoin_byport\";";
		"Then /rehash the IRCd.";
		"For usage information, refer to the module's documentation found at: https://gottem.nl/unreal/man/autojoin_byport";
	}
}
*** <<<MODULE MANAGER END>>>
*/

// One include for all cross-platform compatibility thangs
#include "unrealircd.h"

// Config block
#define MYCONF "autojoin_byport"

// Big hecks go here
typedef struct t_autojoin_port AJPort;

struct t_autojoin_port {
	unsigned short port; // 1024 < x <= 65535
	char *channel;
	AJPort *next; // Quality linked list
};

// Quality fowod declarations
int autojoin_byport_configtest(ConfigFile *cf, ConfigEntry *ce, int type, int *errs);
int autojoin_byport_configposttest(int *errs); // You may not need this
int autojoin_byport_configrun(ConfigFile *cf, ConfigEntry *ce, int type);
int autojoin_byport_localconnect(Client *client);

AJPort *ajoinpList = NULL; // Muh list head y0
int AJCount = 0; // Keep tracc of autojoin count =]
unsigned short checkedem = 0;

// Dat dere module header
ModuleHeader MOD_HEADER = {
	"third/autojoin_byport", // Module name
	"2.0", // Version
	"Auto-join channels on connect based on connection port", // Description
	"Gottem", // Author
	"unrealircd-5", // Modversion
};

// Configuration testing-related hewks go in testing phase obv
MOD_TEST() {
	// We have our own config block so we need to checkem config obv m9
	// Priorities don't really matter here
	HookAdd(modinfo->handle, HOOKTYPE_CONFIGTEST, 0, autojoin_byport_configtest);
	HookAdd(modinfo->handle, HOOKTYPE_CONFIGPOSTTEST, 0, autojoin_byport_configposttest);
	return MOD_SUCCESS;
}

// Initialisation routine (register hooks, commands and modes or create structs etc)
MOD_INIT() {
	MARK_AS_GLOBAL_MODULE(modinfo);

	HookAdd(modinfo->handle, HOOKTYPE_CONFIGRUN, 0, autojoin_byport_configrun);
	HookAdd(modinfo->handle, HOOKTYPE_LOCAL_CONNECT, 0, autojoin_byport_localconnect);
	return MOD_SUCCESS;
}

MOD_LOAD() {
	return MOD_SUCCESS; // We good
}

// Called on unload/rehash obv
MOD_UNLOAD() {
	if(ajoinpList) {
		// This shit is a bit convoluted to prevent memory issues obv famalmalmalmlmalm
		AJPort *ajEntry;
		while((ajEntry = ajoinpList) != NULL) {
			ajoinpList = ajoinpList->next;
			safe_free(ajEntry->channel);
			safe_free(ajEntry);
		}
		ajoinpList = NULL;
	}
	AJCount = 0; // Just to maek shur
	return MOD_SUCCESS; // We good
}

int autojoin_byport_configtest(ConfigFile *cf, ConfigEntry *ce, int type, int *errs) {
	int errors = 0; // Error count
	int i; // Iterat0r
	int pot; // Temporary p0rt st0rage ;]
	char *chan, *tmp, *p; // Pointers for getting multiple channel names
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
		checkedem = 1;
		// Do we even have a valid pair l0l?
		if(!cep->ce_varname || !cep->ce_vardata) {
			config_error("%s:%i: blank %s item", cep->ce_fileptr->cf_filename, cep->ce_varlinenum, MYCONF); // Rep0t error
			errors++; // Increment err0r count fam
			continue; // Next iteration imo tbh
		}

		// The "name" is the port numba ;]
		for(i = 0; cep->ce_varname[i]; i++) {
			if(!isdigit(cep->ce_varname[i])) {
				config_error("%s:%i: invalid port '%s' for %s (must be higher than 1024 and lower than or equal to 65535)", cep->ce_fileptr->cf_filename, cep->ce_varlinenum, cep->ce_varname, MYCONF);
				errors++; // Increment err0r count fam
				break;
			}
		}
		if(errors) // If above loop found an error, no need to pr0ceed yo
			continue;

		// Check port range lol
		pot = atoi(cep->ce_varname);
		if(!pot || pot <= 1024 || pot > 65535) { // Last condition should never b tru, but let's just checkem =]
			config_error("%s:%i: invalid port '%s' for %s (must be higher than 1024 and lower than or equal to 65535)", cep->ce_fileptr->cf_filename, cep->ce_varlinenum, cep->ce_varname, MYCONF);
			errors++; // Increment err0r count fam
			continue;
		}

		// Checkem channels YO
		safe_strdup(tmp, cep->ce_vardata);
		for(chan = strtoken(&p, tmp, ","); chan; chan = strtoken(&p, NULL, ",")) {
			if(chan[0] != '#') {
				config_error("%s:%i: invalid channel name '%s' for %s (must start with a \002#\002)", cep->ce_fileptr->cf_filename, cep->ce_varlinenum, chan, MYCONF);
				errors++; // Increment err0r count fam
				safe_free(tmp);
				continue;
			}

			if(!isdigit(chan[1]) && !isalpha(chan[1])) {
				config_error("%s:%i: invalid channel name '%s' for %s (second char must be alphanumeric)", cep->ce_fileptr->cf_filename, cep->ce_varlinenum, chan, MYCONF);
				errors++; // Increment err0r count fam
				safe_free(tmp);
				continue;
			}

			if(strlen(chan) > CHANNELLEN) {
				config_error("%s:%i: invalid channel name '%s' for %s (name is too long, must be equal to or less than %d chars)", cep->ce_fileptr->cf_filename, cep->ce_varlinenum, chan, MYCONF, CHANNELLEN);
				errors++; // Increment err0r count fam
				safe_free(tmp);
				continue;
			}
		}

		safe_free(tmp);
		if(errors)
			continue;

		AJCount++; // Found a valid entry =]
	}

	*errs = errors;
	return errors ? -1 : 1; // Returning 1 means "all good", -1 means we shat our panties
}

// Post test, check for missing shit here
int autojoin_byport_configposttest(int *errs) {
	int errors = 0;
	if(!AJCount && !checkedem) // Just emit a warn if no entries found yo
		config_warn("%s was loaded but there are no (valid) configuration entries (%s {} block)", MOD_HEADER.name, MYCONF);
	*errs = errors;
	return errors ? -1 : 1;
}

// "Run" the config (everything should be valid at this point)
int autojoin_byport_configrun(ConfigFile *cf, ConfigEntry *ce, int type) {
	ConfigEntry *cep; // To store the current variable/value pair etc
	AJPort *last = NULL; // Initialise to NULL so the loop requires minimal l0gic
	AJPort **ajEntry = &ajoinpList; // Hecks so the ->next chain stays intact
	char *chan, *tmp, *p; // Buffer pointers fam

	// Since we'll add a top-level block to unrealircd.conf, need to filter on CONFIG_MAIN lmao
	if(type != CONFIG_MAIN)
		return 0; // Returning 0 means idgaf bout dis

	// Check for valid config entries first
	if(!AJCount || !ce || !ce->ce_varname)
		return 0;

	// If it isn't autojoin_byport, idc
	if(strcmp(ce->ce_varname, MYCONF))
		return 0;

	// Loop dat shyte fam
	for(cep = ce->ce_entries; cep; cep = cep->ce_next) {
		if(!cep->ce_varname || !cep->ce_vardata) // Sanity checc l0l
			continue;

		safe_strdup(tmp, cep->ce_vardata);
		for(chan = strtoken(&p, tmp, ","); chan; chan = strtoken(&p, NULL, ",")) {
			// Allocate mem0ry for the current entry
			*ajEntry = safe_alloc(sizeof(AJPort));

			// Allocate/initialise shit here
			size_t chanlen = sizeof(char) * (strlen(cep->ce_vardata) + 1);
			(*ajEntry)->port = atoi(cep->ce_varname);
			(*ajEntry)->channel = safe_alloc(chanlen);

			strncpy((*ajEntry)->channel, cep->ce_vardata, chanlen); // Copy that shit fam

			// Premium linked list fam
			if(last)
				last->next = *ajEntry;

			last = *ajEntry;
			ajEntry = &(*ajEntry)->next;
		}
		safe_free(tmp);
	}
	return 1; // We good
}

// Actual hewk function m8
int autojoin_byport_localconnect(Client *client) {
	AJPort *ajEntry;
	if(client && client->local && client->local->listener) { // Just some sanity checks fam
		for(ajEntry = ajoinpList; ajEntry; ajEntry = ajEntry->next) { // Loop em
			if(ajEntry->port == client->local->listener->port) { // Matched em port
				char *parv[] = { // Make args for modularised command
					NULL,
					ajEntry->channel,
					NULL,
				};
				do_cmd(client, NULL, "JOIN", 2, parv);
			}
		}
	}
	return 0; // Can't reject users here, so simply return 0 famalama
}
