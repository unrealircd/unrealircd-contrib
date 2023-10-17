/* Copyright (C) All Rights Reserved
** Written by Gottem <support@gottem.nl>
** Website: https://gottem.nl/unreal
** License: https://gottem.nl/unreal/license
*/

/*** <<<MODULE MANAGER START>>>
module {
	documentation "https://gottem.nl/unreal/man/denyban";
	troubleshooting "In case of problems, check the FAQ at https://gottem.nl/unreal/halp or e-mail me at support@gottem.nl";
	min-unrealircd-version "6.*";
	//max-unrealircd-version "6.*";
	post-install-text {
		"The module is installed, now all you need to do is add a 'loadmodule' line to your config file:";
		"loadmodule \"third/denyban\";";
		"Then /rehash the IRCd.";
		"For usage information, refer to the module's documentation found at: https://gottem.nl/unreal/man/denyban";
	}
}
*** <<<MODULE MANAGER END>>>
*/

// One include for all cross-platform compatibility thangs
#include "unrealircd.h"

// Config block
#define MYCONF "denyban"

// Commands to override
#define OVR_MODE "MODE"
#define OVR_SAMODE "SAMODE"

#define CheckAPIError(apistr, apiobj) \
	do { \
		if(!(apiobj)) { \
			config_error("A critical error occurred on %s for %s: %s", (apistr), MOD_HEADER.name, ModuleGetErrorStr(modinfo->handle)); \
			return MOD_FAILED; \
		} \
	} while(0)

// Big hecks go here
typedef struct t_denyBan denyBan;
struct t_denyBan {
	char *mask;
	denyBan *next;
};

// Quality fowod declarations
denyBan *find_denyBan(const char *mask);
char *replaceem(char *str, char *search, char *replace);
int denyban_configtest(ConfigFile *cf, ConfigEntry *ce, int type, int *errs);
int denyban_configposttest(int *errs);
int denyban_configrun(ConfigFile *cf, ConfigEntry *ce, int type);
int denyban_rehash(void);
CMD_OVERRIDE_FUNC(denyban_modeoverride);

denyBan *denyBanList = NULL; // Premium linked list
int denyCount = 0; // Counter yo

// Set config defaults here
int allowOpers = 1; // Allow IRC opers to set denied bans regardless
int denyNotice = 0; // Display notifications why something was denied/stripped
char *denyReason = NULL; // What message to display

// Dat dere module header
ModuleHeader MOD_HEADER = {
	"third/denyban", // Module name
	"2.1.2", // Version
	"Deny specific ban masks network-wide", // Description
	"Gottem", // Author
	"unrealircd-6", // Modversion
};

// Configuration testing-related hewks go in testing phase obv
MOD_TEST() {
	// We have our own config block so we need to checkem config obv m9
	// Priorities don't really matter here
	HookAdd(modinfo->handle, HOOKTYPE_CONFIGTEST, 0, denyban_configtest);
	HookAdd(modinfo->handle, HOOKTYPE_CONFIGPOSTTEST, 0, denyban_configposttest);
	return MOD_SUCCESS;
}

// Initialisation routine (register hooks, commands and modes or create structs etc)
MOD_INIT() {
	MARK_AS_GLOBAL_MODULE(modinfo);

	HookAdd(modinfo->handle, HOOKTYPE_REHASH, -1, denyban_rehash);
	HookAdd(modinfo->handle, HOOKTYPE_CONFIGRUN, 0, denyban_configrun);
	return MOD_SUCCESS;
}

MOD_LOAD() {
	CheckAPIError("CommandOverrideAdd(MODE)", CommandOverrideAdd(modinfo->handle, OVR_MODE, 0, denyban_modeoverride));
	CheckAPIError("CommandOverrideAdd(SAMODE)", CommandOverrideAdd(modinfo->handle, OVR_SAMODE, 0, denyban_modeoverride));
	return MOD_SUCCESS; // We good
}

