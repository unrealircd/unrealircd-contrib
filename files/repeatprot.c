/* Copyright (C) All Rights Reserved
** Written by Gottem <support@gottem.nl>
** Website: https://gottem.nl/unreal
** License: https://gottem.nl/unreal/license
*/

/*** <<<MODULE MANAGER START>>>
module {
	documentation "https://gottem.nl/unreal/man/repeatprot";
	troubleshooting "In case of problems, check the FAQ at https://gottem.nl/unreal/halp or e-mail me at support@gottem.nl";
	min-unrealircd-version "5.*";
	//max-unrealircd-version "5.*";
	post-install-text {
		"The module is installed, now all you need to do is add a 'loadmodule' line to your config file:";
		"loadmodule \"third/repeatprot\";";
		"Then /rehash the IRCd.";
		"For usage information, refer to the module's documentation found at: https://gottem.nl/unreal/man/repeatprot";
	}
}
*** <<<MODULE MANAGER END>>>
*/

// One include for all cross-platform compatibility thangs
#include "unrealircd.h"

#define OVR_INVITE "INVITE"
#define OVR_NOTICE "NOTICE"
#define OVR_PRIVMSG "PRIVMSG"

// Muh macros/typedefs
#define CheckAPIError(apistr, apiobj) \
	do { \
		if(!(apiobj)) { \
			config_error("A critical error occurred on %s for %s: %s", (apistr), MOD_HEADER.name, ModuleGetErrorStr(modinfo->handle)); \
			return MOD_FAILED; \
		} \
	} while(0)

typedef struct t_exception muhExcept;
typedef struct t_msg muhMessage;
struct t_exception {
	char *mask;
	muhExcept *next;
};
struct t_msg {
	char *last;
	char *prev;
	int count;
	time_t ts;
};

// Quality fowod declarations
int repeatprot_configtest(ConfigFile *cf, ConfigEntry *ce, int type, int *errs);
int repeatprot_configposttest(int *errs);
int repeatprot_configrun(ConfigFile *cf, ConfigEntry *ce, int type);
void setcfg(void);
void dropMessage(Client *client, char *cmd, char *msg);
void blockIt(Client *client);
void doKill(Client *client);
void doXLine(char flag, Client *client);
CMD_OVERRIDE_FUNC(repeatprot_override);
void repeatprot_free(ModData *md);

ModDataInfo *repeatprotMDI; // To store every user's last message with their client pointer ;3
muhExcept *exceptionList = NULL; // Stores exempted masks

// Deez defaults
struct {
	int repeatThreshold; // After how many repeated message the action kicks in
	char tklAction; // Action to take when threshold has been reached -- b = block, k = kill, z = gzline, g = gline
	int tklTime; // How long to gzline for, in seconds -- doesn't apply to block/kill actions obv fam
	char *banmsg; // Quit/blocked message =]
	int showBlocked; // Whether to show banmsg for the block action
	int trigNotice; // Ditto for NOTICE
	int trigPrivmsg; // Ditto^ditto for PRIVMSG
	int trigCTCP; // D I T T O
	int trigInvite; // D D D I I I T T T T T T O O O
	int trigCount; // Need at least one lol
	time_t trigTimespan;
} muhcfg;

// Dat dere module header
ModuleHeader MOD_HEADER = {
	"third/repeatprot", // Module name
	"2.0.1", // Version
	"G(Z)-Line/kill users (or block their messages) who spam through CTCP, INVITE, NOTICE and/or PRIVMSG", // Description
	"Gottem", // Author
	"unrealircd-5", // Modversion
};

// Configuration testing-related hewks go in testing phase obv
// This function is entirely optional
MOD_TEST() {
	// We have our own config block so we need to checkem config obv m9
	memset(&muhcfg, 0, sizeof(muhcfg));
	HookAdd(modinfo->handle, HOOKTYPE_CONFIGTEST, 0, repeatprot_configtest);
	HookAdd(modinfo->handle, HOOKTYPE_CONFIGPOSTTEST, 0, repeatprot_configposttest);
	return MOD_SUCCESS;
}

