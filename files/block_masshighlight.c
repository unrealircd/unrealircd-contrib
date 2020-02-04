/* Copyright (C) All Rights Reserved
** Written by Gottem <support@gottem.nl>
** Contains edits by k4be to make the threshold checking more robust
** Website: https://gottem.nl/unreal
** License: https://gottem.nl/unreal/license
*/

/*** <<<MODULE MANAGER START>>>
module {
	documentation "https://gottem.nl/unreal/man/block_masshighlight";
	troubleshooting "In case of problems, check the FAQ at https://gottem.nl/unreal/halp or e-mail me at support@gottem.nl";
	min-unrealircd-version "5.*";
	//max-unrealircd-version "5.*";
	post-install-text {
		"The module is installed, now all you need to do is add a 'loadmodule' line to your config file:";
		"loadmodule \"third/block_masshighlight\";";
		"Then /rehash the IRCd.";
		"For usage information, refer to the module's documentation found at: https://gottem.nl/unreal/man/block_masshighlight";
	}
}
*** <<<MODULE MANAGER END>>>
*/

// One include for all cross-platform compatibility thangs
#include "unrealircd.h"

// Config block
#define MYCONF "block_masshighlight"

// Channel mode for exempting from highlight checks
#define IsNocheckHighlights(channel) (channel->mode.extmode & extcmode_nocheck_masshl)
#define CHMODE_CHAR 'B'

// Dem macros yo
#define CheckAPIError(apistr, apiobj) \
	do { \
		if(!(apiobj)) { \
			config_error("A critical error occurred on %s for %s: %s", (apistr), MOD_HEADER.name, ModuleGetErrorStr(modinfo->handle)); \
			return MOD_FAILED; \
		} \
	} while(0)

// Quality fowod declarations
int is_accessmode_exempt(Client *client, Channel *channel);
int extcmode_requireowner(Client *client, Channel *channel, char mode, char *para, int checkt, int what);
void doXLine(char flag, Client *client);
int masshighlight_configtest(ConfigFile *cf, ConfigEntry *ce, int type, int *errs);
int masshighlight_configposttest(int *errs);
int masshighlight_configrun(ConfigFile *cf, ConfigEntry *ce, int type);
void masshighlight_md_free(ModData *md);
void masshighlight_client_md_free(ModData *md);
int masshighlight_get_client_moddata(Client *client, Channel *channel);
void masshighlight_set_client_moddata(Client *client, Channel *channel, int hl_count);
int masshighlight_hook_cansend_chan(Client *client, Channel *channel, Membership *lp, char **text, char **errmsg, int notice);

int spamf_ugly_vchanoverride = 0; // For viruschan shit =]
ModDataInfo *massHLMDI; // To store some shit with the channel ;]
ModDataInfo *massHLUserMDI; // For storing external message hls
Cmode_t extcmode_nocheck_masshl; // For storing the exemption chanmode =]

struct {
	unsigned short int maxnicks; // Maxnicks, unsigned cuz can't be negative anyways lol
	char *delimiters; // List of delimiters for splitting a sentence into "words"
	char action; // Simple char like 'g' for gline etc
	time_t duration; // How long to ban for
	char *reason; // Reason for *-Lines or for the notice to offending users
	unsigned short int snotice; // Whether to send snotices or n0, simply 0 or 1
	unsigned short int banident; // Whether to ban ident@host or simply *@host
	unsigned short int multiline; // Check over multiple lines or just the one ;]
	unsigned short int allow_authed; // Allow identified users to bypass this shit
	unsigned int allow_accessmode; // Lowest channel access mode privilege to bypass the limit
	unsigned short int percent; // How many characters in a message recognised as a nickname is enough for the message to be rejected
	unsigned short int show_opers_origmsg; // Display the suspicious message to operators

	// These are just for setting to 0 or 1 to see if we got em config directives ;]
	unsigned short int got_maxnicks;
	unsigned short int got_delimiters;
	unsigned short int got_action;
	unsigned short int got_duration;
	unsigned short int got_reason;
	unsigned short int got_snotice;
	unsigned short int got_banident;
	unsigned short int got_multiline;
	unsigned short int got_allow_authed;
	unsigned short int got_allow_accessmode;
	unsigned short int got_percent;
	unsigned short int got_show_opers_origmsg;
} muhcfg;

struct user_hls {
	char *chname;
	int count;
	struct user_hls *next;
};

