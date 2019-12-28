/* Copyright (C) All Rights Reserved
** Written by Gottem <support@gottem.nl>
** Website: https://gottem.nl/unreal
** License: https://gottem.nl/unreal/license
*/

/*** <<<MODULE MANAGER START>>>
module {
	documentation "https://gottem.nl/unreal/man/websocket_restrict";
	troubleshooting "In case of problems, check the FAQ at https://gottem.nl/unreal/halp or e-mail me at support@gottem.nl";
	min-unrealircd-version "5.*";
	//max-unrealircd-version "5.*";
	post-install-text {
		"The module is installed, now all you need to do is add a 'loadmodule' line to your config file:";
		"loadmodule \"third/websocket_restrict\";";
		"Then /rehash the IRCd.";
		"For usage information, refer to the module's documentation found at: https://gottem.nl/unreal/man/websocket_restrict";
	}
}
*** <<<MODULE MANAGER END>>>
*/

// One include for all cross-platform compatibility thangs
#include "unrealircd.h"

// Config bl0ck
#define MYCONF "websocket_restrict"

// Big hecks go here
typedef struct t_chanstrukk muhchan;
struct t_chanstrukk {
	char *name;
	muhchan *next;
};

// Quality fowod declarations
void doGZLine(Client *client, char *fullErr);
int websocket_restrict_prelocalconnect(Client *client);
int websocket_restrict_prelocaljoin(Client *client, Channel *channel, char *parv[]);
int websocket_restrict_packet_in(Client *client, char *readbuf, int *length);
int websocket_restrict_configtest(ConfigFile *cf, ConfigEntry *ce, int type, int *errs);
int websocket_restrict_configposttest(int *errs);
int websocket_restrict_configrun(ConfigFile *cf, ConfigEntry *ce, int type);
int websocket_restrict_rehash(void);

// Set config defaults here
int *WSOnlyPorts = NULL; // Dynamic array ;]
int c_numPorts = 0; // Keep track of WS only ports
int zlineTime = 60; // Default GZ-Line time is 60 seconds lol
muhchan *chanList = NULL; // Channel restrictions
int chanCount = 0;

// Dat dere module header
ModuleHeader MOD_HEADER = {
	"third/websocket_restrict", // Module name
	"2.0", // Version
	"Impose restrictions on websocket connections", // Description
	"Gottem", // Author
	"unrealircd-5", // Modversion
};

// Configuration testing-related hewks go in testing phase obv
// This function is entirely optional
MOD_TEST() {
	// We have our own config block so we need to checkem config obv m9
	// Priorities don't really matter here
	HookAdd(modinfo->handle, HOOKTYPE_CONFIGTEST, 0, websocket_restrict_configtest);
	HookAdd(modinfo->handle, HOOKTYPE_CONFIGPOSTTEST, 0, websocket_restrict_configposttest);
	return MOD_SUCCESS;
}

// Initialisation routine (register hooks, commands and modes or create structs etc)
MOD_INIT() {
	MARK_AS_GLOBAL_MODULE(modinfo);

	HookAdd(modinfo->handle, HOOKTYPE_REHASH, 0, websocket_restrict_rehash);
	HookAdd(modinfo->handle, HOOKTYPE_CONFIGRUN, 0, websocket_restrict_configrun);
	HookAdd(modinfo->handle, HOOKTYPE_PRE_LOCAL_CONNECT, 0, websocket_restrict_prelocalconnect);
	HookAdd(modinfo->handle, HOOKTYPE_RAWPACKET_IN, -100, websocket_restrict_packet_in); // High priority so we go before the actual websocket module ;]
	HookAdd(modinfo->handle, HOOKTYPE_PRE_LOCAL_JOIN, 0, websocket_restrict_prelocaljoin);
	return MOD_SUCCESS;
}

MOD_LOAD() {
	return MOD_SUCCESS; // We good
}

