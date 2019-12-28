/* Copyright (C) All Rights Reserved
** Written by Gottem <support@gottem.nl>
** Contains edits by k4be to implement a fake channel list
** Website: https://gottem.nl/unreal
** License: https://gottem.nl/unreal/license
*/

/*** <<<MODULE MANAGER START>>>
module {
	documentation "https://gottem.nl/unreal/man/listrestrict";
	troubleshooting "In case of problems, check the FAQ at https://gottem.nl/unreal/halp or e-mail me at support@gottem.nl";
	min-unrealircd-version "5.*";
	//max-unrealircd-version "5.*";
	post-install-text {
		"The module is installed, now all you need to do is add a 'loadmodule' line to your config file:";
		"loadmodule \"third/listrestrict\";";
		"Then /rehash the IRCd.";
		"For usage information, refer to the module's documentation found at: https://gottem.nl/unreal/man/listrestrict";
	}
}
*** <<<MODULE MANAGER END>>>
*/

// One include for all cross-platform compatibility thangs
#include "unrealircd.h"

#define MYCONF "listrestrict"
#define OVR_LIST "LIST"
#define OVR_JOIN "JOIN"

#define FCHAN_DEFUSERS 2 // Let 2 users be the default for a fake channel
#define FCHAN_DEFTOPIC "DO NOT JOIN" // Also topic

#define LR_DELAYFAIL(x) (muhDelay > 0 && (x)->local && TStime() - (x)->local->firsttime < muhDelay)
#define LR_AUTHFAIL(x) (needAuth && !IsLoggedIn((x)))

#define CheckAPIError(apistr, apiobj) \
	do { \
		if(!(apiobj)) { \
			config_error("A critical error occurred on %s for %s: %s", (apistr), MOD_HEADER.name, ModuleGetErrorStr(modinfo->handle)); \
			return MOD_FAILED; \
		} \
	} while(0)

// Big hecks go here
typedef enum {
	LRE_UNKNOWN = -1,
	LRE_ALL = 0,
	LRE_CONNECT = 1,
	LRE_AUTH = 2,
	LRE_FAKECHANS = 3,
} exceptType;

typedef struct t_restrictex restrictExcept;
struct t_restrictex {
	exceptType type;
	char *mask;
	restrictExcept *next;
};

typedef struct t_fakechans fakeChannel;
struct t_fakechans {
	char *name;
	char *topic;
	int users;
	unsigned short glinem;
	fakeChannel *next;
};

// Quality fowod declarations
void checkem_exceptions(Client *client, unsigned short *connect, unsigned short *auth, unsigned short *fakechans);
CMD_OVERRIDE_FUNC(listrestrict_overridelist);
CMD_OVERRIDE_FUNC(listrestrict_overridejoin);
int listrestrict_configtest(ConfigFile *cf, ConfigEntry *ce, int type, int *errs);
int listrestrict_configposttest(int *errs);
int listrestrict_configrun(ConfigFile *cf, ConfigEntry *ce, int type);
int listrestrict_rehash(void);

restrictExcept *exceptList = NULL; // Stores exceptions yo
fakeChannel *fakechanList = NULL; // Also fake channels
int fakechanCount = 0;
unsigned short conf_fakechans = 0;

// Deez defaults
int muhDelay = 0; // Default to off yo
unsigned short needAuth = 0; // Must be identified w/ NickServ (in addition to passing the delay check)
unsigned short fakeChans = 0; // Send fake channels list
unsigned short authIsEnough = 0; // Only NickServ auth is enough to be exempt
time_t glineTime = 86400; // Default to 1 day

// Dat dere module header
ModuleHeader MOD_HEADER = {
	"third/listrestrict", // Module name
	"2.0", // Version
	"Impose certain restrictions on /LIST usage", // Description
	"Gottem / k4be", // Author
	"unrealircd-5", // Modversion
};

// Configuration testing-related hewks go in testing phase obv
MOD_TEST() {
	// We have our own config block so we need to checkem config obv m9
	// Priorities don't really matter here
	HookAdd(modinfo->handle, HOOKTYPE_CONFIGTEST, 0, listrestrict_configtest);
	HookAdd(modinfo->handle, HOOKTYPE_CONFIGPOSTTEST, 0, listrestrict_configposttest);
	return MOD_SUCCESS;
}

