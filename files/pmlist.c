/* Copyright (C) All Rights Reserved
** Written by Gottem <support@gottem.nl>
** Website: https://gottem.nl/unreal
** License: https://gottem.nl/unreal/license
*/

/*** <<<MODULE MANAGER START>>>
module {
	documentation "https://gottem.nl/unreal/man/pmlist";
	troubleshooting "In case of problems, check the FAQ at https://gottem.nl/unreal/halp or e-mail me at support@gottem.nl";
	min-unrealircd-version "5.*";
	//max-unrealircd-version "5.*";
	post-install-text {
		"The module is installed, now all you need to do is add a 'loadmodule' line to your config file:";
		"loadmodule \"third/pmlist\";";
		"Then /rehash the IRCd.";
		"For usage information, refer to the module's documentation found at: https://gottem.nl/unreal/man/pmlist";
	}
}
*** <<<MODULE MANAGER END>>>
*/

// One include for all cross-platform compatibility thangs
#include "unrealircd.h"

#define MSG_PMLIST "PMLIST" // Display whitelist lol
#define MSG_OPENPM "OPENPM" // Accept PM
#define MSG_CLOSEPM "CLOSEPM" // Stop accepting etc
#define MSG_PMHALP "PMHELP" // Stop accepting etc
#define MYCONF "pmlist" // Config block
#define UMODE_FLAG 'P' // User mode lol

// Dem macros yo
#define HasPMList(x) (IsUser(x) && (x)->umodes & extumode_pmlist)
#define CheckAPIError(apistr, apiobj) \
	do { \
		if(!(apiobj)) { \
			config_error("A critical error occurred on %s for %s: %s", (apistr), MOD_HEADER.name, ModuleGetErrorStr(modinfo->handle)); \
			return MOD_FAILED; \
		} \
	} while(0)

// Register command functions
CMD_FUNC(cmd_pmhalp);
CMD_FUNC(cmd_openpm);
CMD_FUNC(cmd_closepm);
CMD_FUNC(cmd_pmlist);

// Big hecks go here
typedef struct t_pmlistEntry pmEntry;
struct t_pmlistEntry {
	pmEntry *next, *prev;
	char *nick;
	char *uid;
	int persist;
};

// Quality fowod declarations
static void dumpit(Client *client, char **p);
int pmlist_hook_cansend_user(Client *client, Client *to, char **text, char **errmsg, int notice);
void tryNotif(Client *client, Client *to, char *text, int notice);
void pmlist_md_free(ModData *md);
void pmlist_md_notice_free(ModData *md);
int match_pmentry(Client *client, char *uid, char *nick);
void add_pmentry(Client *client, pmEntry *pm);
void delete_pmentry(Client *client, pmEntry *pm);
void free_pmentry(pmEntry *pm);
int pmlist_configtest(ConfigFile *cf, ConfigEntry *ce, int type, int *errs);
int pmlist_configrun(ConfigFile *cf, ConfigEntry *ce, int type);
int pmlist_rehash(void);

long extumode_pmlist = 0L; // Store bitwise value latur
ModDataInfo *pmlistMDI, *noticeMDI; // To store some shit with the user's Client pointur ;]

// Set config defaults here
int noticeTarget = 0; // Notice the target instead if the source is a regged and logged-in-to nick?
int noticeDelay = 60; // How many seconds to wait before sending another notice

// Halp strings in case someone does just /<CMD>
/* Special characters:
** \002 = bold -- \x02
** \037 = underlined -- \x1F
*/
static char *pmlistHalp[] = {
	"*** \002Help on /OPENPM\002, \002/CLOSEPM\002, \002/PMLIST\002 ***",
	"Keep a whitelist of people allowed to send you private messages.",
	"Their UID is stored so it persists through nickchanges too.",
	"\002\037Requires usermode +P to actually do something.\037\002",
	"If you set +P and privately message someone else, they will",
	"automatically be added to \037your\037 whitelist.",
	"Stale entries (UID no longer exists on network) will be",
	"automatically purged as well (as long as they're not persistent).",
	" ",
	"Syntax:",
	"    \002/OPENPM\002 \037nick\037 [\037-persist\037]",
	"        Allow messages from the given user",
	"        The argument \037must be an actual, existing nick\037",
	"        Also accepts -persist to keep an entry through",
	"        reconnects. Requires the nick to be registered and",
	"        logged into after they reconnect.",
	"    \002/CLOSEPM\002 \037nickmask\037",
	"        Clear matching entries from your list",
	"        Supports wildcard matches too (* and ?)",
	"    \002/PMLIST\002",
	"        Display your current whitelist",
	" ",
	"Examples:",
	"    \002/OPENPM guest8\002",
	"    \002/OPENPM muhb0i -persist\002",
	"    \002/CLOSEPM guest*\002",
	"    \002/CLOSEPM *\002",
	NULL
};

