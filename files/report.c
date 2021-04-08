/* Copyright (C) All Rights Reserved
** Written by Gottem <support@gottem.nl>
** Website: https://gottem.nl/unreal
** License: https://gottem.nl/unreal/license
*/

/*** <<<MODULE MANAGER START>>>
module {
	documentation "https://gottem.nl/unreal/man/report";
	troubleshooting "In case of problems, check the FAQ at https://gottem.nl/unreal/halp or e-mail me at support@gottem.nl";
	min-unrealircd-version "5.*";
	//max-unrealircd-version "5.*";
	post-install-text {
		"The module is installed, now all you need to do is add a 'loadmodule' line to your config file:";
		"loadmodule \"third/report\";";
		"Then /rehash the IRCd.";
		"For usage information, refer to the module's documentation found at: https://gottem.nl/unreal/man/report";
	}
}
*** <<<MODULE MANAGER END>>>
*/

// One include for all cross-platform compatibility thangs
#include "unrealircd.h"

// Command strings, let's just split that shit to make it easier to usem
#define MSG_REPORT "REPORT" // Main command, used by regular users to submittem rep0ts
#define MSG_REPORTLIST "REPORTLIST" // Used by opers to listem
#define MSG_REPORTDEL "REPORTDEL" // Also by opers to deletem obv m9
#define MSG_REPORTSYNC "REPORTSYNC" // Might as well use a separate command for syncing em lmao (also used when e.g. an oper removes shit to p00pagate the changes to the other serburs)

#define MYCONF "report"

// Dem macros yo, register command functions
CMD_FUNC(report);
CMD_FUNC(reportlist);
CMD_FUNC(reportdel);
CMD_FUNC(reportsync);

#define CheckAPIError(apistr, apiobj) \
	do { \
		if(!(apiobj)) { \
			config_error("A critical error occurred on %s for %s: %s", (apistr), MOD_HEADER.name, ModuleGetErrorStr(modinfo->handle)); \
			return MOD_FAILED; \
		} \
	} while(0)

// Big hecks go here
typedef struct t_report Report;
struct t_report {
	Report *prev;
	Report *next;
	int id; // Kind of, at least :>
	time_t deet; // Tiemstampus imo tbh
	char *reporturd; // nick!user@host phambly
	char *msg;
};

// Store config options here
struct cfgstruct {
	int min_chars;

	// Just some "booleans" to keep track of whether the admin specified a directive
	unsigned short int got_min_chars;
};

// Quality fowod declarations
static void dumpit(Client *client, char **p);
Report *addem_report(int id, time_t deet, char *reporturd, const char *msg);
void freem_report(Report *reportItem);
void notifyopers_add(Report *reportItem);
void notifyopers_del(char *byuser, Report *reportItem);
void syncem(Client *excludem, Client *to_one, char *flag, char *byuser, Report *reportItem);
void setcfg(void);
ConfigEntry *gottem_getmodconf(ConfigEntry *ce, int type);
void report_moddata_free(ModData *md);
int report_configtest(ConfigFile *cf, ConfigEntry *ce, int type, int *errs);
int report_configrun(ConfigFile *cf, ConfigEntry *ce, int type);
int report_hook_serversync(Client *client);

// Help strings in case someone does just /REPORT*
static char *report_help[] = {
	/* Special characters:
	** \002 = bold -- \x02
	** \037 = underlined -- \x1F
	*/
	"*** \002Help on /REPORT\002 ***",
	"Allows you to report bad stuff to the assigned IRC operators.",
	" ",
	"Syntax:",
	"    \002/REPORT\002 \037comment\037",
	NULL
};

static char *reportdel_help[] = {
	/* Special characters:
	** \002 = bold -- \x02
	** \037 = underlined -- \x1F
	*/
	"*** \002Help on /REPORTDEL\002 ***",
	"Allows you to delete submitted reports. Use \002REPORTLIST\002 to get the proper ID.",
	" ",
	"Syntax:",
	"    \002/REPORTDEL\002 \037report ID\037",
	NULL
};

// Globals
Report *reportList = NULL;
int reportList_lastID = 0;
static struct cfgstruct muhcfg;
ModDataInfo *reportMDI; // To store the rep0ts with &me lol (hack so we don't have to use a .db file or some shit)