// Initialisation routine (register hooks, commands and modes or create structs etc)
MOD_INIT() {
	ModDataInfo mreq; // Request that shit
	memset(&mreq, 0, sizeof(mreq));
	mreq.type = MODDATATYPE_LOCAL_CLIENT;
	mreq.name = "repeatprot_lastmessage"; // Name it
	mreq.free = repeatprot_free; // Function to free 'em
	repeatprotMDI = ModDataAdd(modinfo->handle, mreq);
	CheckAPIError("ModDataAdd(repeatprot_lastmessage)", repeatprotMDI);

	MARK_AS_GLOBAL_MODULE(modinfo);
	setcfg();

	HookAdd(modinfo->handle, HOOKTYPE_CONFIGRUN, 0, repeatprot_configrun);
	return MOD_SUCCESS;
}

// Actually load the module here (also command overrides as they may not exist in MOD_INIT yet)
MOD_LOAD() {
	CheckAPIError("CommandOverrideAddEx(INVITE)", CommandOverrideAddEx(modinfo->handle, OVR_INVITE, -99, repeatprot_override));
	CheckAPIError("CommandOverrideAddEx(NOTICE)", CommandOverrideAddEx(modinfo->handle, OVR_NOTICE, -99, repeatprot_override));
	CheckAPIError("CommandOverrideAddEx(PRIVMSG)", CommandOverrideAddEx(modinfo->handle, OVR_PRIVMSG, -99, repeatprot_override));
	return MOD_SUCCESS; // We good
}

// Called on unload/rehash obv
MOD_UNLOAD() {
	if(exceptionList) {
		// This shit is a bit convoluted to prevent memory issues obv famalmalmalmlmalm
		muhExcept *exEntry;
		while((exEntry = exceptionList) != NULL) {
			exceptionList = exceptionList->next;
			safe_free(exEntry->mask);
			safe_free(exEntry);
		}
		exceptionList = NULL;
	}
	safe_free(muhcfg.banmsg);
	return MOD_SUCCESS; // We good
}

