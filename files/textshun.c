/* Copyright (C) All Rights Reserved
** Written by Gottem <support@gottem.nl>
** Website: https://gottem.nl/unreal
** License: https://gottem.nl/unreal/license
*/

/*** <<<MODULE MANAGER START>>>
module {
	documentation "https://gottem.nl/unreal/man/textshun";
	troubleshooting "In case of problems, check the FAQ at https://gottem.nl/unreal/halp or e-mail me at support@gottem.nl";
	min-unrealircd-version "5.*";
	//max-unrealircd-version "5.*";
	post-install-text {
		"The module is installed, now all you need to do is add a 'loadmodule' line to your config file:";
		"loadmodule \"third/textshun\";";
		"Then /rehash the IRCd.";
		"For usage information, refer to the module's documentation found at: https://gottem.nl/unreal/man/textshun";
	}
}
*** <<<MODULE MANAGER END>>>
*/

// One include for all cross-platform compatibility thangs
#include "unrealircd.h"

// Since v5.0.5 some hooks now include a SendType
#define BACKPORT_HOOK_SENDTYPE (UNREAL_VERSION_GENERATION == 5 && UNREAL_VERSION_MAJOR == 0 && UNREAL_VERSION_MINOR < 5)

// Command strings
#define MSG_TEXTSHUN "TEXTSHUN"
#define MSG_TEXTSHUN_SHORT "TS"
#define MSG_TEXTSHUN_ALT "TLINE"

// Hewktypez
// Big hecks go here
typedef struct t_tline TLine;
struct t_tline {
	char *nickrgx;
	char *bodyrgx;
	time_t set;
	time_t expire;
	char *raisin;
	char *setby;
	TLine *next;
};

// Dem macros yo
CMD_FUNC(textshun); // Register command function

#define CheckAPIError(apistr, apiobj) \
	do { \
		if(!(apiobj)) { \
			config_error("A critical error occurred on %s for %s: %s", (apistr), MOD_HEADER.name, ModuleGetErrorStr(modinfo->handle)); \
			return MOD_FAILED; \
		} \
	} while(0)

// Quality fowod declarations
static void dumpit(Client *client, char **p);
EVENT(textshun_event); // For expiring that shit fam
void textshun_moddata_free(ModData *md);
void check_tlines(void);
void add_tline(TLine *newtl);
void del_tline(TLine *muhtl);
TLine *get_tlines(void);
TLine *find_tline(char *nickrgx, char *bodyrgx);
TLine *match_tline(Client *client, char *text);
int textshun_hook_serverconnect(Client *client);
int _check_cansend(Client *client, char **text);

#if BACKPORT_HOOK_SENDTYPE
	int textshun_hook_cansend_chan(Client *client, Channel *channel, Membership *lp, char **text, char **errmsg, int notice);
	int textshun_hook_cansend_user(Client *client, Client *to, char **text, char **errmsg, int notice);
#else
	int textshun_hook_cansend_chan(Client *client, Channel *channel, Membership *lp, char **text, char **errmsg, SendType sendtype);
	int textshun_hook_cansend_user(Client *client, Client *to, char **text, char **errmsg, SendType sendtype);
#endif

ModDataInfo *textshunMDI; // To store the T-Lines with &me lol (hack so we don't have to use a .db file or some shit)
Command *textshunCmd, *textshunCmdShort, *textshunCmdAlt; // Pointers to the commands we're gonna add
int TLC; // A counter for T-Lines so we can change the moddata back to NULL