// Called on unload/rehash obv
MOD_UNLOAD() {
	// Clean up shit here
	if(c_numPorts)
		safe_free(WSOnlyPorts);
	if(chanList) {
		// This shit is a bit convoluted to prevent memory issues obv famalmalmalmlmalm
		muhchan *ch;
		while((ch = chanList) != NULL) {
			chanList = chanList->next;
			safe_free(ch->name);
			safe_free(ch);
		}
		chanList = NULL;
	}
	chanCount = 0;
	return MOD_SUCCESS; // We good
}

void doGZLine(Client *client, char *fullErr) {
	char setTime[100], expTime[100];
	ircsnprintf(setTime, sizeof(setTime), "%li", TStime());
	ircsnprintf(expTime, sizeof(expTime), "%li", TStime() + zlineTime);
	char *tkllayer[9] = {
		me.name,
		"+",
		"Z",
		"*",
		client->ip,
		me.name,
		expTime,
		setTime,
		fullErr
	};
	cmd_tkl(&me, NULL, 9, tkllayer); // Ban 'em
}

// Check port restrictions for non-websocket users
int websocket_restrict_prelocalconnect(Client *client) {
	int ws_port = 0; // "Boolean"
	int i; // Iter8or lol
	ModDataInfo *websocket_md;
	char fullErr[128];

	if(!(websocket_md = findmoddata_byname("websocket", MODDATATYPE_CLIENT))) // Something went wrong getting ModDataInfo
		return HOOK_CONTINUE; // Np

	if((moddata_client(client, websocket_md).ptr)) // If we found moddata, means this is a WS user, so gtfo
		return HOOK_CONTINUE; // Np

	if(c_numPorts) { // If we found some valid ports in the config
		for(i = 0; i < c_numPorts; i++) { // Iter8 em
			if(client->local->listener->port == WSOnlyPorts[i]) { // User is connecting from a special designated port?
				ws_port = 1; // Flip boolean
				break; // Gtfo
			}
		}
	}

	if(ws_port) { // Regular user connecting to WS only p0t
		// Since we kinda __have__ to GZ-Line WS users, let's be consistent for non-WS users too ;]
		ircsnprintf(fullErr, sizeof(fullErr), "User is using a websocket-only port (%d)", client->local->listener->port); // Make error string
		doGZLine(client, fullErr); // Ban 'em
		exit_client(client, NULL, fullErr); // Kbye
		return HOOK_DENY;
	}
	return HOOK_CONTINUE; // We good
}

int websocket_restrict_prelocaljoin(Client *client, Channel *channel, char *parv[]) {
	muhchan *chan;
	int found;
	ModDataInfo *websocket_md;

	if(!chanCount) // No restrictions to begin with =]
		return HOOK_CONTINUE;

	if(!(websocket_md = findmoddata_byname("websocket", MODDATATYPE_CLIENT))) // Something went wrong getting ModDataInfo
		return HOOK_CONTINUE; // Np

	if(!(moddata_client(client, websocket_md).ptr)) // If we didn't find moddata, means this is not a WS user, so gtfo
		return HOOK_CONTINUE; // Np

	found = 0;
	for(chan = chanList; chan; chan = chan->next) {
		if(!strcasecmp(channel->chname, chan->name)) {
			found = 1;
			break;
		}
	}
	if(!found) {
		sendnotice(client, "[websocket_restrict] Not allowed to join %s", channel->chname);
		return HOOK_DENY;
	}
	return HOOK_CONTINUE;
}

