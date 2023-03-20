/* Copyright (C) All Rights Reserved
** Written by Gottem <support@gottem.nl>
** Website: https://gottem.nl/unreal
** License: https://gottem.nl/unreal/license
*/

/*** <<<MODULE MANAGER START>>>
module {
	documentation "https://gottem.nl/unreal/man/nicksuffix";
	troubleshooting "In case of problems, check the FAQ at https://gottem.nl/unreal/halp or e-mail me at support@gottem.nl";
	min-unrealircd-version "6.*";
	//max-unrealircd-version "6.*";
	post-install-text {
		"The module is installed, now all you need to do is add a 'loadmodule' line to your config file:";
		"loadmodule \"third/nicksuffix\";";
		"Then /rehash the IRCd.";
		"For usage information, refer to the module's documentation found at: https://gottem.nl/unreal/man/nicksuffix";
	}
}
*** <<<MODULE MANAGER END>>>
*/

// One include for all cross-platform compatibility thangs
#include "unrealircd.h"

// Config bl0ck
#define MYCONF "nicksuffix"

// Command to override
#define OVR_NICK "NICK"

// Muh macros
#define CheckAPIError(apistr, apiobj) \
	do { \
		if(!(apiobj)) { \
			config_error("A critical error occurred on %s for %s: %s", (apistr), MOD_HEADER.name, ModuleGetErrorStr(modinfo->handle)); \
			return MOD_FAILED; \
		} \
	} while(0)

// Quality fowod declarations
EVENT(nicksuffix_init);
void nicksuffix_md_free(ModData *md);
int nicksuffix_configtest(ConfigFile *cf, ConfigEntry *ce, int type, int *errs);
int nicksuffix_configposttest(int *errs); // You may not need this
int nicksuffix_configrun(ConfigFile *cf, ConfigEntry *ce, int type);
int nicksuffix_rehash(void);
CMD_OVERRIDE_FUNC(nicksuffix_override);
int nicksuffix_hook_prelocalconnect(Client *client);

// Muh globals
ModDataInfo *nicksfxMDI; // Moddata for the original nick =]
const char *aloud_sep = "`-_[]{}\\|";

// Config vars
struct {
	char sfx_separator; // One of "aloud_sep"
	char *sfx_restore; // "Command" to restore original nick
	int init; // Write moddata for all currently connected users on module load

	int got_separator;
	int got_restore;
	int got_init;
} muhcfg;

// Dat dere module header
ModuleHeader MOD_HEADER = {
	"third/nicksuffix", // Module name
	"2.1.0", // Version
	"Restrict /nick usage to suffixing your base nick", // Description
	"Gottem", // Author
	"unrealircd-6", // Modversion
};

// Configuration testing-related hewks go in testing phase obv
MOD_TEST() {
	// We have our own config block so we need to checkem config obv m9
	// Priorities don't really matter here
	memset(&muhcfg, 0, sizeof(muhcfg));
	HookAdd(modinfo->handle, HOOKTYPE_CONFIGTEST, 0, nicksuffix_configtest);
	HookAdd(modinfo->handle, HOOKTYPE_CONFIGPOSTTEST, 0, nicksuffix_configposttest);
	return MOD_SUCCESS;
}

// Initialisation routine (register hooks, commands and modes or create structs etc)
MOD_INIT() {
	// Request moddata for storing the original nick
	ModDataInfo mreq;
	memset(&mreq, 0, sizeof(mreq));
	mreq.type = MODDATATYPE_LOCAL_CLIENT; // Apply to users only (CLIENT actually includes servers but we'll disregard that here =])
	mreq.name = "nicksuffix"; // Name it
	mreq.free = nicksuffix_md_free; // Function to free 'em
	nicksfxMDI = ModDataAdd(modinfo->handle, mreq);
	CheckAPIError("ModDataAdd(nicksuffix)", nicksfxMDI);

	MARK_AS_GLOBAL_MODULE(modinfo);

	HookAdd(modinfo->handle, HOOKTYPE_CONFIGRUN, 0, nicksuffix_configrun);
	HookAdd(modinfo->handle, HOOKTYPE_PRE_LOCAL_CONNECT, 0, nicksuffix_hook_prelocalconnect);

	EventAdd(modinfo->handle, "nicksuffix_init", nicksuffix_init, NULL, 3000, 1);
	return MOD_SUCCESS;
}

MOD_LOAD() {
	// Lower priority override so we can go *after* any potential set::restrict-command directives
	CheckAPIError("CommandOverrideAdd(NICK)", CommandOverrideAdd(modinfo->handle, OVR_NICK, 10, nicksuffix_override));
	return MOD_SUCCESS; // We good
}

// Called on unload/rehash obv
MOD_UNLOAD() {
	safe_free(muhcfg.sfx_restore);
	return MOD_SUCCESS; // We good
}

EVENT(nicksuffix_init) {
	Client *client;

	if(!muhcfg.init)
		return;

	list_for_each_entry(client, &lclient_list, lclient_node) {
		// Only care for local non-ulined users
		if(!MyUser(client) || IsULine(client))
			continue;

		// Let's not overwrite the on-connect nick if we already have one ;];;]
		if(!moddata_local_client(client, nicksfxMDI).ptr)
			safe_strdup(moddata_local_client(client, nicksfxMDI).ptr, client->name);
	}
}

