/* Copyright (C) All Rights Reserved
** Written by Gottem <support@gottem.nl>
** Website: https://gottem.nl/unreal
** License: https://gottem.nl/unreal/license
*/

/*** <<<MODULE MANAGER START>>>
module {
	documentation "https://gottem.nl/unreal/man/bancheck_access";
	troubleshooting "In case of problems, check the FAQ at https://gottem.nl/unreal/halp or e-mail me at support@gottem.nl";
	min-unrealircd-version "6.*";
	//max-unrealircd-version "6.*";
	post-install-text {
		"The module is installed, now all you need to do is add a 'loadmodule' line to your config file:";
		"loadmodule \"third/bancheck_access\";";
		"Then /rehash the IRCd.";
		"For usage information, refer to the module's documentation found at: https://gottem.nl/unreal/man/bancheck_access";
	}
}
*** <<<MODULE MANAGER END>>>
*/

// One include for all cross-platform compatibility thangs
#include "unrealircd.h"

// Command to override
#define OVR_MODE "MODE"

#define MYCONF "bancheck_access_notif"

#define CheckAPIError(apistr, apiobj) \
	do { \
		if(!(apiobj)) { \
			config_error("A critical error occurred on %s for %s: %s", (apistr), MOD_HEADER.name, ModuleGetErrorStr(modinfo->handle)); \
			return MOD_FAILED; \
		} \
	} while(0)

#if UNREAL_VERSION >= 0x06020000
	#define CallCommandOverrideCompatU06020000(_parc, _parv) (CallCommandOverride(ovr, clictx, client, recv_mtags, _parc, _parv))
#else
	#define CallCommandOverrideCompatU06020000(_parc, _parv) (CallCommandOverride(ovr, client, recv_mtags, _parc, _parv))
#endif

// Quality fowod declarations
int bancheck_configtest(ConfigFile *cf, ConfigEntry *ce, int type, int *errs);
int bancheck_configrun(ConfigFile *cf, ConfigEntry *ce, int type);
int bancheck_rehash(void);
CMD_OVERRIDE_FUNC(bancheck_override);

int showNotif = 0; // Display message in case of denied masks

// Dat dere module header
ModuleHeader MOD_HEADER = {
	"third/bancheck_access", // Module name
	"2.1.4", // Version
	"Prevents people who have +o or higher from getting banned, unless done by people with +a/+q or opers", // Description
	"Gottem", // Author
	"unrealircd-6", // Modversion
};

// Configuration testing-related hewks go in testing phase obv
MOD_TEST() {
	// We have our own config block so we need to checkem config obv m9
	// Priorities don't really matter here
	HookAdd(modinfo->handle, HOOKTYPE_CONFIGTEST, 0, bancheck_configtest);
	return MOD_SUCCESS;
}

// Initialisation routine (register hooks, commands and modes or create structs etc)
MOD_INIT() {
	MARK_AS_GLOBAL_MODULE(modinfo);

	HookAdd(modinfo->handle, HOOKTYPE_CONFIGRUN, 0, bancheck_configrun);
	HookAdd(modinfo->handle, HOOKTYPE_REHASH, 0, bancheck_rehash);
	return MOD_SUCCESS;
}

// Actually load the module here (also command overrides as they may not exist in MOD_INIT yet)
MOD_LOAD() {
	CheckAPIError("CommandOverrideAdd(MODE)", CommandOverrideAdd(modinfo->handle, OVR_MODE, 0, bancheck_override));
	return MOD_SUCCESS; // We good
}

// Called on unload/rehash obv
MOD_UNLOAD() {
	showNotif = 0;
	return MOD_SUCCESS; // We good
}

int bancheck_configtest(ConfigFile *cf, ConfigEntry *ce, int type, int *errs) {
	int errors = 0; // Error count

	// Since we'll add a top-level block to unrealircd.conf, need to filter on CONFIG_MAIN lmao
	if(type != CONFIG_SET)
		return 0; // Returning 0 means idgaf bout dis

	// Check for valid config entries first
	if(!ce || !ce->name)
		return 0;

	// If it isn't our directive, idc
	if(strcmp(ce->name, MYCONF))
		return 0;

	if(!ce->value || (strcmp(ce->value, "0") && strcmp(ce->value, "1"))) {
		config_error("%s:%i: %s must be either 0 or 1 fam", ce->file->filename, ce->line_number, MYCONF);
		errors++; // Increment err0r count fam
	}

	*errs = errors;
	// Returning 1 means "all good", -1 means we shat our panties
	return errors ? -1 : 1;
}

