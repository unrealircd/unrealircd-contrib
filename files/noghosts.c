/* Copyright (C) All Rights Reserved
** Written by Gottem <support@gottem.nl>
** Website: https://gottem.nl/unreal
** License: https://gottem.nl/unreal/license
*/

/*** <<<MODULE MANAGER START>>>
module {
	documentation "https://gottem.nl/unreal/man/noghosts";
	troubleshooting "In case of problems, check the FAQ at https://gottem.nl/unreal/halp or e-mail me at support@gottem.nl";
	min-unrealircd-version "6.*";
	//max-unrealircd-version "6.*";
	post-install-text {
		"The module is installed, now all you need to do is add a 'loadmodule' line to your config file:";
		"loadmodule \"third/noghosts\";";
		"Then /rehash the IRCd.";
		"For usage information, refer to the module's documentation found at: https://gottem.nl/unreal/man/noghosts";
	}
}
*** <<<MODULE MANAGER END>>>
*/

// One include for all cross-platform compatibility thangs
#include "unrealircd.h"

// Config block
#define MYCONF "noghosts"

// Big hecks go here
typedef struct t_chanstrukk muhchan;
struct t_chanstrukk {
	char *name;
	muhchan *next;
};

// Quality fowod declarations;
int noghosts_configtest(ConfigFile *cf, ConfigEntry *ce, int type, int *errs);
int noghosts_configposttest(int *errs);
int noghosts_configrun(ConfigFile *cf, ConfigEntry *ce, int type);
unsigned short int is_chan_monitored(char *name);
int noghosts_hook_change_umode(Client *client, long oldflags, long newflags);

// Muh config shit y0
struct {
	char *message;
	char *flags;
	muhchan *channels;
	int chancount;

	unsigned short int got_message;
	unsigned short int got_flags;
} muhcfg;

// Dat dere module header
ModuleHeader MOD_HEADER = {
	"third/noghosts", // Module name
	"2.1.0", // Version
	"Keep channels clear of \"ghosts\" of opers", // Description
	"Gottem", // Author
	"unrealircd-6", // Modversion
};

// Configuration testing-related hewks go in testing phase obv
MOD_TEST() {
	// We have our own config block so we need to checkem config obv m9
	// Priorities don't really matter here
	HookAdd(modinfo->handle, HOOKTYPE_CONFIGTEST, 0, noghosts_configtest);
	HookAdd(modinfo->handle, HOOKTYPE_CONFIGPOSTTEST, 0, noghosts_configposttest);
	return MOD_SUCCESS;
}

// Initialisation routine (register hooks, commands and modes or create structs etc)
MOD_INIT() {
	MARK_AS_GLOBAL_MODULE(modinfo);

	HookAdd(modinfo->handle, HOOKTYPE_CONFIGRUN, 0, noghosts_configrun);
	HookAdd(modinfo->handle, HOOKTYPE_UMODE_CHANGE, 0, noghosts_hook_change_umode);
	return MOD_SUCCESS;
}

MOD_LOAD() {
	return MOD_SUCCESS; // We good
}

// Called on unload/rehash obv
MOD_UNLOAD() {
	// Clean up any structs and other shit here
	safe_free(muhcfg.message);
	safe_free(muhcfg.flags);
	if(muhcfg.channels) {
		// This shit is a bit convoluted to prevent memory issues obv famalmalmalmlmalm
		muhchan *ch;
		while((ch = muhcfg.channels) != NULL) {
			muhcfg.channels = muhcfg.channels->next;
			safe_free(ch->name);
			safe_free(ch);
		}
		muhcfg.channels = NULL;
	}
	muhcfg.chancount = 0;
	return MOD_SUCCESS; // We good
}

int noghosts_configtest(ConfigFile *cf, ConfigEntry *ce, int type, int *errs) {
	int errors = 0; // Error count
	ConfigEntry *cep, *cep2; // To store the current variable/value pair etc, nested

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

		if(!strcmp(cep->name, "flags")) {
			if(!cep->value || !strlen(cep->value)) {
				config_error("%s:%i: %s::%s must be non-empty fam", cep->file->filename, cep->line_number, MYCONF, cep->name);
				errors++; // Increment err0r count fam
				continue;
			}
			if(strcmp(cep->value, "O")) {
				//config_error("%s:%i: %s::%s must be one of: O", cep->file->filename, cep->line_number, MYCONF, cep->name);
				config_error("%s:%i: %s::%s must be: O", cep->file->filename, cep->line_number, MYCONF, cep->name);
				errors++; // Increment err0r count fam
				continue;
			}
			muhcfg.got_flags = 1;
			continue;
		}

		if(!strcmp(cep->name, "message")) {
			if(!cep->value || !strlen(cep->value)) {
				config_error("%s:%i: %s::%s must be non-empty fam", cep->file->filename, cep->line_number, MYCONF, cep->name);
				errors++; // Increment err0r count fam
				continue;
			}
			muhcfg.got_message = 1;
			continue;
		}

		// Here comes a nested block =]
		if(!strcmp(cep->name, "channels")) {
			// Loop 'em again
			for(cep2 = cep->items; cep2; cep2 = cep2->next) {
				if(!cep2->name || !strlen(cep2->name)) {
					config_error("%s:%i: blank %s::%s item", cep2->file->filename, cep2->line_number, MYCONF, cep->name); // Rep0t error
					errors++; // Increment err0r count fam
					continue; // Next iteration imo tbh
				}
				if(cep2->name[0] != '#') {
					config_error("%s:%i: invalid channel name '%s': must start with #", cep2->file->filename, cep2->line_number, cep2->name); // Rep0t error
					errors++; // Increment err0r count fam
					continue; // Next iteration imo tbh
				}
				if(strlen(cep2->name) > CHANNELLEN) {
					config_error("%s:%i: invalid channel name '%s': must not exceed %d characters in length", cep2->file->filename, cep2->line_number, cep2->name, CHANNELLEN);
					errors++; // Increment err0r count fam
					continue; // Next iteration imo tbh
				}
				muhcfg.chancount++;
			}
			continue;
		}

		// Anything else is unknown to us =]
		config_warn("%s:%i: unknown item %s::%s", cep->file->filename, cep->line_number, MYCONF, cep->name); // So display just a warning
	}

	*errs = errors;
	return errors ? -1 : 1; // Returning 1 means "all good", -1 means we shat our panties
}