// Help string in case someone does just /TEXTSHUN
static char *muhhalp[] = {
	/* Special characters:
	** \002 = bold -- \x02
	** \037 = underlined -- \x1F
	*/
	"*** \002Help on /TEXTSHUN\002 ***",
	"Enables opers to drop messages based on nick and body regexes (T-Lines).",
	"It only supports (PCRE) regexes because regular wildcards seem",
	"ineffective to me. ;] Also, you can't have spaces so you",
	"should simply use \\s. Also supports the aliases TS and TLINE.",
	"It's all case-insensitive by default. It also tells you if your",
	"regex is wrong (and what). The lines are network-wide.",
	"Servers, U-Lines and opers are exempt for obvious reasons.",
	"The nick regex is matched against both n!u@realhost and n!u@vhost masks.",
	" ",
	"Syntax:",
	"    \002/TEXTSHUN\002 \037ADD/DEL\037 \037nickrgx\037 \037bodyrgx\037 [\037expiration\037] \037reason\037",
	" ",
	"Examples:",
	"    \002/tline add guest.+ h[o0]+m[o0]+ 0 nope\002",
	"    \002/textshun add guest.+ h[o0]+m[o0]+ nope\002",
	"    \002/ts del guest.+ h[o0]+m[o0]+\002",
	"        Adds/deletes the same T-Line, with no expiration",
	"    \002/tline add guest.+ h[o0]+m[o0]+ 3600 ain't gonna happen\002",
	"    \002/tline add guest.+ h[o0]+m[o0]+ 1h ain't gonna happen\002",
	"        Add a T-Line that expires in an hour",
	"    \002/tline\002",
	"        Show all T-Lines",
	NULL
};

// Dat dere module header
ModuleHeader MOD_HEADER = {
	"third/textshun", // Module name
	"2.0.1", // Version
	"Drop messages based on nick and body", // Description
	"Gottem", // Author
	"unrealircd-5", // Modversion
};

// Initialisation routine (register hooks, commands and modes or create structs etc)
MOD_INIT() {
	TLine *TLineList, *tEntry; // To initialise the TLC counter imo tbh fam

	// Dem commands fam
	CheckAPIError("CommandAdd(TEXTSHUN)", CommandAdd(modinfo->handle, MSG_TEXTSHUN, textshun, MAXPARA, CMD_SERVER | CMD_USER));
	CheckAPIError("CommandAdd(TS)", CommandAdd(modinfo->handle, MSG_TEXTSHUN_SHORT, textshun, MAXPARA, CMD_SERVER | CMD_USER));
	CheckAPIError("CommandAdd(TLINE)", CommandAdd(modinfo->handle, MSG_TEXTSHUN_ALT, textshun, MAXPARA, CMD_SERVER | CMD_USER));

	// Run event every 10 seconds, indefinitely and without any additional data (void *NULL etc)
	CheckAPIError("EventAdd(textshun_event)", EventAdd(modinfo->handle, "textshun_event", textshun_event, NULL, 10000, 0));

	TLC = 0; // Start with 0 obv lmao
	if(!(textshunMDI = findmoddata_byname("textshun_list", MODDATATYPE_CLIENT))) { // Attempt to find active moddata (like in case of a rehash)
		ModDataInfo mreq; // No moddata, let's request that shit
		memset(&mreq, 0, sizeof(mreq)); // Set 'em lol
		mreq.type = MODDATATYPE_LOCAL_VARIABLE; // Apply to servers only (CLIENT actually includes users but we'll disregard that =])
		mreq.name = "textshun_list"; // Name it
		mreq.free = textshun_moddata_free; // Function to free 'em
		mreq.serialize = NULL;
		mreq.unserialize = NULL;
		mreq.sync = 0;
		textshunMDI = ModDataAdd(modinfo->handle, mreq); // Add 'em yo
		CheckAPIError("ModDataAdd(textshun_list)", textshunMDI);
	}
	else { // We did get moddata
		if((TLineList = get_tlines())) { // So load 'em
			for(tEntry = TLineList; tEntry; tEntry = tEntry->next) // and iter8 m8
				TLC++; // Ayyy premium countur
		}
	}

	MARK_AS_GLOBAL_MODULE(modinfo);

	// Add muh hooks with (mostly) high prio lol
	HookAdd(modinfo->handle, HOOKTYPE_SERVER_CONNECT, 0, textshun_hook_serverconnect);
	HookAdd(modinfo->handle, HOOKTYPE_CAN_SEND_TO_CHANNEL, -100, textshun_hook_cansend_chan);
	HookAdd(modinfo->handle, HOOKTYPE_CAN_SEND_TO_USER, -100, textshun_hook_cansend_user);

	return MOD_SUCCESS;
}

// Actually load the module here (also command overrides as they may not exist in MOD_INIT yet)
MOD_LOAD() {
	return MOD_SUCCESS; // We good
}

// Called on unload/rehash obv
MOD_UNLOAD() {
	// Not clearing the moddata structs here so we can re-use them easily ;];]
	return MOD_SUCCESS; // We good
}