// Dat dere module header
ModuleHeader MOD_HEADER = {
	"third/block_masshighlight", // Module name
	"2.1", // Version
	"Prevent mass highlights network-wide", // Description
	"Gottem / k4be", // Author
	"unrealircd-5", // Modversion
};

// Configuration testing-related hewks go in testing phase obv
MOD_TEST() {
	// We have our own config block so we need to checkem config obv m9
	// Priorities don't really matter here
	HookAdd(modinfo->handle, HOOKTYPE_CONFIGTEST, 0, masshighlight_configtest);
	HookAdd(modinfo->handle, HOOKTYPE_CONFIGPOSTTEST, 0, masshighlight_configposttest);
	return MOD_SUCCESS;
}

// Initialisation routine (register hooks, commands and modes or create structs etc)
MOD_INIT() {
	// Request moddata for storing the last counter etc
	ModDataInfo mreq;
	memset(&mreq, 0, sizeof(mreq));
	mreq.type = MODDATATYPE_MEMBERSHIP; // Apply to memberships only (look at the user and then iterate through the channel list)
	mreq.name = "masshighlight"; // Name it
	mreq.free = masshighlight_md_free; // Function to free 'em
	massHLMDI = ModDataAdd(modinfo->handle, mreq);
	CheckAPIError("ModDataAdd(masshighlight_membership)", massHLMDI);

	memset(&mreq, 0, sizeof(mreq));
	mreq.type = MODDATATYPE_LOCAL_CLIENT; // Apply to clients
	mreq.name = "masshighlight_clent"; // Name it
	mreq.free = masshighlight_client_md_free; // Function to free 'em
	massHLUserMDI = ModDataAdd(modinfo->handle, mreq);
	CheckAPIError("ModDataAdd(masshighlight_client)", massHLUserMDI);

	// Also request +H channel mode fam
	CmodeInfo req;
	memset(&req, 0, sizeof(req));
	req.paracount = 0; // No args required ;]
	req.flag = CHMODE_CHAR;
	req.is_ok = extcmode_requireowner; // Need owner privs to set em imo tbh
	CheckAPIError("CmodeAdd(extcmode_nocheck_masshl)", CmodeAdd(modinfo->handle, req, &extcmode_nocheck_masshl));

	MARK_AS_GLOBAL_MODULE(modinfo);

	// Register hewks m8
	HookAdd(modinfo->handle, HOOKTYPE_CAN_SEND_TO_CHANNEL, 0, masshighlight_hook_cansend_chan);
	HookAdd(modinfo->handle, HOOKTYPE_CONFIGRUN, 0, masshighlight_configrun);
	return MOD_SUCCESS;
}

// Actually load the module here (also command overrides as they may not exist in MOD_INIT yet)
MOD_LOAD() {
	return MOD_SUCCESS; // We good
}

// Called on unload/rehash obv
MOD_UNLOAD() {
	safe_free(muhcfg.reason); // Let's free this lol
	safe_free(muhcfg.delimiters);
	return MOD_SUCCESS; // We good
}

// Client exempted through allow_accessmode?
int is_accessmode_exempt(Client *client, Channel *channel) {
	Membership *lp; // For checkin' em list access level =]

	if(IsServer(client) || IsMe(client)) // Allow servers always lel
		return 1;

	if(!muhcfg.allow_accessmode) // Don't even bother ;]
		return 0;

	if(channel) { // Sanity cheqq
		if((lp = find_membership_link(client->user->channel, channel))) {
			if(lp->flags & muhcfg.allow_accessmode)
				return 1;
		}
	}

	return 0; // No valid channel/membership or doesn't have enough axx lol
}

// Testing for owner status on channel
int extcmode_requireowner(Client *client, Channel *channel, char mode, char *para, int checkt, int what) {
	if(IsUser(client) && is_chanowner(client, channel))
		return EX_ALLOW;
	if(checkt == EXCHK_ACCESS_ERR)
		sendnumeric(client, ERR_CHANOWNPRIVNEEDED, channel->chname);
	return EX_DENY;
}

