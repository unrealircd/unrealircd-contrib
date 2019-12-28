/* Copyright (C) All Rights Reserved
** Written by Gottem <support@gottem.nl>
** Website: https://gottem.nl/unreal
** License: https://gottem.nl/unreal/license
*/

/*** <<<MODULE MANAGER START>>>
module {
	documentation "https://gottem.nl/unreal/man/anticaps";
	troubleshooting "In case of problems, check the FAQ at https://gottem.nl/unreal/halp or e-mail me at support@gottem.nl";
	min-unrealircd-version "5.*";
	//max-unrealircd-version "5.*";
	post-install-text {
		"The module is installed, now all you need to do is add a 'loadmodule' line to your config file:";
		"loadmodule \"third/anticaps\";";
		"Then /rehash the IRCd.";
		"For usage information, refer to the module's documentation found at: https://gottem.nl/unreal/man/anticaps";
	}
}
*** <<<MODULE MANAGER END>>>
*/

// One include for all cross-platform compatibility thangs
#include "unrealircd.h"

// Command to override
#define OVR_PRIVMSG "PRIVMSG"
#define OVR_NOTICE "NOTICE"

// Config block
#define MYCONF "anticaps"

#define CheckAPIError(apistr, apiobj) \
	do { \
		if(!(apiobj)) { \
			config_error("A critical error occurred on %s for %s: %s", (apistr), MOD_HEADER.name, ModuleGetErrorStr(modinfo->handle)); \
			return MOD_FAILED; \
		} \
	} while(0)

// Quality fowod declarations
int anticaps_configtest(ConfigFile *cf, ConfigEntry *ce, int type, int *errs);
int anticaps_configrun(ConfigFile *cf, ConfigEntry *ce, int type);
int anticaps_rehash(void);
CMD_OVERRIDE_FUNC(anticaps_override);

// Muh globals
int capsLimit = 50; // Default to blocking >= 50% caps
int minLength = 30; // Minimum length of 30 before we checkem
int lcIt = 0; // Lowercase 'em instead

// Dat dere module header
ModuleHeader MOD_HEADER = {
	"third/anticaps", // Module name
	"2.0", // Version
	"Block/lowercase messages that contain a configurable amount of capital letters", // Description
	"Gottem", // Author
	"unrealircd-5", // Modversion
};

// Configuration testing-related hewks go in testing phase obv
MOD_TEST() {
	// We have our own config block so we need to checkem config obv m9
	// Priorities don't really matter here
	HookAdd(modinfo->handle, HOOKTYPE_CONFIGTEST, 0, anticaps_configtest);
	return MOD_SUCCESS;
}

// Initialisation routine (register hooks, commands and modes or create structs etc)
MOD_INIT() {
	MARK_AS_GLOBAL_MODULE(modinfo);

	HookAdd(modinfo->handle, HOOKTYPE_REHASH, 0, anticaps_rehash);
	HookAdd(modinfo->handle, HOOKTYPE_CONFIGRUN, 0, anticaps_configrun);
	return MOD_SUCCESS;
}

// Actually load the module here (also command overrides as they may not exist in MOD_INIT yet)
MOD_LOAD() {
	// Lower priority overrides so we can go *after* any potential set::restrict-command directives
	CheckAPIError("CommandOverrideAdd(PRIVMSG)", CommandOverrideAdd(modinfo->handle, OVR_PRIVMSG, anticaps_override));
	CheckAPIError("CommandOverrideAdd(NOTICE)", CommandOverrideAdd(modinfo->handle, OVR_NOTICE, anticaps_override));
	return MOD_SUCCESS; // We good
}

// Called on unload/rehash obv
MOD_UNLOAD() {
	return MOD_SUCCESS; // We good
}

int anticaps_configtest(ConfigFile *cf, ConfigEntry *ce, int type, int *errs) {
	int errors = 0; // Error count
	int i, limit; // Iterat0r
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
		if(!cep->ce_varname || !cep->ce_vardata) {
			config_error("%s:%i: blank %s item", cep->ce_fileptr->cf_filename, cep->ce_varlinenum, MYCONF); // Rep0t error
			errors++; // Increment err0r count fam
			continue; // Next iteration imo tbh
		}

		if(!strcmp(cep->ce_varname, "capslimit")) {
			limit = atoi(cep->ce_vardata);
			if(limit <= 0 || limit > 100) {
				config_error("%s:%i: %s::capslimit must be an integer from 1 thru 99 (represents a percentage)", cep->ce_fileptr->cf_filename, cep->ce_varlinenum, MYCONF);
				errors++;
			}
			continue;
		}

		if(!strcmp(cep->ce_varname, "minlength")) {
			for(i = 0; cep->ce_vardata[i]; i++) {
				if(!isdigit(cep->ce_vardata[i])) {
					config_error("%s:%i: %s::minlength must be an integer of 0 or larger m8", cep->ce_fileptr->cf_filename, cep->ce_varlinenum, MYCONF);
					errors++; // Increment err0r count fam
					break;
				}
			}
			continue;
		}

		if(!strcmp(cep->ce_varname, "lowercase_it")) {
			if(!cep->ce_vardata || (strcmp(cep->ce_vardata, "0") && strcmp(cep->ce_vardata, "1"))) {
				config_error("%s:%i: %s::lowercase_it must be either 0 or 1 fam", cep->ce_fileptr->cf_filename, cep->ce_varlinenum, MYCONF);
				errors++; // Increment err0r count fam
			}
			continue;
		}
	}

	*errs = errors;
	// Returning 1 means "all good", -1 means we shat our panties
	return errors ? -1 : 1;
}

