/* Copyright (C) All Rights Reserved
** Written by Gottem <support@gottem.nl>
** Website: https://gottem.nl/unreal
** License: https://gottem.nl/unreal/license
*/

/*** <<<MODULE MANAGER START>>>
module {
	documentation "https://gottem.nl/unreal/man/nicksuffix";
	troubleshooting "In case of problems, check the FAQ at https://gottem.nl/unreal/halp or e-mail me at support@gottem.nl";
	min-unrealircd-version "5.*";
	//max-unrealircd-version "5.*";
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
void nicksuffix_md_free(ModData *md);
int nicksuffix_configtest(ConfigFile *cf, ConfigEntry *ce, int type, int *errs);
int nicksuffix_configposttest(int *errs); // You may not need this
int nicksuffix_configrun(ConfigFile *cf, ConfigEntry *ce, int type);
int nicksuffix_rehash(void);
CMD_OVERRIDE_FUNC(nicksuffix_override);
int nicksuffix_hook_prelocalconnect(Client *client);

// Muh globals
ModDataInfo *nicksfxMDI; // Moddata for the original nick =]
char *aloud_sep = "`-_[]{}\\|";

// Config vars
int got_separator = 0; // Ayy
int got_restore = 0; // Checkem
char sfx_separator; // One of "aloud_sep"
char *sfx_restore; // "Command" to restore original nick

// Dat dere module header
ModuleHeader MOD_HEADER = {
	"third/nicksuffix", // Module name
	"2.0.1", // Version
	"Restrict /nick usage to suffixing your base nick", // Description
	"Gottem", // Author
	"unrealircd-5", // Modversion
};

// Configuration testing-related hewks go in testing phase obv
MOD_TEST() {
	// We have our own config block so we need to checkem config obv m9
	// Priorities don't really matter here
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
	return MOD_SUCCESS;
}

MOD_LOAD() {
	CheckAPIError("CommandOverrideAdd(NICK)", CommandOverrideAdd(modinfo->handle, OVR_NICK, nicksuffix_override));
	return MOD_SUCCESS; // We good
}

// Called on unload/rehash obv
MOD_UNLOAD() {
	safe_free(sfx_restore);
	sfx_separator = 0;
	return MOD_SUCCESS; // We good
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

		if(!strcmp(cep->ce_varname, "separator")) {
			got_separator = 1;
			if(!cep->ce_vardata || !strlen(cep->ce_vardata)) {
				config_error("%s:%i: %s::%s must be non-empty fam", cep->ce_fileptr->cf_filename, cep->ce_varlinenum, MYCONF, cep->ce_varname);
				errors++; // Increment err0r count fam
			}
			else if(strlen(cep->ce_vardata) > 1 || !strchr(aloud_sep, cep->ce_vardata[0])) {
				config_error("%s:%i: %s::%s must be exactly one character (and it must be one of: '%s')", cep->ce_fileptr->cf_filename, cep->ce_varlinenum, MYCONF, cep->ce_varname, aloud_sep);
				errors++; // Increment err0r count fam
			}
			continue;
		}

		if(!strcmp(cep->ce_varname, "restore")) {
			got_restore = 1;
			if(!cep->ce_vardata || !strlen(cep->ce_vardata)) {
				config_error("%s:%i: %s::%s must be non-empty fam", cep->ce_fileptr->cf_filename, cep->ce_varlinenum, MYCONF, cep->ce_varname);
				errors++; // Increment err0r count fam
			}
			continue;
		}

		config_warn("%s:%i: unknown item %s::%s", cep->ce_fileptr->cf_filename, cep->ce_varlinenum, MYCONF, cep->ce_varname); // Rep0t warn if unknown directive =]
	}

	*errs = errors;
	return errors ? -1 : 1; // Returning 1 means "all good", -1 means we shat our panties
}

int nicksuffix_configposttest(int *errs) {
	int errors = 0;

	if(!got_separator) {
		config_error("%s::separator is required fam", MYCONF);
		errors++; // Increment err0r count fam
	}
	if(!got_restore) {
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
	if(!ce || !ce->ce_varname)
		return 0;

	// If it isn't nicksuffix, idc
	if(strcmp(ce->ce_varname, MYCONF))
		return 0;

	// Loop dat shyte fam
	for(cep = ce->ce_entries; cep; cep = cep->ce_next) {
		// Do we even have a valid name l0l?
		if(!cep->ce_varname)
			continue; // Next iteration imo tbh

		if(!strcmp(cep->ce_varname, "separator")) {
			sfx_separator = cep->ce_vardata[0];
			continue;
		}

		if(!strcmp(cep->ce_varname, "restore")) {
			safe_strdup(sfx_restore, cep->ce_vardata);
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
		if(!BadPtr(parv[1]) && MyUser(client) && strlen(parv[1]) && orig) {
			if(!strcasecmp(parv[1], sfx_restore))
				ircsnprintf(nsfx, sizeof(nsfx), "%s", orig);
			else
				ircsnprintf(nsfx, sizeof(nsfx), "%s%c%s", orig, sfx_separator, parv[1]);
			parv[1] = nsfx;
			do_cmd(client, NULL, "NICK", parc, parv);
			return;
		}
	}

	CallCommandOverride(ovr, client, recv_mtags, parc, parv); // Run original function yo
}

int nicksuffix_hook_prelocalconnect(Client *client) {
	if(!IsULine(client)) { // Let's allow U-Lines to do whatever they want =]
		if(strchr(client->name, sfx_separator)) { // See if nick contains the separator char
			char fullErr[128];
			ircsnprintf(fullErr, sizeof(fullErr), "Nickname is unavailable: Cannot contain %c", sfx_separator); // Make error string
			exit_client(client, NULL, fullErr); // Kbye
			return HOOK_DENY;
		}
		safe_strdup(moddata_local_client(client, nicksfxMDI).ptr, client->name);
	}
	return 0; // We good
}