// Not using place_host_ban() cuz I need more control over the mask to ban ;];;];]
void doXLine(char flag, Client *client) {
	// Double check for client existing, cuz inb4segfault
	if(client) {
		char setTime[100], expTime[100];
		ircsnprintf(setTime, sizeof(setTime), "%li", TStime());
		ircsnprintf(expTime, sizeof(expTime), "%li", TStime() + muhcfg.duration);
		char *tkltype = safe_alloc(sizeof(char) * 2); // Convert the single char to a char *
		tkltype[0] = (flag == 's' ? flag : toupper(flag)); // Uppercase that shit if not shunning y0
		tkltype[1] = '\0';

		// Build TKL args
		char *tkllayer[9] = {
			// :SERVER +FLAG IDENT HOST SETBY EXPIRATION SETAT :REASON
			me.name,
			"+",
			tkltype,
			(muhcfg.banident ? client->user->username : "*"), // Wildcard ident if banident == 0
			(flag == 'z' ? GetIP(client) : client->user->realhost), // Let's use the IP in case of Z-Lines lel
			me.name,
			expTime,
			setTime,
			muhcfg.reason
		};

		cmd_tkl(&me, NULL, 9, tkllayer); // Ban 'em
		safe_free(tkltype); // Free that shit lol
	}
}

