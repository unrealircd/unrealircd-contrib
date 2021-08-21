/* Copyright (C) All Rights Reserved
** Written by k4be
** Website: https://github.com/pirc-pl/unrealircd-modules/
** License: GPLv3 https://www.gnu.org/licenses/gpl-3.0.html
*/

/*** <<<MODULE MANAGER START>>>
module
{
	documentation "https://github.com/pirc-pl/unrealircd-modules/blob/master/README.md#geoip-connect-notice";
	troubleshooting "In case of problems, contact k4be on irc.pirc.pl.";
	min-unrealircd-version "5.*";
	post-install-text {
		"The module is installed. Now all you need to do is add a loadmodule line:";
		"loadmodule \"third/geoip-connect-notice\";";
		"And /REHASH the IRCd.";
		"Remember that you need a correctly configured \"geoip-base\" or \"geoip-transfer\"";
		"module installed on this server for geoip-connect-notice to work.";
	}
}
*** <<<MODULE MANAGER END>>>
*/

#include "unrealircd.h"

#define BUFLEN 8191

struct country {
	char code[10];
	char name[100];
	char continent[25];
	int id;
	struct country *next;
};

int suppress_null_warning = 0;
ModuleInfo *geoip_modinfo;
ModDataInfo *geoipcnMD;

// function declarations here
static int geoip_connectnotice_userconnect(Client *);
static char *get_country_text(Client *);
EVENT(allow_next_warning_evt);
CMD_OVERRIDE_FUNC(geoip_connectnotice_overridemd);

ModuleHeader MOD_HEADER = {
	"third/geoip-connect-notice",   /* Name of module */
	"5.0.3", /* Version */
	"Notify opers about user's country", /* Short description of module */
	"k4be@PIRC",
	"unrealircd-5"
};

#define WARNING_SUPPRESS_TIME 24 // hours

static char *get_country_text(Client *cptr){
	static char buf[BUFLEN];
	struct country *curr_country;
	
	if(!cptr) return NULL;
	if(!cptr->ip) return NULL;
	
	// detecting local / private IP addresses so we don't send unneeded warnings
	unsigned int e[8];
	if(cptr->ip && strchr(cptr->ip, '.')){ // ipv4
		if(sscanf(cptr->ip, "%u.%u.%u.%u", &e[0], &e[1], &e[2], &e[3]) == 4){
			if(e[0] == 127 || e[0] == 10 || (e[0] == 192 && e[1] == 168) || (e[0] == 172 && e[1] >= 16 && e[2] <= 31)) return NULL;
		}
	} else if(cptr->ip && strchr(cptr->ip, ':')){ // ipv6
		if(sscanf(cptr->ip, "%x:%x:%x:%x:%x:%x:%x:%x", &e[0], &e[1], &e[2], &e[3], &e[4], &e[5], &e[6], &e[7]) == 8){
			if(e[0] == 0xfd00 || (e[0] == 0 && e[1] == 0 && e[2] == 0 && e[3] == 0 && e[4] == 0 && e[5] == 0 && e[6] == 0 && e[7] == 1)) return NULL;
		}
	}
	
	ModDataInfo *md = findmoddata_byname("geoip", MODDATATYPE_CLIENT);
	
	if(!md){
		sendto_realops("geoip-whois: no ModData field, perhaps no geoip-base nor geoip-transfer module available on this server? It won't work.");
		return NULL;
	}
	
	if(IsULine(cptr)) return NULL; // service bots won't have the valid geoip location anyway, hence we suppress the error message
	
	curr_country = moddata_client(cptr, md).ptr;
	if(!curr_country){
		if(!suppress_null_warning){
			sendto_realops("geoip-connect-notice: curr_country is NULL for %s (%s), perhaps no geoip-base module available on this server, or incomplete/outdated database?", cptr->name, GetIP(cptr));
			sendto_realops("geoip-connect-notice: Please note that the warning won't reappear for the next %d hours.", WARNING_SUPPRESS_TIME);
			suppress_null_warning = 1;
			EventAdd(geoip_modinfo->handle, "allow_next_warning", allow_next_warning_evt, NULL, (long)WARNING_SUPPRESS_TIME * 60 * 60 * 1000, 1);
		}
		return NULL;
	}
	snprintf(buf, BUFLEN, "(%s/%s) %s", curr_country->continent, curr_country->code, curr_country->name);
	return buf;
}

MOD_TEST(){
	return MOD_SUCCESS;
}

MOD_INIT(){
	HookAdd(modinfo->handle, HOOKTYPE_REMOTE_CONNECT, 0, geoip_connectnotice_userconnect);
	HookAdd(modinfo->handle, HOOKTYPE_LOCAL_CONNECT, 0, geoip_connectnotice_userconnect);
	ModDataInfo mreq;
	memset(&mreq, 0, sizeof(mreq));
	mreq.type = MODDATATYPE_CLIENT;
	mreq.name = "geoip-connect-notice";
	geoipcnMD = ModDataAdd(modinfo->handle, mreq);
	if(!geoipcnMD){
		config_error("%s: critical error for ModDataAdd: %s. Failed to add geoip-connect-notice moddata.", MOD_HEADER.name, ModuleGetErrorStr(modinfo->handle));
		return MOD_FAILED;
	}
	return MOD_SUCCESS;
}

MOD_LOAD(){
	geoip_modinfo = modinfo;

	if(!CommandOverrideAddEx(modinfo->handle, "MD", 0, geoip_connectnotice_overridemd)){
		config_error("%s: CommandOverrideAddEx failed: %s", MOD_HEADER.name, ModuleGetErrorStr(modinfo->handle));
		return MOD_FAILED;
	}
	return MOD_SUCCESS;
}

MOD_UNLOAD(){
	return MOD_SUCCESS;
}

static int geoip_connectnotice_userconnect(Client *cptr) {
	if(!cptr)
		return HOOK_CONTINUE; // is it possible?
	if(moddata_client(cptr, geoipcnMD).i)
		return HOOK_CONTINUE;
	char *cdata = get_country_text(cptr);
	if(!cdata)
		return HOOK_CONTINUE; // no country data for this user
	sendto_snomask_global(SNO_FCLIENT, "%s!%s@%s is connecting from %s", cptr->name, cptr->user->username, GetHost(cptr), cdata);
	moddata_client(cptr, geoipcnMD).i = 1;
	return HOOK_CONTINUE;
}

EVENT(allow_next_warning_evt){
	suppress_null_warning = 0;
}

CMD_OVERRIDE_FUNC(geoip_connectnotice_overridemd){ // this is needed when the local db is outdated or someone installed -transfer instead of -base
	int i;

	CallCommandOverride(ovr, client, recv_mtags, parc, parv); // Run original function

	if(parc == 5 && !strcasecmp(parv[1], "client") && !strcasecmp(parv[3], "geoip")){
		Client *target = find_client(parv[2], NULL);
		geoip_connectnotice_userconnect(target); // we try again if other server is sending a geoip moddata
	}
}

