/* Copyright (C) All Rights Reserved
** Written by Gottem <support@gottem.nl>
** Website: https://gottem.nl/unreal
** License: https://gottem.nl/unreal/license
*/

/*** <<<MODULE MANAGER START>>>
module {
	documentation "https://gottem.nl/unreal/man/fantasy";
	troubleshooting "In case of problems, check the FAQ at https://gottem.nl/unreal/halp or e-mail me at support@gottem.nl";
	min-unrealircd-version "5.*";
	//max-unrealircd-version "5.*";
	post-install-text {
		"The module is installed, now all you need to do is add a 'loadmodule' line to your config file:";
		"loadmodule \"third/fantasy\";";
		"Then /rehash the IRCd.";
		"For usage information, refer to the module's documentation found at: https://gottem.nl/unreal/man/fantasy";
	}
}
*** <<<MODULE MANAGER END>>>
*/

// One include for all cross-platform compatibility thangs
#include "unrealircd.h"

// Since v5.0.5 some hooks now include a SendType
#define BACKPORT_HOOK_SENDTYPE (UNREAL_VERSION_GENERATION == 5 && UNREAL_VERSION_MAJOR == 0 && UNREAL_VERSION_MINOR < 5)

// Config block
#define MYCONF "fantasy"

#define free_args(args, count) do { \
		for(i = 0; i < count && !BadPtr(args[i]); i++) \
			safe_free(args[i]); \
	} while(0)

// Big hecks go here
typedef struct t_fantasy fantasyCmd;

struct t_fantasy {
	char *alias;
	char *cmdstr;
	fantasyCmd *next;
};

// Quality fowod declarations
char *replaceem(char *str, char *search, char *replace);
char *recurseArg(Client *client, Channel *channel, int index, char *var, int parc, char *parv[], char *cmdv[], char *multidelim);
int fixSpecialVars(char *cmdv[], int i, Client *client, Channel *channel, int parc, char *parv[], char *multidelim);
int fantasy_configtest(ConfigFile *cf, ConfigEntry *ce, int type, int *errs);
int fantasy_configposttest(int *errs);
int fantasy_configrun(ConfigFile *cf, ConfigEntry *ce, int type);
int fantasy_rehash(void);

#if BACKPORT_HOOK_SENDTYPE
	int fantasy_chanmsg(Client *client, Channel *channel, int sendflags, int prefix, char *targetstr, MessageTag *recv_mtags, char *text, int notice);
#else
	int fantasy_chanmsg(Client *client, Channel *channel, int sendflags, int prefix, char *targetstr, MessageTag *recv_mtags, char *text, SendType sendtype);
#endif

fantasyCmd *fantasyList = NULL; // Store fantasy aliases lol
int fantasyCount = 0; // Keep trakk of count lol
char cmdChar = '!'; // Pick between shit like !cmd, `cmd, etc
char svartypes[] = { '-', 'i', 'h', '*', 0 };

// Dat dere module header
ModuleHeader MOD_HEADER = {
	"third/fantasy", // Module name
	"2.0.3", // Version
	"Implements custom fantasy channel !cmds", // Description
	"Gottem", // Author
	"unrealircd-5", // Modversion
};

// Configuration testing-related hewks go in testing phase obv
// This function is entirely optional
MOD_TEST() {
	// We have our own config block so we need to checkem config obv m9
	// Priorities don't really matter here
	HookAdd(modinfo->handle, HOOKTYPE_CONFIGTEST, 0, fantasy_configtest);
	HookAdd(modinfo->handle, HOOKTYPE_CONFIGPOSTTEST, 0, fantasy_configposttest);
	return MOD_SUCCESS;
}

// Initialisation routine (register hooks, commands and modes or create structs etc)
MOD_INIT() {
	MARK_AS_GLOBAL_MODULE(modinfo);

	HookAdd(modinfo->handle, HOOKTYPE_REHASH, 0, fantasy_rehash);
	HookAdd(modinfo->handle, HOOKTYPE_CONFIGRUN, 0, fantasy_configrun);
	HookAdd(modinfo->handle, HOOKTYPE_CHANMSG, -100, fantasy_chanmsg); // High prio hook just in case lol
	return MOD_SUCCESS;
}

MOD_LOAD() {
	return MOD_SUCCESS; // We good
}