int masshighlight_configtest(ConfigFile *cf, ConfigEntry *ce, int type, int *errs) {
	int errors = 0; // Error count
	int i; // Iterat0r
	int tmp; // f0 checking integer values
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

		if(!strcmp(cep->ce_varname, "action")) {
			muhcfg.got_action = 1;
			if(!cep->ce_vardata || !strlen(cep->ce_vardata)) {
				config_error("%s:%i: %s::%s must be non-empty fam", cep->ce_fileptr->cf_filename, cep->ce_varlinenum, MYCONF, cep->ce_varname);
				errors++; // Increment err0r count fam
				continue;
			}

			// Checkem valid actions
			if(strcmp(cep->ce_vardata, "drop") && strcmp(cep->ce_vardata, "notice") && strcmp(cep->ce_vardata, "gline") && strcmp(cep->ce_vardata, "zline") &&
				strcmp(cep->ce_vardata, "kill") && strcmp(cep->ce_vardata, "tempshun") && strcmp(cep->ce_vardata, "shun") && strcmp(cep->ce_vardata, "viruschan")) {
				config_error("%s:%i: %s::%s must be one of: drop, notice, gline, zline, shun, tempshun, kill, viruschan", cep->ce_fileptr->cf_filename, cep->ce_varlinenum, MYCONF, cep->ce_varname);
				errors++;
			}

			if(!errors)
				muhcfg.action = cep->ce_vardata[0]; // We need this value to be set in posttest
			continue;
		}

		if(!strcmp(cep->ce_varname, "reason")) {
			muhcfg.got_reason = 1;
			if(!cep->ce_vardata || strlen(cep->ce_vardata) < 4) {
				config_error("%s:%i: %s::%s must be at least 4 characters long", cep->ce_fileptr->cf_filename, cep->ce_varlinenum, MYCONF, cep->ce_varname);
				errors++; // Increment err0r count fam
			}
			continue;
		}

		if(!strcmp(cep->ce_varname, "delimiters")) {
			muhcfg.got_delimiters = 1;
			if(!cep->ce_vardata || !strlen(cep->ce_vardata)) {
				config_error("%s:%i: %s::%s must contain at least one character", cep->ce_fileptr->cf_filename, cep->ce_varlinenum, MYCONF, cep->ce_varname);
				errors++; // Increment err0r count fam
			}
			continue;
		}

		if(!strcmp(cep->ce_varname, "duration")) {
			muhcfg.got_duration = 1;
			// Should be a time string imo (7d10s etc, or just 20)
			if(!cep->ce_vardata || config_checkval(cep->ce_vardata, CFG_TIME) <= 0) {
				config_error("%s:%i: %s::%s must be a time string like '7d10m' or simply '20'", cep->ce_fileptr->cf_filename, cep->ce_varlinenum, MYCONF, cep->ce_varname);
				errors++; // Increment err0r count fam
			}
			continue;
		}

		if(!strcmp(cep->ce_varname, "maxnicks")) {
			muhcfg.got_maxnicks = 1;
			// Should be an integer yo
			if(!cep->ce_vardata) {
				config_error("%s:%i: %s::%s must be an integer between 1 and 512 m8", cep->ce_fileptr->cf_filename, cep->ce_varlinenum, MYCONF, cep->ce_varname);
				errors++; // Increment err0r count fam
				continue;
			}
			for(i = 0; cep->ce_vardata[i]; i++) {
				if(!isdigit(cep->ce_vardata[i])) {
					config_error("%s:%i: %s::%s must be an integer between 1 and 512 m8", cep->ce_fileptr->cf_filename, cep->ce_varlinenum, MYCONF, cep->ce_varname);
					errors++; // Increment err0r count fam
					break;
				}
			}
			if(!errors) {
				tmp = atoi(cep->ce_vardata);
				if(tmp <= 0 || tmp > 512) {
					config_error("%s:%i: %s::%s must be an integer between 1 and 512 m8", cep->ce_fileptr->cf_filename, cep->ce_varlinenum, MYCONF, cep->ce_varname);
					errors++; // Increment err0r count fam
				}
			}
			continue;
		}

		if(!strcmp(cep->ce_varname, "snotice")) {
			muhcfg.got_snotice = 1;
			if(!cep->ce_vardata || (strcmp(cep->ce_vardata, "0") && strcmp(cep->ce_vardata, "1"))) {
				config_error("%s:%i: %s::%s must be either 0 or 1 fam", cep->ce_fileptr->cf_filename, cep->ce_varlinenum, MYCONF, cep->ce_varname);
				errors++; // Increment err0r count fam
			}
			continue;
		}

		if(!strcmp(cep->ce_varname, "banident")) {
			muhcfg.got_banident = 1;
			if(!cep->ce_vardata || (strcmp(cep->ce_vardata, "0") && strcmp(cep->ce_vardata, "1"))) {
				config_error("%s:%i: %s::%s must be either 0 or 1 fam", cep->ce_fileptr->cf_filename, cep->ce_varlinenum, MYCONF, cep->ce_varname);
				errors++; // Increment err0r count fam
			}
			continue;
		}

		if(!strcmp(cep->ce_varname, "multiline")) {
			muhcfg.got_multiline = 1;
			if(!cep->ce_vardata || (strcmp(cep->ce_vardata, "0") && strcmp(cep->ce_vardata, "1"))) {
				config_error("%s:%i: %s::%s must be either 0 or 1 fam", cep->ce_fileptr->cf_filename, cep->ce_varlinenum, MYCONF, cep->ce_varname);
				errors++; // Increment err0r count fam
			}
			continue;
		}

		if(!strcmp(cep->ce_varname, "allow_authed")) {
			muhcfg.got_allow_authed = 1;
			if(!cep->ce_vardata || (strcmp(cep->ce_vardata, "0") && strcmp(cep->ce_vardata, "1"))) {
				config_error("%s:%i: %s::%s must be either 0 or 1 fam", cep->ce_fileptr->cf_filename, cep->ce_varlinenum, MYCONF, cep->ce_varname);
				errors++; // Increment err0r count fam
			}
			continue;
		}

		if(!strcmp(cep->ce_varname, "allow_accessmode")) {
			muhcfg.got_allow_accessmode = 1;
			if(!cep->ce_vardata || !strlen(cep->ce_vardata)) {
				config_error("%s:%i: %s::%s must be either non-empty or not specified at all", cep->ce_fileptr->cf_filename, cep->ce_varlinenum, MYCONF, cep->ce_varname);
				errors++; // Increment err0r count fam
				continue;
			}

			if(strlen(cep->ce_vardata) > 1) {
				config_error("%s:%i: %s::%s must be exactly one character (mode) in length", cep->ce_fileptr->cf_filename, cep->ce_varlinenum, MYCONF, cep->ce_varname);
				errors++; // Increment err0r count fam
				continue;
			}

			if(strcmp(cep->ce_vardata, "v") && strcmp(cep->ce_vardata, "h") && strcmp(cep->ce_vardata, "o")
		#ifdef PREFIX_AQ
				&& strcmp(cep->ce_vardata, "a") && strcmp(cep->ce_vardata, "q")
		#endif
			) {
				config_error("%s:%i: %s::%s must be one of: v, h, o"
		#ifdef PREFIX_AQ
				", a, q"
		#endif
				, cep->ce_fileptr->cf_filename, cep->ce_varlinenum, MYCONF, cep->ce_varname);
				errors++;
			}
			continue;
		}

		if(!strcmp(cep->ce_varname, "percent")) {
			muhcfg.got_percent = 1;
			// Should be an integer yo
			if(!cep->ce_vardata) {
				config_error("%s:%i: %s::%s must be an integer between 1 and 100 m8", cep->ce_fileptr->cf_filename, cep->ce_varlinenum, MYCONF, cep->ce_varname);
				errors++; // Increment err0r count fam
				continue;
			}
			for(i = 0; cep->ce_vardata[i]; i++) {
				if(!isdigit(cep->ce_vardata[i])) {
					config_error("%s:%i: %s::%s must be an integer between 1 and 100 m8", cep->ce_fileptr->cf_filename, cep->ce_varlinenum, MYCONF, cep->ce_varname);
					errors++; // Increment err0r count fam
					break;
				}
			}
			if(!errors) {
				tmp = atoi(cep->ce_vardata);
				if(tmp <= 0 || tmp > 100) {
					config_error("%s:%i: %s::%s must be an integer between 1 and 100 m8", cep->ce_fileptr->cf_filename, cep->ce_varlinenum, MYCONF, cep->ce_varname);
					errors++; // Increment err0r count fam
				}
			}
			continue;
		}

		if(!strcmp(cep->ce_varname, "show_opers_origmsg")) {
			muhcfg.got_show_opers_origmsg = 1;
			if(!cep->ce_vardata || (strcmp(cep->ce_vardata, "0") && strcmp(cep->ce_vardata, "1"))) {
				config_error("%s:%i: %s::%s must be either 0 or 1 fam", cep->ce_fileptr->cf_filename, cep->ce_varlinenum, MYCONF, cep->ce_varname);
				errors++; // Increment err0r count fam
			}
			continue;
		}

		// Anything else is unknown to us =]
		config_warn("%s:%i: unknown item %s::%s", cep->ce_fileptr->cf_filename, cep->ce_varlinenum, MYCONF, cep->ce_varname); // So display just a warning
	}

	*errs = errors;
	return errors ? -1 : 1; // Returning 1 means "all good", -1 means we shat our panties
}