// Dat dere module header
ModuleHeader MOD_HEADER = {
	"third/report", // Module name
	"1.0-rc1", // Version
	"For reporting bad stuff to the assigned IRC operators", // Description
	"Gottem", // Author
	"unrealircd-5", // Modversion
};

// Configuration testing-related hewks go in testing phase obv
MOD_TEST() {
	memset(&muhcfg, 0, sizeof(muhcfg)); // Zero-initialise config

	// We have our own config block so we need to checkem config obv m9
	// Priorities don't really matter here
	HookAdd(modinfo->handle, HOOKTYPE_CONFIGTEST, 0, report_configtest);
	return MOD_SUCCESS;
}

// Initialisation routine (register hooks, commands and modes or create structs etc)
MOD_INIT() {
	// Serburs have no use for the first 3 commands ;];]
	CheckAPIError("CommandAdd(REPORT)", CommandAdd(modinfo->handle, MSG_REPORT, report, 1, CMD_USER));
	CheckAPIError("CommandAdd(REPORTLIST)", CommandAdd(modinfo->handle, MSG_REPORTLIST, reportlist, 0, CMD_USER));
	CheckAPIError("CommandAdd(REPORTDEL)", CommandAdd(modinfo->handle, MSG_REPORTDEL, reportdel, 1, CMD_USER));
	CheckAPIError("CommandAdd(REPORTSYNC)", CommandAdd(modinfo->handle, MSG_REPORTSYNC, reportsync, 4, CMD_SERVER));

	if(!(reportMDI = findmoddata_byname("report_list", MODDATATYPE_LOCAL_VARIABLE))) { // Attempt to find active moddata (like in case of a rehash)
		ModDataInfo mreq; // No moddata, let's request that shit
		memset(&mreq, 0, sizeof(mreq));
		mreq.type = MODDATATYPE_LOCAL_VARIABLE; // Apply to servers only (CLIENT actually includes users but we'll disregard that =])
		mreq.name = "report_list";
		mreq.free = report_moddata_free;
		mreq.serialize = NULL;
		mreq.unserialize = NULL;
		mreq.sync = 0; // Even though we *do* sync rep0ts I prefer having more control over when and what, so let's not let Unreal handle em =]
		reportMDI = ModDataAdd(modinfo->handle, mreq); // Add 'em yo
		CheckAPIError("ModDataAdd(report_list)", reportMDI);
	}
	else { // We did get moddata
		reportList = moddata_local_variable(reportMDI).ptr;
		if(reportList)
			reportList_lastID = reportList->id; // Most recent is at the "top", ez ;]
	}

	MARK_AS_GLOBAL_MODULE(modinfo);

	setcfg();
	HookAdd(modinfo->handle, HOOKTYPE_SERVER_SYNC, 0, report_hook_serversync);
	HookAdd(modinfo->handle, HOOKTYPE_CONFIGRUN, 0, report_configrun);
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
	if(IsServer(client))
		return;

	// Using sendto_one() instead of sendnumericfmt() because the latter strips indentation and stuff ;]
	for(; *p != NULL; p++)
		sendto_one(client, NULL, ":%s %03d %s :%s", me.name, RPL_TEXT, client->name, *p);

	// Let user take 8 seconds to read it
	client->local->since += 8;
}

Report *addem_report(int id, time_t deet, char *reporturd, const char *msg) {
	// This function can be passed 0 for the first 2 arguments, in that case we'll auto-set that shit
	// Otherwise we'll use em as-is ;];];];];]];];];
	Report *reportItem = safe_alloc(sizeof(Report));

	if(id > 0) {
		// If we got passed a proper ID, then update the max count if necessary
		if(id > reportList_lastID)
			reportList_lastID = id;
		reportItem->id = id; // And always set the ID of this item 0bv ;]
	}
	else
		reportItem->id = ++reportList_lastID;

	reportItem->deet = (deet > 0 ? deet : TStime()); // Tiemstampus is mucho easier lol
	reportItem->reporturd = strdup(reporturd);
	reportItem->msg = strdup(msg);
	AddListItem(reportItem, reportList);
	return reportItem;
}

void freem_report(Report *reportItem) {
	safe_free(reportItem->reporturd);
	safe_free(reportItem->msg);
	safe_free(reportItem);
}