// Check restrictions for websocket users trying shit meant for non-websocket users
int websocket_restrict_packet_in(Client *client, char *readbuf, int *length) {
	/* Return values:
	** -1: Don't touch this client anymore, it might have been killed lol
	** 0: Don't process this data, but you can read another packet if you want
	** > 0 means: Allow others to process this data still
	*/
	if((client->local->receiveM == 0) && (*length > 8) && !strncmp(readbuf, "GET ", 4)) {
		ModDataInfo *websocket_md;
		if(!(websocket_md = findmoddata_byname("websocket", MODDATATYPE_CLIENT))) // Something went wrong getting ModDataInfo
			return 1; // Let others process the data

		// Check if user is connecting to a websocket-restricted port
		if(moddata_client(client, websocket_md).ptr) // If we DID find moddata, the client is already online and we can skip this part ;];]
			return 1;

		int ws_port = 0; // "Boolean"
		int i; // Iter8or lol
		if(c_numPorts) { // If we found some valid ports in the config
			for(i = 0; i < c_numPorts; i++) { // Iter8 em
				if(client->local->listener->port == WSOnlyPorts[i]) { // User is connecting from a special designated port?
					ws_port = 1; // Flip boolean
					break; // Gtfo
				}
			}
		}

		// If this module is loaded, it will always deny websocket connections if no special port was found
		if(!ws_port) {
			// Have to GZ-Line here to prevent duplicate notices lol (since client is not fully online, websocket.c won't create a proper frame)
			if(client->ip) { // IP may or may not be resolved yet (seems to be a race condition of sorts =])
				char fullErr[128];
				ircsnprintf(fullErr, sizeof(fullErr), "Websocket client using illegal port (%d)", client->local->listener->port); // Make error string
				doGZLine(client, fullErr); // Ban 'em
			}
			exit_client(client, NULL, "Illegal port used for websocket connections"); // Kbye
			return -1; // Notify main loop of lost client
		}
	}
	return 1; // Let others process the data
}

int websocket_restrict_configtest(ConfigFile *cf, ConfigEntry *ce, int type, int *errs) {
	int errors = 0; // Error count
	int pot; // For checkin' em p0t
	int i; // Iter8 em m8
	ConfigEntry *cep, *cep2; // To store the current variable/value pair etc, nested

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
		if(!cep || !cep->ce_varname)
			continue;

		// Checkem port restrictions
		if(!strcmp(cep->ce_varname, "port")) {
			if(!cep->ce_vardata) { // Sanity check lol
				config_error("%s:%i: no value specified for %s::port", cep->ce_fileptr->cf_filename, cep->ce_varlinenum, MYCONF);
				errors++; // Increment err0r count fam
				continue;
			}

			pot = atoi(cep->ce_vardata); // Convert em
			if(pot > 1024 && pot <= 65535) // Check port range lol
				c_numPorts++; // Got a valid port
			else {
				config_error("%s:%i: invalid %s::port '%s' (must be higher than 1024 and lower than or equal to 65535)", cep->ce_fileptr->cf_filename, cep->ce_varlinenum, MYCONF, cep->ce_vardata);
				errors++; // Increment err0r count fam
			}
			continue;
		}

		if(!strcmp(cep->ce_varname, "zlinetime")) {
			// Should be an integer yo
			if(!cep->ce_vardata) {
				config_error("%s:%i: %s::zlinetime must be an integer of 0 or larger m8", cep->ce_fileptr->cf_filename, cep->ce_varlinenum, MYCONF);
				errors++; // Increment err0r count fam
				continue;
			}
			for(i = 0; cep->ce_vardata[i]; i++) {
				if(!isdigit(cep->ce_vardata[i])) {
					config_error("%s:%i: %s::zlinetime must be an integer of 0 or larger m8", cep->ce_fileptr->cf_filename, cep->ce_varlinenum, MYCONF);
					errors++; // Increment err0r count fam
					break;
				}
			}
			continue;
		}

		// Here comes a nested block =]
		if(!strcmp(cep->ce_varname, "channels")) {
			// Loop 'em again
			for(cep2 = cep->ce_entries; cep2; cep2 = cep2->ce_next) {
				if(!cep2->ce_varname || !strlen(cep2->ce_varname)) {
					config_error("%s:%i: blank %s::%s item", cep2->ce_fileptr->cf_filename, cep2->ce_varlinenum, MYCONF, cep->ce_varname); // Rep0t error
					errors++; // Increment err0r count fam
					continue; // Next iteration imo tbh
				}
				if(cep2->ce_varname[0] != '#') {
					config_error("%s:%i: invalid channel name '%s': must start with #", cep2->ce_fileptr->cf_filename, cep2->ce_varlinenum, cep2->ce_varname); // Rep0t error
					errors++; // Increment err0r count fam
					continue; // Next iteration imo tbh
				}
				if(strlen(cep2->ce_varname) > CHANNELLEN) {
					config_error("%s:%i: invalid channel name '%s': must not exceed %d characters in length", cep2->ce_fileptr->cf_filename, cep2->ce_varlinenum, cep2->ce_varname, CHANNELLEN);
					errors++; // Increment err0r count fam
					continue; // Next iteration imo tbh
				}
				chanCount++;
			}
			continue;
		}

		config_warn("%s:%i: unknown directive %s::%s", cep->ce_fileptr->cf_filename, cep->ce_varlinenum, MYCONF, cep->ce_varname);
	}

	*errs = errors;
	return errors ? -1 : 1; // Returning 1 means "all good", -1 means we shat our panties
}

