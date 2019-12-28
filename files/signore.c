/* Copyright (C) All Rights Reserved
** Written by Gottem <support@gottem.nl>
** Website: https://gottem.nl/unreal
** License: https://gottem.nl/unreal/license
*/

/*** <<<MODULE MANAGER START>>>
module {
	documentation "https://gottem.nl/unreal/man/signore";
	troubleshooting "In case of problems, check the FAQ at https://gottem.nl/unreal/halp or e-mail me at support@gottem.nl";
	min-unrealircd-version "5.*";
	//max-unrealircd-version "5.*";
	post-install-text {
		"The module is installed, now all you need to do is add a 'loadmodule' line to your config file:";
		"loadmodule \"third/signore\";";
		"Then /rehash the IRCd.";
		"For usage information, refer to the module's documentation found at: https://gottem.nl/unreal/man/signore";
	}
}
*** <<<MODULE MANAGER END>>>
*/

// One include for all cross-platform compatibility thangs
#include "unrealircd.h"

// Command strings
#define MSG_SIGNORE "SIGNORE"
#define MSG_SIGNORE_ALT "ILINE"

// Big hecks go here
typedef struct t_iline ILine;
struct t_iline {
	char *mask;
	char *other;
	time_t set;
	time_t expire;
	char *raisin;
	char *setby;
	ILine *next;
};

// Dem macros yo
CMD_FUNC(signore); // Register command function

#define CheckAPIError(apistr, apiobj) \
	do { \
		if(!(apiobj)) { \
			config_error("A critical error occurred on %s for %s: %s", (apistr), MOD_HEADER.name, ModuleGetErrorStr(modinfo->handle)); \
			return MOD_FAILED; \
		} \
	} while(0)

// Quality fowod declarations
static void dumpit(Client *client, char **p);
EVENT(signore_event); // For expiring that shit fam
void signore_moddata_free(ModData *md);
void check_ilines(void);
void add_iline(ILine *newsig);
void del_iline(ILine *muhsig);
void free_all_ilines(ILine *ILineList);
ILine *get_ilines(void);
ILine *find_iline(char *mask, char *other);
ILine *match_iline(Client *client, Client *acptr);
int signore_hook_serverconnect(Client *client);
int signore_hook_cansend_user(Client *client, Client *to, char **text, char **errmsg, int notice);

// Muh globals
ModDataInfo *signoreMDI; // To store the I-Lines with &me lol (hack so we don't have to use a .db file or some shit)
int ILineCount; // A counter for I-Lines so we can change the moddata back to NULL

// Help string in case someone does just /SIGNORE
static char *muhhalp[] = {
	/* Special characters:
	** \002 = bold -- \x02
	** \037 = underlined -- \x1F
	*/
	"*** \002Help on /SIGNORE\002 ***",
	"Enables opers to make two users \"ignore\" each other, meaning they can't send private messages",
	"to each other nor will their channel messages be visible.",
	"The masks should be of the format \037ident@host\037 (as is usual with *-Lines).",
	"The wildcards * and ? are also supported.",
	" ",
	"Syntax:",
	"    \002/SIGNORE\002 [-]\037mask\037 \037othermask\037 [\037expiration\037] \037reason\037",
	" ",
	"Examples:",
	"    \002/signore someone*@* else*@* 0 nope\002",
	"    \002/iline -someone*@* else*@*\002",
	"        Adds/deletes the same I-Line, with no expiration",
	"    \002/signore someone*@* else*@* 3600 ain't gonna happen\002",
	"    \002/iline someone*@* else*@* 1h ain't gonna happen\002",
	"        Add an I-Line that expires in an hour",
	"    \002/signore someone else 1h kek\002",
	"        Add an I-Line for the users \037someone\037 and \037else\037 (resolves their \002id@ho\002 masks if they're online)",
	"    \002/signore\002",
	"        Show all I-Lines",
	NULL
};

// Dat dere module header
ModuleHeader MOD_HEADER = {
	"third/signore", // Module name
	"2.0", // Version
	"Implements an I-Line for adding server-side ignores", // Description
	"Gottem", // Author
	"unrealircd-5", // Modversion
};