void notifyopers_add(Report *reportItem) {
	// n0tify 0pers pls ok (this only contains *local* opers, other servers will handle this themselves ;])
	// ne 0per with the 'list' permission should prolly also receive deez ~~nuts~~ notifications, they don't necessarily need to be able to deletem
	char buf[BUFSIZE];
	Client *operclient;

	ircsnprintf(buf, sizeof(buf), "*** [report] [%s] Report (ID #%d) by [%s]: %s", pretty_date(reportItem->deet), reportItem->id, reportItem->reporturd, reportItem->msg);
	list_for_each_entry(operclient, &oper_list, special_node) {
		if(ValidatePermissionsForPath("gottem:report:notify", operclient, NULL, NULL, NULL))
			sendnotice(operclient, buf);
	}
}

void notifyopers_del(char *byuser, Report *reportItem) {
	char buf[BUFSIZE];
	Client *operclient;

	ircsnprintf(buf, sizeof(buf), "*** [report] %s deleted report with ID #%d by [%s]: %s", byuser, reportItem->id, reportItem->reporturd, reportItem->msg);
	list_for_each_entry(operclient, &oper_list, special_node) {
		if(ValidatePermissionsForPath("gottem:report:notify", operclient, NULL, NULL, NULL))
			sendnotice(operclient, buf);
	}
}

void syncem(Client *excludem, Client *to_one, char *flag, char *byuser, Report *reportItem) {
	char buf[BUFSIZE];

	if(*flag == '-')
		ircsnprintf(buf, sizeof(buf), "REPORTSYNC %s%d %s", flag, reportItem->id, (byuser ? byuser : "<UNKNOWN>"));
	else
		ircsnprintf(buf, sizeof(buf), "REPORTSYNC %s%d %ld %s :%s", flag, reportItem->id, reportItem->deet, reportItem->reporturd, reportItem->msg);

	if(to_one)
		sendto_one(to_one, NULL, ":%s %s", me.id, buf);
	else
		sendto_server(excludem, 0, 0, NULL, buf);
}

// Set config defaults here ;]
void setcfg(void) {
	// Anything that would resolve to a zero value (for char * this could be NULL, for int this is simply 0) doesn't need to
	// be specified here, as the memset() in MOD_TEST already initialised everything to zero =]
	muhcfg.min_chars = 10; // Let's require 10 chars by default for report messages =]]
}

ConfigEntry *gottem_getmodconf(ConfigEntry *ce, int type) {
	/* This function is to parse a config bl0cc like:
	** somemodule {
	**     foo "bar";
	** };
	** And return the ConfigEntry pointer to the module bl0cc itself (i.e. ce_varname is "somemodule")
	** We don't care if the block is entirely empty, as long as it exists we'll tell Unreal we handled em (to prevent an error on the block being unknown ;])
	** We *do* care about it being an actual block and not just an individual directive like: somemodule;
	**
	** Eventually this function will be used for something a lil' different, but I'm still working out the best way ;];];];]];];];];];;]
	*/

	// Since we'll add a top-level block to unrealircd.conf, need to filter on CONFIG_MAIN lmao
	if(type != CONFIG_MAIN)
		return NULL;

	// Check for valid config entries first (should at least have a name kek)
	if(!ce || !ce->ce_varname)
		return NULL;

	// If it isn't our block, idc
	if(strcmp(ce->ce_varname, MYCONF))
		return NULL;

	// Verify that it's actually a bl0cc/section, it has a line number if so :>
	// If there's no line number then that shit is invalid and we'll termin8 earlier m8
	if(ce->ce_sectlinenum)
		return ce;

	return NULL;
}

void report_moddata_free(ModData *md) {
	if(md->ptr) { // r u insaiyan?
		freem_report(md->ptr);
		md->ptr = NULL; // Make sure we clearem
	}
}