// Dump a NULL-terminated array of strings to the user (taken from DarkFire IRCd)
static void dumpit(Client *client, char **p) {
	if(IsServer(client)) // Bail out early and silently if it's a server =]
		return;

	// Using sendto_one() instead of sendnumericfmt() because the latter strips indentation and stuff ;]
	for(; *p != NULL; p++)
		sendto_one(client, NULL, ":%s %03d %s :%s", me.name, RPL_TEXT, client->name, *p);

	// Let user take 8 seconds to read it
	client->local->since += 8;
}

EVENT(textshun_event) {
	check_tlines(); // Checkem and expirem
}

// Probably never called but it's a required function
// The free shit here normally only happens when the client attached to the moddata quits (afaik), but that's us =]
void textshun_moddata_free(ModData *md) {
	if(md->ptr) { // r u insaiyan?
		TLine *tEntry = md->ptr; // Cast em
		safe_free(tEntry->nickrgx); // Gotta
		safe_free(tEntry->bodyrgx); // free
		safe_free(tEntry->raisin); // 'em
		safe_free(tEntry->setby); // all
		safe_free(tEntry); // y0
		tEntry->set = 0; // Just in case lol
		tEntry->expire = 0L; // ditt0
		md->ptr = NULL; // d-d-ditt0
	}
}

// Check for expiring T-Lines
void check_tlines(void) {
	TLine *TLineList, *head, *last, **tEntry;
	char gmt[128]; // For a pretty timestamp instead of UNIX time lol
	if(!(TLineList = get_tlines())) // Ayyy no T-Lines known
		return;

	tEntry = &TLineList; // Hecks so the ->next chain stays intact
	head = TLineList;
	while(*tEntry) { // Loop while we have entries obv
		if((*tEntry)->expire > 0 && TStime() > ((*tEntry)->set + (*tEntry)->expire)) { // Do we need to expire it?
			last = *tEntry; // Get the entry pointur
			*tEntry = last->next; // Set the iterat0r to the next one

			if(last == head) { // If it's the first entry, need to take special precautions ;]
				moddata_local_variable(textshunMDI).ptr = *tEntry; // Cuz shit rips if we don't do dis
				head = *tEntry; // Move head up
			}

			// Get pretty timestamp =]
			*gmt = '\0';
			short_date(last->set, gmt);

			// Send expiration notice to all _local_ opers lol (every server checks expirations itself newaysz y0)
			sendto_snomask(SNO_TKL, "*** Expiring T-Line set by %s at %s GMT for nick /%s/ and body /%s/ [reason: %s]", last->setby, gmt, last->nickrgx, last->bodyrgx, last->raisin);

			safe_free(last->nickrgx); // Gotta
			safe_free(last->bodyrgx); // free
			safe_free(last->raisin); // em
			safe_free(last->setby); // all
			safe_free(last); // lol
			TLC--;
		}
		else {
			tEntry = &(*tEntry)->next; // No need for expiration, go to the next one
		}
	}
	if(TLC <= 0) // Cuz shit rips if we don't do dis
		moddata_local_variable(textshunMDI).ptr = NULL;
}

// Add new T-Line obv fam
void add_tline(TLine *newtl) {
	TLine *TLineList, *tEntry; // Head + iter8or imo tbh
	TLC++; // Always increment count
	if(!(TLineList = get_tlines())) { // If TLineList is NULL...
		TLineList = newtl; // ...simply have it point to the newly alloc8ed entry
		moddata_local_variable(textshunMDI).ptr = TLineList; // And st0re em
		return;
	}
	for(tEntry = TLineList; tEntry && tEntry->next; tEntry = tEntry->next) { }
	tEntry->next = newtl; // Append lol
}