int masshighlight_configposttest(int *errs) {
	int errors = 0;
	// Set defaults and display warnings where needed
	if(!muhcfg.got_maxnicks) {
		muhcfg.maxnicks = 5;
		config_warn("[block_masshighlight] Unable to find 'maxnicks' directive, defaulting to: %d", muhcfg.maxnicks);
	}

	if(!muhcfg.got_action) {
		muhcfg.action = 'g'; // Gline em by default lol
		config_warn("[block_masshighlight] Unable to find 'action' directive, defaulting to: gline");
	}

	if(strchr("gzs", muhcfg.action) && !muhcfg.got_duration) { // Duration is only required for *-Lines =]
		muhcfg.duration = 604800; // 7 days yo
		config_warn("[block_masshighlight] Unable to find 'duration' directive, defaulting to: %li seconds", muhcfg.duration);
	}

	if(muhcfg.action != 'd' && !muhcfg.got_reason) // For everything besides drop, we need a reason =]
		safe_strdup(muhcfg.reason, "No mass highlighting allowed"); // So it doesn't fuck with free(), also no need to display config_warn() imo tbh

	if(!muhcfg.got_delimiters)
		safe_strdup(muhcfg.delimiters, "\t ,.-_/\\:;"); // Ditto =]

	if(!muhcfg.got_snotice)
		muhcfg.snotice = 1; // Show 'em, no need to display config_warn() imo tbh

	if(!muhcfg.got_banident) // Lazy mode, even though it's not required for all actions, set it anyways =]
		muhcfg.banident = 1; // Default to ident@host imo tbh

	if(!muhcfg.got_multiline)
		muhcfg.multiline = 0; // Default to single line imo

	if(!muhcfg.got_allow_authed)
		muhcfg.allow_authed = 0; // Default to n0 fam

	if(!muhcfg.got_allow_accessmode)
		muhcfg.allow_accessmode = 0; // None ;]

	if(!muhcfg.got_percent)
		muhcfg.percent = 1; // 1%, max sensitivity

	if(!muhcfg.got_show_opers_origmsg)
		muhcfg.show_opers_origmsg = 1; // Default to showing em

	*errs = errors;
	return errors ? -1 : 1;
}