int report_configtest(ConfigFile *cf, ConfigEntry *ce, int type, int *errs) {
	int errors = 0;
	int i;
	ConfigEntry *cep; // To store the current variable/value pair etc

	if(!(cep = gottem_getmodconf(ce, type)))
		return 0; // Returning 0 means idgaf bout dis (i.e. mark/keep marked as unknown)

	// Loop dat shyte fam
	for(cep = cep->ce_entries; cep; cep = cep->ce_next) {
		// Do we even have a valid name l0l?
		// This should already be checked by Unreal's core functions but there's no harm in having it here too =]
		if(!cep->ce_varname) {
			config_error("%s:%i: blank %s item", cep->ce_fileptr->cf_filename, cep->ce_varlinenum, MYCONF);
			errors++;
			continue;
		}

		if(!cep->ce_vardata) {
			config_error("%s:%i: blank %s value", cep->ce_fileptr->cf_filename, cep->ce_varlinenum, MYCONF);
			errors++;
			continue;
		}

		if(!strcmp(cep->ce_varname, "min-chars")) {
			if(muhcfg.got_min_chars) {
				config_error("%s:%i: duplicate %s::%s directive", cep->ce_fileptr->cf_filename, cep->ce_varlinenum, MYCONF, cep->ce_varname);
				errors++;
				continue;
			}

			// Should be an integer y0
			muhcfg.got_min_chars = 1;
			for(i = 0; cep->ce_vardata[i]; i++) {
				if(!isdigit(cep->ce_vardata[i])) {
					config_error("%s:%i: %s::%s must be an integer between 1 and 50", cep->ce_fileptr->cf_filename, cep->ce_varlinenum, MYCONF, cep->ce_varname);
					errors++;
					break;
				}
			}

			// If we still have a valid char, then the loop broke early due to an error, so we don't need to check the range yet ;];];];];]
			if(cep->ce_vardata[i])
				continue;

			i = atoi(cep->ce_vardata);
			if(i < 1 || i > 50) {
				config_error("%s:%i: %s::%s must be an integer between 1 and 50", cep->ce_fileptr->cf_filename, cep->ce_varlinenum, MYCONF, cep->ce_varname);
				errors++;
			}

			continue;
		}

		// Anything else is unknown to us =]
		config_warn("%s:%i: unknown item %s::%s", cep->ce_fileptr->cf_filename, cep->ce_varlinenum, MYCONF, cep->ce_varname); // So display just a warning
	}

	*errs = errors;
	return errors ? -1 : 1; // Returning 1 means "all good", -1 means we shat our panties
}

// "Run" the config (everything should be valid at this point)
int report_configrun(ConfigFile *cf, ConfigEntry *ce, int type) {
	ConfigEntry *cep;

	if(!(cep = gottem_getmodconf(ce, type)))
		return 0;

	for(cep = cep->ce_entries; cep; cep = cep->ce_next) {
		if(!cep->ce_varname)
			continue;

		if(!strcmp(cep->ce_varname, "min-chars")) {
			muhcfg.min_chars = atoi(cep->ce_vardata);
			continue;
		}
	}

	return 1; // We good
}

CMD_FUNC(report) {
	/* Gets args: Client *client, MessageTag *recv_mtags, int parc, char *parv[]
	**
	** client: Pointer to user executing command
	** recv_mtags: Received/incoming message tags (IRCv3 stuff)
	** parc: Amount of arguments (also includes the command in the count)
	** parv: Contains the actual args, first one starts at parv[1]
	**
	** So "REPORT test" would result in parc = 2 and parv[1] = "test"
	** Also, parv[0] seems to always be NULL, so better not rely on it fam
	*/
	Report *reportItem;
	const char *msg;
	char reporturd[NICKLEN + USERLEN + HOSTLEN + 4]; // +4 in order to account for the chars : ! @ \0

	// For users the format is simple: REPORT :<message>
	// Let's start by excluding non-local clients (non-users shouldn't be able to reach this, but let's czech em anyways) ;]
	if(!MyUser(client))
		return;

	if(parc < 2 || BadPtr(parv[1])) { // If first argument is a bad pointer, don't proceed
		dumpit(client, report_help); // Return help string instead
		return;
	}

	// Big hecks go here
	make_nick_user_host_r(reporturd, client->name, client->user->username, client->user->realhost);
	msg = StripControlCodes(parv[1]); // Strip col0urs and other markup etc
	if(strlen(msg) < muhcfg.min_chars) {
		sendnotice(client, "*** [report] Your comment is too short: must be at least %d characters long", muhcfg.min_chars);
			return;
	}

	// Let's try to find an exact duplicate first y0
	for(reportItem = reportList; reportItem; reportItem = reportItem->next) {
		if(!strcasecmp(reportItem->msg, msg)) {
			sendnotice(client, "*** [report] Duplicate report found, yours will be ignored");
			return;
		}
	}

	reportItem = addem_report(0, 0, reporturd, msg);
	sendnotice(client, "*** [report] Your report was successfully submitted");

	notifyopers_add(reportItem);
	moddata_local_variable(reportMDI).ptr = reportList; // Let's make sure we (still) have a proper p0inturd =]]]
	syncem(NULL, NULL, "+", NULL, reportItem); // p00pagate to all other serburs
}