// Delete em fam
void del_tline(TLine *muhtl) {
	TLine *TLineList, *last, **tEntry;
	if(!(TLineList = get_tlines())) // Ayyy no T-Lines known
		return;

	tEntry = &TLineList; // Hecks so the ->next chain stays intact
	if(*tEntry == muhtl) { // If it's the first entry, need to take special precautions ;]
		last = *tEntry; // Get the entry pointur
		*tEntry = last->next; // Set the iterat0r to the next one
		safe_free(last->nickrgx); // Gotta
		safe_free(last->bodyrgx); // free
		safe_free(last->raisin); // em
		safe_free(last->setby); // all
		safe_free(last); // lol
		moddata_local_variable(textshunMDI).ptr = *tEntry; // Cuz shit rips if we don't do dis
		TLC--;
		return;
	}

	while(*tEntry) { // Loop while we have entries obv
		if(*tEntry == muhtl) { // Do we need to delete em?
			last = *tEntry; // Get the entry pointur
			*tEntry = last->next; // Set the iterat0r to the next one
			safe_free(last->nickrgx); // Gotta
			safe_free(last->bodyrgx); // free
			safe_free(last->raisin); // em
			safe_free(last->setby); // all
			safe_free(last); // lol
			TLC--;
			break;
		}
		else {
			tEntry = &(*tEntry)->next; // No need, go to the next one
		}
	}
	if(TLC <= 0) // Cuz shit rips if we don't do dis
		moddata_local_variable(textshunMDI).ptr = NULL;
}

// Get (head of) the T-Line list
TLine *get_tlines(void) {
	TLine *TLineList = moddata_local_variable(textshunMDI).ptr; // Get mod data
	// Sanity check lol
	if(TLineList && TLineList->nickrgx)
		return TLineList;
	return NULL;
}

// Find a specific T-Line (based on nick and body regex lol)
TLine *find_tline(char *nickrgx, char *bodyrgx) {
	TLine *TLineList, *tEntry; // Head and iter8or fam
	if((TLineList = get_tlines())) { // Check if the list even has entries kek
		for(tEntry = TLineList; tEntry; tEntry = tEntry->next) { // Iter8 em
			// The regex match itself (Match *) is case-insensitive anyways, so let's do strcasecmp() here =]
			if(!strcasecmp(tEntry->nickrgx, nickrgx) && !strcasecmp(tEntry->bodyrgx, bodyrgx))
				return tEntry;
		}
	}
	return NULL; // Not found m8
}

// For matching a user and string to a T-Line
TLine *match_tline(Client *client, char *text) {
	char mask[NICKLEN + USERLEN + HOSTLEN + 3], vmask[NICKLEN + USERLEN + HOSTLEN + 3];
	Match *exprNick, *exprBody; // For creating the actual match struct pointer thingy
	int nickmatch, bodmatch; // Did we get een match?
	TLine *TLineList, *tEntry; // Head and iter8or fam

	snprintf(mask, sizeof(mask), "%s", make_nick_user_host(client->name, client->user->username, client->user->realhost)); // Get nick!user@host with the real hostnaem
	if(client->user->virthost)
		snprintf(vmask, sizeof(vmask), "%s", make_nick_user_host(client->name, client->user->username, client->user->virthost)); // Get nick!user@host with the real hostnaem
	else
		*vmask = '\0';

	if(!text || !*mask) // r u insaiyan lol?
		return NULL;

	if((TLineList = get_tlines())) { // Check if the list even has entries kek
		for(tEntry = TLineList; tEntry; tEntry = tEntry->next) {
			nickmatch = bodmatch = 0;
			exprNick = unreal_create_match(MATCH_PCRE_REGEX, tEntry->nickrgx, NULL); // Create match struct for nikk regex
			exprBody = unreal_create_match(MATCH_PCRE_REGEX, tEntry->bodyrgx, NULL); // Also for body
			if(!exprNick || !exprBody) // If either failed, gtfo
				continue;

			if(*vmask) // If virthost exists
				nickmatch = (unreal_match(exprNick, mask) || unreal_match(exprNick, vmask)); // Check if either matches obv
			else // If it doesn't (no umode +x, no NickServ vhost, etc)
				nickmatch = unreal_match(exprNick, mask); // Matchem real host only
			bodmatch = unreal_match(exprBody, text);

			unreal_delete_match(exprNick); // Cleanup
			unreal_delete_match(exprBody); // lol
			if(nickmatch && bodmatch)
				return tEntry;
		}
	}
	return NULL; // rip
}

