/* Copyright (C) All Rights Reserved
** Written by k4be
** Website: https://github.com/pirc-pl/unrealircd-modules/
** License: GPLv3 https://www.gnu.org/licenses/gpl-3.0.html
*/

/*** <<<MODULE MANAGER START>>>
module
{
        documentation "https://github.com/pirc-pl/unrealircd-modules/blob/master/README.md";
        troubleshooting "In case of problems, contact k4be on irc.pirc.pl.";
        min-unrealircd-version "5.*";
        post-install-text {
                "The module is installed. Now you need to add a loadmodule line:";
                "loadmodule \"third/geoip-chanban\";";
  				"And /REHASH the IRCd.";
  				"The module does not need any other configuration.";
				"Remember that you need \"geoip-base\" or \"geoip-transfer\" module installed on this server";
				"for geoip-chanban to work. See docs for more info.";
				"Detailed documentation is available on https://github.com/pirc-pl/unrealircd-modules/blob/master/README.md";
        }
}
*** <<<MODULE MANAGER END>>>
*/

#include "unrealircd.h"

ModuleHeader MOD_HEADER = {
	"third/geoip-chanban",   /* Name of module */
	"5.0", /* Version */
	"ExtBan ~C - Ban/exempt by country code", /* Short description of module */
	"k4be@PIRC",
	"unrealircd-5"
};

struct country {
	char code[10];
	char name[100];
	char continent[25];
	int id;
	struct country *next;
};

/* Forward declarations */
char *extban_geoip_conv_param(char *para);
int extban_geoip_is_banned(Client *client, Channel *channel, char *banin, int type, char **msg, char **errmsg);

/** Called upon module init */
MOD_INIT()
{
	ExtbanInfo req;
	
	MARK_AS_GLOBAL_MODULE(modinfo);
	
	req.flag = 'C';
	req.is_ok = NULL;
	req.conv_param = extban_geoip_conv_param;
	req.is_banned = extban_geoip_is_banned;
	req.options = EXTBOPT_INVEX;
	if (!ExtbanAdd(modinfo->handle, req))
	{
		config_error("%s: could not register extended ban type: %s", MOD_HEADER.name, ModuleGetErrorStr(modinfo->handle));
		return MOD_FAILED;
	}
	
	return MOD_SUCCESS;
}

/** Called upon module load */
MOD_LOAD()
{
	return MOD_SUCCESS;
}

/** Called upon unload */
MOD_UNLOAD()
{
	return MOD_SUCCESS;
}

#define COUNTRY_CODE_LENGTH 2 // all country codes length is 2 characters, notify me if this changes

char *extban_geoip_conv_param(char *para)
{
	char *mask, *code;
	static char retbuf[4+COUNTRY_CODE_LENGTH];
	int i;
	
	if(strlen(para) != 3+COUNTRY_CODE_LENGTH)
		return NULL;

	strlcpy(retbuf, para, sizeof(retbuf));

	code = retbuf+3;
	for(i=0; i<COUNTRY_CODE_LENGTH; i++){
		code[i] = toupper(code[i]);
		if(code[i] < 'A' || code[i] > 'Z')
			return NULL; // accepting only letters for country code, no more checks needed
	}


	return retbuf;
}

int extban_geoip_is_banned(Client *client, Channel *channel, char *banin, int type, char **msg, char **errmsg)
{
	struct country *curr_country;
	char *ban = banin+3;
	ModDataInfo *md = findmoddata_byname("geoip", MODDATATYPE_CLIENT);
	
	if(!md){
		sendto_realops("geoip-chanban: no ModData field, perhaps no geoip-base nor geoip-transfer module available on this server? It won't work.");
		return 0;
	}
	curr_country = moddata_client(client, md).ptr;
	if(!curr_country){
//		sendto_realops("geoip-chanban: curr_country is NULL for %s, perhaps no geoip-base module available on the network, or incomplete/outdated database?", client->name);
		return 0;
	}
	if(!strcasecmp(ban, curr_country->code))
		return 1;

	return 0;
}