// Called on unload/rehash obv
MOD_UNLOAD() {
	// Clean up any structs and other shit
	if(fantasyList) {
		// This shit is a bit convoluted to prevent memory issues obv famalmalmalmlmalm
		fantasyCmd *fCmd;
		while((fCmd = fantasyList) != NULL) {
			fantasyList = fantasyList->next;
			safe_free(fCmd->alias);
			safe_free(fCmd->cmdstr);
			safe_free(fCmd);
		}
		fantasyList = NULL;
	}
	fantasyCount = 0; // Just to maek shur
	return MOD_SUCCESS; // We good
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

// Fix up a cmdv[] entry in case of multiple targets
char *recurseArg(Client *client, Channel *channel, int index, char *var, int parc, char *parv[], char *cmdv[], char *multidelim) {
	int i; // Iterat0r
	Client *acptr; // Target client pointer, if ne
	char *ret; // Return value, NULL indicates not found
	int hostbit; // Get only a hostname
	int identbit; // Get only an ident
	char *p; // Temp pointer =]
	static char arglist[512]; // Store all nicks/other arguments
	size_t varlen;
	int use_default;

	// Check for special vars $1h and $1i
	hostbit = 0;
	identbit = 0;
	varlen = strlen(var);

	// Can't really happen but let's just =]
	if(varlen <= 1 || !parc || !parv[0])
		return NULL;

	if(varlen > 2) {
		if(var[2] == 'h')
			hostbit = 1;
		else if(var[2] == 'i')
			identbit = 1;
	}

	ret = NULL;

	// Did we even get multiple args from the user?
	if(parc > index + 1) {
		memset(arglist, '\0', sizeof(arglist));

		for(i = index; i < parc && parv[i]; i++) {
			// Maybe we reached the end of buffer, in which case further iterations don't make sense ;]
			// Doing this check at the beginning because the previous loop might have caused it to be exactly 512 bytes long, this way we can properly detect when it would be truncated =]
			if(strlen(arglist) + 1 == sizeof(arglist)) {
				// The arglist here will be truncated by Unreal anyways, but it might give people just enough information to fix/work around it
				sendto_realops("[fantasy] The alias '%s' resolved to a command that was too long (> 512 bytes/characters): %s", parv[0], arglist);
				break;
			}

			use_default = 1;

			if((acptr = find_person(parv[i], NULL))) {
				if(hostbit) {
					p = GetHost(acptr);
					use_default = 0;
				}

				else if(identbit) {
					p = acptr->user->username;
					use_default = 0;
				}
			}

			if(use_default)
				p = parv[i];

			// On the first pass we need to do *cpy()
			if(i == index)
				strlcpy(arglist, p, sizeof(arglist));

			// Then do *cat() ;]
			else {
				strlcat(arglist, multidelim, sizeof(arglist)); // Can either be a space or comma
				strlcat(arglist, p, sizeof(arglist)); // Append the actual arg lol
			}

			// If this is not a "greedy" var ($1-, $2-), gtfo
			if(var[varlen - 1] != '-' || varlen > 3)
				break;
		}

		if(arglist[0])
			ret = arglist; // Set return value
	}

	// No or just one arg, maybe default to the user itself
	else {
		// If we got exactly one arg, check for *!*@host etc
		if(index < parc && parv[index]) {
			use_default = 1;

			if((acptr = find_person(parv[index], NULL))) {
				if(hostbit) {
					ret = GetHost(acptr);
					use_default = 0;
				}

				else if(identbit) {
					ret = acptr->user->username;
					use_default = 0;
				}
			}

			if(use_default) {
				strlcpy(arglist, parv[index], sizeof(arglist)); // Use as-is
				ret = arglist;
			}
		}

		// No arg lol
		else {
			use_default = 1;

			if(index == 1) {
				if(hostbit) {
					ret = GetHost(client);
					use_default = 0;
				}
				else if(identbit) {
					ret = client->user->username;
					use_default = 0;
				}
			}

			if(use_default)
				ret = client->name;
		}
	}

	// If no ret value found, emit an in-channel warning for the user only
	if(!ret)
		sendto_one(client, NULL, ":%s NOTICE %s :Missing argument #%d for %c%s", me.name, channel->chname, index, cmdChar, parv[0]);

	return ret;
}

int fixSpecialVars(char *cmdv[], int i, Client *client, Channel *channel, int parc, char *parv[], char *multidelim) {
	int numvar; // 1 thru 9 iter80r
	int svari; // Index of svartype
	int stoppem = 0; // Break out of the lewps
	char svar[8]; // To store special variable names ($1h)
	char svarmask[8]; // To store special variable names' masks (*$1*)
	char *recTemp; // Temp shit for recurseArg =]
	char *multitmp; // After replacement

	// Channel var y0
	if(match_simple("*$chan*", cmdv[i])) {
		multitmp = replaceem(cmdv[i], "$chan", channel->chname);
		safe_strdup(cmdv[i], multitmp); // Dup it agen
		safe_free(multitmp);
	}

	for(numvar = 1; !stoppem && numvar <= 9; numvar++) {
		// Fix the greedy vars first ($1-)
		// Then check w/e is left of the singular ones ($1i, $1h), recurseArg() stops after one iteration for these ;]
		for(svari = 0; svartypes[svari]; svari++) {
			if(svartypes[svari] == '*')
				snprintf(svar, sizeof(svar), "$%d", numvar);
			else
				snprintf(svar, sizeof(svar), "$%d%c", numvar, svartypes[svari]);

			snprintf(svarmask, sizeof(svarmask), "*$%d%c*", numvar, svartypes[svari]);

			if(match_simple(svarmask, cmdv[i])) {
				// recurseArg() returns NULL on error, so let's free our shit if that happens and go to the next fantasyCmd =]
				if((recTemp = recurseArg(client, channel, numvar, svar, parc, parv, cmdv, multidelim)) == NULL) {
					stoppem = 1;
					break;
				}

				multitmp = replaceem(cmdv[i], svar, recTemp); // Replace all these special variables =]
				safe_strdup(cmdv[i], (multitmp ? multitmp : recTemp)); // Dup it agen
				safe_free(multitmp);
			}
		}
	}
	return stoppem;
}

int fantasy_configtest(ConfigFile *cf, ConfigEntry *ce, int type, int *errs) {
	int errors = 0; // Error count
	int i; // Iterat0r
	int found; // How many greedy vars we g0t
	char svarmask[8]; // To store special variable names' masks (*$1-*)
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
		found = 0;
		// Do we even have a valid pair l0l?
		if(!cep->ce_varname || !cep->ce_vardata) {
			config_error("%s:%i: blank %s item", cep->ce_fileptr->cf_filename, cep->ce_varlinenum, MYCONF); // Rep0t error
			errors++; // Increment err0r count fam
			continue; // Next iteration imo tbh
		}

		if(!strcmp(cep->ce_varname, "cmdchar")) {
			if(strlen(cep->ce_vardata) != 1 || cep->ce_vardata[0] == '/') {
				config_error("%s:%i: %s::%s must be exactly one character in length and cannot be '/'", cep->ce_fileptr->cf_filename, cep->ce_varlinenum, MYCONF, cep->ce_varname); // Rep0t error
				errors++; // Increment err0r count fam
				continue;
			}
			continue;
		}

		for(i = 1; i <= 9; i++) {
			snprintf(svarmask, sizeof(svarmask), "*$%d-*", i);
			if(match_simple(svarmask, cep->ce_vardata))
				found++;
		}

		if(found > 1) {
			config_error("%s:%i: you can't use multiple greedy variables (like $1-) for %s::%s", cep->ce_fileptr->cf_filename, cep->ce_varlinenum, MYCONF, cep->ce_varname); // Rep0t error
			errors++; // Increment err0r count fam
			continue; // Next iteration imo tbh
		}

		fantasyCount++;
	}

	*errs = errors;
	// Returning 1 means "all good", -1 means we shat our panties
	return errors ? -1 : 1;
}

