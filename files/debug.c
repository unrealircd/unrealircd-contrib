/* Copyright (C) All Rights Reserved
** Written by Gottem <support@gottem.nl>
** Website: https://gottem.nl/unreal
** License: https://gottem.nl/unreal/license
*/

/*** <<<MODULE MANAGER START>>>
module {
	documentation "https://gottem.nl/unreal/man/debug";
	troubleshooting "In case of problems, check the FAQ at https://gottem.nl/unreal/halp or e-mail me at support@gottem.nl";
	min-unrealircd-version "6.*";
	//max-unrealircd-version "6.*";
	post-install-text {
		"The module is installed, now all you need to do is add a 'loadmodule' line to your config file:";
		"loadmodule \"third/debug\";";
		"Then /rehash the IRCd.";
		"For usage information, refer to the module's documentation found at: https://gottem.nl/unreal/man/debug";
	}
}
*** <<<MODULE MANAGER END>>>
*/

// One include for all cross-platform compatibility thangs
#include "unrealircd.h"

// Command string yo
#define MSG_DBG "DBG"

// Dem macros yo
CMD_FUNC(cmd_debug); // Register command function

#define CheckAPIError(apistr, apiobj) \
	do { \
		if(!(apiobj)) { \
			config_error("A critical error occurred on %s for %s: %s", (apistr), MOD_HEADER.name, ModuleGetErrorStr(modinfo->handle)); \
			return MOD_FAILED; \
		} \
	} while(0)

// Quality fowod declarations
static void dumpit(Client *client, char **p);
void debug_opers(Client *client);
void debug_operclasses(Client *client);
int recurseOC(OperClass *oc);
int recurseOCACL(OperClassACL *acls);

extern ConfigItem_operclass *conf_operclass; // Need access to the global var conf_operclass =]
Command *debugCmd; // Pointer to the command we're gonna add

// Help string in case someone does just /DBG
static char *debughelp[] = {
	/* Special characters:
	** \002 = bold -- \x02
	** \037 = underlined -- \x1F
	*/
	"*** \002Help on /DBG\002 ***",
	"Enables you to easily view internal (configuration) data",
	" ",
	"Syntax:",
	"    \002/DBG\002 \037datatype\037 [\037server\037]",
	"        Get a list regarding \037datatype\037, optionally asking another \037server\037",
	" ",
	"Data types:",
	"    Currently supported types are: \037opers\037 and \037operclasses\037",
	" ",
	"Examples:",
	"    \002/DBG opers\002",
	"        Get a list of configured opers",
	"    \002/DBG operclasses someleaf.dom.tld\002",
	"        Asks \037someleaf.dom.tld\037 about the operclasses it knows",
	NULL
};

// Dat dere module header
ModuleHeader MOD_HEADER = {
	"third/debug", // Module name
	"2.1.1", // Version
	"Allows privileged opers to easily view internal (configuration) data", // Description
	"Gottem", // Author
	"unrealircd-6", // Modversion
};

// Initialisation routine (register hooks, commands and modes or create structs etc)
MOD_INIT() {
	CheckAPIError("CommandAdd(DBG)", CommandAdd(modinfo->handle, MSG_DBG, cmd_debug, 2, CMD_USER));

	MARK_AS_GLOBAL_MODULE(modinfo);

	conf_operclass = NULL; // Otherwise we get duplicates after every rehash lol (is safe in INIT cuz we haven't actually loaded the config yet =])
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
}

CMD_FUNC(cmd_debug) {
	// Gets args: Client *client, MessageTag *recv_mtags, int parc, char *parv[]
	if(IsUser(client) && !ValidatePermissionsForPath("debug", client, NULL, NULL, NULL)) { // Only check operprivs for persons =]
		sendnumeric(client, ERR_NOPRIVILEGES);
		return;
	}

	if(BadPtr(parv[1])) { // If first argument is a bad pointer, don't proceed
		dumpit(client, debughelp); // Return help string instead
		return;
	}

	if(!BadPtr(parv[2])) { // Second arg is optional, may be a server name
		if(hunt_server(client, NULL, "DBG", 2, parc, parv) != HUNTED_ISME) // Attempt to relay message to server
			return;
	}

	// Ayyy checkem data types lol
	if(!strcasecmp(parv[1], "opers"))
		debug_opers(client);

	else if(!strcasecmp(parv[1], "operclasses"))
		debug_operclasses(client);

	else // Le default handler faec
		sendnotice(client, "*** [debug] Unknown data type '%s'", parv[1]);
}