// Dat dere module header
ModuleHeader MOD_HEADER = {
	"third/pmlist", // Module name
	"2.0", // Version
	"Implements umode +P to only allow only certain people to privately message you", // Description
	"Gottem", // Author
	"unrealircd-5", // Modversion
};

// Configuration testing-related hewks go in testing phase obv
MOD_TEST() {
	// We have our own config block so we need to checkem config obv m9
	// Priorities don't really matter here
	HookAdd(modinfo->handle, HOOKTYPE_CONFIGTEST, 0, pmlist_configtest);
	return MOD_SUCCESS;
}

// Initialisation routine (register hooks, commands and modes or create structs etc)
MOD_INIT() {
	// Add a global umode (i.e. propagate to all servers), allow anyone to set/remove it on themselves
	CheckAPIError("UmodeAdd(extumode_pmlist)", UmodeAdd(modinfo->handle, UMODE_FLAG, UMODE_GLOBAL, 0, NULL, &extumode_pmlist));

	// Commands lol
	CheckAPIError("CommandAdd(OPENPM)", CommandAdd(modinfo->handle, MSG_OPENPM, cmd_openpm, 2, CMD_USER));
	CheckAPIError("CommandAdd(CLOSEPM)", CommandAdd(modinfo->handle, MSG_CLOSEPM, cmd_closepm, 1, CMD_USER));
	CheckAPIError("CommandAdd(PMLIST)", CommandAdd(modinfo->handle, MSG_PMLIST, cmd_pmlist, 0, CMD_USER));
	CheckAPIError("CommandAdd(PMHELP)", CommandAdd(modinfo->handle, MSG_PMHALP, cmd_pmhalp, 0, CMD_USER));

	// Request moddata for storing the actual whitelists
	ModDataInfo mreq;
	memset(&mreq, 0, sizeof(mreq));
	mreq.type = MODDATATYPE_LOCAL_CLIENT; // Apply to users only (CLIENT actually includes servers but we'll disregard that here =])
	mreq.name = "pmlist"; // Name it
	mreq.free = pmlist_md_free; // Function to free 'em
	pmlistMDI = ModDataAdd(modinfo->handle, mreq);
	CheckAPIError("ModDataAdd(pmlist)", pmlistMDI);

	// And another for delaying notices =]
	ModDataInfo mreq2;
	memset(&mreq2, 0, sizeof(mreq2));
	mreq2.type = MODDATATYPE_CLIENT; // Apply to users only (CLIENT actually includes servers but we'll disregard that here =])
	mreq2.name = "pmlist_lastnotice"; // Name it
	mreq2.free = pmlist_md_notice_free; // Function to free 'em
	noticeMDI = ModDataAdd(modinfo->handle, mreq2);
	CheckAPIError("ModDataAdd(pmlist_lastnotice)", noticeMDI);

	MARK_AS_GLOBAL_MODULE(modinfo);

	// Dem hewks yo
	HookAdd(modinfo->handle, HOOKTYPE_REHASH, 0, pmlist_rehash);
	HookAdd(modinfo->handle, HOOKTYPE_CONFIGRUN, 0, pmlist_configrun);
	HookAdd(modinfo->handle, HOOKTYPE_CAN_SEND_TO_USER, -9999, pmlist_hook_cansend_user); // High prio hewk pls
	return MOD_SUCCESS;
}

// Actually load the module here (also command overrides as they may not exist in MOD_INIT yet)
MOD_LOAD() {
	return MOD_SUCCESS; // We good
}

