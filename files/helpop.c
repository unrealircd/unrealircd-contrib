/*
  Licence: GPLv3
  Helpop
  usermode h (helpop) (settable by IRCops only)
  channelmode g (helpop-only)
  command HELPOPS <string to send to other helpops>
  
  this module have no configurable option
*/
/*** <<<MODULE MANAGER START>>>
module
{
        documentation "https://github.com/ValwareIRC/valware-unrealircd-mods/blob/main/helpop/README.md";
	troubleshooting "In case of problems, documentation or e-mail me at v.a.pond@outlook.com";
        min-unrealircd-version "6.*";
        max-unrealircd-version "6.*";
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
long extumode_helpop = 0L;
#define HELPONLY_FLAG 'g'
#define UMODE_HELPOP 'h' 
#define IsHelpOnly(channel)    (channel->mode.mode & EXTCMODE_HELPOP)
#define IsHelpop(x) (IsUser(x) && (x)->umodes & extumode_helpop)
#define MSG_MYCMD "HELPOPS"

CMD_FUNC(HELPOPS); // Register command function
CMD_FUNC(REPORT);

#define CheckAPIError(apistr, apiobj) \
	do { \
		if(!(apiobj)) { \
			config_error("A critical error occurred on %s for %s: %s", (apistr), MOD_HEADER.name, ModuleGetErrorStr(modinfo->handle)); \
			return MOD_FAILED; \
		} \
	} while(0)



ModuleHeader MOD_HEADER = {
	"third/helpop", // Module name
	"1.2", // Module Version
	"HelpOp - Provides usermode h (HelpOp) and swhois line, channelmode g (HelpOp-only room), and command /HELPOPS", // Description
	"Valware", // Author
	"unrealircd-6", // Unreal Version
};

int helpop_whois(Client *requester, Client *acptr, NameValuePrioList **list);
int helponly_check (Client *client, Channel *channel, const char *key, char **errmsg);

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
	req.letter = HELPONLY_FLAG;
	req.is_ok = extcmode_default_requirehalfop;
	CmodeAdd(modinfo->handle, req, &EXTCMODE_HELPOP);
	HookAdd(modinfo->handle, HOOKTYPE_CAN_JOIN, 0, helponly_check);

	CheckAPIError("UmodeAdd(extumode_helpop)", UmodeAdd(modinfo->handle, UMODE_HELPOP, UMODE_GLOBAL, 0, umode_allow_opers, &extumode_helpop));
	CheckAPIError("CommandAdd(MSG_MYCMD)", CommandAdd(modinfo->handle, MSG_MYCMD, HELPOPS, 1, CMD_SERVER | CMD_USER));
	
	HookAdd(modinfo->handle, HOOKTYPE_WHOIS, 0, helpop_whois);

	MARK_AS_GLOBAL_MODULE(modinfo);
	return MOD_SUCCESS;
}


MOD_LOAD() {
	return MOD_SUCCESS;
}

MOD_UNLOAD() {
	return MOD_SUCCESS;
}
int helponly_check (Client *client, Channel *channel, const char *key, char **errmsg)
{
	if (IsHelpOnly(channel) && !IsHelpop(client)) {
		
		*errmsg = ":This channel is for Helpops only (%s)", channel->name, channel->name;
		return ERR_NOPRIVILEGES;
	}
	return 0;
}

int helpop_whois(Client *requester, Client *acptr, NameValuePrioList **list)
{
	char buf[512];
	int hideoper = (IsHideOper(acptr) && (requester != acptr) && !IsOper(requester)) ? 1 : 0;

	if (hideoper)
		return 0;
	if (IsHelpop(requester))
		add_nvplist_numeric_fmt(list, 0, "helpop", requester, 320, "%s :is available for help", acptr->name);
	
	return 0;
	
}

static void dumpit(Client *client, char **p);

static char *helpop_help[] = {
	/* Special characters:
	** \002 = bold -- \x02
	** \037 = underlined -- \x1F
	*/
	"*** \002Help on /HELPOPS\002 ***",
	"DESC",
	" ",
	"Syntax:",
	"    \002/HELPOPS\002 \037<msg>\037",
	" ",
	"Examples:",
	"    \002/HELPOPS Help needed in #channel\002",
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
	add_fake_lag(client, 8000);
}


CMD_FUNC(HELPOPS) {

	if(parc < 2 || BadPtr(parv[1])) { // If first argument is a bad pointer, don't proceed
		dumpit(client, helpop_help); // Return help string instead
		return;
	}
	else if (!IsHelpop(client)) {
		sendnumeric(client, ERR_NOPRIVILEGES);
		return;
	}
	sendto_umode_global(extumode_helpop,"(HelpOps) from %s: %s",client->name,parv[1]);

}