// Initialisation routine (register hooks, commands and modes or create structs etc)
MOD_INIT() {
	ILine *ILineList, *sigEntry; // To initialise the ILineCount counter imo tbh fam

	// Dem commands fam
	CheckAPIError("CommandAdd(SIGNORE)", CommandAdd(modinfo->handle, MSG_SIGNORE, signore, MAXPARA, CMD_SERVER | CMD_USER));
	CheckAPIError("CommandAdd(ILINE)", CommandAdd(modinfo->handle, MSG_SIGNORE_ALT, signore, MAXPARA, CMD_SERVER | CMD_USER));

	// Run event every 10 seconds, indefinitely and without any additional data (void *NULL etc)
	CheckAPIError("EventAdd(signore_event)", EventAdd(modinfo->handle, "signore_event", signore_event, NULL, 10000, 0));

	ILineCount = 0; // Start with 0 obv lmao
	if(!(signoreMDI = findmoddata_byname("signore_list", MODDATATYPE_LOCAL_VARIABLE))) { // Attempt to find active moddata (like in case of a rehash)
		ModDataInfo mreq; // No moddata, let's request that shit
		memset(&mreq, 0, sizeof(mreq)); // Set 'em lol
		mreq.type = MODDATATYPE_LOCAL_VARIABLE;
		mreq.name = "signore_list"; // Name it
		mreq.free = signore_moddata_free; // Function to free 'em
		mreq.serialize = NULL;
		mreq.unserialize = NULL;
		mreq.sync = 0;
		signoreMDI = ModDataAdd(modinfo->handle, mreq); // Add 'em yo
		CheckAPIError("ModDataAdd(signore_list)", signoreMDI);
	}
	else { // We did get moddata
		if((ILineList = get_ilines())) { // So load 'em
			for(sigEntry = ILineList; sigEntry; sigEntry = sigEntry->next) // and iter8 m8
				ILineCount++; // Ayyy premium countur
		}
	}

	MARK_AS_GLOBAL_MODULE(modinfo);

	// Add muh hooks with varying priorities lol
	HookAdd(modinfo->handle, HOOKTYPE_SERVER_CONNECT, 0, signore_hook_serverconnect); // Order n0 matur
	HookAdd(modinfo->handle, HOOKTYPE_CAN_SEND_TO_USER, -100, signore_hook_cansend_user); // Run with high pri0rity
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

EVENT(signore_event) {
	check_ilines(); // Checkem and expirem
}

// Probably never called but it's a required function
// The free shit here normally only happens when the client attached to the moddata quits (afaik), but that's us =]
void signore_moddata_free(ModData *md) {
	if(md->ptr) { // r u insaiyan?
		free_all_ilines(md->ptr);
		md->ptr = NULL; // Prolly a good idea too =]
	}
}

// Check for expiring I-Lines
void check_ilines(void) {
	ILine *ILineList, *head, *last, **sigEntry;
	char gmt[128]; // For a pretty timestamp instead of UNIX time lol
	if(!(ILineList = get_ilines())) // Ayyy no I-Lines known
		return;

	sigEntry = &ILineList; // Hecks so the ->next chain stays intact
	head = ILineList;
	while(*sigEntry) { // Loop while we have entries obv
		if((*sigEntry)->expire > 0 && TStime() > ((*sigEntry)->set + (*sigEntry)->expire)) { // Do we need to expire it?
			last = *sigEntry; // Get the entry pointur
			*sigEntry = last->next; // Set the iterat0r to the next one

			if(last == head) { // If it's the first entry, need to take special precautions ;]
				moddata_local_variable(signoreMDI).ptr = *sigEntry; // Cuz shit rips if we don't do dis
				head = *sigEntry; // Move head up
			}

			// Get pretty timestamp =]
			*gmt = '\0';
			short_date(last->set, gmt);

			// Send expiration notice to all _local_ opers lol (every server checks expirations itself newaysz y0)
			sendto_snomask(SNO_TKL, "*** Expiring I-Line by %s (set at %s GMT) for %s and %s [reason: %s]", last->setby, gmt, last->mask, last->other, last->raisin);

			safe_free(last->mask); // Gotta
			safe_free(last->other); // Gotta
			safe_free(last->raisin); // 'em
			safe_free(last->setby); // all
			safe_free(last); // lol
			ILineCount--;
		}
		else {
			sigEntry = &(*sigEntry)->next; // No need for expiration, go to the next one
		}
	}
	if(ILineCount <= 0) // Cuz shit rips if we don't do dis
		moddata_local_variable(signoreMDI).ptr = NULL;
}

// Add new I-Line obv fam
void add_iline(ILine *newsig) {
	ILine *ILineList, *sigEntry; // Head + iter8or imo tbh
	ILineCount++; // Always increment count
	if(!(ILineList = get_ilines())) { // If ILineList is NULL...
		ILineList = newsig; // ...simply have it point to the newly alloc8ed entry
		moddata_local_variable(signoreMDI).ptr = ILineList; // And st0re em
		return;
	}
	for(sigEntry = ILineList; sigEntry && sigEntry->next; sigEntry = sigEntry->next) { }
	sigEntry->next = newsig; // Append lol
}

// Delete em fam
void del_iline(ILine *muhsig) {
	ILine *ILineList, *last, **sigEntry;
	if(!(ILineList = get_ilines())) // Ayyy no I-Lines known
		return;

	sigEntry = &ILineList; // Hecks so the ->next chain stays intact
	if(*sigEntry == muhsig) { // If it's the first entry, need to take special precautions ;]
		last = *sigEntry; // Get the entry pointur
		*sigEntry = last->next; // Set the iterat0r to the next one
		safe_free(last->mask); // Gotta
		safe_free(last->other); // free
		safe_free(last->raisin); // 'em
		safe_free(last->setby); // all
		safe_free(last); // lol
		moddata_local_variable(signoreMDI).ptr = *sigEntry; // Cuz shit rips if we don't do dis
		ILineCount--;
		return;
	}

	while(*sigEntry) { // Loop while we have entries obv
		if(*sigEntry == muhsig) { // Do we need to delete em?
			last = *sigEntry; // Get the entry pointur
			*sigEntry = last->next; // Set the iterat0r to the next one
			safe_free(last->mask); // Gotta
			safe_free(last->other); // free
			safe_free(last->raisin); // 'em
			safe_free(last->setby); // all
			safe_free(last); // lol
			ILineCount--;
			break;
		}
		else {
			sigEntry = &(*sigEntry)->next; // No need, go to the next one
		}
	}
	if(ILineCount <= 0) // Cuz shit rips if we don't do dis
		moddata_local_variable(signoreMDI).ptr = NULL;
}

void free_all_ilines(ILine *ILineList) {
	ILine *last, **sigEntry;
	if(!ILineList)
		return;

	sigEntry = &ILineList; // Hecks so the ->next chain stays intact
	while(*sigEntry) { // Loop while we have entries obv
		last = *sigEntry; // Get the entry pointur
		*sigEntry = last->next; // Set the iterat0r to the next one
		safe_free(last->mask); // Gotta
		safe_free(last->other); // free
		safe_free(last->raisin); // 'em
		safe_free(last->setby); // all
		safe_free(last); // l0l
		ILineCount--;
	}
}

// Get (head of) the I-Line list
ILine *get_ilines(void) {
	ILine *ILineList = moddata_local_variable(signoreMDI).ptr; // Get mod data
	// Sanity check lol
	if(ILineList && ILineList->mask && ILineList->other)
		return ILineList;
	return NULL;
}

// Find a specific I-Line by masks
ILine *find_iline(char *mask, char *other) {
	ILine *ILineList, *sigEntry; // Head and iter8or fam
	if((ILineList = get_ilines())) { // Check if the list even has entries kek
		for(sigEntry = ILineList; sigEntry; sigEntry = sigEntry->next) { // Iter8 em
			// Checkem both directions lol
			if((match_simple(sigEntry->mask, mask) && match_simple(sigEntry->other, other)) || (match_simple(sigEntry->mask, other) && match_simple(sigEntry->other, mask)))
				return sigEntry;
		}
	}
	return NULL; // Not found m8
}

// For matching users to an I-Line
ILine *match_iline(Client *client, Client *acptr) {
	// Never matches if either side is either: not a person, U-Line or oper ;]
	if(!IsUser(client) || IsOper(client) || IsULine(client) || !IsUser(acptr) || IsOper(acptr) || IsULine(acptr))
		return NULL;

	char *mptr, mask[USERLEN + HOSTLEN + 2], other[USERLEN + HOSTLEN + 2]; // Masks lol

	mptr = make_user_host(client->user->username, client->user->realhost); // Get user@host with the real hostnaem
	ircsnprintf(mask, sizeof(mask), "%s", mptr);

	mptr = make_user_host(acptr->user->username, acptr->user->realhost); // Also other user's
	ircsnprintf(other, sizeof(other), "%s", mptr);

	if(!mask || !other || !*mask || !*other) // r u insaiyan lol?
		return NULL;

	return find_iline(mask, other); // Returns NULL if n0, gg ez
}

// Server connect hewk familia
int signore_hook_serverconnect(Client *client) {
	// Sync I-Lines fam
	ILine *ILineList, *sigEntry; // Head and iter8or ;];]
	if((ILineList = get_ilines())) { // Gettem list
		for(sigEntry = ILineList; sigEntry; sigEntry = sigEntry->next) {
			if(!sigEntry || !sigEntry->mask) // Sanity check imo ;]
				continue;
			// Syntax for servers is a bit different (namely the setby arg and the : before reason (makes the entire string after be considered one arg ;];])
			sendto_one(client, NULL, ":%s %s ADD %s %s %ld %ld %s :%s", me.id, MSG_SIGNORE, sigEntry->mask, sigEntry->other, sigEntry->set, sigEntry->expire, sigEntry->setby, sigEntry->raisin);
		}
	}
	return HOOK_CONTINUE;
}

// Pre message hewk lol
int signore_hook_cansend_user(Client *client, Client *to, char **text, char **errmsg, int notice) {
	static char errbuf[256];

	// Let's exclude some shit lol
	if(!MyUser(client) || !IsUser(to) || IsOper(client) || IsOper(to) || IsULine(client) || IsULine(to))
		return HOOK_CONTINUE;

	if(match_iline(client, to)) { // Attempt to find match pls
		ircsnprintf(errbuf, sizeof(errbuf), "[signore] I-Line is in effect between you and \002%s\002", to->name);
		*errmsg = errbuf;
		return HOOK_DENY;
	}

	return HOOK_CONTINUE;
}

// Function for /SIGNORE etc
CMD_FUNC(signore) {
	// Gets args: Client *client, MessageTag *recv_mtags, int parc, char *parv[]
	ILine *ILineList, *newsig, *sigEntry; // Quality struct pointers
	char *mptr, *optr, mask[USERLEN + HOSTLEN + 2], other[USERLEN + HOSTLEN + 2], *exptmp, *setby; // Muh args
	char raisin[BUFSIZE]; // Reasons may or may not be pretty long
	char gmt[128], gmt2[128]; // For a pretty timestamp instead of UNIX time lol
	char cur, prev, prev2; // For checking time strings
	long setat, expire; // After how many seconds the I-Line should expire
	int i, adoffset, rindex, del; // Iterat0rs, indices and "booleans" =]
	Client *acptr; // To check if the mask is a nick instead =]

	// Gotta be at least a server, U-Line or oper with correct privs lol
	if((!IsServer(client) && !IsULine(client) && !IsOper(client)) || !ValidatePermissionsForPath("signore", client, NULL, NULL, NULL)) {
		sendnumeric(client, ERR_NOPRIVILEGES); // Check ur privilege fam
		return; // Ain't gonna happen lol
	}

	if(IsServer(client) && parc < 8)
		return;

	// If no args given (or we got /signore list)
	if(BadPtr(parv[1]) || !strcasecmp(parv[1], "list")) {
		if(IsServer(client)) // No need to list shit for servers =]
			return;
		if(!(ILineList = get_ilines())) // Attempt to get list
			sendnotice(client, "*** No I-Lines found");
		else {
			for(sigEntry = ILineList; sigEntry; sigEntry = sigEntry->next) {
				*gmt2 = '\0';
				short_date(sigEntry->set, gmt2);
				if(sigEntry->expire == 0) // Let's show "permanent" for permanent I-Lines, n0? =]
					sendnotice(client, "*** Permanent I-Line by %s (set at %s GMT) for %s and %s [reason: %s]", sigEntry->setby, gmt2, sigEntry->mask, sigEntry->other, sigEntry->raisin);
				else {
					// Get pretty timestamp for expiring lines =]
					*gmt = '\0';
					short_date(sigEntry->set + sigEntry->expire, gmt);
					sendnotice(client, "*** I-Line by %s (set at %s GMT) for %s and %s, expiring at %s GMT [reason: %s]", sigEntry->setby, gmt2, sigEntry->mask, sigEntry->other, gmt, sigEntry->raisin);
				}
			}
		}
		return;
	}

	// Need at the very least 2 args lol
	if(parc < 3 || !strcasecmp(parv[1], "help") || !strcasecmp(parv[1], "halp")) { // If no args found or first arg is "help"
		dumpit(client, muhhalp); // Return help string instead
		return;
	}

	// Need to offset parv to the left if we got a shorthand like /signore -dick@butt
	adoffset = 0;
	del = 0;
	if(IsUser(client)) { // Regular clients always use a shorter form =]
		mptr = parv[1]; // First arg is the mask hur
		optr = parv[2]; // Next is other mask y0
		if(strchr("+-", *mptr)) { // Check if it starts with either + or - fam
			del = (*mptr == '-' ? 1 : 0); // We deleting shyte?
			mptr++; // Skip past the sign lol
		}
		adoffset++; // Need to shift by one rn

		// Attempt to resolve online nicks to their respective masqs =]
		if((acptr = find_person(mptr, NULL)))
			mptr = make_user_host(acptr->user->username, acptr->user->realhost); // Get user@host with the real hostnaem
		ircsnprintf(mask, sizeof(mask), "%s", mptr);

		if((acptr = find_person(optr, NULL)))
			optr = make_user_host(acptr->user->username, acptr->user->realhost); // Get user@host with the real hostnaem
		ircsnprintf(other, sizeof(other), "%s", optr);
	}

	// Servers always use the full/long form
	else {
		if(strcasecmp(parv[1], "add") && strcasecmp(parv[1], "del")) // If first arg is neither add nor del, fuck off
			return;
		del = (!strcasecmp(parv[1], "del") ? 1 : 0); // Are we deleting?
		ircsnprintf(mask, sizeof(mask), "%s", parv[2]);
		ircsnprintf(other, sizeof(other), "%s", parv[3]);
	}

	if((!del && (parc + adoffset) < 5) || (del && (parc + adoffset) < 4)) { // Delete doesn't require the expire and reason fields
		sendnumeric(client, ERR_NEEDMOREPARAMS, MSG_SIGNORE); // Need m0ar lol
		return;
	}

	// Check for the sanity of the passed masks
	if(match_simple("*!*@*", mask) || !match_simple("*@*", mask) || strlen(mask) < 3 || match_simple("*!*@*", other) || !match_simple("*@*", other) || strlen(other) < 3) {
		if(!IsServer(client)) // Let's be silent for servers kek
			sendnotice(client, "*** The masks should be of the format ident@host");
		return; // Let's bail lol
	}
	if(match_simple(mask, other) || match_simple(other, mask)) {
		if(!IsServer(client)) // Let's be silent for servers kek
			sendnotice(client, "*** The masks cannot overlap each other");
		return; // Let's bail lol
	}

	// Initialise a bunch of shit
	memset(raisin, '\0', sizeof(raisin));
	exptmp = NULL;
	expire = DEFAULT_BANTIME;
	setat = TStime();
	rindex = 0;
	setby = client->name;
	sigEntry = find_iline(mask, other); // Attempt to find existing I-Line

	// Most shit is silent for servers
	if(IsServer(client)) {
		if(!del && sigEntry) // Adding an I-Line but it already exists
			return; // Return silently
		else if(del && !sigEntry) // Deleting but doesn't exist
			return; // Return silently

		strlcpy(raisin, parv[7], sizeof(raisin)); // Copy the reason field imo tbh
		setat = atol(parv[4]); // Extra arg yo
		expire = atol(parv[5]); // Set expiration
		setby = parv[6]; // Extra arg yo
		if(setat <= 0) // Some error occured lol
			return; // Gtfo silently
	}

	// Command came from a user
	else {
		if(!del && sigEntry) { // Adding an I-Line but it already exists
			sendnotice(client, "*** I-Line matching masks %s and %s already exists", mask, other);
			return; // Lolnope
		}
		else if(del && !sigEntry) { // Deleting but doesn't exist
			sendnotice(client, "*** I-Line for masks %s and %s doesn't exist", mask, other);
			return; // Lolnope
		}

		// If adding, check for expiration and reason fields
		if(!del) {
			exptmp = parv[3];
			// Let's check for a time string (3600, 1h, 2w3d, etc)
			for(i = 0; exptmp[i] != 0; i++) {
				cur = exptmp[i];
				if(!isdigit(cur)) { // No digit, check for the 'h' in '1h' etc
					prev = (i >= 1 ? exptmp[i - 1] : 0);
					prev2 = (i >= 2 ? exptmp[i - 2] : 0);

					if((prev && prev2 && isdigit(prev2) && prev == 'm' && cur == 'o') || (prev && isdigit(prev) && strchr("smhdw", cur))) // Check for allowed combos
						continue;

					exptmp = NULL; // Fuck off
					rindex = 3; // Reason index for parv[] is now 3 for normal clients
					break; // Only one mismatch is enough
				}
			}

			if(exptmp) { // If the for() loop didn't enter the inner if(), expire field is sane
				expire = config_checkval(exptmp, CFG_TIME); // So get a long from the (possible) time string
				rindex = 4; // And set reason index for parv[] to 4
			}

			if(!rindex || BadPtr(parv[rindex]) || !strlen(parv[rindex])) { // If rindex is 0 it means the arg is missing
				sendnotice(client, "*** The reason field is required");
				return; // No good fam
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
		newsig = safe_alloc(sizeof(ILine));
		safe_strdup(newsig->mask, mask);
		safe_strdup(newsig->other, other);
		newsig->set = setat;
		newsig->expire = expire;
		safe_strdup(newsig->raisin, raisin);
		safe_strdup(newsig->setby, setby);
		sigEntry = newsig;
		add_iline(newsig); // Add em
	}

	// Propagate the I-Line to other local servers fam (excluding the direction it came from ;])
	sendto_server(client, 0, 0, NULL, ":%s %s %s %s %s %ld %ld %s :%s", me.id, MSG_SIGNORE, (del ? "DEL" : "ADD"), mask, other, sigEntry->set, sigEntry->expire, setby, sigEntry->raisin); // Muh raw command

	// Also send snomask notices to all local opers =]
	// Make pretty set timestamp first tho
	*gmt2 = '\0';
	short_date(sigEntry->set, gmt2);

	if(sigEntry->expire == 0) { // Permanent lol
		if(IsServer(client)) // Show "set at" during sync phase ;]
			sendto_snomask(SNO_TKL, "*** Permanent I-Line %sed by %s (set at %s GMT) for %s and %s [reason: %s]", (del ? "delet" : "add"), setby, gmt2, mask, other, sigEntry->raisin);
		else
			sendto_snomask(SNO_TKL, "*** Permanent I-Line %sed by %s for %s and %s [reason: %s]", (del ? "delet" : "add"), setby, mask, other, sigEntry->raisin);
	}
	else {
		// Make pretty expiration timestamp if not a permanent I-Line
		*gmt = '\0';
		short_date(sigEntry->set + sigEntry->expire, gmt);
		if(IsServer(client)) // Show "set at" during sync phase ;]
			sendto_snomask(SNO_TKL, "*** I-Line %sed by %s (set at %s GMT) for %s and %s, expiring at %s GMT [reason: %s]", (del ? "delet" : "add"), setby, gmt2, mask, other, gmt, sigEntry->raisin);
		else
			sendto_snomask(SNO_TKL, "*** I-Line %sed by %s for %s and %s, expiring at %s GMT [reason: %s]", (del ? "delet" : "add"), setby, mask, other, gmt, sigEntry->raisin);
	}

	// Delete em famamlamlamlmal
	if(del)
		del_iline(sigEntry);
}
