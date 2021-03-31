/*
  Licence: GPLv3
  Provides "helpop" by Valware :D
  usermode h (helpop) (settable by IRCops only)
  channelmode g (helpop-only)
  command HELPOP <string to send to other helpops>
  command REPORT <string for users to send to helpops>
  
  parts taken from unrealircd source code, and Gottem's modules and dank module templates ayyyy
  shout out to Syzop for the epic documentation of unrealircd module api :D
  my first module submission lmao
  
  this module have no configurable option
*/
/*** <<<MODULE MANAGER START>>>
module
{
        documentation "https://github.com/ValwareIRC/valware-unrealircd-mods/blob/main/helpop/README.md";
	troubleshooting "In case of problems, documentation or e-mail me at v.a.pond@outlook.com";
        min-unrealircd-version "5.*";
        // THE FOLLOWING FIELDS ARE OPTIONAL:
        // Maximum version that this module supports:
        max-unrealircd-version "5.*";
        // This text is displayed after running './unrealircd module install ...'
        // It is recommended not to make this an insane number of lines and refer to a URL instead
        // if you have lots of text/information to share:
        post-install-text {
                "The module is installed. Now all you need to do is add a loadmodule line:";
                "loadmodule \"third/helpop\";";
                "And /REHASH the IRCd.";
                "The module does not need any other configuration.";
        }
}
*** <<<MODULE MANAGER END>>>
*/
#include "unrealircd.h"

Cmode_t EXTCMODE_HELPOP;

#define HELPONLY_FLAG 'g'
#define UMODE_HELPOP 'h' 
#define IsHelpOnly(channel)    (channel->mode.extmode & EXTCMODE_HELPOP)
#define IsHelpop(x) (IsUser(x) && (x)->umodes & extumode_helpop)
#define WHOIS_HELPOP_STRING ":%s 313 %s %s :is available for help"
#define MSG_MYCMD "HELPOPER"
#define REP_MYCMD "REPORT"

CMD_FUNC(HELPOPER); // Register command function
CMD_FUNC(REPORT);

#define CheckAPIError(apistr, apiobj) \
	do { \
		if(!(apiobj)) { \
			config_error("A critical error occurred on %s for %s: %s", (apistr), MOD_HEADER.name, ModuleGetErrorStr(modinfo->handle)); \
			return MOD_FAILED; \
		} \
	} while(0)

long extumode_helpop = 0L;


ModuleHeader MOD_HEADER = {
	"third/helpop", // Module name
	"1.0", // Module Version
	"HelpOp - Provides usermode h (HelpOp) and swhois line, channelmode g (HelpOp-only room), commands /HELPOPER and /REPORT", // Description
	"Valware", // Author
	"unrealircd-5", // Unreal Version
};

int helpop_whois(Client *requester, Client *acptr);
int helponly_check (Client *client, Channel *channel, char *key, char *parv[]);

typedef struct {
	// Change this or add more variables, whatever suits you fam
	char flag;
	int p;
} aModeX;

// Initialisation routine (register hooks, commands and modes or create structs etc)
MOD_INIT() {
	CmodeInfo req;

	memset(&req, 0, sizeof(req));
	req.paracount = 0;
	req.flag = HELPONLY_FLAG;
	req.is_ok = extcmode_default_requirehalfop;
	CmodeAdd(modinfo->handle, req, &EXTCMODE_HELPOP);
	HookAdd(modinfo->handle, HOOKTYPE_CAN_JOIN, 0, helponly_check);

	CheckAPIError("UmodeAdd(extumode_helpop)", UmodeAdd(modinfo->handle, UMODE_HELPOP, UMODE_GLOBAL, 0, umode_allow_opers, &extumode_helpop));
	CheckAPIError("CommandAdd(MSG_MYCMD)", CommandAdd(modinfo->handle, MSG_MYCMD, HELPOPER, 1, CMD_SERVER | CMD_USER));
	CheckAPIError("CommandAdd(REP_MYCMD)", CommandAdd(modinfo->handle, REP_MYCMD, REPORT, 1, CMD_SERVER | CMD_USER));
	
	HookAdd(modinfo->handle, HOOKTYPE_WHOIS, 0, helpop_whois);

	MARK_AS_GLOBAL_MODULE(modinfo);
	return MOD_SUCCESS;
}


MOD_LOAD() {
	return MOD_SUCCESS;
}

// Called on unload/rehash obv
MOD_UNLOAD() {
	return MOD_SUCCESS;
}
int helponly_check (Client *client, Channel *channel, char *key, char *parv[])
{
	if (IsHelpOnly(channel) && !IsHelpop(client)) {
		sendnotice(client, "*** (%s) That room is for HelpOps only.",channel->chname);
		return ERR_NEEDREGGEDNICK;
	}
	return 0;
}

int helpop_whois(Client *requester, Client *acptr)
{
	int hideoper = (IsHideOper(acptr) && (requester != acptr) && !IsOper(requester)) ? 1 : 0;

	if (IsHelpop(acptr) && !hideoper)
		sendto_one(requester, NULL, WHOIS_HELPOP_STRING, me.name, requester->name, acptr->name);

	return 0;
}

static void dumpit(Client *client, char **p);

static char *helpop_help[] = {
	/* Special characters:
	** \002 = bold -- \x02
	** \037 = underlined -- \x1F
	*/
	"*** \002Help on /HELPOPER\002 ***",
	"DESC",
	" ",
	"Syntax:",
	"    \002/HELPOPER\002 \037<msg>\037",
	" ",
	"Examples:",
	"    \002/HELPOPER Help needed in #channel\002",
	NULL
};

static char *report_help[] = {
	/* Special characters:
	** \002 = bold -- \x02
	** \037 = underlined -- \x1F
	*/
	"*** \002Help on /REPORT\002 ***",
	"DESC",
	" ",
	"Syntax:",
	"    \002/REPORT\002 \037<msg>\037",
	" ",
	"Examples:",
	"    \002/REPORT Guest1072 is being abusive in #friendly\002",
	NULL
};

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

CMD_FUNC(REPORT) {

	if (parc < 2 || BadPtr(parv[1])) {
		dumpit(client, report_help);
		return;
	}
	else if (IsHelpop(client)) {
		sendnotice(client, "*** /REPORT is for non-helpops. Try /HELPOPER instead");
		return;
	}
	else {
		sendnotice(client, "*** Thank you. Your report has been submitted.");
		sendto_umode_global(extumode_helpop,"*** REPORT by %s: %s",client->name,parv[1]);

	}
	return;

}
	

CMD_FUNC(HELPOPER) {

	if(parc < 2 || BadPtr(parv[1])) { // If first argument is a bad pointer, don't proceed
		dumpit(client, helpop_help); // Return help string instead
		return;
	}

	else if (IsHelpop(client)) {
		sendto_umode_global(extumode_helpop,"*** HelpOper: from %s: %s",client->name,parv[1]);
	}
	else {
		sendnotice(client, "*** You may not use this command.");
		return;
	}
	return;

}