// "Run" the config (everything should be valid at this point)
int anticaps_configrun(ConfigFile *cf, ConfigEntry *ce, int type) {
	ConfigEntry *cep; // To store the current variable/value pair etc, nested

	// Since we'll add a top-level block to unrealircd.conf, need to filter on CONFIG_MAIN lmao
	if(type != CONFIG_MAIN)
		return 0; // Returning 0 means idgaf bout dis

	// Check for valid config entries first
	if(!ce || !ce->ce_varname)
		return 0;

	// If it isn't anticaps, idc
	if(strcmp(ce->ce_varname, MYCONF))
		return 0;

	for(cep = ce->ce_entries; cep; cep = cep->ce_next) {
		// Do we even have a valid name l0l?
		if(!cep->ce_varname || !cep->ce_vardata)
			continue; // Next iteration imo tbh

		if(!strcmp(cep->ce_varname, "capslimit")) {
			capsLimit = atoi(cep->ce_vardata);
			continue;
		}

		if(!strcmp(cep->ce_varname, "minlength")) {
			minLength = atoi(cep->ce_vardata);
			continue;
		}

		if(!strcmp(cep->ce_varname, "lowercase_it")) {
			lcIt = atoi(cep->ce_vardata);
			continue;
		}
	}

	return 1; // We good
}

int anticaps_rehash(void) {
	// Reset config defaults
	capsLimit = 50;
	minLength = 30;
	lcIt = 0;
	return HOOK_CONTINUE;
}

// Now for the actual override
CMD_OVERRIDE_FUNC(anticaps_override) {
	// Gets args: CommandOverride *ovr, Client *client, MessageTag *recv_mtags, int parc, char *parv[]
	char plaintext[BUFSIZE]; // Let's not modify parv[2] directly =]
	char *tmpp; // We gonna fix up da string fam
	int perc; // Store percentage etc
	int i, len, rlen, caps; // To count full length as well as caps

	if(BadPtr(parv[1]) || BadPtr(parv[2]) || !client || !MyUser(client) || IsULine(client) || IsOper(client) || strlen(parv[2]) < minLength) {
		CallCommandOverride(ovr, client, recv_mtags, parc, parv); // Run original function yo
		return;
	}

	// Some shitty ass scripts may use different colours/markup across chans, so fuck that
	if(!(tmpp = (char *)StripControlCodes(parv[2]))) {
		CallCommandOverride(ovr, client, recv_mtags, parc, parv); // Run original function yo
		return;
	}

	memset(plaintext, '\0', sizeof(plaintext));
	strlcpy(plaintext, tmpp, sizeof(plaintext));
	perc = len = rlen = caps = 0;

	for(i = 0; plaintext[i]; i++, rlen++) {
		if(plaintext[i] == 32) // Let's skip spaces too
			continue;

		if(plaintext[i] >= 65 && plaintext[i] <= 90) // Premium ASCII yo
			caps++;
		len++;
	}

	if(!caps || !len) { // Inb4division by zero lmao {
		CallCommandOverride(ovr, client, recv_mtags, parc, parv); // Run original function yo
		return;
	}

	i = 0;
	if(*plaintext == '\001') { // Might be an ACTION or CTCP
		if(rlen > 7 && !strncmp(&plaintext[1], "ACTION ", 7) && plaintext[rlen - 1] == '\001') {
			caps -= 6; // Reduce caps count by 6 chars y0
			len -= 8; // Also reduce the total length (including both \001's) so we're still good percent-wise
			i = 8; // Let's not lowercase the ACTION bit later ;];];]];]
		}
		else if(plaintext[rlen - 1] == '\001') // Not an ACTION so maybe a CTCP, ignore it all if so
			caps = 0;

		if(caps <= 0 || len <= 0) { // Correction may have reduced it to zero (never really _below_ zero but let's just anyways lel)
			CallCommandOverride(ovr, client, recv_mtags, parc, parv); // Run original function yo
			return;
		}
	}

	perc = (int)(((float)caps / (float)len) * 100);
	if(perc >= capsLimit) {
		if(!lcIt) { // If not configured to lowercase em, deny/drop the message
			sendnotice(client, "*** Cannot send to %s: your message contains too many capital letters (%d%% >= %d%%)", parv[1], perc, capsLimit);
			return; // Stop processing yo
		}

		// Lowercase it all lol
		for(; plaintext[i]; i++) {
			if(plaintext[i] < 65 || plaintext[i] > 122) // Premium ASCII yo, lazy mode
				continue;
			plaintext[i] = tolower(plaintext[i]);
		}
		parv[2] = plaintext;
	}

	CallCommandOverride(ovr, client, recv_mtags, parc, parv); // Run original function yo
}