int repeatprot_configtest(ConfigFile *cf, ConfigEntry *ce, int type, int *errs) {
	int errors = 0; // Error count
	int i; // Iterat0r
	ConfigEntry *cep, *cep2; // To store the current variable/value pair etc, nested

	// Since we'll add a top-level block to unrealircd.conf, need to filter on CONFIG_MAIN lmao
	if(type != CONFIG_MAIN)
		return 0; // Returning 0 means idgaf bout dis

	// Check for valid config entries first
	if(!ce || !ce->ce_varname)
		return 0;

	// If it isn't repeatprot, idc
	if(strcmp(ce->ce_varname, "repeatprot"))
		return 0;

		// Loop dat shyte fam
	for(cep = ce->ce_entries; cep; cep = cep->ce_next) {
		// Do we even have a valid name l0l?
		if(!cep->ce_varname) {
			config_error("%s:%i: blank repeatprot item", cep->ce_fileptr->cf_filename, cep->ce_varlinenum); // Rep0t error
			errors++; // Increment err0r count fam
			continue; // Next iteration imo tbh
		}

		// Check for optionals first =]
		if(!strcmp(cep->ce_varname, "action")) {
			if(!cep->ce_vardata || (strcmp(cep->ce_vardata, "block") && strcmp(cep->ce_vardata, "kill") && strcmp(cep->ce_vardata, "gzline") && strcmp(cep->ce_vardata, "gline"))) {
				config_error("%s:%i: repeatprot::action must be either 'block', 'kill', 'gline' or 'gzline'", cep->ce_fileptr->cf_filename, cep->ce_varlinenum);
				errors++; // Increment err0r count fam
				continue;
			}
		}

		if(!strcmp(cep->ce_varname, "banmsg")) {
			if(!cep->ce_vardata || !strlen(cep->ce_vardata)) {
				config_error("%s:%i: repeatprot::banmsg must be non-empty fam", cep->ce_fileptr->cf_filename, cep->ce_varlinenum);
				errors++; // Increment err0r count fam
				continue;
			}
		}

		if(!strcmp(cep->ce_varname, "showblocked")) {
			if(!cep->ce_vardata || (strcmp(cep->ce_vardata, "0") && strcmp(cep->ce_vardata, "1"))) {
				config_error("%s:%i: repeatprot::showblocked must be either 0 or 1 fam", cep->ce_fileptr->cf_filename, cep->ce_varlinenum);
				errors++; // Increment err0r count fam
				continue;
			}
		}

		if(!strcmp(cep->ce_varname, "tkltime")) {
			// Should be a time string imo (7d10s etc, or just 20)
			if(!cep->ce_vardata || config_checkval(cep->ce_vardata, CFG_TIME) <= 0) {
				config_error("%s:%i: repeatprot::tkltime must be a time string like '7d10m' or simply '20'", cep->ce_fileptr->cf_filename, cep->ce_varlinenum);
				errors++; // Increment err0r count fam
				continue;
			}
		}

		if(!strcmp(cep->ce_varname, "threshold")) {
			// Should be an integer yo
			if(!cep->ce_vardata) {
				config_error("%s:%i: repeatprot::threshold must be an integer of 0 or larger m8", cep->ce_fileptr->cf_filename, cep->ce_varlinenum);
				errors++; // Increment err0r count fam
				continue;
			}
			for(i = 0; cep->ce_vardata[i]; i++) {
				if(!isdigit(cep->ce_vardata[i])) {
					config_error("%s:%i: repeatprot::threshold must be an integer of 0 or larger m8", cep->ce_fileptr->cf_filename, cep->ce_varlinenum);
					errors++; // Increment err0r count fam
					break;
				}
			}
			continue;
		}

		if(!strcmp(cep->ce_varname, "timespan")) {
			// Should be a time string imo (7d10s etc, or just 20)
			if(!cep->ce_vardata || config_checkval(cep->ce_vardata, CFG_TIME) <= 0) {
				config_error("%s:%i: repeatprot::timespan must be a time string like '7d10m' or simply '20'", cep->ce_fileptr->cf_filename, cep->ce_varlinenum);
				errors++; // Increment err0r count fam
			}
			continue;
		}

		// Now check for the repeatprot::triggers bl0qq
		if(!strcmp(cep->ce_varname, "triggers")) {
			// Loop 'em
			for(cep2 = cep->ce_entries; cep2; cep2 = cep2->ce_next) {
				if(!cep2->ce_varname) {
					config_error("%s:%i: blank repeatprot trigger", cep2->ce_fileptr->cf_filename, cep2->ce_varlinenum); // Rep0t error
					errors++; // Increment err0r count fam
					continue; // Next iteration imo tbh
				}

				if(strcmp(cep2->ce_varname, "notice") && strcmp(cep2->ce_varname, "privmsg") && strcmp(cep2->ce_varname, "ctcp") && strcmp(cep2->ce_varname, "invite")) {
					config_error("%s:%i: invalid repeatprot trigger", cep2->ce_fileptr->cf_filename, cep2->ce_varlinenum); // Rep0t error
					errors++; // Increment err0r count fam
					continue; // Next iteration imo tbh
				}
				muhcfg.trigCount++; // Seems to be a valid trigger yo
			}
			continue;
		}

		// Also dem exceptions
		if(!strcmp(cep->ce_varname, "exceptions")) {
			// Loop 'em
			for(cep2 = cep->ce_entries; cep2; cep2 = cep2->ce_next) {
				if(!cep2->ce_varname) {
					config_error("%s:%i: blank exception mask", cep2->ce_fileptr->cf_filename, cep2->ce_varlinenum); // Rep0t error
					errors++; // Increment err0r count fam
					continue; // Next iteration imo tbh
				}

				if(!match_simple("*!*@*", cep2->ce_varname)) {
					config_error("%s:%i: invalid nick!user@host exception mask", cep2->ce_fileptr->cf_filename, cep2->ce_varlinenum); // Rep0t error
					errors++; // Increment err0r count fam
					continue; // Next iteration imo tbh
				}
			}
			continue;
		}
	}

	*errs = errors;
	// Returning 1 means "all good", -1 means we shat our panties
	return errors ? -1 : 1;
}

// Post test, check for missing shit here
int repeatprot_configposttest(int *errs) {
	// Let's croak when there are no items in our block, even though the module was loaded
	if(!muhcfg.trigCount)
		config_warn("Module %s was loaded but no repeatprot::triggers were specified", MOD_HEADER.name);
	return 1;
}