// "Run" the config (everything should be valid at this point)
int masshighlight_configrun(ConfigFile *cf, ConfigEntry *ce, int type) {
	ConfigEntry *cep; // To store the current variable/value pair etc

	// Since we'll add a top-level block to unrealircd.conf, need to filter on CONFIG_MAIN lmao
	if(type != CONFIG_MAIN)
		return 0; // Returning 0 means idgaf bout dis

	// Check for valid config entries first
	if(!ce || !ce->ce_varname)
		return 0;

	// If it isn't masshighlight, idc
	if(strcmp(ce->ce_varname, MYCONF))
		return 0;

	// Loop dat shyte fam
	for(cep = ce->ce_entries; cep; cep = cep->ce_next) {
		// Do we even have a valid name l0l?
		if(!cep->ce_varname)
			continue; // Next iteration imo tbh

		if(!strcmp(cep->ce_varname, "delimiters")) {
			safe_strdup(muhcfg.delimiters, cep->ce_vardata);
			continue;
		}

		if(!strcmp(cep->ce_varname, "reason")) {
			safe_strdup(muhcfg.reason, cep->ce_vardata);
			continue;
		}

		if(!strcmp(cep->ce_varname, "duration")) {
			muhcfg.duration = config_checkval(cep->ce_vardata, CFG_TIME);
			continue;
		}

		if(!strcmp(cep->ce_varname, "maxnicks")) {
			muhcfg.maxnicks = atoi(cep->ce_vardata);
			continue;
		}

		if(!strcmp(cep->ce_varname, "snotice")) {
			muhcfg.snotice = atoi(cep->ce_vardata);
			continue;
		}

		if(!strcmp(cep->ce_varname, "banident")) {
			muhcfg.banident = atoi(cep->ce_vardata);
			continue;
		}

		if(!strcmp(cep->ce_varname, "multiline")) {
			muhcfg.multiline = atoi(cep->ce_vardata);
			continue;
		}

		if(!strcmp(cep->ce_varname, "allow_authed")) {
			muhcfg.allow_authed = atoi(cep->ce_vardata);
			continue;
		}

		if(!strcmp(cep->ce_varname, "allow_accessmode")) {
			muhcfg.allow_accessmode = 0;
			switch(cep->ce_vardata[0]) {
				case 'v':
					muhcfg.allow_accessmode |= CHFL_VOICE;
					/* fall through */
				case 'h':
					muhcfg.allow_accessmode |= CHFL_HALFOP;
					/* fall through */
				case 'o':
					muhcfg.allow_accessmode |= CHFL_CHANOP;
		#ifdef PREFIX_AQ
					/* fall through */
				case 'a':
					muhcfg.allow_accessmode |= CHFL_CHANADMIN;
					/* fall through */
				case 'q':
					muhcfg.allow_accessmode |= CHFL_CHANOWNER;
		#endif
					/* fall through */
				default:
					break;
			}
			continue;
		}

		if(!strcmp(cep->ce_varname, "percent")) {
			muhcfg.percent = atoi(cep->ce_vardata);
			continue;
		}

		if(!strcmp(cep->ce_varname, "show_opers_origmsg")) {
			muhcfg.show_opers_origmsg = atoi(cep->ce_vardata);
			continue;
		}

	}

	return 1; // We good
}

void masshighlight_md_free(ModData *md) {
	if(md && md->i) // Just in case kek, as this function is required
		md->i = 0;
}

void masshighlight_client_md_free(ModData *md) {
	struct user_hls *hl, *next_hl;
	for(hl = md->ptr; hl; hl=next_hl) { // cleaning the list
		next_hl = hl->next;
		safe_free(hl->chname);
		safe_free(hl);
	}
	md->ptr = NULL;
}

// these functions are used with external messages (unusual)
int masshighlight_get_client_moddata(Client *client, Channel *channel) {
	struct user_hls *hl;
	if(!MyUser(client)) // somehow got called for non-local client
		return 0;
	for(hl = moddata_local_client(client, massHLUserMDI).ptr; hl; hl = hl->next) {
		if(!strcasecmp(channel->chname, hl->chname))
			return hl->count;
	}
	return 0; // none was found
}