void nicksuffix_md_free(ModData *md) {
	safe_free(md->ptr);
}

int nicksuffix_configtest(ConfigFile *cf, ConfigEntry *ce, int type, int *errs) {
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

		if(!strcmp(cep->name, "separator")) {
			muhcfg.got_separator = 1;
			if(!cep->value || !strlen(cep->value)) {
				config_error("%s:%i: %s::%s must be non-empty fam", cep->file->filename, cep->line_number, MYCONF, cep->name);
				errors++; // Increment err0r count fam
			}
			else if(strlen(cep->value) > 1 || !strchr(aloud_sep, cep->value[0])) {
				config_error("%s:%i: %s::%s must be exactly one character (and it must be one of: '%s')", cep->file->filename, cep->line_number, MYCONF, cep->name, aloud_sep);
				errors++; // Increment err0r count fam
			}
			continue;
		}

		if(!strcmp(cep->name, "restore")) {
			muhcfg.got_restore = 1;
			if(!cep->value || !strlen(cep->value)) {
				config_error("%s:%i: %s::%s must be non-empty fam", cep->file->filename, cep->line_number, MYCONF, cep->name);
				errors++; // Increment err0r count fam
			}
			continue;
		}

		if(!strcmp(cep->name, "init-on-load")) {
			muhcfg.got_init = 1;
			if(!cep->value || (strcmp(cep->value, "0") && strcmp(cep->value, "1"))) {
				config_error("%s:%i: %s::%s must be either 0 or 1 fam", cep->file->filename, cep->line_number, MYCONF, cep->name);
				errors++; // Increment err0r count fam
			}
			continue;
		}

		config_warn("%s:%i: unknown item %s::%s", cep->file->filename, cep->line_number, MYCONF, cep->name); // Rep0t warn if unknown directive =]
	}

	*errs = errors;
	return errors ? -1 : 1; // Returning 1 means "all good", -1 means we shat our panties
}

int nicksuffix_configposttest(int *errs) {
	int errors = 0;

	if(!muhcfg.got_separator) {
		config_error("%s::separator is required fam", MYCONF);
		errors++; // Increment err0r count fam
	}
	if(!muhcfg.got_restore) {
		config_error("%s::restore is required fam", MYCONF);
		errors++; // Increment err0r count fam
	}

	*errs = errors;
	return errors ? -1 : 1;
}

// "Run" the config (everything should be valid at this point)
int nicksuffix_configrun(ConfigFile *cf, ConfigEntry *ce, int type) {
	ConfigEntry *cep; // To store the current variable/value pair etc, nested

	// Since we'll add a top-level block to unrealircd.conf, need to filter on CONFIG_MAIN lmao
	if(type != CONFIG_MAIN)
		return 0; // Returning 0 means idgaf bout dis

	// Check for valid config entries first
	if(!ce || !ce->name)
		return 0;

	// If it isn't nicksuffix, idc
	if(strcmp(ce->name, MYCONF))
		return 0;

	// Loop dat shyte fam
	for(cep = ce->items; cep; cep = cep->next) {
		// Do we even have a valid name l0l?
		if(!cep->name)
			continue; // Next iteration imo tbh

		if(!strcmp(cep->name, "separator")) {
			muhcfg.sfx_separator = cep->value[0];
			continue;
		}

		if(!strcmp(cep->name, "restore")) {
			safe_strdup(muhcfg.sfx_restore, cep->value);
			continue;
		}

		if(!strcmp(cep->name, "init-on-load")) {
			muhcfg.init = atoi(cep->value);
			continue;
		}
	}

	return 1; // We good
}

// Now for the actual override
CMD_OVERRIDE_FUNC(nicksuffix_override) {
	// Gets args: CommandOverride *ovr, Client *client, MessageTag *recv_mtags, int parc, char *parv[]
	// Checkem conditions =]
	char nsfx[NICKLEN + 1];
	char *orig;

	if(MyUser(client)) {
		orig = moddata_local_client(client, nicksfxMDI).ptr;
		if(!BadPtr(parv[1]) && strlen(parv[1]) && orig) {
			if(!strcasecmp(parv[1], muhcfg.sfx_restore))
				ircsnprintf(nsfx, sizeof(nsfx), "%s", orig);
			else
				ircsnprintf(nsfx, sizeof(nsfx), "%s%c%s", orig, muhcfg.sfx_separator, parv[1]);
			parv[1] = nsfx;
			do_cmd(client, NULL, "NICK", parc, parv);
			return;
		}
	}

	CallCommandOverride(ovr, client, recv_mtags, parc, parv); // Run original function yo
}

int nicksuffix_hook_prelocalconnect(Client *client) {
	if(!IsULine(client)) { // Let's allow U-Lines to do whatever they want =]
		if(strchr(client->name, muhcfg.sfx_separator)) { // See if nick contains the separator char
			char fullErr[128];
			ircsnprintf(fullErr, sizeof(fullErr), "Nickname is unavailable: Cannot contain %c", muhcfg.sfx_separator); // Make error string
			exit_client(client, NULL, fullErr); // Kbye
			return HOOK_DENY;
		}
		safe_strdup(moddata_local_client(client, nicksfxMDI).ptr, client->name);
	}
	return 0; // We good
}