// "Run" the config (everything should be valid at this point)
int bancheck_configrun(ConfigFile *cf, ConfigEntry *ce, int type) {
	// Since we'll add a top-level block to unrealircd.conf, need to filter on CONFIG_MAIN lmao
	if(type != CONFIG_SET)
		return 0; // Returning 0 means idgaf bout dis

	// Check for valid config entries first
	if(!ce || !ce->name)
		return 0;

	// If it isn't our directive, idc
	if(strcmp(ce->name, MYCONF))
		return 0;

	showNotif = atoi(ce->value);

	return 1; // We good
}

int bancheck_rehash(void) {
	showNotif = 0;
	return HOOK_CONTINUE;
}

// Now for the actual override
CMD_OVERRIDE_FUNC(bancheck_override) {
	// Gets args: CommandOverride *ovr, ClientContext *clictx, Client *client, MessageTag *recv_mtags, int parc, const char *parv[]
	Channel *channel; // Channel pointer
	Client *acptr; // Ban target
	Member *memb = NULL; // Channel members thingy =]
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
	const char *tmpmask; // Store "cleaned" ban mask
	char umask[NICKLEN + USERLEN + HOSTLEN + 24], realumask[NICKLEN + USERLEN + HOSTLEN + 24]; // Full nick!ident@host masks for users yo
	Cmode *chanmode;
	int chanmode_max;
	BanContext *ban_ctx;

	// May not be anything to do =]
	if(parc < 3 || (!IsULine(client) && (!MyUser(client) || IsOper(client)))) {
		CallCommandOverrideCompatU06020000(parc, parv); // Run original function yo
		return;
	}

	// Need to be at least hops or higher on a channel for this to kicc in obv (or U-Line, to prevent bypassing this module with '/cs mode')
	if(!(channel = find_channel(parv[1])) || (!IsULine(client) && !check_channel_access(client, channel, "ho"))) {
		CallCommandOverrideCompatU06020000(parc, parv);
		return;
	}

	// Allow human +a/+q users anyway, but U-Lines will always be subject to restrictions because we have no way to tell who originally executed it
	if(!IsULine(client) && check_channel_access(client, channel, "aq")) {
		CallCommandOverrideCompatU06020000(parc, parv);
		return;
	}

	newparc = 3; // Initialise new param count, starting at 3 lol (MODE #chan +something)
	fc = mc = cc = 0; // Ditto for em other counters
	curdir = '+'; // Set "direction" (+ or -)
	stripped = 0;
	newparv[0] = parv[0];
	newparv[1] = parv[1];
	memset(newflags, '\0', sizeof(newflags)); // Set 'em
	memset(&skip, 0, sizeof(skip));
	memset(umask, '\0', sizeof(umask));
	memset(realumask, '\0', sizeof(realumask));

	// Loop over every mode flag
	for(i = 0; i < strlen(parv[2]); i++) {
		c = parv[2][i];
		cont = 0;

		// Check if we need to verify somethang
		switch(c) {
			// Directionals yo
			case '+':
			case '-':
				curdir = c;
				break;

			// Do shit for bans only
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

				// Ensure "+b *" is turned into shit like "+b *!*@*" so we can easily check bel0w =]
				#if UNREAL_VERSION <= 0x06010700
					tmpmask = clean_ban_mask(parv[j], MODE_ADD, client, 0);
				#else
					tmpmask = clean_ban_mask(parv[j], MODE_ADD, EXBTYPE_BAN, client, channel, 0);
				#endif

				if(!tmpmask)
					break;

				ban_ctx = safe_alloc(sizeof(BanContext));
				ban_ctx->channel = channel;
				ban_ctx->ban_check_types = BANCHK_ALL;
				ban_ctx->ban_type = EXBTYPE_BAN;
				for(memb = channel->members; memb; memb = memb->next) {
					acptr = memb->client; // Potential ban target
					if(!acptr || !check_channel_access(acptr, channel, "oaq"))
						continue;

					ban_ctx->client = acptr;
					ban_ctx->banstr = tmpmask; // Need to reset this every time because it may be changed by below function call xd
					if(ban_check_mask(ban_ctx)) {
						skip[j] = 1; // Skip it lol
						newparc--; // Decrement parc so Unreal doesn't shit itself =]
						cont = 1;
						stripped++;
						break;
					}
				}

				safe_free(ban_ctx);
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

			// Let's also account for ne third-party m0ds we don't know about, which should always have ->paracount set I think
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

	if(stripped && showNotif)
		sendto_one(client, NULL, ":%s NOTICE %s :[BA] Stripped %d mask(s) (denied)", me.name, channel->name, stripped);

	// Nothing left, don't even bother passing it back =]
	if(!newflags[0])
		return;

	// Heck 'em
	newparv[2] = newflags;
	if(newparc <= MAXPARA)
		newparv[newparc] = NULL;
	else
		newparv[MAXPARA] = NULL;
	CallCommandOverrideCompatU06020000(newparc, newparv);
}