// Called on unload/rehash obv
MOD_UNLOAD() {
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

int pmlist_hook_cansend_user(Client *client, Client *to, char **text, char **errmsg, int notice) {
	pmEntry *pm; // Dat entry fam

	if(!IsUser(client) || IsULine(client) || IsULine(to) || IsOper(client) || client == to) // Check for exclusions imo
		return HOOK_CONTINUE;

	if(MyUser(client) && HasPMList(client) && !match_pmentry(client, to->id, to->name)) { // If YOU have +P and target is not on the list, add 'em
		pm = safe_alloc(sizeof(pmEntry)); // Alloc8 new entry pls
		pm->uid = strdup(to->id); // Set 'em UID
		pm->nick = strdup(to->name); // And current nick
		pm->persist = 0; // No persistence for this one
		add_pmentry(client, pm); // Add 'em lol
		sendnotice(client, "[pmlist] Added %s to your whitelist", to->name); // Notify 'em
	}

	if(!MyUser(to) || !HasPMList(to)) // If the recipient is not on this server, we can't check _their_ pmlist anyways, so fuck off here =]
		return HOOK_CONTINUE;

	if(!match_pmentry(to, client->id, client->name)) { // Attempt to match UID lol
		tryNotif(client, to, *text, notice); // Send notice if applicable atm
		*text = NULL;
		// Can't return HOOK_DENY here cuz Unreal will abort() in that case :D
	}

	return HOOK_CONTINUE;
}

void tryNotif(Client *client, Client *to, char *text, int notice) {
	long last = moddata_client(client, noticeMDI).l; // Get timestamp of last notice yo
	int sendem = (!last || TStime() - last >= noticeDelay); // We past the delay nao?
	moddata_client(client, noticeMDI).l = TStime(); // Set it to current time

	if(!IsLoggedIn(client) || !noticeTarget) { // User has not identified with services
		if(sendem)
			sendnotice(client, "[pmlist] %s does not accept private messages from you, please instruct them to do /openpm %s", to->name, client->name);
		return;
	}

	// Source user has identified with services, maybe need to send a notice to the target
	if(sendem)
		sendnotice(to, "[pmlist] %s just tried to send you a private %s, use /openpm %s to allow all of their messages [msg: %s]", client->name, (notice ? "notice" : "message"), client->name, text);
}

void pmlist_md_free(ModData *md) {
	if(md->ptr) { // r u insaiyan?
		pmEntry *pmList, *pm, *next; // Sum iter8ors lol
		pmList = md->ptr; // Get pointur to head of teh list
		for(pm = pmList; pm; pm = next) { // Let's do all entries yo
			next = pm->next; // Get next entry in advance lol
			free_pmentry(pm); // Free 'em imo
		}
		md->ptr = NULL; // Shit rip's if we don't kek
	}
}

void pmlist_md_notice_free(ModData *md) {
	if(md) // gg
		md->l = 0L; // ez
}

int match_pmentry(Client *client, char *uid, char *nick) {
	pmEntry *pmList, *pm; // Some iter80rs lol
	Client *acptr; // Check if the other user (still) exists
	int uidmatch;

	if(!client || !uid || !uid[0]) // Sanity checks
		return 0; // Lolnope

	acptr = NULL;
	if(nick)
		acptr = find_person(nick, NULL); // Attempt to find em

	if((pmList = moddata_local_client(client, pmlistMDI).ptr)) { // Something st0red?
		for(pm = pmList; pm; pm = pm->next) { // Iter8 em
			// Checkem UID and nick (if no UID match, check if the entry allows persistence and the nick is regged + authed)
			uidmatch = (!strcmp(pm->uid, uid));
			if(uidmatch || (pm->persist && acptr && IsLoggedIn(acptr) && !strcasecmp(acptr->name, pm->nick))) {
				if(!uidmatch) // Maybe need2update the entry lol
					safe_strdup(pm->uid, uid);
				return 1; // Gottem
			}
		}
	}
	return 0; // Loln0pe
}

void add_pmentry(Client *client, pmEntry *pm) {
	pmEntry *pmList, *last, *cur; // Sum iterators famalam
	if(!client || !pm || !pm->uid) // Sanity checks
		return; // kbai

	if(!(pmList = moddata_local_client(client, pmlistMDI).ptr)) { // One of the for loops MIGHT have cleared the entire list ;]
		moddata_local_client(client, pmlistMDI).ptr = pm; // Necessary to properly st0re that shit
		return; // We good
	}

	// Gonna add that shit to the end of el list y0
	for(cur = pmList; cur; cur = cur->next)
		last = cur; // cur will end up pointing to NULL, so let's use the entry just before ;];]
	pm->prev = last; // The new entry's prev should point to the actual last entry
	last->next = pm; // And the last entry's next should be the new one obv =]
}

void delete_pmentry(Client *client, pmEntry *pm) {
	pmEntry *pmList, *last; // Sum iter8ors lol
	if(!pm || !pm->uid) // r u insaiyan?
		return;

	if(client && (pmList = moddata_local_client(client, pmlistMDI).ptr)) { // One of the for loops MIGHT have cleared the entire list ;]
		for(last = pmList; last; last = last->next) { // Iterate em lol
			if(last == pm) { // We gottem match?
				// Doubly linked lists ftw yo
				if(last->prev) // Is anything but the FIRST entry
					last->prev->next = last->next; // Previous entry should skip over dis one
				else { // Is the first entry
					moddata_local_client(client, pmlistMDI).ptr = last->next; // So make the moddata thingy point to the second one
					pmList = last->next; // Really just for the if below =]
				}

				if(last->next) // If anything but the LAST entry
					last->next->prev = last->prev; // Next entry should skip over dis one

				free_pmentry(last); // Free 'em lol
				break; // Gtfo imo tbh famlammlmflma
			}
		}

		if(!pmList) // We empty nao?
			moddata_local_client(client, pmlistMDI).ptr = NULL; // Cuz inb4ripperoni
	}
}

void free_pmentry(pmEntry *pm) {
	if(!pm) // LOLNOPE
		return;
	safe_free(pm->uid);
	safe_free(pm->nick);
	safe_free(pm);
}

CMD_FUNC(cmd_pmhalp) {
	dumpit(client, pmlistHalp); // Return help string always
}

CMD_FUNC(cmd_openpm) {
	// Gets args: Client *client, MessageTag *recv_mtags, int parc, char *parv[]
	Client *acptr; // Who r u allowin?
	pmEntry *pmList, *pm, *next; // Iterators imho tbh fambi
	int gtfo; // Maybe won't need to add an entry ;]
	int persist; // Whether to persist this entry or no

	if(!MyUser(client))
		return;

	 // If first argument is a bad pointer or user doesn't even have umode +P, don't proceed (also if the optional -persist is written incorrectly]
	if(BadPtr(parv[1]) || !HasPMList(client) || (!BadPtr(parv[2]) && strcasecmp(parv[2], "-persist")))
		return dumpit(client, pmlistHalp); // Return help string instead

	if(!(acptr = find_person(parv[1], NULL))) { // Verify target user
		sendnumeric(client, ERR_NOSUCHNICK, parv[1]); // Send error lol
		return;
	}

	if(IsULine(acptr)) { // Checkem U-Line lol
		sendnotice(client, "[pmlist] There's no need to whitelist U-Lined users (%s)", acptr->name);
		return;
	}

	if(IsOper(acptr)) { // Checkem opers lol
		sendnotice(client, "[pmlist] There's no need to whitelist IRC operators (%s)", acptr->name);
		return;
	}

	if(!acptr->id) { // Sanity check lol
		sendnotice(client, "[pmlist] Something went wrong getting %s's UID", acptr->name);
		return;
	}

	persist = (!BadPtr(parv[2]) && !strcasecmp(parv[2], "-persist") ? 1 : 0);
	if(!(pmList = moddata_local_client(client, pmlistMDI).ptr)) { // If no list yet
		pm = safe_alloc(sizeof(pmEntry)); // Alloc8 new entray
		pm->uid = strdup(acptr->id); // Set 'em UID
		pm->nick = strdup(acptr->name); // And current nick
		pm->persist = persist; // Set persistence yo
		add_pmentry(client, pm); // Add 'em lol
		sendnotice(client, "[pmlist] Added %s to your whitelist%s", acptr->name, (persist ? ", persistently" : "")); // Notify 'em
		return;
	}

	gtfo = 0;
	for(pm = pmList; pm; pm = next) { // Check if the UID is already whitelisted (check for stale entries too ;];])
		next = pm->next; // Get next entry in advance lol

		if(!find_person(pm->uid, NULL) && !pm->persist) { // Check for stale entry lol (UID no longer existing on the netwerk)
			delete_pmentry(client, pm); // Delete from list lol
			continue; // No need to check below if =]
		}

		if(!gtfo && (!strcmp(pm->uid, acptr->id) || !strcasecmp(pm->nick, acptr->name))) // UID/nick already listed
			gtfo = 1; // Flippem ;]
	}

	if(gtfo) {
		sendnotice(client, "[pmlist] You've already whitelisted %s", acptr->name);
		return;
	}

	// New entry, alloc8 memory m8
	pm = safe_alloc(sizeof(pmEntry));
	pm->uid = strdup(acptr->id); // Set 'em UID
	pm->nick = strdup(acptr->name); // And current nick
	pm->persist = persist; // Set persistence yo
	add_pmentry(client, pm); // Add 'em yo
	sendnotice(client, "[pmlist] Added %s to your whitelist%s", acptr->name, (persist ? ", persistently" : "")); // Notify 'em
}

CMD_FUNC(cmd_closepm) {
	pmEntry *pmList, *pm, *next; // Iterators imho tbh fambi
	int found; // Maybe won't need to delete an entry ;]

	if(!MyUser(client))
		return;

	if(BadPtr(parv[1]) || !HasPMList(client)) { // If first argument is a bad pointer or user doesn't even have umode +P, don't proceed
		dumpit(client, pmlistHalp); // Return help string instead
		return;
	}

	if(!(pmList = moddata_local_client(client, pmlistMDI).ptr)) {
		sendnotice(client, "[pmlist] You don't have any entries in your whitelist");
		return;
	}

	char buf[256]; // For outputting multiple nicks etc lol
	memset(buf, 0, sizeof(buf));
	for(pm = pmList; pm; pm = next) { // Remove all =]
		next = pm->next; // Get next entry in advance lol

		if(!find_person(pm->uid, NULL) && !pm->persist) { // Check for stale entry lol (UID no longer existing on the netwerk)
			delete_pmentry(client, pm); // Delete from list lol
			continue; // No need to check below if =]
		}

		if(!match_simple(parv[1], pm->nick)) // If the entry's nick doesn't match the given mask
			continue; // Let's fuck off

		found = 1; // Ayy we gottem

		if(!buf[0]) {// First nick in this set
			strlcpy(buf, pm->nick, sizeof(buf)); // Need cpy instead of cat ;]
			if(pm->persist) strlcat(buf, " (P)", sizeof(buf)); // Dat persistence
		}
		else {
			strlcat(buf, ", ", sizeof(buf)); // Dat separator lol
			strlcat(buf, pm->nick, sizeof(buf)); // Now append non-first nikk =]
			if(pm->persist) strlcat(buf, " (P)", sizeof(buf)); // Dat persistence
		}

		if(strlen(buf) > (sizeof(buf) - NICKLEN - 4 - 3)) { // If another nick won't fit (-4 , -3 cuz optional " (P)" plus mandatory ", " and nullbyet)
			sendnotice(client, "[pmlist] Removed from whitelist: %s", buf); // Send what we have
			memset(buf, 0, sizeof(buf)); // And reset buffer lmoa
		}
		delete_pmentry(client, pm); // Delete from list lol
	}

	if(buf[0]) // If we still have some nicks (i.e. we didn't exceed buf's size for the last set)
		sendnotice(client, "[pmlist] Removed from whitelist: %s", buf); // Dump whatever's left

	if(!found)
		sendnotice(client, "[pmlist] No matches found for usermask %s", parv[1]);
}

CMD_FUNC(cmd_pmlist) {
	pmEntry *pmList, *pm, *next; // Iterators imho tbh fambi
	int found; // Gottem

	if(!MyUser(client))
		return;

	if(!HasPMList(client)) { // If user doesn't even have umode +P, don't proceed
		sendnotice(client, "[pmlist] You need to have umode +%c set to use this feature", UMODE_FLAG);
		return;
	}

	if(!(pmList = moddata_local_client(client, pmlistMDI).ptr)) {
		sendnotice(client, "[pmlist] You don't have any entries in your whitelist");
		return;
	}

	char buf[256]; // For outputting multiple nicks etc lol
	memset(buf, 0, sizeof(buf));
	found = 0;
	for(pm = pmList; pm; pm = next) { // Check if the UID is already whitelisted (check for stale entries too ;];])
		next = pm->next; // Get next entry in advance lol

		if(!find_person(pm->uid, NULL) && !pm->persist) { // Check for stale entry lol (UID no longer existing on the netwerk)
			delete_pmentry(client, pm); // Delete from list lol
			continue; // No need to check below if =]
		}

		found = 1;

		if(!buf[0]) { // First nick in this set
			strlcpy(buf, pm->nick, sizeof(buf)); // Need cpy instead of cat ;]
			if(pm->persist) strlcat(buf, " (P)", sizeof(buf)); // Dat persistence
		}
		else {
			strlcat(buf, ", ", sizeof(buf)); // Dat separator lol
			strlcat(buf, pm->nick, sizeof(buf)); // Now append non-first nikk =]
			if(pm->persist) strlcat(buf, " (P)", sizeof(buf)); // Dat persistence
		}

		if(strlen(buf) > (sizeof(buf) - NICKLEN - 4 - 3)) { // If another nick won't fit (-4 , -3 cuz optional " (P)" plus mandatory ", " and nullbyet)
			sendnotice(client, "[pmlist] Whitelist: %s", buf); // Send what we have
			memset(buf, 0, sizeof(buf)); // And reset buffer just in caes lmoa
		}
	}

	if(buf[0]) // If we still have some nicks
		sendnotice(client, "[pmlist] Whitelist: %s", buf); // Dump whatever's left

	if(!found) // The for loop above MIGHT have cleared the entire list ;]
		sendnotice(client, "[pmlist] You don't have any entries in your whitelist");
}

int pmlist_configtest(ConfigFile *cf, ConfigEntry *ce, int type, int *errs) {
	int errors = 0; // Error count
	int i; // Iterat0r
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

		if(!strcmp(cep->ce_varname, "noticetarget")) {
			if(!cep->ce_vardata || (strcmp(cep->ce_vardata, "0") && strcmp(cep->ce_vardata, "1"))) {
				config_error("%s:%i: %s::%s must be either 0 or 1 fam", cep->ce_fileptr->cf_filename, cep->ce_varlinenum, MYCONF, cep->ce_varname);
				errors++; // Increment err0r count fam
			}
			continue;
		}

		if(!strcmp(cep->ce_varname, "noticedelay")) {
			// Should be an integer yo
			if(!cep->ce_vardata) {
				config_error("%s:%i: %s::%s must be an integer of 0 or larger m8", cep->ce_fileptr->cf_filename, cep->ce_varlinenum, MYCONF, cep->ce_varname);
				errors++; // Increment err0r count fam
				continue;
			}
			for(i = 0; cep->ce_vardata[i]; i++) {
				if(!isdigit(cep->ce_vardata[i])) {
					config_error("%s:%i: %s::%s must be an integer of 0 or larger m8", cep->ce_fileptr->cf_filename, cep->ce_varlinenum, MYCONF, cep->ce_varname);
					errors++; // Increment err0r count fam
					break;
				}
			}
			continue;
		}
	}

	*errs = errors;
	return errors ? -1 : 1; // Returning 1 means "all good", -1 means we shat our panties
}