// Initialisation routine (register hooks, commands and modes or create structs etc)
MOD_INIT() {
	MARK_AS_GLOBAL_MODULE(modinfo);

	HookAdd(modinfo->handle, HOOKTYPE_REHASH, 0, listrestrict_rehash);
	HookAdd(modinfo->handle, HOOKTYPE_CONFIGRUN, 0, listrestrict_configrun);
	return MOD_SUCCESS;
}

// Actually load the module here
MOD_LOAD() {
	// Add command overrides with a priority of < 0 so we run *before* set::restrict-commands ;]
	CheckAPIError("CommandOverrideAddEx(LIST)", CommandOverrideAddEx(modinfo->handle, OVR_LIST, -10, listrestrict_overridelist));
	CheckAPIError("CommandOverrideAddEx(JOIN)", CommandOverrideAddEx(modinfo->handle, OVR_JOIN, -10, listrestrict_overridejoin));
	return MOD_SUCCESS; // We good
}

// Called on unload/rehash obv
MOD_UNLOAD() {
	if(exceptList) {
		// This shit is a bit convoluted to prevent memory issues obv famalmalmalmlmalm
		restrictExcept *exEntry;
		while((exEntry = exceptList) != NULL) {
			exceptList = exceptList->next;
			safe_free(exEntry->mask);
			safe_free(exEntry);
		}
		exceptList = NULL;
	}

	if(fakechanList) {
		// This shit is a bit convoluted to prevent memory issues obv famalmalmalmlmalm
		fakeChannel *fchanEntry;
		while((fchanEntry = fakechanList) != NULL) {
			fakechanList = fakechanList->next;
			safe_free(fchanEntry->name);
			safe_free(fchanEntry->topic);
			safe_free(fchanEntry);
		}
		fakechanList = NULL;
	}
	fakechanCount = 0;

	return MOD_SUCCESS; // We good
}

void checkem_exceptions(Client *client, unsigned short *connect, unsigned short *auth, unsigned short *fakechans) {
	restrictExcept *exEntry; // For iteration yo
	for(exEntry = exceptList; exEntry; exEntry = exEntry->next) {
		if(match_simple(exEntry->mask, make_user_host(client->user->username, client->user->realhost)) || match_simple(exEntry->mask, make_user_host(client->user->username, client->ip))) {
			switch(exEntry->type) {
				case LRE_ALL:
					*connect = 1;
					*auth = 1;
					*fakechans = 1;
					break;
				case LRE_CONNECT:
					*connect = 1;
					break;
				case LRE_AUTH:
					*auth = 1;
					break;
				case LRE_FAKECHANS:
					*fakechans = 1;
					break;
				default:
					break;
			}
			// Keep checking entries to support just whitelisting 2 instead of 1 or all ;]
		}
	}
	if(authIsEnough && (*auth || IsLoggedIn(client)))
		*connect = 1;
}