// Post test, check for missing shit here
int websocket_restrict_configposttest(int *errs) {
	int errors = 0;

	if(!is_module_loaded("websocket")) {
		config_error("websocket_restrict was loaded but \002websocket\002 itself was not");
		*errs = 1;
		return -1;
	}

	if(!c_numPorts)
		config_warn("websocket_restrict was loaded but %s::port has no valid candidates, \002nobody using websocket clients will be able to connect\002", MYCONF); // Just a warning is enough lol
	else
		WSOnlyPorts = calloc(c_numPorts, sizeof(int)); // Allocate array and init to 0 ;]

	*errs = errors;
	return errors ? -1 : 1; // Returning 1 means "all good", -1 means we shat our panties
}

// "Run" the config (everything should be valid at this point)
int websocket_restrict_configrun(ConfigFile *cf, ConfigEntry *ce, int type) {
	ConfigEntry *cep, *cep2; // To store the current variable/value pair etc, nested
	int pot; // For checkin' em p0t
	int i; // Iter8or for WSOnlyPorts[]
	muhchan *last = NULL; // Initialise to NULL so the loop requires minimal l0gic
	muhchan **chan = &chanList; // Hecks so the ->next chain stays intact

	if(type != CONFIG_MAIN)
		return 0; // Returning 0 means idgaf bout dis

	// Check for valid config entries first
	if(!ce || !ce->ce_varname)
		return 0;

	// If it isn't our block, idc
	if(strcmp(ce->ce_varname, MYCONF))
		return 0;

	i = 0;
	// Loop dat shyte fam
	for(cep = ce->ce_entries; cep; cep = cep->ce_next) {
		// Do we even have a valid name l0l?
		if(!cep || !cep->ce_varname)
			continue;

		if(!strcmp(cep->ce_varname, "port")) {
			if(!cep->ce_vardata)
				continue;

			pot = atoi(cep->ce_vardata);
			if(pot > 1024 && pot <= 65535) // Got a valid port
				WSOnlyPorts[i++] = pot;
			continue;
		}

		if(!strcmp(cep->ce_varname, "zlinetime")) {
			zlineTime = atoi(cep->ce_vardata);
			continue;
		}

		// Nesting
		if(!strcmp(cep->ce_varname, "channels")) {
			// Loop 'em
			for(cep2 = cep->ce_entries; cep2; cep2 = cep2->ce_next) {
				if(!cep2->ce_varname)
					continue; // Next iteration imo tbh

				// Gotta get em length yo
				size_t namelen = sizeof(char) * (strlen(cep2->ce_varname) + 1);

				// Allocate mem0ry for the current entry
				*chan = safe_alloc(sizeof(muhchan));

				// Allocate/initialise shit here
				(*chan)->name = safe_alloc(namelen);

				// Copy that shit fam
				strncpy((*chan)->name, cep2->ce_varname, namelen);

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

int websocket_restrict_rehash(void) {
	// Reset config defaults
	c_numPorts = 0;
	zlineTime = 60;
	return HOOK_CONTINUE;
}