// "Run" the config (everything should be valid at this point)
int repeatprot_configrun(ConfigFile *cf, ConfigEntry *ce, int type) {
	ConfigEntry *cep, *cep2; // To store the current variable/value pair etc, nested
	muhExcept *last = NULL; // Initialise to NULL so the loop requires minimal l0gic
	muhExcept **exEntry = &exceptionList; // Hecks so the ->next chain stays intact

	// Since we'll add a top-level block to unrealircd.conf, need to filter on CONFIG_MAIN lmao
	if(type != CONFIG_MAIN)
		return 0; // Returning 0 means idgaf bout dis

	// Check for valid config entries first
	if(!ce || !ce->ce_varname)
		return 0;

	// If it isn't repeatprot, idc
	if(strcmp(ce->ce_varname, "repeatprot"))
		return 0;

		// Loop dat shyte fam
	for(cep = ce->ce_entries; cep; cep = cep->ce_next) {
		// Do we even have a valid name l0l?
		if(!cep->ce_varname)
			continue; // Next iteration imo tbh

		// Check for optionals first =]
		if(!strcmp(cep->ce_varname, "action")) {
			switch(cep->ce_vardata[1]) {
				case 'l':
					muhcfg.tklAction = (cep->ce_vardata[0] == 'b' ? 'b' : 'G');
					break;
				case 'i':
					muhcfg.tklAction = 'k';
					break;
				case 'z':
					muhcfg.tklAction = 'Z';
					break;
				default:
					break;
			}
			continue;
		}

		if(!strcmp(cep->ce_varname, "banmsg")) {
			safe_strdup(muhcfg.banmsg, cep->ce_vardata);
			continue;
		}

		if(!strcmp(cep->ce_varname, "showblocked")) {
			muhcfg.showBlocked = atoi(cep->ce_vardata);
			continue;
		}

		if(!strcmp(cep->ce_varname, "tkltime")) {
			muhcfg.tklTime = config_checkval(cep->ce_vardata, CFG_TIME);
			continue;
		}

		if(!strcmp(cep->ce_varname, "threshold")) {
			muhcfg.repeatThreshold = atoi(cep->ce_vardata);
			continue;
		}

		if(!strcmp(cep->ce_varname, "timespan")) {
			muhcfg.trigTimespan = config_checkval(cep->ce_vardata, CFG_TIME);
			continue;
		}

		// Now check for the repeatprot::triggers bl0qq
		if(!strcmp(cep->ce_varname, "triggers")) {
			// Loop 'em
			for(cep2 = cep->ce_entries; cep2; cep2 = cep2->ce_next) {
				if(!cep2->ce_varname)
					continue; // Next iteration imo tbh

				else if(!strcmp(cep2->ce_varname, "notice"))
					muhcfg.trigNotice = 1;

				else if(!strcmp(cep2->ce_varname, "privmsg"))
					muhcfg.trigPrivmsg = 1;

				else if(!strcmp(cep2->ce_varname, "ctcp"))
					muhcfg.trigCTCP = 1;

				else if(!strcmp(cep2->ce_varname, "invite"))
					muhcfg.trigInvite = 1;
			}
			continue;
		}

		if(!strcmp(cep->ce_varname, "exceptions")) {
			// Loop 'em
			for(cep2 = cep->ce_entries; cep2; cep2 = cep2->ce_next) {
				if(!cep2->ce_varname)
					continue; // Next iteration imo tbh

				// Get size
				size_t masklen = sizeof(char) * (strlen(cep2->ce_varname) + 1);

				// Allocate mem0ry for the current entry
				*exEntry = safe_alloc(sizeof(muhExcept));

				// Allocate/initialise shit here
				(*exEntry)->mask = safe_alloc(masklen);

				// Copy that shit fam
				strlcpy((*exEntry)->mask, cep2->ce_varname, masklen);

				// Premium linked list fam
				if(last)
					last->next = *exEntry;

				last = *exEntry;
				exEntry = &(*exEntry)->next;
			}
			continue;
		}
	}

	return 1; // We good
}

void setcfg(void) {
	// Reset em defaults
	muhcfg.repeatThreshold = 3;
	muhcfg.tklAction = 'G';
	muhcfg.tklTime = 60;
	safe_strdup(muhcfg.banmsg, "Nice spam m8");
}