void masshighlight_set_client_moddata(Client *client, Channel *channel, int hl_count) {
	struct user_hls *hl, *prev_hl = NULL;
	if(!MyUser(client)) // somehow got called for non-local client
		return;
	for(hl = moddata_local_client(client, massHLUserMDI).ptr; hl; hl = hl->next) {
		if(!strcasecmp(channel->chname, hl->chname)) {
			if(hl_count == 0) { // let's free the memory
				if(!prev_hl) { // it's the first list item
					moddata_local_client(client, massHLUserMDI).ptr = hl->next;
				} else {
					prev_hl->next = hl->next; // update the list link
				}
				safe_free(hl->chname); // release memory
				safe_free(hl);
			} else { // have some data, update the list
				hl->count = hl_count; // set the data
			}
			return; // we're done now
		}
		prev_hl = hl;
	}

	// no data found for this client/user combination
	hl = safe_alloc(sizeof(struct user_hls)); // create a new structure
	hl->next = NULL; // initialize the fields
	hl->count = hl_count;
	hl->chname = strdup(channel->chname);
	if(!prev_hl) { // no data stored for this user yet
		moddata_local_client(client, massHLUserMDI).ptr = hl; // first item
	} else {
		prev_hl->next = hl; // append to list
	}
}

int masshighlight_hook_cansend_chan(Client *client, Channel *channel, Membership *lp, char **text, char **errmsg, int notice) {
	int hl_cur = 0; // Current highlight count yo
	char *p; // For tokenising that shit
	char *werd; // Store current token etc
	char *cleantext; // Let's not modify char *text =]
	Client *acptr; // Temporarily store a mentioned nick to get the membership link
	int blockem; // Need to block the message?
	int clearem; // For clearing the counter
	char joinbuf[CHANNELLEN + 4]; // For viruschan, need to make room for "0,#viruschan" etc ;];]
	char killbuf[256]; // For rewriting the kill message
	char *vcparv[3]; // Arguments for viruschan JOIN
	int bypass_nsauth; // If config var allow_authed is tr00 and user es logged in
	char gotnicks[1024]; // For storing nicks that were mentioned =]
	int hl_new; // Store highlight count for further processing
	size_t werdlen; // Word length ;]
	size_t hl_nickslen; // Amount of chars that are part of highlights
	size_t msglen; // Full message length (both of these are required for calculating percent etc), excludes delimiters

	// Initialise some shit lol
	bypass_nsauth = (muhcfg.allow_authed && IsUser(client) && IsLoggedIn(client));
	blockem = 0;
	clearem = 1;
	p = NULL;
	cleantext = NULL;
	memset(gotnicks, '\0', sizeof(gotnicks));
	hl_new = 0;
	hl_nickslen = 0;
	msglen = 0;

	if(IsNocheckHighlights(channel))
		return HOOK_CONTINUE; // if channelmode is set, allow without further checking

	// Some sanity + privilege checks ;];] (allow_accessmode, U-Lines and opers are exempt from this shit)
	if(text && MyUser(client) && !bypass_nsauth && !is_accessmode_exempt(client, channel) && !IsULine(client) && !IsOper(client)) {
		if(lp) { // the user has joined the channel
			hl_cur = moddata_membership(lp, massHLMDI).i; // Get current count
		} else { // external message
			hl_cur = masshighlight_get_client_moddata(client, channel);
		}

		// In case someone tries some funny business =]
		if(!(cleantext = (char *)StripControlCodes(*text)))
			return HOOK_CONTINUE;

		for(werd = strtoken(&p, cleantext, muhcfg.delimiters); werd; werd = strtoken(&p, NULL, muhcfg.delimiters)) { // Split that shit
			werdlen = strlen(werd);
			msglen += werdlen; // We do not count ze delimiters, otherwise the percents would get strangely low
			if((acptr = find_person(werd, NULL)) && (find_membership_link(acptr->user->channel, channel))) { // User mentioned another user in this channel
				if(!(strstr(gotnicks, acptr->id))) { // Do not count the same nickname appended multiple times to a single message
					ircsnprintf(gotnicks, sizeof(gotnicks), "%s%s,", gotnicks, acptr->id);
					hl_new++; // Got another highlight this round
					hl_nickslen += werdlen; // Also add the nick's length
				}
			}
		}

		if(msglen && 100 * hl_nickslen / msglen > muhcfg.percent) { // Check if we exceed the max allowed percentage
			hl_cur += hl_new; // Set moddata counter to include the ones found this round
			clearem = 0; // And don't clear moddata
			if(hl_cur > muhcfg.maxnicks) // Check if we also exceed max allowed nicks
				blockem = 1; // Flip flag to blockin' dat shit
		}

		if(!muhcfg.multiline && !clearem) // If single line mode and found a nick
			clearem = 1; // Need to clear that shit always lol

		if(lp) {
			moddata_membership(lp, massHLMDI).i = (clearem ? 0 : hl_cur); // Actually set the counter =]
		} else {
			masshighlight_set_client_moddata(client, channel, (clearem ? 0 : hl_cur));
		}

		if(blockem) { // Need to bl0ck?
			switch(muhcfg.action) {
				case 'd': // Drop silently
					if(muhcfg.snotice)
						sendto_snomask_global(SNO_EYES, "*** [block_masshighlight] Detected highlight spam in %s by %s, dropping silently", channel->chname, client->name);
					break;

				case 'n': // Drop, but send notice
					if(muhcfg.snotice)
						sendto_snomask_global(SNO_EYES, "*** [block_masshighlight] Detected highlight spam in %s by %s, dropping with a notice", channel->chname, client->name);

					// Not doing sendnotice() here because we send to *client* but the channel in the notice is actually the intended *target*, which means they'll get a notice in the proper window ;];;]];
					sendto_one(client, NULL, ":%s NOTICE %s :%s", me.name, channel->chname, muhcfg.reason);
					break;

				case 'k': // Kill em all
					if(muhcfg.snotice)
						sendto_snomask_global(SNO_EYES, "*** [block_masshighlight] Detected highlight spam in %s by %s, killing 'em", channel->chname, client->name);

					// Notify user of being killed
					sendto_prefix_one(client, &me, NULL, ":%s KILL %s :%s", me.name, client->name, muhcfg.reason);

					// Oper notice
					ircsnprintf(killbuf, sizeof(killbuf), "Killed: %s", muhcfg.reason);
					sendto_snomask_global(SNO_KILLS, "*** Received KILL message for %s (%s@%s): %s", client->name, client->user->username, GetHost(client), muhcfg.reason);
					exit_client(client, NULL, killbuf);
					break;

				case 't': // Tempshun kek
					if(muhcfg.snotice)
						sendto_snomask_global(SNO_EYES, "*** [block_masshighlight] Detected highlight spam in %s by %s, setting tempshun", channel->chname, client->name);

					sendto_snomask(SNO_TKL, "Temporary shun added on user %s (%s@%s) [%s]", client->name,
						(client->user ? client->user->username : "unknown"),
						(client->user ? client->user->realhost : GetIP(client)),
						muhcfg.reason);
					SetShunned(client); // Ez m0de
					break;

				case 's': // Shun, ...
				case 'g': // ...G-Line and ...
				case 'z': // ...Z-Line share the same internal function ;];]
					if(muhcfg.snotice) {
						if(muhcfg.action == 's')
							sendto_snomask_global(SNO_EYES, "*** [block_masshighlight] Detected highlight spam in %s by %s, shunning 'em", channel->chname, client->name);
						else
							sendto_snomask_global(SNO_EYES, "*** [block_masshighlight] Detected highlight spam in %s by %s, setting %c-Line", channel->chname, client->name, toupper(muhcfg.action));
					}
					doXLine(muhcfg.action, client);
					break;

				case 'v': // Viruschan lol
					if(muhcfg.snotice)
						sendto_snomask_global(SNO_EYES, "*** [block_masshighlight] Detected highlight spam in %s by %s, following viruschan protocol", channel->chname, client->name);

					// This bit is ripped from tkl.c with some logic changes to suit what we need to do =]
					snprintf(joinbuf, sizeof(joinbuf), "0,%s", SPAMFILTER_VIRUSCHAN);
					vcparv[0] = client->name;
					vcparv[1] = joinbuf;
					vcparv[2] = NULL;

					spamf_ugly_vchanoverride = 1;
					do_cmd(client, NULL, "JOIN", 2, vcparv);
					spamf_ugly_vchanoverride = 0;
					sendnotice(client, "You are now restricted to talking in %s: %s", SPAMFILTER_VIRUSCHAN, muhcfg.reason);
					SetVirus(client); // Ayy rekt
					break;

				default:
					break;
			}
			if(muhcfg.show_opers_origmsg)
				sendto_snomask_global(SNO_EYES, "*** [block_masshighlight] The message was: %s", *text);
			*text = NULL; // NULL makes Unreal drop the message entirely afterwards ;3
			// Can't return HOOK_DENY here cuz Unreal will abort() in that case :D
		}
	}

	return HOOK_CONTINUE;
}