CMD_FUNC(reportlist) {
	Report *reportItem;

	if(!MyUser(client))
		return;

	// Also excludes U-Lines ;]
	if(!ValidatePermissionsForPath("gottem:report:list", client, NULL, NULL, NULL)) {
		sendnumeric(client, ERR_NOPRIVILEGES);
		return;
	}

	if(!reportList) {
		sendnotice(client, "*** [report] No reports found");
		return;
	}

	for(reportItem = reportList; reportItem; reportItem = reportItem->next)
		sendnotice(client, "*** [report] [%s] ID #%d) By [%s]: %s", pretty_date(reportItem->deet), reportItem->id, reportItem->reporturd, reportItem->msg);
}

CMD_FUNC(reportdel) {
	Report *reportItem;
	int id;
	char *p; // For checking if we actually got a number/integer for ID lel

	if(!MyUser(client))
		return;

	if(!ValidatePermissionsForPath("gottem:report:delete", client, NULL, NULL, NULL)) {
		sendnumeric(client, ERR_NOPRIVILEGES);
		return;
	}

	if(parc < 2 || BadPtr(parv[1])) {
		dumpit(client, reportdel_help);
		return;
	}

	for(p = parv[1]; *p; p++) {
		if(!isdigit(*p)) {
			sendnotice(client, "*** [report] Invalid ID '%s': must be numeric", parv[1]);
			return;
		}
	}

	id = atoi(parv[1]);
	if(id <= 0 || id > reportList_lastID) {
		sendnotice(client, "*** [report] Invalid ID '%d': matching report not found", id);
		return;
	}

	for(reportItem = reportList; reportItem; reportItem = reportItem->next) {
		if(reportItem->id == id)
			break;
	}

	if(!reportItem) {
		sendnotice(client, "*** [report] Report with ID #%d not found", id);
		return;
	}

	DelListItem(reportItem, reportList);
	notifyopers_del(client->name, reportItem);
	moddata_local_variable(reportMDI).ptr = reportList;
	syncem(NULL, NULL, "-", client->name, reportItem);
	freem_report(reportItem);
}