// Post test, check for missing shit here
int fantasy_configposttest(int *errs) {
	// Let's croak when there are no items in our block, even though the module was loaded
	if(!fantasyCount)
		config_warn("Module %s was loaded but the %s { } block contains no (valid) aliases/commands", MOD_HEADER.name, MYCONF);
	return 1;
}

// "Run" the config (everything should be valid at this point)
int fantasy_configrun(ConfigFile *cf, ConfigEntry *ce, int type) {
	ConfigEntry *cep; // To store the current variable/value pair etc
	fantasyCmd *last = NULL; // Initialise to NULL so the loop requires minimal l0gic
	fantasyCmd **fCmd = &fantasyList; // Hecks so the ->next chain stays intact

	// Since we'll add a top-level block to unrealircd.conf, need to filter on CONFIG_MAIN lmao
	if(type != CONFIG_MAIN)
		return 0; // Returning 0 means idgaf bout dis

	// Check for valid config entries first
	if(!ce || !ce->ce_varname)
		return 0;

	// If it isn't fantasy, idc
	if(strcmp(ce->ce_varname, MYCONF))
		return 0;

		// Loop dat shyte fam
	for(cep = ce->ce_entries; cep; cep = cep->ce_next) {
		// Do we even have a valid name l0l?
		if(!cep->ce_varname || !cep->ce_vardata)
			continue; // Next iteration imo tbh

		if(!strcmp(cep->ce_varname, "cmdchar")) {
			cmdChar = cep->ce_vardata[0];
			continue;
		}

		// Lengths to alloc8 the struct vars with in a bit
		size_t aliaslen = sizeof(char) * (strlen(cep->ce_varname) + 1);
		size_t cmdlen = sizeof(char) * (strlen(cep->ce_vardata) + 1);

		// Allocate mem0ry for the current entry
		*fCmd = safe_alloc(sizeof(fantasyCmd));

		// Allocate/initialise shit here
		(*fCmd)->alias = safe_alloc(aliaslen);
		(*fCmd)->cmdstr = safe_alloc(cmdlen);

		// Copy that shit fam
		strncpy((*fCmd)->alias, cep->ce_varname, aliaslen);
		strncpy((*fCmd)->cmdstr, cep->ce_vardata, cmdlen);

		// Premium linked list fam
		if(last)
			last->next = *fCmd;

		last = *fCmd;
		fCmd = &(*fCmd)->next;
	}

	return 1; // We good
}