// Now for the actual override
CMD_OVERRIDE_FUNC(listrestrict_overridelist) {
	// Gets args: CommandOverride *ovr, Client *client, MessageTag *recv_mtags, int parc, char *parv[]
	fakeChannel *fchanEntry; // For iteration yo
	unsigned short except_connect; // We gottem exception?
	unsigned short except_auth; // Ditt0
	unsigned short except_fakechans;
	unsigned short delayFail;
	unsigned short authFail;
	unsigned short fakechanFail;

	// Checkem exceptions bro
	except_connect = 0;
	except_auth = 0;
	except_fakechans = 0;
	if(!MyUser(client) || IsOper(client) || IsULine(client)) { // Default set lel
		CallCommandOverride(ovr, client, recv_mtags, parc, parv); // Run original function yo
		return;
	}

	// Not an oper/U-Line/server, checkem whitelist (if ne)
	checkem_exceptions(client, &except_connect, &except_auth, &except_fakechans);
	delayFail = (!except_connect && LR_DELAYFAIL(client)); // Sanity check + delay check =]
	authFail = (!except_auth && LR_AUTHFAIL(client)); // Need identified check ;];;]
	fakechanFail = (!except_fakechans && fakeChans);

	// Send fake list if necessary
	if(fakechanFail && (delayFail || authFail)) {
		sendnumeric(client, RPL_LISTSTART);
		for(fchanEntry = fakechanList; fchanEntry; fchanEntry = fchanEntry->next)
			sendnumeric(client, RPL_LIST, fchanEntry->name, fchanEntry->users, "[+ntr]", fchanEntry->topic);
		sendnumeric(client, RPL_LISTEND);
	}

	if(delayFail) {
		sendnotice(client, "You have to be connected for at least %d seconds before being able to /%s%s", muhDelay, OVR_LIST, (fakechanFail ? ", please ignore the fake output above" : ""));
		return;
	}

	if(authFail) {
		sendnotice(client, "You have to be identified with services before being able to /%s%s", OVR_LIST, (fakechanFail ? ", please ignore the fake output above" : ""));
		return;
	}

	CallCommandOverride(ovr, client, recv_mtags, parc, parv); // Run original function yo
}

CMD_OVERRIDE_FUNC(listrestrict_overridejoin) {
	// Doing the G-Line thing in an override too so we run _before_ the channel is actually created, plus we need to return FLUSH_BUFFER
	// which isn't supported by HOOKTYPE_PRE_LOCAL_JOIN and might crash shit =]
	fakeChannel *fchanEntry;
	unsigned short except_connect;
	unsigned short except_auth;
	unsigned short except_fakechans;
	unsigned short delayFail;
	unsigned short authFail;
	unsigned short fakechanFail;
	unsigned short glinem;
	char *chan, *tmp, *p; // Pointers for getting multiple channel names

	// Only act on local joins etc
	if(BadPtr(parv[1]) || !MyUser(client) || IsOper(client) || IsULine(client) || !fakeChans) {
		CallCommandOverride(ovr, client, recv_mtags, parc, parv); // Run original function yo
		return;
	}

	glinem = 0;
	tmp = strdup(parv[1]);
	fchanEntry = NULL;
	for(chan = strtoken(&p, tmp, ","); !glinem && chan; chan = strtoken(&p, NULL, ",")) {
		for(fchanEntry = fakechanList; fchanEntry; fchanEntry = fchanEntry->next) {
			if(chan && !strcasecmp(chan, fchanEntry->name)) {
				// Should only be one channel per unique name, so break regardless of gline flag
				if(fchanEntry->glinem)
					glinem = 1;
				break;
			}
		}
	}
	safe_free(tmp);

	// Check if we got an entry matching this channel AND the gline flag is enabled
	if(!fchanEntry || !glinem) {
		CallCommandOverride(ovr, client, recv_mtags, parc, parv); // Run original function yo
		return;
	}

	except_connect = 0;
	except_auth = 0;
	except_fakechans = 0;
	checkem_exceptions(client, &except_connect, &except_auth, &except_fakechans);
	delayFail = (!except_connect && LR_DELAYFAIL(client));
	authFail = (!except_auth && LR_AUTHFAIL(client));
	fakechanFail = (!except_fakechans);

	// Place ban if necessary =]
	if(fakechanFail && (delayFail || authFail)) {
		place_host_ban(client, BAN_ACT_GLINE, "Invalid channel", glineTime);
		return;
	}
	CallCommandOverride(ovr, client, recv_mtags, parc, parv); // Run original function yo
}