void dropMessage(Client *client, char *cmd, char *msg) {
	int kill = 0, xline = 0; // Whether to kill or G/GZ-Line
	char snomsg[BUFSIZE]; // Message to send to 0pers =]
	ircsnprintf(snomsg, sizeof(snomsg), "*** [repeatprot] \037\002%s\002\037 flood from \002%s\002", cmd, client->name); // Initialise that shit with some basic inf0

	switch(muhcfg.tklAction) { // Checkem action to apply
		case 'b': // Block 'em
			ircsnprintf(snomsg + strlen(snomsg), sizeof(snomsg), " [action: \037block\037, body: %s]", msg);
			blockIt(client);
			break;
		case 'k': // Kill pls
			ircsnprintf(snomsg + strlen(snomsg), sizeof(snomsg), " [action: \037kill\037, body: %s]", msg);
			kill = 1; // Delay the actual kill for a bit
			break;
		case 'G': // *-Lines
		case 'Z': // y0
			ircsnprintf(snomsg + strlen(snomsg), sizeof(snomsg), " [action: \037%s-Line\037, body: %s]", (muhcfg.tklAction == 'Z' ? "GZ" : "G"), msg);
			xline = 1; // Delay the actual *-Line for a bit
			break;
		default: // Shouldn't happen kek (like, ever)
			ircsnprintf(snomsg + strlen(snomsg), sizeof(snomsg), " [UNKNOWN ACTION, body: %s]", msg);
			break;
	}

	// Send snomasks before actually rip'ing the user (muh chronology pls)
	sendto_snomask_global(SNO_EYES, "%s", snomsg);

	// Checkem delayed actions
	if(kill)
		doKill(client);
	else if(xline)
		doXLine(muhcfg.tklAction, client);
}

void blockIt(Client *client) {
	if(muhcfg.showBlocked)
		sendnotice(client, "*** Message blocked (%s)", muhcfg.banmsg);
}

void doKill(Client *client) {
	char msg[BUFSIZE];
	snprintf(msg, sizeof(msg), "Killed (%s)", muhcfg.banmsg);
	sendto_snomask_global(SNO_KILLS, "*** [repeatprot] Received KILL message for %s!%s@%s", client->name, client->user->username, client->user->realhost);
	exit_client(client, NULL, msg);
}

void doXLine(char flag, Client *client) {
	char tkltype[2];
	// Double check for client existing, cuz inb4segfault
	if(client) {
		char setTime[100], expTime[100];
		ircsnprintf(setTime, sizeof(setTime), "%li", TStime());
		ircsnprintf(expTime, sizeof(expTime), "%li", TStime() + muhcfg.tklTime);
		tkltype[0] = flag;
		tkltype[1] = '\0';

		// Build TKL args
		char *tkllayer[9] = {
			me.name,
			"+",
			tkltype,
			client->user->username,
			(flag == 'Z' ? GetIP(client) : client->user->realhost),
			me.name,
			expTime,
			setTime,
			muhcfg.banmsg
		};
		cmd_tkl(&me, NULL, 9, tkllayer); // Ban 'em
	}
}

