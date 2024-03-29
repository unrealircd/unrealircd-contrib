/* Copyright (C) All Rights Reserved
** Written by Gottem <support@gottem.nl>
** Website: https://gottem.nl/unreal
** License: https://gottem.nl/unreal/license
*/

/*** <<<MODULE MANAGER START>>>
module {
	documentation "https://gottem.nl/unreal/man/nopmchannel";
	troubleshooting "In case of problems, check the FAQ at https://gottem.nl/unreal/halp or e-mail me at support@gottem.nl";
	min-unrealircd-version "6.*";
	//max-unrealircd-version "6.*";
	post-install-text {
		"The module is installed, now all you need to do is add a 'loadmodule' line to your config file:";
		"loadmodule \"third/nopmchannel\";";
		"Then /rehash the IRCd.";
		"For usage information, refer to the module's documentation found at: https://gottem.nl/unreal/man/nopmchannel";
	}
}
*** <<<MODULE MANAGER END>>>
*/

// One include for all cross-platform compatibility thangs
#include "unrealircd.h"

#define MYCONF "nopmchannel"

// Big hecks go here
typedef struct t_nopmchan noPMChan;
struct t_nopmchan {
	char *name;
	noPMChan *next;
};

// Quality fowod declarations
int nopmchannel_configtest(ConfigFile *cf, ConfigEntry *ce, int type, int *errs);
int nopmchannel_configposttest(int *errs); // You may not need this
int nopmchannel_configrun(ConfigFile *cf, ConfigEntry *ce, int type);
int nopmchannel_hook_cansend_user(Client *client, Client *to, const char **text, const char **errmsg, SendType sendtype);

// Muh globals
int noPMCount = 0;
noPMChan *noPMList = NULL;

// Dat dere module header
ModuleHeader MOD_HEADER = {
	"third/nopmchannel", // Module name
	"2.1.0", // Version
	"Prevents users sharing a channel from privately messaging each other", // Description
	"Gottem", // Author
	"unrealircd-6", // Modversion
};

// Configuration testing-related hewks go in testing phase obv
MOD_TEST() {
	// We have our own config block so we need to checkem config obv m9
	// Priorities don't really matter here
	HookAdd(modinfo->handle, HOOKTYPE_CONFIGTEST, 0, nopmchannel_configtest);
	HookAdd(modinfo->handle, HOOKTYPE_CONFIGPOSTTEST, 0, nopmchannel_configposttest);
	return MOD_SUCCESS;
}

// Initialisation routine (register hooks, commands and modes or create structs etc)
MOD_INIT() {
	MARK_AS_GLOBAL_MODULE(modinfo);

	HookAdd(modinfo->handle, HOOKTYPE_CONFIGRUN, 0, nopmchannel_configrun);
	HookAdd(modinfo->handle, HOOKTYPE_CAN_SEND_TO_USER, -100, nopmchannel_hook_cansend_user); // High priority lol
	return MOD_SUCCESS;
}

MOD_LOAD() {
	return MOD_SUCCESS; // We good
}

// Called on unload/rehash obv
MOD_UNLOAD() {
	// Clean up any structs and other shit here
	if(noPMList) {
		// This shit is a bit convoluted to prevent memory issues obv famalmalmalmlmalm
		noPMChan *npEntry;
		while((npEntry = noPMList) != NULL) {
			noPMList = noPMList->next;
			safe_free(npEntry->name);
			safe_free(npEntry);
		}
		noPMList = NULL;
	}
	noPMCount = 0; // Just to maek shur
	return MOD_SUCCESS; // We good
}