// Called on unload/rehash obv
MOD_UNLOAD() {
	// Clean up shit here
	if(denyBanList) {
		// This shit is a bit convoluted to prevent memory issues obv famalmalmalmlmalm
		denyBan *dbEntry;
		while((dbEntry = denyBanList) != NULL) {
			denyBanList = denyBanList->next;
			safe_free(dbEntry->mask);
			safe_free(dbEntry);
		}
		denyBanList = NULL;
	}
	denyCount = 0; // Just to maek shur
	safe_free(denyReason);
	return MOD_SUCCESS; // We good
}

denyBan *find_denyBan(const char *mask) {
	denyBan *dbEntry; // Muh iter80r
	for(dbEntry = denyBanList; dbEntry; dbEntry = dbEntry->next) {
		// Check if the denied entry's mask matches the user-specified mask
		// BAN_ALL is special obv ;]
		if(!strcmp(dbEntry->mask, "BAN_ALL")) {
			const char *p = mask;
			int banall = 1; // Default to trve, we only need one mismatch to break out
			while(*p) { // While we have a char
				// The +b mask must match *!*@*, *!*@******, *!*@*.***, etc =]
				if(*p != '*' && *p != '!' && *p != '@' && *p != '.') {
					banall = 0;
					break;
				}
				p++;
			}

			if(banall)
				return dbEntry;
		}

		// Otherwise just use the internal wildcard matching thingy =]
		else if(match_simple(dbEntry->mask, mask))
			return dbEntry;
	}

	return NULL;
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

int denyban_configtest(ConfigFile *cf, ConfigEntry *ce, int type, int *errs) {
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

		if(!strcmp(cep->name, "mask")) {
			if(!cep->value || !strlen(cep->value)) {
				config_error("%s:%i: %s::mask must be non-empty fam", cep->file->filename, cep->line_number, MYCONF);
				errors++; // Increment err0r count fam
				continue;
			}

			if(strcmp(cep->value, "BAN_ALL") && (strlen(cep->value) < 5 || !strchr(cep->value, '!') || !strchr(cep->value, '@'))) {
				config_error("%s:%i: %s::mask must be a wildcard match on a full nick mask, like: nick!ident@host", cep->file->filename, cep->line_number, MYCONF);
				errors++; // Increment err0r count fam
				continue;
			}
			denyCount++;
			continue;
		}

		if(!strcmp(cep->name, "allowopers")) {
			if(!cep->value || (strcmp(cep->value, "0") && strcmp(cep->value, "1"))) {
				config_error("%s:%i: %s::allowopers must be either 0 or 1 fam", cep->file->filename, cep->line_number, MYCONF);
				errors++; // Increment err0r count fam
			}
			continue;
		}

		if(!strcmp(cep->name, "denynotice")) {
			if(!cep->value || (strcmp(cep->value, "0") && strcmp(cep->value, "1"))) {
				config_error("%s:%i: %s::denynotice must be either 0 or 1 fam", cep->file->filename, cep->line_number, MYCONF);
				errors++; // Increment err0r count fam
			}
			continue;
		}

		if(!strcmp(cep->name, "reason")) {
			if(!cep->value || strlen(cep->value) <= 2) {
				config_error("%s:%i: %s::reason must be longer than 2 characters", cep->file->filename, cep->line_number, MYCONF);
				errors++; // Increment err0r count fam
			}
			continue;
		}

		config_warn("%s:%i: unknown directive %s::%s", cep->file->filename, cep->line_number, MYCONF, cep->name);
	}

	*errs = errors;
	// Returning 1 means "all good", -1 means we shat our panties
	return errors ? -1 : 1;
}

// Post test, check for missing shit here
int denyban_configposttest(int *errs) {
	if(!denyCount)
		config_warn("%s was loaded but there aren't any configured deny entries (denyban {} block)", MOD_HEADER.name);
	return 1;
}