// Now for the actual override
CMD_OVERRIDE_FUNC(repeatprot_override) {
	// Gets args: CommandOverride *ovr, Client *client, MessageTag *recv_mtags, int parc, char *parv[]
	char *cmd; // One override function for multiple commands ftw
	int invite, noticed, privmsg; // "Booleans"
	int exempt; // Is exempted?
	int ctcp, ctcpreply; // CTCP?
	int i;
	char msg[BUFSIZE]; // Store comparison part for this command/message
	char msgtmp[BUFSIZE]; // To rebuild that shit and store it in msg
	size_t msglen;
	char *tok;
	int breakem;
	char *sp; // Garbage pointer =]
	char *plaintext; // To get a plaintext version of msg (i.e. without bold/underline/etc shit and col0urs)
	char *werd; // Store each w0rd from the message
	Client *acptr; // Store /message target
	muhMessage *message; // Current/new message struct
	muhExcept *exEntry; // For iteration yo

	// Lest we massively shit ourselves =]
	if(!client || BadPtr(parv[1]) || BadPtr(parv[2]) || BadPtr(parv[parc - 1])) {
		CallCommandOverride(ovr, client, recv_mtags, parc, parv); // Run original function yo
		return;
	}

	// Preemptively allow non-local users, non-users, U-Lines and 0pers
	if(!MyUser(client) || IsULine(client) || IsOper(client)) {
		CallCommandOverride(ovr, client, recv_mtags, parc, parv); // Run original function yo
		return;
	}

	exempt = 0;
	cmd = ovr->command->cmd;
	ctcp = (parv[2][0] == 1 && parv[parc - 1][0] == 1);
	noticed = (!strcmp(cmd, "NOTICE") && !ctcp);
	privmsg = (!strcmp(cmd, "PRIVMSG") && !ctcp);
	ctcpreply = (!strcmp(cmd, "NOTICE") && ctcp);
	invite = (!strcmp(cmd, "INVITE"));

	for(exEntry = exceptionList; exEntry; exEntry = exEntry->next) {
		if(match_simple(exEntry->mask, make_nick_user_host(client->name, client->user->username, client->user->realhost)) ||
			match_simple(exEntry->mask, make_nick_user_host(client->name, client->user->username, client->ip))) {
			exempt = 1;
			break;
		}
	}

	// Check for enabled triggers, also exclude CTCP _replies_ always ;3
	if(exempt || parv[1][0] == '#' || ctcpreply ||
		(noticed && !muhcfg.trigNotice) ||
		(privmsg && !muhcfg.trigPrivmsg) ||
		(ctcp && !muhcfg.trigCTCP) ||
		(invite && !muhcfg.trigInvite)) {
		CallCommandOverride(ovr, client, recv_mtags, parc, parv); // Run original function yo
		return;
	}

	// Allow messages TO self and U-Lines ;3
	acptr = find_person(parv[1], NULL); // Attempt to find message target
	if(!acptr || acptr == client || IsULine(acptr)) {
		CallCommandOverride(ovr, client, recv_mtags, parc, parv); // Run original function yo
		return;
	}

	if(ctcp || noticed || privmsg || invite) // Need to skip the "target" fam ('PRIVMSG <target> :<msg>' and 'INVITE <target> #chan')
		i = 2;
	else
		i = 1; // Just a default for anything else

	// (Re)build full message
	memset(msg, '\0', sizeof(msg)); // Premium nullbytes
	msglen = 0;
	for(breakem = 0; !breakem && i < parc && !BadPtr(parv[i]); i++) {
		strlcpy(msgtmp, parv[i], sizeof(msgtmp));
		tok = msgtmp;
		while(!breakem && (werd = strtoken(&sp, tok, " "))) { // Required for shit like PRIVMSG ;]
			if(msglen + strlen(werd) + 2 > sizeof(msg)) // Out of space, just truncate that shit (can't really happen anyways :>)
				breakem = 1;
			ircsnprintf(msg + msglen, sizeof(msg) - msglen, "%s%s", (msglen > 0 ? " " : ""), werd);
			if(tok != NULL)
				tok = NULL;
		}
	}

	// Case-insansativatay imo tbh
	plaintext = (char *)StripControlCodes(msg);
	for(i = 0; plaintext[i]; i++) {
		if(plaintext[i] > 64)
			plaintext[i] = tolower(plaintext[i]);
	}

	message = moddata_local_client(client, repeatprotMDI).ptr; // Get last message info
	// We ain't gottem struct yet
	if(!message) {
		message = safe_alloc(sizeof(muhMessage)); // Alloc8 a fresh strukk
		message->count = 1;
		message->prev = NULL; // Required
		message->ts = 0;
	}

	// Or we d0
	else {
		// Check if we need to expire the counter first kek
		// trigTimespan == 0 if not specified in the config, meaning never expire
		if(message->ts > 0 && muhcfg.trigTimespan > 0) {
			if(TStime() - message->ts >= muhcfg.trigTimespan)
				message->count = 1;
		}

		// Nigga may be alternating messages
		if(!strcmp(message->last, plaintext) || (message->prev && !strcmp(message->prev, plaintext)))
			message->count++;
		else {
			// In case we just blocked it and this isn't a known message, reset the counter to allow it through
			if(message->count >= (muhcfg.repeatThreshold + 1))
				message->count = 1;
		}

		safe_strdup(message->prev, message->last);
		safe_free(message->last);
	}

	message->ts = TStime();
	safe_strdup(message->last, plaintext);
	moddata_local_client(client, repeatprotMDI).ptr = message; // Store that shit within Unreal

	if(message->count >= muhcfg.repeatThreshold) {
		// Let's not reset the counter here, causing it to keep getting blocked ;3
		// No need to free the message object either as repeatprot_free flushes it when the client quits anyways =]
		dropMessage(client, (ctcp ? "CTCP" : cmd), plaintext); // R E K T
		return;
	}
	CallCommandOverride(ovr, client, recv_mtags, parc, parv); // Run original function yo
}

void repeatprot_free(ModData *md) {
	if(md->ptr) {
		muhMessage *message = md->ptr;
		message->count = 0;
		safe_free(message->last);
		safe_free(message->prev);
		safe_free(message);
		md->ptr = NULL;
	}
}