int fantasy_rehash(void) {
	// Reset defaults
	cmdChar = '!';
	return HOOK_CONTINUE;
}

#if BACKPORT_HOOK_SENDTYPE
	int fantasy_chanmsg(Client *client, Channel *channel, int sendflags, int prefix, char *targetstr, MessageTag *recv_mtags, char *text, int notice) {
		if(notice)
			return HOOK_CONTINUE; // Just process the next hewk lol
#else
	int fantasy_chanmsg(Client *client, Channel *channel, int sendflags, int prefix, char *targetstr, MessageTag *recv_mtags, char *text, SendType sendtype) {
		if(sendtype != SEND_TYPE_PRIVMSG)
			return HOOK_CONTINUE;
#endif

	// Checkem privs lol
	if(!client || !MyUser(client) || (!is_chanowner(client, channel) && !is_chanadmin(client, channel) && !IsOper(client)))
		return HOOK_CONTINUE;

	// Jus checkin for empty/non-fantasy messages, also ignore "!!fjert" etc
	if(!text || strlen(text) <= 1 || text[0] != cmdChar || text[1] == cmdChar)
		return HOOK_CONTINUE;

	fantasyCmd *fCmd = NULL; // iter8or lol
	char *p, *p2, *p3; // For tokenising dat shit
	char *ttemp; // Let's keep char *text intact yo
	char *parv[MAXPARA + 1]; // Muh array
	int parc; // Arg counter
	int i, j; // Quality iterators

	char *cmdtemp; // Let's keep fCmd->cmdstr intact yo
	char *cmdv[MAXPARA + 1]; // Muh array
	int cmdc; // Arg counter
	char *multidelim; // In case of multiple nicks, wat delimiter to use
	int gotmode; // Got MODE?
	int gotkick; // Got KICK?
	int multikick; // Allow multikicks?
	int passem; // To pass over other do_cmd() calls ;]
	int stoppem; // Pass over do_cmd() but still free our shit
	char *multitmp; // Temp shit for multi-target stuff
	char *cmd; // To strip the leading ! w/o causing mem issues xd
	size_t cmdlen;

	// Gonna split ur shit into werds
	parc = 0;
	ttemp = strdup(text);
	p = strtok(ttemp, " \t"); // Split on whitespace 0bv
	while(p != NULL && strlen(p) && parc < (MAXPARA - 1)) { // While we g0t shit
		parv[parc++] = strdup(p); // Dup 'em
		p = strtok(NULL, " \t"); // Next t0ken
	}
	parv[parc] = NULL;
	safe_free(ttemp);

	// Double check imo tbh
	cmdlen = strlen(parv[0]);
	if(!parv[0] || cmdlen <= 1 || parv[0][0] != cmdChar) {
		free_args(parv, parc);
		return HOOK_CONTINUE; // Just process the next hewk yo
	}

	// Lowercase the command only =]
	cmd = safe_alloc(cmdlen + 1);
	for(i = 0, j = 1; i < cmdlen; i++, j++)
		cmd[i] = tolower(parv[0][j]);
	cmd[i] = '\0'; // Required since we shifted to da left

	// Checkem entries
	for(fCmd = fantasyList; fCmd; fCmd = fCmd->next) {
		// First word should match the command obv
		if(!strcmp(cmd, fCmd->alias)) {
			// Initialise all that shit
			cmdtemp = strdup(fCmd->cmdstr);
			cmdc = 0;
			multidelim = " ";
			gotmode = 0;
			gotkick = 0;
			multikick = 0;
			passem = 0;
			stoppem = 0;
			multitmp = NULL;
			p2 = NULL;
			p3 = NULL;

			// Now split the cmdstr into words as well =]
			p2 = strtok(cmdtemp, " \t");
			while(p2 != NULL && strlen(p2)) {
				if(cmdc == MAXPARA - 1)
					break;

				// Dirty hack for shit like kick messages, as that's like :this is a kick message
				// This should go into one cmdv[] for do_cmd() in ein bit
				// It's also always the last argument ;]
				if(p2[0] == ':') {
					p2 = fCmd->cmdstr; // Let's point to the full command string =]
					while(*p2) { // While not 0 etc
						p3 = p2; // Keep track of original pointur
						p2++; // Increment ours
						if(*p2 == ':' && *p3 == ' ') { // Find shit like " :this is a kick message"
							p2++; // Go past the colon imo tbh fam
							break; // Gtfo
						}
					}
					if(*p2) // If the loop above didn't shit itself
						cmdv[cmdc++] = strdup(p2); // Dup 'em
					break; // Gtfo always =]
				}

				cmdv[cmdc++] = strdup(p2); // Dup 'em
				p2 = strtok(NULL, " \t"); // Get next t0ken
			}
			cmdv[cmdc] = NULL;
			safe_free(cmdtemp);

			// Just a check lol, shouldn't happen cuz muh configtest() etc
			if(!cmdv[0]) {
				sendto_realops("[fantasy] The alias '%s' is configured incorrectly: seems to be entirely empty", fCmd->alias);
				continue;
			}

			// Uppercase the command only =]
			for(i = 0; i < strlen(cmdv[0]); i++)
				cmdv[0][i] = toupper(cmdv[0][i]);

			// MODE is sorta special yo
			if((gotmode = (!strcmp(cmdv[0], "MODE")))) {
				// Check sanity of dem arguments yo
				if(!cmdv[1] || !cmdv[2]) {
					sendto_realops("[fantasy] The alias '%s' is configured incorrectly: missing arguments (channel and mode flag(s))", fCmd->alias);
					// Gotta free em
					free_args(cmdv, cmdc);
					continue;
				}

				if(cmdv[2][0] != '+' && cmdv[2][0] != '-') {
					sendto_realops("[fantasy] The alias '%s' is configured incorrectly: invalid mode direction, must be either + or -", fCmd->alias);
					// Gotta free em
					free_args(cmdv, cmdc);
					continue;
				}

				if(!isalpha(cmdv[2][1])) {
					sendto_realops("[fantasy] The alias '%s' is configured incorrectly: invalid mode flag, must be an alphabetic character", fCmd->alias);
					// Gotta free em
					free_args(cmdv, cmdc);
					continue;
				}
			}

			// Checkem KICK, multiple ones are temporarily delimited by a special char
			else if((gotkick = (!strcmp(cmdv[0], "KICK"))))
				multidelim = ",";

			// If we got a kick and the cmdstr contains $1 or $2- etc in the kick message bit, change delimiter back
			if(gotkick && cmdv[3] && match_simple("$?*", cmdv[3]))
				multidelim = " ";

			// If we got een kick but contains $1- in the nick bit, allow multikick
			if(gotkick && cmdv[2] && match_simple("$?-", cmdv[2]))
				multikick = 1;

			// Fix up dynamic variables lol
			for(i = 1; !stoppem && i < cmdc && !BadPtr(cmdv[i]); i++)
				stoppem = fixSpecialVars(cmdv, i, client, channel, parc, parv, multidelim);

			// If this is a KICK and multikick is allowed (see a bit above), fix the targets
			// The comma is a special temporary delimiter that facilit88 this bs m8
			if(!stoppem && gotkick && multikick && cmdc >= 3 && cmdv[2] && strchr(cmdv[2], ',')) {
				multitmp = strdup(cmdv[2]); // Second arg is the "nick1,nick2" bit
				p3 = strtok(multitmp, ","); // Now tokenise on the premium comma
				while(p3 != NULL) {
					safe_strdup(cmdv[2], p3); // Dup 'em lol
					p3 = strtok(NULL, ","); // Next target
					do_cmd(client, NULL, cmdv[0], cmdc, cmdv); // It's required to send one do_cmd() for every target
				}
				passem = 1; // Skip the do_cmd() below ;]
				safe_free(multitmp);
			}

			// do_cmd() sends the actual command on behalf of the user, so it takes care of permissions by itself ;]
			if(!stoppem && !passem) {
				do_cmd(client, NULL, cmdv[0], cmdc, cmdv);
				passem = 1;
			}

			// Free our shit lol
			free_args(cmdv, cmdc);
		}
	}

	// Free the remaining stuff
	safe_free(cmd);
	free_args(parv, parc);
	return HOOK_CONTINUE; // Just process the next hewk lol
}