// Internal function called by the pre*msg hooks ;];]
int _check_cansend(Client *client, char **text) {
	TLine *tEntry; // Iter8or
	char *body;

	if(!MyConnect(client)) // No need to check if it's not our client =]
		return HOOK_CONTINUE;

	// Strip all markup shit (bold, italikk etc) and colours
	if(!(body = (char *)StripControlCodes(*text)))
		return HOOK_CONTINUE;

	if(!IsServer(client) && !IsMe(client) && !IsULine(client) && !IsOper(client) && (tEntry = match_tline(client, body))) { // Servers, U-Lines and opers are exempt for obv raisins
		// If match, send notices to all other opers =]
		sendto_snomask_global(SNO_TKL, "*** T-Line for nick /%s/ and body /%s/ matched by %s [body: %s]", tEntry->nickrgx, tEntry->bodyrgx, client->name, body);
		*text = NULL;
		// Can't return HOOK_DENY here cuz Unreal will abort() in that case :D
	}
	return HOOK_CONTINUE;
}

// Server connect hewk familia
int textshun_hook_serverconnect(Client *client) {
	// Sync T-Lines fam
	TLine *TLineList, *tEntry; // Head and iter8or ;];]
	if((TLineList = get_tlines())) { // Gettem list
		for(tEntry = TLineList; tEntry; tEntry = tEntry->next) {
			if(!tEntry || !tEntry->nickrgx) // Sanity check imo ;]
				continue;
			// Syntax for servers is a bit different (namely the setby arg and the : before reason (makes the entire string after be considered one arg ;];])
			sendto_one(client, NULL, ":%s TLINE ADD %s %s %ld %ld %s :%s", me.id, tEntry->nickrgx, tEntry->bodyrgx, tEntry->set, tEntry->expire, tEntry->setby, tEntry->raisin);
		}
	}
	return HOOK_CONTINUE;
}

// Pre message hewks lol
#if BACKPORT_HOOK_SENDTYPE
	int textshun_hook_cansend_chan(Client *client, Channel *channel, Membership *lp, char **text, char **errmsg, int notice) {
		return _check_cansend(client, text);
	}

	int textshun_hook_cansend_user(Client *client, Client *to, char **text, char **errmsg, int notice) {
		return _check_cansend(client, text);
	}

#else
	int textshun_hook_cansend_chan(Client *client, Channel *channel, Membership *lp, char **text, char **errmsg, SendType sendtype) {
		if(sendtype != SEND_TYPE_PRIVMSG && sendtype != SEND_TYPE_NOTICE)
			return HOOK_CONTINUE;
		return _check_cansend(client, text);
	}

	int textshun_hook_cansend_user(Client *client, Client *to, char **text, char **errmsg, SendType sendtype) {
		if(sendtype != SEND_TYPE_PRIVMSG && sendtype != SEND_TYPE_NOTICE)
			return HOOK_CONTINUE;
		return _check_cansend(client, text);
	}
#endif

