/*
  Licence: GPLv3
*/
/*** <<<MODULE MANAGER START>>>
module
{
        documentation "https://github.com/ValwareIRC/valware-unrealircd-mods/blob/main/webirconly/README.md";
		troubleshooting "In case of problems e-mail me at v.a.pond@outlook.com";
        min-unrealircd-version "5.*";
        max-unrealircd-version "5.*";
        post-install-text {
                "The module is installed. Now all you need to do is add a loadmodule line:";
                "loadmodule \"third/webirconly\";";
                "And /REHASH the IRCd.";
                "The module does not need any other configuration.";
        }
}
*** <<<MODULE MANAGER END>>>
*/
#include "unrealircd.h"

Cmode_t EXTCMODE_WEBONLY;

#define WEBIRCONLY_FLAG 'W'

#define IsWebircOnly(channel)    (channel->mode.extmode & EXTCMODE_WEBONLY)
#define IsWebircUser(x) (IsUser(x) && webircchecklol(x))

#define CheckAPIError(apistr, apiobj) \
	do { \
		if(!(apiobj)) { \
			config_error("A critical error occurred on %s for %s: %s", (apistr), MOD_HEADER.name, ModuleGetErrorStr(modinfo->handle)); \
			return MOD_FAILED; \
		} \
	} while(0)



static int webircchecklol(Client *cptr);

ModuleHeader MOD_HEADER = {
	"third/webirconly", // Module name
	"1.0", // Module Version
	"WebIRC Only - Provides channelmode W (webirc only channel)", // Description
	"Valware", // Author
	"unrealircd-5", // Unreal Version
};

int webirconly_check (Client *client, Channel *channel, char *key, char *parv[]);


// Initialisation routine (register hooks, commands and modes or create structs etc)
MOD_INIT() {
	CmodeInfo req;

	memset(&req, 0, sizeof(req));
	req.paracount = 0;
	req.flag = WEBIRCONLY_FLAG;
	req.is_ok = extcmode_default_requirehalfop;
	CheckAPIError("CmodeAdd(EXTCMODE_WEBONLY)",CmodeAdd(modinfo->handle, req, &EXTCMODE_WEBONLY));
	HookAdd(modinfo->handle, HOOKTYPE_CAN_JOIN, 0, webirconly_check);
	

	MARK_AS_GLOBAL_MODULE(modinfo);
	return MOD_SUCCESS;
}


MOD_LOAD() {
	return MOD_SUCCESS;
}

MOD_UNLOAD() {
	return MOD_SUCCESS;
}
int webirconly_check (Client *client, Channel *channel, char *key, char *parv[])
{
	
	if ((IsWebircOnly(channel) && !IsWebircUser(client)) && !IsOper(client)) {
		sendnumeric(client, ERR_FORBIDDENCHANNEL, channel->chname, "This channel is for web users only.");
		return ERR_NEEDREGGEDNICK;
	}
	return 0;
}

// copied and edited from third/showwwebirc by k4be lol
static int webircchecklol(Client *cptr) {
	ModDataInfo *moddata;
	moddata = findmoddata_byname("webirc", MODDATATYPE_CLIENT);
	if(moddata == NULL) { return 0; }
	if(moddata_client(cptr, moddata).l){ return 1; }
	return 0;
}