void debug_opers(Client *client) {
	ConfigItem_oper *oper; // To iter8 over the config items
	sendnotice(client, "*** [debug] Listing all configured \037opers\037"); // Send acknowledgement notice
	for(oper = conf_oper; oper; oper = (ConfigItem_oper *)oper->next) { // Checkem configured opers
		if(!oper->name || !oper->operclass) // Sanity check imo, need at least a name and operclass to be useful
			continue;

		char buf[BUFSIZE]; // Store the message
		const char *umodes = get_usermode_string_raw(oper->require_modes); // Converts a long to shit like +z
		int len = 0; // Keep track of the length imo

		memset(buf, '\0', BUFSIZE); // Initialise that shit tbh
		len += snprintf(buf, BUFSIZE, "*** [debug] \002%s\002 using operclass \037%s\037", oper->name, oper->operclass); // Append first part, always the same =]

		if(len < BUFSIZE && oper->snomask) // Double check for the current outgoing buffer's string length, then see if snomask contains nethang (usually doesn't)
			len += snprintf(buf + len, BUFSIZE, ", snomasks +%s", oper->snomask); // Append

		if(len < BUFSIZE && umodes[1]) // get_modestr() may return just "+", in which case there are no required modes ;]
			len += snprintf(buf + len, BUFSIZE, ", requires umodes %s", umodes); // dat

		if(len < BUFSIZE && oper->maxlogins) // Maxlogins may not be specified either
			len += snprintf(buf + len, BUFSIZE, ", allows %d login attempts", oper->maxlogins); // shit

		sendnotice(client, buf); // Now finally send 'em notice
	}
}

void debug_operclasses(Client *client) {
	ConfigItem_operclass *cfg_oc; // To iter8 over the config items
	OperClass *oc; // The actual operclass info is inside a struct in cfg_oc
	sendnotice(client, "*** [debug] Listing all configured \037operclasses\037"); // Send acknowledgement notice
	for(cfg_oc = conf_operclass; cfg_oc; cfg_oc = (ConfigItem_operclass *)cfg_oc->next) { // Checkem configured operclasses
		if(!cfg_oc->classStruct) // Sanity check imo, can't do shit without this
			continue;

		oc = cfg_oc->classStruct; // Get dat dere classStruct fam
		int privs = recurseOC(oc); // Recurse into dat operclass (to resolve the amount of parent privs too) =]
		if(oc->ISA) // ISA is the parent operclass's name (if ne)
			sendnotice(client, "*** [debug] \002%s\002 (parent: \002%s\002): found \037%d\037 privilege%s", oc->name, oc->ISA, privs, (privs == 1 ? "" : "s")); // So mention that shit
		else
			sendnotice(client, "*** [debug] \002%s\002: found \037%d\037 privilege%s", oc->name, privs, (privs == 1 ? "" : "s")); // >tfw orphan
	}
}

int recurseOC(OperClass *oc) {
	ConfigItem_operclass *cfg_oc; // find_operclass() returns a ConfigItem and not the classStruct inside it =]
	int privs = recurseOCACL(oc->acls); // Recurse into the privs for the current class first
	if(oc->ISA && (cfg_oc = find_operclass(oc->ISA)) && cfg_oc->classStruct) { // See if there's a parent, attempt to get the ConfigItem and check its sanity
		privs += recurseOC(cfg_oc->classStruct); // Then also recurse into that shit lol
	}
	return privs; // Ayyy
}

int recurseOCACL(OperClassACL *acls) {
	OperClassACL *acl; // To iter8 that shit
	int privs = 0; // Muh counter
	for(acl = acls; acl; acl = acl->next) { // Checkem
		if(!acl->acls && acl->name) { // Sanity check imo, if !acls then it's a "priv" and not "priv { something; };"
			privs++; // So increment by just one
			continue; // And go to the next one
		}
		if(acl->name) // Sanity check lol
			privs += recurseOCACL(acl->acls); // Seems to be priv { something; else; }; shit, recurse 'em (and don't count the block itself ;])
	}
	return privs; // lmao
}