// Function for /TLINE etc
CMD_FUNC(textshun) {
	// Gets args: Client *client, MessageTag *recv_mtags, int parc, char *parv[]
	Match *exprNick, *exprBody; // For verifying the regexes
	char *regexerr;
	TLine *TLineList, *newtl, *tEntry; // Quality struct pointers
	char *nickrgx, *bodyrgx, *exptmp, *setby; // Muh args
	char raisin[BUFSIZE]; // Reasons may or may not be pretty long
	char gmt[128], gmt2[128]; // For a pretty timestamp instead of UNIX time lol
	char cur, prev, prev2; // For checking time strings
	long setat, expire; // After how many seconds the T-Line should expire
	int i, rindex, del, nickrgx_ok, bodyrgx_ok; // Iterat0rs and "booleans" =]

	if((!IsServer(client) && !IsULine(client) && !IsOper(client)) || !ValidatePermissionsForPath("textshun", client, NULL, NULL, NULL)) {
		sendnumeric(client, ERR_NOPRIVILEGES); // Check ur privilege fam
		return;
	}

	if(IsServer(client) && parc < 8)
		return;

	// If no args given (or we got /tline list)
	if(BadPtr(parv[1]) || !strcasecmp(parv[1], "list")) {
		if(IsServer(client)) // No need to list shit for servers =]
			return;
		if(!(TLineList = get_tlines())) // Attempt to get list
			sendnotice(client, "*** No T-Lines found");
		else {
			for(tEntry = TLineList; tEntry; tEntry = tEntry->next) {
				*gmt2 = '\0';
				short_date(tEntry->set, gmt2);
				if(tEntry->expire == 0) // Let's show "permanent" for permanent T-Lines, n0? =]
					sendnotice(client, "*** Permanent T-Line set by %s at %s GMT for nick /%s/ and body /%s/ [reason: %s]", tEntry->setby, gmt2, tEntry->nickrgx, tEntry->bodyrgx, tEntry->raisin);
				else {
					// Get pretty timestamp for expiring lines =]
					*gmt = '\0';
					short_date(tEntry->set + tEntry->expire, gmt);
					sendnotice(client, "*** T-Line set by %s at %s GMT for nick /%s/ and body /%s/, expiring at %s GMT [reason: %s]", tEntry->setby, gmt2, tEntry->nickrgx, tEntry->bodyrgx, gmt, tEntry->raisin);
				}
			}
		}
		return;
	}

	// Need at least 4 args lol
	if(!strcasecmp(parv[1], "help") || !strcasecmp(parv[1], "halp")) {
		dumpit(client, muhhalp); // Return help string instead
		return;
	}

	del = (!strcasecmp(parv[1], "del") ? 1 : 0); // Are we deleting?
	if((!del && parc < 5) || (del && parc < 4)) { // Delete doesn't require the expire and reason fields
		sendnumeric(client, ERR_NEEDMOREPARAMS, MSG_TEXTSHUN); // Need m0ar lol
		return;
	}
	if(strcasecmp(parv[1], "add") && strcasecmp(parv[1], "del")) { // If first arg is neither add nor del, fuck off
		sendnotice(client, "*** First arg must be either ADD or DEL");
		return;
	}

	// Initialise a bunch of shit
	memset(raisin, '\0', sizeof(raisin));
	exptmp = NULL;
	expire = DEFAULT_BANTIME;
	setat = TStime();
	rindex = nickrgx_ok = bodyrgx_ok = 0;
	nickrgx = parv[2];
	bodyrgx = parv[3];
	exptmp = parv[4];
	setby = client->name;

	exprNick = unreal_create_match(MATCH_PCRE_REGEX, nickrgx, &regexerr); // Attempt to create match struct
	if(!exprNick && regexerr && !IsServer(client)) { // Servers don't need to get a notice for invalid shit
		sendnotice(client, "*** Invalid nick regex /%s/ [err: %s]", nickrgx, regexerr); // Report regex error for nikk
		regexerr = NULL; // Nullify just to be shur
	}

	exprBody = unreal_create_match(MATCH_PCRE_REGEX, bodyrgx, &regexerr);
	if(!exprBody && regexerr && !IsServer(client))
		sendnotice(client, "*** Invalid body regex /%s/ [err: %s]", bodyrgx, regexerr); // For body too

	// We good?
	if(exprNick) {
		nickrgx_ok = 1;
		unreal_delete_match(exprNick);
	}
	if(exprBody) {
		bodyrgx_ok = 1;
		unreal_delete_match(exprBody);
	}

	if(!nickrgx_ok || !bodyrgx_ok) // Both should be sane obv
		return;

	// Most shit is silent for servers
	if(IsServer(client)) {
		tEntry = find_tline(nickrgx, bodyrgx); // Attempt to find existing T-Line
		if(!del && tEntry) // Adding a T-Line but it already exists
			return;
		else if(del && !tEntry) // Deleting but doesn't exist
			return;

		strlcpy(raisin, parv[7], sizeof(raisin)); // Copy the reason field imo tbh
		setat = atol(parv[4]); // Extra arg yo
		expire = atol(parv[5]); // Set expiration
		setby = parv[6]; // Extra arg yo
		if(setat <= 0) // Some error occured lol
			return;
	}

	// Command came from a user
	else {
		tEntry = find_tline(nickrgx, bodyrgx); // Attempt to find existing T-Line
		if(!del && tEntry) { // Adding a T-Line but it already exists
			sendnotice(client, "*** T-Line for nick /%s/ and body /%s/ already exists", nickrgx, bodyrgx);
			return;
		}
		else if(del && !tEntry) { // Deleting but doesn't exist
			sendnotice(client, "*** T-Line for nick /%s/ and body /%s/ doesn't exist", nickrgx, bodyrgx);
			return;
		}

		// If adding, check for expiration and reason fields
		if(!del) {
			// Let's check for a time string (3600, 1h, 2w3d, etc)
			for(i = 0; exptmp[i] != 0; i++) {
				cur = exptmp[i];
				if(!isdigit(cur)) { // No digit, check for the 'h' in '1h' etc
					prev = (i >= 1 ? exptmp[i - 1] : 0);
					prev2 = (i >= 2 ? exptmp[i - 2] : 0);

					if((prev && prev2 && isdigit(prev2) && prev == 'm' && cur == 'o') || (prev && isdigit(prev) && strchr("smhdw", cur))) // Check for allowed combos
						continue;

					exptmp = NULL; // Fuck off
					rindex = 4; // Reason index for parv[] is 4
					break; // Only one mismatch is enough
				}
			}

			if(exptmp) { // If the for() loop didn't pass over the innermost if(), expire field is sane
				expire = config_checkval(exptmp, CFG_TIME); // So get a long from the (possible) time string
				rindex = 5; // And set reason index for parv[] to 5
			}

			if(!rindex || BadPtr(parv[rindex]) || !strlen(parv[rindex])) { // If rindex is 0 it means the arg is missing
				sendnotice(client, "*** The reason field is required");
				return;
			}

			// Now start from rindex and copy dem remaining args
			for(i = rindex; parv[i] != NULL; i++) {
				if(i == rindex)
					strlcpy(raisin, parv[i], sizeof(raisin));
				else {
					strlcat(raisin, " ", sizeof(raisin));
					strlcat(raisin, parv[i], sizeof(raisin));
				}
			}
		}
	}

	// For both servers and users ;]
	if(!del) {
		// Allocate/initialise mem0ry for the new entry
		newtl = safe_alloc(sizeof(TLine));
		newtl->nickrgx = strdup(nickrgx);
		newtl->bodyrgx = strdup(bodyrgx);
		newtl->set = setat;
		newtl->expire = expire;
		newtl->raisin = strdup(raisin);
		newtl->setby = strdup(setby);
		tEntry = newtl;
		add_tline(newtl); // Add em
	}

	// Propagate the T-Line to other local servers fam (excluding the direction it came from ;])
	sendto_server(client, 0, 0, NULL, ":%s TLINE %s %s %s %ld %ld %s :%s", me.id, (del ? "DEL" : "ADD"), nickrgx, bodyrgx, tEntry->set, tEntry->expire, setby, tEntry->raisin);

	// Also send snomask notices to all local opers =]
	// Make pretty set timestamp first tho
	*gmt2 = '\0';
	short_date(tEntry->set, gmt2);

	if(tEntry->expire == 0) { // Permanent lol
		if(IsServer(client)) // Show "set at" during sync phase ;]
			sendto_snomask(SNO_TKL, "*** Permanent T-Line %sed by %s at %s GMT for nick /%s/ and body /%s/ [reason: %s]", (del ? "delet" : "add"), setby, gmt2, nickrgx, bodyrgx, tEntry->raisin);
		else
			sendto_snomask(SNO_TKL, "*** Permanent T-Line %sed by %s for nick /%s/ and body /%s/ [reason: %s]", (del ? "delet" : "add"), setby, nickrgx, bodyrgx, tEntry->raisin);
	}
	else {
		// Make pretty expiration timestamp if not a permanent T-Line
		*gmt = '\0';
		short_date(tEntry->set + tEntry->expire, gmt);
		if(IsServer(client)) // Show "set at" during sync phase ;]
			sendto_snomask(SNO_TKL, "*** T-Line %sed by %s at %s GMT for nick /%s/ and body /%s/, expiring at %s GMT [reason: %s]", (del ? "delet" : "add"), setby, gmt2, nickrgx, bodyrgx, gmt, tEntry->raisin);
		else
			sendto_snomask(SNO_TKL, "*** T-Line %sed by %s for nick /%s/ and body /%s/, expiring at %s GMT [reason: %s]", (del ? "delet" : "add"), setby, nickrgx, bodyrgx, gmt, tEntry->raisin);
	}

	// Delete em famamlamlamlmal
	if(del)
		del_tline(tEntry);
}