// "Run" the config (everything should be valid at this point)
int denyban_configrun(ConfigFile *cf, ConfigEntry *ce, int type) {
	ConfigEntry *cep; // To store the current variable/value pair etc
	denyBan *last = NULL; // Initialise to NULL so the loop requires minimal l0gic
	denyBan **dbEntry = &denyBanList; // Hecks so the ->next chain stays intact

	// If configtest didn't find ne entries, just bail out here
	if(!denyCount)
		return 0; // Returning 0 means idgaf bout dis

	// Since we'll add a top-level block to unrealircd.conf, need to filter on CONFIG_MAIN lmao
	if(type != CONFIG_MAIN)
		return 0; // Returning 0 means idgaf bout dis

	// Check for valid config entries first
	if(!ce || !ce->name)
		return 0;

	// If it isn't denyban, idc
	if(strcmp(ce->name, MYCONF))
		return 0;

		// Loop dat shyte fam
	for(cep = ce->items; cep; cep = cep->next) {
		// Do we even have a valid name l0l?
		if(!cep->name && cep->value)
			continue; // Next iteration imo tbh

		if(!strcmp(cep->name, "mask")) {
			// Lengths to alloc8 the struct vars with in a bit
			size_t masklen = sizeof(char) * (strlen(cep->value) + 1);

			// Allocate mem0ry for the current entry
			*dbEntry = safe_alloc(sizeof(denyBan));

			// Allocate/initialise shit here
			(*dbEntry)->mask = safe_alloc(masklen);

			// Copy that shit fam
			strncpy((*dbEntry)->mask, cep->value, masklen);

			// Premium linked list fam
			if(last)
				last->next = *dbEntry;

			last = *dbEntry;
			dbEntry = &(*dbEntry)->next;
			continue;
		}

		if(!strcmp(cep->name, "allowopers")) {
			allowOpers = atoi(cep->value);
			continue;
		}

		if(!strcmp(cep->name, "denynotice")) {
			denyNotice = atoi(cep->value);
			continue;
		}

		if(!strcmp(cep->name, "reason")) {
			safe_strdup(denyReason, cep->value);
			continue;
		}
	}

	return 1; // We good
}

int denyban_rehash(void) {
	// Reset config defaults
	allowOpers = 1;
	denyNotice = 0;
	safe_free(denyReason);
	return HOOK_CONTINUE;
}