// Post test, check for missing shit here
int noghosts_configposttest(int *errs) {
	int errors = 0;

	if(!muhcfg.got_flags)
		safe_strdup(muhcfg.flags, "O");

	if(!muhcfg.got_message) // I may remove/change this when more flags are used ;]
		safe_strdup(muhcfg.message, "Opered down");

	*errs = errors;
	return errors ? -1 : 1;
}

// "Run" the config (everything should be valid at this point)
int noghosts_configrun(ConfigFile *cf, ConfigEntry *ce, int type) {
	ConfigEntry *cep, *cep2; // To store the current variable/value pair etc, nested
	muhchan *last = NULL; // Initialise to NULL so the loop requires minimal l0gic
	muhchan **chan = &(muhcfg.channels); // Hecks so the ->next chain stays intact

	// Since we'll add a top-level block to unrealircd.conf, need to filter on CONFIG_MAIN lmao
	if(type != CONFIG_MAIN)
		return 0; // Returning 0 means idgaf bout dis

	// Check for valid config entries first
	if(!ce || !ce->name)
		return 0;

	// If it isn't noghosts, idc
	if(strcmp(ce->name, MYCONF))
		return 0;

	// Loop dat shyte fam
	for(cep = ce->items; cep; cep = cep->next) {
		// Do we even have a valid name l0l?
		if(!cep->name)
			continue; // Next iteration imo tbh

		if(!strcmp(cep->name, "flags")) {
			safe_strdup(muhcfg.flags, cep->value);
			continue;
		}

		if(!strcmp(cep->name, "message")) {
			safe_strdup(muhcfg.message, cep->value);
			continue;
		}

		// Nesting
		if(!strcmp(cep->name, "channels")) {
			// Loop 'em
			for(cep2 = cep->items; cep2; cep2 = cep2->next) {
				if(!cep2->name)
					continue; // Next iteration imo tbh

				// Gotta get em length yo
				size_t namelen = sizeof(char) * (strlen(cep2->name) + 1);

				// Allocate mem0ry for the current entry
				*chan = safe_alloc(sizeof(muhchan));

				// Allocate/initialise shit here
				(*chan)->name = safe_alloc(namelen);

				// Copy that shit fam
				strncpy((*chan)->name, cep2->name, namelen);

				// Premium linked list fam
				if(last)
					last->next = *chan;

				last = *chan;
				chan = &(*chan)->next;
			}
			continue;
		}
	}

	return 1; // We good
}

unsigned short int is_chan_monitored(char *name) {
	muhchan *chan;
	if(!muhcfg.chancount) // No channels specified = default to all ;]
		return 1;

	// Checkem configured channels nao
	for(chan = muhcfg.channels; chan; chan = chan->next) {
		if(!strcasecmp(chan->name, name))
			return 1;
	}
	return 0;
}

// Actual hewk functions m8
int noghosts_hook_change_umode(Client *client, long oldflags, long newflags) {
	Membership *lp, *next;
	const char *parv[4];

	// Don't bother with remote clients or if flags r set but don't contain O ;];]
	if(!MyUser(client) || (muhcfg.flags && !strchr(muhcfg.flags, 'O')))
		return HOOK_CONTINUE;

	// Check oper down lol
	if((oldflags & UMODE_OPER) && !(newflags & UMODE_OPER)) {
		for(lp = client->user->channel; lp; lp = next) {
			next = lp->next; // Cuz inb4rip linked list after a PART
			// Check for chancount here to save one function call/iteration =]]]
			if(has_channel_mode(lp->channel, 'O') && is_chan_monitored(lp->channel->name)) {
				// Rebuild all parv cuz we prolly should =]
				parv[0] = NULL; // PART
				parv[1] = lp->channel->name;
				parv[2] = muhcfg.message;
				parv[3] = NULL; // EOL
				do_cmd(client, NULL, "PART", 3, parv);
			}
		}
	}
	return HOOK_CONTINUE;
}