int listrestrict_configtest(ConfigFile *cf, ConfigEntry *ce, int type, int *errs) {
	ConfigEntry *cep, *cep2; // For looping through our bl0cc, nested
	int errors = 0; // Error count
	int i; // iter8or m8
	int have_fchanname;

	// Since we'll add a new top-level block to unrealircd.conf, need to filter on CONFIG_MAIN lmao
	if(type != CONFIG_MAIN)
		return 0; // Returning 0 means idgaf bout dis

	// Check for valid config entries first
	if(!ce || !ce->ce_varname)
		return 0;

	// If it isn't our bl0ck, idc
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

		if(!strcmp(cep->ce_varname, "connectdelay")) {
			if(!cep->ce_vardata) {
				config_error("%s:%i: %s::%s must be an integer of 10 or larger m8", cep->ce_fileptr->cf_filename, cep->ce_varlinenum, MYCONF, cep->ce_varname);
				errors++; // Increment err0r count fam
				continue; // Next iteration imo tbh
			}
			// Should be an integer yo
			for(i = 0; cep->ce_vardata[i]; i++) {
				if(!isdigit(cep->ce_vardata[i])) {
					config_error("%s:%i: %s::%s must be an integer of 10 or larger m8", cep->ce_fileptr->cf_filename, cep->ce_varlinenum, MYCONF, cep->ce_varname);
					errors++; // Increment err0r count fam
					break;
				}
			}
			if(!errors && atoi(cep->ce_vardata) < 10) {
				config_error("%s:%i: %s::%s must be an integer of 10 or larger m8", cep->ce_fileptr->cf_filename, cep->ce_varlinenum, MYCONF, cep->ce_varname);
				errors++; // Increment err0r count fam
			}
			continue;
		}

		if(!strcmp(cep->ce_varname, "needauth")) {
			if(!cep->ce_vardata || (strcmp(cep->ce_vardata, "0") && strcmp(cep->ce_vardata, "1"))) {
				config_error("%s:%i: %s::%s must be either 0 or 1 fam", cep->ce_fileptr->cf_filename, cep->ce_varlinenum, MYCONF, cep->ce_varname);
				errors++; // Increment err0r count fam
			}
			continue;
		}

		if(!strcmp(cep->ce_varname, "authisenough")) {
			if(!cep->ce_vardata || (strcmp(cep->ce_vardata, "0") && strcmp(cep->ce_vardata, "1"))) {
				config_error("%s:%i: %s::%s must be either 0 or 1 fam", cep->ce_fileptr->cf_filename, cep->ce_varlinenum, MYCONF, cep->ce_varname);
				errors++; // Increment err0r count fam
			}
			continue;
		}

		if(!strcmp(cep->ce_varname, "fakechans")) {
			if(!cep->ce_vardata || (strcmp(cep->ce_vardata, "0") && strcmp(cep->ce_vardata, "1"))) {
				config_error("%s:%i: %s::%s must be either 0 or 1 fam", cep->ce_fileptr->cf_filename, cep->ce_varlinenum, MYCONF, cep->ce_varname);
				errors++; // Increment err0r count fam
			}
			else
				conf_fakechans = atoi(cep->ce_vardata);
			continue;
		}

		if(!strcmp(cep->ce_varname, "glinetime")) {
			// Should be a time string imo (7d10s etc, or just 20)
			if(!cep->ce_vardata || config_checkval(cep->ce_vardata, CFG_TIME) <= 0) {
				config_error("%s:%i: %s::%s must be a time string like '7d10m' or simply '20'", cep->ce_fileptr->cf_filename, cep->ce_varlinenum, MYCONF, cep->ce_varname);
				errors++; // Increment err0r count fam
			}
			continue;
		}

		// Here comes a nested block =]
		if(!strcmp(cep->ce_varname, "exceptions")) {
			// Loop 'em again
			for(cep2 = cep->ce_entries; cep2; cep2 = cep2->ce_next) {
				if(!cep2->ce_varname || !cep2->ce_vardata) {
					config_error("%s:%i: blank/incomplete %s::exceptions entry", cep2->ce_fileptr->cf_filename, cep2->ce_varlinenum, MYCONF); // Rep0t error
					errors++; // Increment err0r count fam
					continue; // Next iteration imo tbh
				}

				if(strcmp(cep2->ce_varname, "all") && strcmp(cep2->ce_varname, "connect") && strcmp(cep2->ce_varname, "auth") && strcmp(cep2->ce_varname, "fakechans")) {
					config_error("%s:%i: invalid %s::exceptions type (must be one of: connect, auth, fakechans, all)", cep2->ce_fileptr->cf_filename, cep2->ce_varlinenum, MYCONF); // Rep0t error
					errors++; // Increment err0r count fam
					continue; // Next iteration imo tbh
				}

				if(match_simple("*!*@*", cep2->ce_vardata) || !match_simple("*@*", cep2->ce_vardata) || strlen(cep2->ce_vardata) < 3) {
					config_error("%s:%i: invalid %s::exceptions mask (must be of the format ident@hostip)", cep2->ce_fileptr->cf_filename, cep2->ce_varlinenum, MYCONF); // Rep0t error
					errors++; // Increment err0r count fam
					continue; // Next iteration imo tbh
				}
			}
			continue;
		}

		// Here comes another nested block =]
		if(!strcmp(cep->ce_varname, "fakechannel")) {
			have_fchanname = 0;
			for(cep2 = cep->ce_entries; cep2; cep2 = cep2->ce_next) {
				if(!cep2->ce_varname || !cep2->ce_vardata) {
					config_error("%s:%i: blank/incomplete %s::fakechannel entry", cep2->ce_fileptr->cf_filename, cep2->ce_varlinenum, MYCONF); // Rep0t error
					errors++; // Increment err0r count fam
					continue; // Next iteration imo tbh
				}

				if(strcmp(cep2->ce_varname, "name") && strcmp(cep2->ce_varname, "topic") && strcmp(cep2->ce_varname, "users") && strcmp(cep2->ce_varname, "gline")) {
					config_error("%s:%i: invalid %s::fakechannel attribute (must be one of: name, topic, users, gline)", cep2->ce_fileptr->cf_filename, cep2->ce_varlinenum, MYCONF); // Rep0t error
					errors++; // Increment err0r count fam
					continue; // Next iteration imo tbh
				}

				if(!strcmp(cep2->ce_varname, "name")) {
					have_fchanname = 1;
					if(cep2->ce_vardata[0] != '#') {
						config_error("%s:%i: invalid %s::fakechannel::%s (channel name must start with a #)", cep2->ce_fileptr->cf_filename, cep2->ce_varlinenum, MYCONF, cep2->ce_varname); // Rep0t error
						errors++;
						continue;
					}
					if(strchr(cep2->ce_vardata, ',') || strchr(cep2->ce_vardata, ' ')) {
						config_error("%s:%i: invalid %s::fakechannel::%s (contains space or comma)", cep2->ce_fileptr->cf_filename, cep2->ce_varlinenum, MYCONF, cep2->ce_varname); // Rep0t error
						errors++;
						continue;
					}
					if(strlen(cep2->ce_vardata) > CHANNELLEN) {
						config_error("%s:%i: invalid %s::fakechannel::%s (too long), max length is %i characters", cep2->ce_fileptr->cf_filename, cep2->ce_varlinenum, MYCONF, cep2->ce_varname, CHANNELLEN); // Rep0t error
						errors++;
						continue;
					}
				}

				if(!strcmp(cep2->ce_varname, "topic")) {
					if(strlen(cep2->ce_vardata) > MAXTOPICLEN) {
						config_error("%s:%i: invalid %s::fakechannel::%s (too long), absolute max length is %i characters", cep2->ce_fileptr->cf_filename, cep2->ce_varlinenum, MYCONF, cep2->ce_varname, MAXTOPICLEN); // Rep0t error
						errors++;
						continue;
					}
				}

				if(!strcmp(cep2->ce_varname, "users")) {
					if(!cep2->ce_vardata) {
						config_error("%s:%i: %s::fakechannel::%s must be an integer of 1 or larger m8", cep2->ce_fileptr->cf_filename, cep2->ce_varlinenum, MYCONF, cep2->ce_varname);
						errors++; // Increment err0r count fam
						continue;
					}
					for(i = 0; cep2->ce_vardata[i]; i++) {
						if(!isdigit(cep2->ce_vardata[i])) {
							config_error("%s:%i: %s::fakechannel::%s must be an integer of 1 or larger m8", cep2->ce_fileptr->cf_filename, cep2->ce_varlinenum, MYCONF, cep2->ce_varname);
							errors++; // Increment err0r count fam
							break;
						}
					}
					if(!errors && atoi(cep2->ce_vardata) < 1) {
						config_error("%s:%i: %s::fakechannel::%s must be an integer of 1 or larger m8", cep2->ce_fileptr->cf_filename, cep2->ce_varlinenum, MYCONF, cep2->ce_varname);
						errors++; // Increment err0r count fam
					}
					continue;
				}

				if(!strcmp(cep2->ce_varname, "gline")) {
					if(!cep2->ce_vardata || (strcmp(cep2->ce_vardata, "0") && strcmp(cep2->ce_vardata, "1"))) {
						config_error("%s:%i: %s::fakechannel::%s must be either 0 or 1 fam", cep2->ce_fileptr->cf_filename, cep2->ce_varlinenum, MYCONF, cep2->ce_varname);
						errors++; // Increment err0r count fam
					}
					continue;
				}
			}
			if(!have_fchanname) {
				config_error("%s:%i: invalid %s::fakechannel entry (must contain a channel name)", cep->ce_fileptr->cf_filename, cep->ce_varlinenum, MYCONF); // Rep0t error
				errors++;
				continue;
			}
			fakechanCount++;
			continue;
		}

		// Anything else is unknown to us =]
		config_warn("%s:%i: unknown item %s::%s", cep->ce_fileptr->cf_filename, cep->ce_varlinenum, MYCONF, cep->ce_varname); // So display just a warning
	}

	*errs = errors;
	return errors ? -1 : 1; // Returning 1 means "all good", -1 means we shat our panties
}