// Hecks
CMD_OVERRIDE_FUNC(denyban_modeoverride) {
	// Gets args: CommandOverride *ovr, Client *client, MessageTag *recv_mtags, int parc, char *parv[]
	Channel *channel; // Channel pointer
	int fc, mc, cc; // Flag count, mask count and char count respectively
	int i, j; // Just s0em iterators fam
	int stripped; // Count 'em
	int skip[MAXPARA + 1]; // Skippem
	int newparc; // Keep track of proper param count
	int cont, dironly;
	char newflags[MODEBUFLEN + 3]; // Store cleaned up flags
	const char *newparv[MAXPARA + 1]; // Ditto for masks etc
	char c; // Current flag lol, can be '+', '-' or any letturchar
	char curdir; // Current direction (add/del etc)
	const char *mask; // Store "cleaned" ban mask
	char *reason; // Message to display
	char num[8]; // Store stripped as char lol
	Cmode *chanmode;
	int chanmode_max;

	// May not be anything to do =]
	if(!MyUser(client) || (IsOper(client) && allowOpers) || parc < 3) {
		CallCommandOverride(ovr, client, recv_mtags, parc, parv); // Run original function yo
		return;
	}

	// Need to be at least hops or higher on a channel for this to kicc in obv (or U-Line, to prevent bypassing this module with '/cs mode')
	if(!(channel = find_channel(parv[1])) || !(check_channel_access(client, channel, "hoaq") || IsULine(client))) {
		CallCommandOverride(ovr, client, recv_mtags, parc, parv); // Run original function yo
		return;
	}

	newparc = 3; // Initialise new param count, starting at 3 lol (MODE #chan +something)
	fc = mc = cc = 0; // Ditto for em other counters
	curdir = '+'; // Set "direction" (+ or -)
	stripped = 0;
	newparv[0] = parv[0];
	newparv[1] = parv[1];
	memset(newflags, '\0', sizeof(newflags)); // Set 'em
	for(i = 0; i < parc; i++)
		skip[i] = 0; // Set default so the loop doesn't fuck it up as it goes along

	// Loop over every mode flag
	for(i = 0; i < strlen(parv[2]); i++) {
		c = parv[2][i];
		mask = NULL; // Aye elemao
		cont = 0;

		// Check if we need to verify somethang
		switch(c) {
			// Directionals yo
			case '+':
			case '-':
				curdir = c;
				break;

			// El list stuff
			case 'b': // Ban
				fc++;
				j = mc + 3; // In parv[] the first mask is found at index 3
				if(parc <= j || BadPtr(parv[j])) {
					if(fc > 1) // Skip this flag entirely so we don't fall through to listing ban masks in case of shit like '+bb foo!bar@ke.ks'
						cont = 1;
					break;
				}
				mc++;
				newparc++;

				// Only check setting of that shit
				if(curdir != '+')
					break;

				// Turn "+b *" into "+b *!*@*" so we can easily check bel0w =]
				mask = clean_ban_mask(parv[j], (curdir == '-' ? MODE_DEL : MODE_ADD), client, 0);
				if(!mask)
					break;

				if(find_denyBan(mask)) {
					skip[j] = 1; // Skip it lol
					newparc--; // Decrement parc again so Unreal doesn't shit itself =]
					cont = 1;
					stripped++;
				}
				break;

			// Some other modes may have an argument t00
			case 'e': // Ban exempts
			case 'I': // Invite exempts
			case 'v': // Access mode, voice
			case 'h': // Hops
			case 'o': // Ops
			case 'a': // Chanadmin
			case 'q': // Chanowner
			case 'f': // Floodprot
			case 'k': // Channel key
			case 'L': // Channel link
			case 'l': // Limit
			case 'j': // Kickjoindelay
			case 'J': // Joinmute
			case 'H': // History
				fc++;
				j = mc + 3;
				if(parc <= j || BadPtr(parv[j])) {
					if(fc > 1)
						cont = 1;
					break;
				}
				mc++;
				newparc++;
				break;

			default:
				fc++;
				chanmode = find_channel_mode_handler(c);
				if(!chanmode || !chanmode->paracount || (!chanmode->unset_with_param && curdir == '-'))
					break;

				j = mc + 3;
				chanmode_max = j + chanmode->paracount;
				while(j < chanmode_max) {
					if(parc <= j || BadPtr(parv[j])) {
						if(fc > 1)
							cont = 1;
						break;
					}
					mc++;
					newparc++;
					j++;
				}
				break;
		}

		if(cont)
			continue;
		newflags[cc++] = c; // Seems to be a sane mode, append it
	}

	// Correct parv count due to possibly (now) missing flags/masks lol
	dironly = 1;
	for(i = 0; i < cc && newflags[i]; i++) {
		if(!strchr("+-", newflags[i])) {
			dironly = 0;
			break;
		}
	}
	if(!cc || dironly) {
		newparc--;
		memset(newflags, '\0', sizeof(newflags)); // Reset 'em
	}

	// Now checkem masks, we have to do this separately so we can reliably get the (proper) corresponding mask
	for(i = 3, j = 3; i < parc && j < newparc && !BadPtr(parv[i]); i++) {
		if(skip[i] || !strlen(parv[i]))
			continue;

		// Now store this mask =]
		newparv[j++] = parv[i];
	}

	if(stripped && denyNotice) {
		if(denyReason && match_simple("*$num*", denyReason)) {
			snprintf(num, sizeof(num), "%d", stripped);
			reason = replaceem(denyReason, "$num", num);
			sendto_one(client, NULL, ":%s NOTICE %s :%s", me.name, channel->name, reason);
			safe_free(reason);
		}
		else if(denyReason)
			sendto_one(client, NULL, ":%s NOTICE %s :%s", me.name, channel->name, denyReason);
		else
			sendto_one(client, NULL, ":%s NOTICE %s :[DB] Stripped %d mask(s) (denied)", me.name, channel->name, stripped);
	}

	// Nothing left, don't even bother passing it back =]
	if(!newflags[0])
		return;

	// Heck 'em
	newparv[2] = newflags;
	if(newparc <= MAXPARA)
		newparv[newparc] = NULL;
	else
		newparv[MAXPARA] = NULL;
	CallCommandOverride(ovr, client, recv_mtags, newparc, newparv); // Run original function yo
}