int nopmchannel_configtest(ConfigFile *cf, ConfigEntry *ce, int type, int *errs) {
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

		if(!strcmp(cep->name, "name")) {
			char *chan = cep->value;
			size_t chanlen = strlen(chan);
			if(!chanlen) {
				config_error("%s:%i: %s::%s must be non-empty fam", cep->file->filename, cep->line_number, MYCONF, cep->name);
				errors++; // Increment err0r count fam
			}
			else if(chan[0] != '#') {
				config_error("%s:%i: invalid channel name '%s' for %s (must start with a \002#\002)", cep->file->filename, cep->line_number, chan, MYCONF);
				errors++; // Increment err0r count fam
			}
			else if(chan[1] && !isdigit(chan[1]) && !isalpha(chan[1])) {
				config_error("%s:%i: invalid channel name '%s' for %s (second char must be alphanumeric)", cep->file->filename, cep->line_number, chan, MYCONF);
				errors++; // Increment err0r count fam
			}
			else if(chanlen > CHANNELLEN) {
				config_error("%s:%i: invalid channel name '%s' for %s (name is too long, must be equal to or less than %d chars)", cep->file->filename, cep->line_number, chan, MYCONF, CHANNELLEN);
				errors++; // Increment err0r count fam
			}
			else
				noPMCount++;
			continue;
		}

		// Anything else is unknown to us =]
		config_warn("%s:%i: unknown item %s::%s", cep->file->filename, cep->line_number, MYCONF, cep->name); // So display just a warning
	}

	*errs = errors;
	return errors ? -1 : 1; // Returning 1 means "all good", -1 means we shat our panties
}

// Post test, check for missing shit here
int nopmchannel_configposttest(int *errs) {
	if(!noPMCount) // Just a warning will suffice imo =]
		config_warn("Module %s was loaded but the %s { } block contains no (valid) channels", MOD_HEADER.name, MYCONF);
	return 1;
}

noPMChan *find_npchan(char *name) {
	noPMChan *npEntry = NULL;
	for(npEntry = noPMList; npEntry; npEntry = npEntry->next) {
		if(!strcasecmp(npEntry->name, name))
			break;
	}
	return npEntry;
}

// "Run" the config (everything should be valid at this point)
int nopmchannel_configrun(ConfigFile *cf, ConfigEntry *ce, int type) {
	ConfigEntry *cep; // To store the current variable/value pair etc
	noPMChan *last = NULL; // Initialise to NULL so the loop requires minimal l0gic
	noPMChan **npEntry = &noPMList; // Hecks so the ->next chain stays intact

	// Since we'll add a top-level block to unrealircd.conf, need to filter on CONFIG_MAIN lmao
	if(type != CONFIG_MAIN)
		return 0; // Returning 0 means idgaf bout dis

	// Check for valid config entries first
	if(!noPMCount || !ce || !ce->name)
		return 0;

	// If it isn't nopmchannel, idc
	if(strcmp(ce->name, MYCONF))
		return 0;

	// Loop dat shyte fam
	for(cep = ce->items; cep; cep = cep->next) {
		// Do we even have a valid name and value l0l?
		if(!cep->name || !cep->value)
			continue; // Next iteration imo tbh

		if(!strcmp(cep->name, "name")) {
			size_t namelen = sizeof(char) * (strlen(cep->value) + 1);
			*npEntry = safe_alloc(sizeof(noPMChan));
			(*npEntry)->name = safe_alloc(namelen);
			strncpy((*npEntry)->name, cep->value, namelen);

			// Premium linked list fam
			if(last)
				last->next = *npEntry;
			last = *npEntry;
			npEntry = &(*npEntry)->next;
		}
	}

	return 1; // We good
}

// Actual hewk function m8
int nopmchannel_hook_cansend_user(Client *client, Client *to, const char **text, const char **errmsg, SendType sendtype) {
	Channel *channel;
	Membership *lp;
	static char errbuf[256];

	if(sendtype != SEND_TYPE_PRIVMSG && sendtype != SEND_TYPE_NOTICE)
		return HOOK_CONTINUE;
	if(!text || !*text)
		return HOOK_CONTINUE;

	// Let's exclude some shit lol
	if(!client || !to || client == to || !MyUser(client) || IsULine(client) || IsULine(to) || IsOper(client) || !IsUser(to))
		return HOOK_CONTINUE;

	for(lp = client->user->channel; lp; lp = lp->next) {
		channel = lp->channel;
		if(find_npchan(channel->name)) {
			// Receiver should prolly be a member of the currently checked channel to begin with ;]
			if(IsMember(to, channel) && !check_channel_access(client, channel, "hoaq") && !check_channel_access(to, channel, "hoaq")) {
				ircsnprintf(errbuf, sizeof(errbuf), "You have to part from channel '%s' in order to send private messages to %s", channel->name, to->name);
				*errmsg = errbuf;
				return HOOK_DENY;
			}
		}
	}

	return HOOK_CONTINUE;
}