int listrestrict_configposttest(int *errs) {
	int errors = 0;
	if(conf_fakechans && !fakechanCount) {
		config_error("[%s] %s::fakechans was enabled but there aren't any configured channels (fakechannel {} block)", MOD_HEADER.name, MYCONF);
		errors++;
	}
	*errs = errors;
	return errors ? -1 : 1;
}

// "Run" the config (everything should be valid at this point)
int listrestrict_configrun(ConfigFile *cf, ConfigEntry *ce, int type) {
	ConfigEntry *cep, *cep2; // For looping through our bl0cc, nested
	restrictExcept *exLast = NULL; // Initialise to NULL so the loop requires minimal l0gic
	fakeChannel *fchanLast = NULL;
	restrictExcept **exEntry = &exceptList; // Hecks so the ->next chain stays intact
	fakeChannel **fchanEntry = &fakechanList;
	exceptType etype = LRE_UNKNOWN; // Just a lil' default =]
	char fchanName[BUFSIZE];
	char fchanTopic[BUFSIZE];
	int fchanUsers;
	int fchanGlinem;
	size_t tlen;
	int topiclen;

	// Since we'll add a new top-level block to unrealircd.conf, need to filter on CONFIG_MAIN lmao
	if(type != CONFIG_MAIN)
		return 0; // Returning 0 means idgaf bout dis

	// Check for valid config entries first
	if(!ce || !ce->ce_varname)
		return 0;

	// If it isn't our bl0cc, idc
	if(strcmp(ce->ce_varname, MYCONF))
		return 0;

	// Loop dat shyte fam
	for(cep = ce->ce_entries; cep; cep = cep->ce_next) {
		// Do we even have a valid name l0l?
		if(!cep->ce_varname)
			continue; // Next iteration imo tbh

		if(cep->ce_vardata && !strcmp(cep->ce_varname, "connectdelay")) {
			muhDelay = atoi(cep->ce_vardata);
			continue;
		}

		if(cep->ce_vardata && !strcmp(cep->ce_varname, "needauth")) {
			needAuth = atoi(cep->ce_vardata);
			continue;
		}

		if(cep->ce_vardata && !strcmp(cep->ce_varname, "authisenough")) {
			authIsEnough = atoi(cep->ce_vardata);
			continue;
		}

		if(cep->ce_vardata && !strcmp(cep->ce_varname, "fakechans")) {
			fakeChans = atoi(cep->ce_vardata);
			continue;
		}

		if(cep->ce_vardata && !strcmp(cep->ce_varname, "glinetime")) {
			glineTime = config_checkval(cep->ce_vardata, CFG_TIME);
			continue;
		}

		if(!strcmp(cep->ce_varname, "exceptions")) {
			// Loop 'em
			for(cep2 = cep->ce_entries; cep2; cep2 = cep2->ce_next) {
				if(!cep2->ce_varname || !cep2->ce_vardata) // Sanity checks imo
					continue; // Next iteration imo tbh

				if(!strcmp(cep2->ce_varname, "all"))
					etype = LRE_ALL;
				else if(!strcmp(cep2->ce_varname, "connect"))
					etype = LRE_CONNECT;
				else if(!strcmp(cep2->ce_varname, "auth"))
					etype = LRE_AUTH;
				else if(!strcmp(cep2->ce_varname, "fakechans"))
					etype = LRE_FAKECHANS;

				// Allocate mem0ry for the current entry
				*exEntry = safe_alloc(sizeof(restrictExcept));

				// Allocate/initialise shit here
				(*exEntry)->mask = strdup(cep2->ce_vardata);

				// Copy that shit fam
				(*exEntry)->type = etype;

				// Premium linked list fam
				if(exLast)
					exLast->next = *exEntry;

				exLast = *exEntry;
				exEntry = &(*exEntry)->next;
			}
			continue;
		}

		if(!strcmp(cep->ce_varname, "fakechannel")) {
			// Gotta reset values imo
			fchanName[0] = '\0';
			fchanTopic[0] = '\0';
			fchanUsers = 0;
			fchanGlinem = 0;

			// Loop through parameters of a single fakechan
			for(cep2 = cep->ce_entries; cep2; cep2 = cep2->ce_next) {
				if(!cep2->ce_varname || !cep2->ce_vardata) // Sanity checks imo
					continue; // Next iteration imo tbh

				if(!strcmp(cep2->ce_varname, "name")) {
					if(valid_channelname(cep2->ce_vardata)) {
						strlcpy(fchanName, cep2->ce_vardata, sizeof(fchanName));
						continue;
					}
					else {
						config_error("%s:%i: invalid %s::fakechannel::%s '%s' (not a valid channel name), skipping it", cep2->ce_fileptr->cf_filename, cep2->ce_varlinenum, MYCONF, cep2->ce_varname, cep2->ce_vardata);
						break;
					}
				}

				if(!strcmp(cep2->ce_varname, "topic")) {
					if((tlen = strlen(cep2->ce_vardata)) > 0) {
						topiclen = (iConf.topic_length > 0 ? iConf.topic_length : tempiConf.topic_length);
						if(tlen > topiclen)
							config_warn("%s:%i: %s::fakechannel::%s exceeds maximum allowed length (%d chars), truncating it", cep2->ce_fileptr->cf_filename, cep2->ce_varlinenum, MYCONF, cep2->ce_varname, topiclen);
						strlcpy(fchanTopic, cep2->ce_vardata, topiclen + 1);
					}
					continue;
				}

				if(!strcmp(cep2->ce_varname, "users")) {
					fchanUsers = atoi(cep2->ce_vardata);
					continue;
				}

				if(!strcmp(cep2->ce_varname, "gline")) {
					fchanGlinem = atoi(cep2->ce_vardata);
					continue;
				}
			}

			// Make sure we don't overallocate shit (no topic/users specified is all0wed, only name is required)
			if(!fchanName[0])
				continue;

			// Allocate mem0ry for the current entry
			*fchanEntry = safe_alloc(sizeof(fakeChannel));

			(*fchanEntry)->name = strdup(fchanName);
			(*fchanEntry)->topic = (fchanTopic[0] ? strdup(fchanTopic) : strdup(FCHAN_DEFTOPIC));
			(*fchanEntry)->users = (fchanUsers <= 0 ? FCHAN_DEFUSERS : fchanUsers);
			(*fchanEntry)->glinem = fchanGlinem;

			if(fchanLast)
				fchanLast->next = *fchanEntry;

			fchanLast = *fchanEntry;
			fchanEntry = &(*fchanEntry)->next;
			continue;
		}
	}

	return 1; // We good
}

int listrestrict_rehash(void) {
	// Reset config defaults
	muhDelay = 0;
	needAuth = 0;
	fakeChans = 0;
	authIsEnough = 0;
	glineTime = 86400;
	conf_fakechans = 0;
	return HOOK_CONTINUE;
}