CMD_FUNC(reportsync) {
	// We have 3 states here, it's almost like quantum computing brah
	// uno) + means adding ofc (like a user submitting shit)
	// dos) - means an oper deleted that shit 0bv
	// tres) No "flag" means sync that shit m9, which is basically just adding without a notification =]]
	Report *reportItem;
	char flag;
	int id;
	int is_signed; // Synonymous for "pls notify"
	time_t deet;
	char *byuser, *reporturd, *msg;
	char *p;

	// Only accept shit from local servers 0fc
	if(!IsServer(client) || !MyConnect(client))
		return;

	// Format: REPORTSYNC [flag]<id> <deet> <byuser> <reporturd> :<msg>
	// deet/reporturd/msg are not meant for deletions, byuser is not meant for additions =]
	// Flag is either + or - or none y0

	// For any malformed shit we'll simply notify all local opers, as it may indicate a module version mismatch or some shit =]
	// Let's start with the bare minimum to run at least one command successfully (which is delete lol)
	if(parc < 3 || BadPtr(parv[2])) {
		sendto_realops("[report] Malformed %s command from %s: not enough arguments", MSG_REPORTSYNC, client->name);
		return;
	}

	// Should have at least something lel
	if(!strlen(parv[1])) {
		sendto_realops("[report] Malformed %s command from %s: 'ID' argument has no length", MSG_REPORTSYNC, client->name);
		return;
	}

	// Check flag first
	p = parv[1];
	flag = '+'; // Add by default y0
	is_signed = 0;
	byuser = NULL;
	if(*p == '+' || *p == '-') { // Gotta check for plus again due to it being explicitly specified on a new user-created report, we gotta "move" past it
		flag = *p++;
		is_signed = 1;
	}

	// Then ID
	for(; *p; p++) {
		if(!isdigit(*p)) {
			sendto_realops("[report] Malformed %s command from %s: invalid ID (must be numeric)", MSG_REPORTSYNC, client->name);
			break;
		}
	}

	// Now check the amount of arguments for additions too
	if(flag == '+') {
		if(parc < 5 || BadPtr(parv[4])) {
			sendto_realops("[report] Malformed %s command from %s: not enough arguments", MSG_REPORTSYNC, client->name);
			return;
		}
	}

	if(is_signed)
		id = atoi(parv[1] + 1);
	else
		id = atoi(parv[1]);

	// Check d00plicate/existing ID
	// For now we'll allow IDs lower than the currently known last ID; this means that during a netsplit one end deleted a report, we should resync it to make sure all servers have the same data
	// Afterwards you can just deletem again lol
	for(reportItem = reportList; reportItem; reportItem = reportItem->next) {
		if(reportItem->id == id)
			break;
	}

	// For the arguments with string values I don't really care besides them having any length =]]]]]]]]]]]]]]]]]]]]]]
	if(flag == '-') {
		// Should prolly have a reportItem huh
		if(!reportItem) {
			sendto_realops("[report] Malformed %s command from %s: received ID #%d but doesn't exist on our end", MSG_REPORTSYNC, client->name, id);
			return;
		}

		byuser = parv[2];
		if(!strlen(byuser)) {
			sendto_realops("[report] Malformed %s command from %s: 'byuser' argument has no length", MSG_REPORTSYNC, client->name);
			return;
		}

		DelListItem(reportItem, reportList);
		if(is_signed) // Deletions are always signed, but whatever =]
			notifyopers_del(byuser, reportItem);
		moddata_local_variable(reportMDI).ptr = reportList;
		syncem(client, NULL, "-", byuser, reportItem);
		freem_report(reportItem);
		return;
	}

	// Now the deet
	if(!strlen(parv[2])) {
		sendto_realops("[report] Malformed %s command from %s: 'timestamp' argument has no length", MSG_REPORTSYNC, client->name);
		return;
	}

	for(p = parv[2]; *p; p++) {
		if(!isdigit(*p)) {
			sendto_realops("[report] Malformed %s command from %s: invalid timestamp (must be numeric)", MSG_REPORTSYNC, client->name);
			return;
		}
	}

	// If the l00p found a matching ID and we're adding (or syncing), this is a d00plicate so fucc that (should pr0lly be removed then anyways kek)
	// We ain't checkin' em earlier cuz I wanna compare the deetz to see if it's actually a d00p or n0 (ID + date should be unique enough imo tbh)
	// Doing this because on one hand I wanna notify about d00ps, but not when it's not actually a d00p 0bv
	// So: if the IDs are the same but the timestamps ain't, then it's prolly a different rep0rt and we should emit a warning aboot dis
	deet = atol(parv[2]);
	reporturd = parv[3];
	msg = parv[4]; // We don't check the min-chars here; in case it changed inbetween we should still accept noncompliant rep0ts ;]

	if(!strlen(reporturd)) {
		sendto_realops("[report] Malformed %s command from %s: 'reporter' argument has no length", MSG_REPORTSYNC, client->name);
		return;
	}
	if(!strlen(msg)) {
		sendto_realops("[report] Malformed %s command from %s: 'message' argument has no length", MSG_REPORTSYNC, client->name);
		return;
	}

	if(reportItem) {
		if(reportItem->deet != deet)
			sendto_realops("[report] Malformed %s command from %s: duplicate ID #%d) [%s] by [%s]: %s", MSG_REPORTSYNC, client->name, id, pretty_date(deet), reporturd, msg);
		return;
	}

	reportItem = addem_report(id, deet, reporturd, msg);
	notifyopers_add(reportItem);
	moddata_local_variable(reportMDI).ptr = reportList;
	if(is_signed) // Let's not send yet another sync command when it should be man0 a man0 (at link-time etc)
		syncem(client, NULL, "+", NULL, reportItem);
}

int report_hook_serversync(Client *client) {
	Report *reportItem;

	if(!reportList)
		return HOOK_CONTINUE;

	// We need to send that shit in reverse order to make sure the highest ID gets synced last ;];]];];
	for(reportItem = reportList; reportItem->next; reportItem = reportItem->next) { } // Find the last actual entry
	for(; reportItem; reportItem = reportItem->prev) // Back it up lmao
		syncem(NULL, client, "", NULL, reportItem); // Only send to this specific serbur ;]
	return HOOK_CONTINUE;
}