// "Run" the config (everything should be valid at this point)
int pmlist_configrun(ConfigFile *cf, ConfigEntry *ce, int type) {
	ConfigEntry *cep; // To store the current variable/value pair etc

	// Since we'll add a top-level block to unrealircd.conf, need to filter on CONFIG_MAIN lmao
	if(type != CONFIG_MAIN)
		return 0; // Returning 0 means idgaf bout dis

	// Check for valid config entries first
	if(!ce || !ce->ce_varname)
		return 0;

	// If it isn't pmlist, idc
	if(strcmp(ce->ce_varname, MYCONF))
		return 0;

		// Loop dat shyte fam
	for(cep = ce->ce_entries; cep; cep = cep->ce_next) {
		// Do we even have a valid name l0l?
		if(!cep->ce_varname)
			continue; // Next iteration imo tbh

		if(!strcmp(cep->ce_varname, "noticetarget")) {
			noticeTarget = atoi(cep->ce_vardata);
			continue;
		}

		if(!strcmp(cep->ce_varname, "noticedelay")) {
			noticeDelay = atoi(cep->ce_vardata);
			continue;
		}
	}

	return 1; // We good
}

int pmlist_rehash(void) {
	// Reset config defaults
	noticeTarget = 0;
	noticeDelay = 60;
	return HOOK_CONTINUE;
}
