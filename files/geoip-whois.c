/* Copyright (C) All Rights Reserved
** Written by k4be
** Website: https://github.com/pirc-pl/unrealircd-modules/
** License: GPLv3 https://www.gnu.org/licenses/gpl-3.0.html
*/

/*** <<<MODULE MANAGER START>>>
module
{
        documentation "https://github.com/pirc-pl/unrealircd-modules/blob/master/README.md#geoip-whois";
        troubleshooting "In case of problems, contact k4be on irc.pirc.pl.";
        min-unrealircd-version "5.*";
        post-install-text {
                "The module is installed. Now you need to add a loadmodule line:";
                "loadmodule \"third/geoip-whois\";";
  				"And /REHASH the IRCd.";
  				"It'll take care of users on all servers in your network.";
				"Remember that you need \"geoip-base\" module installed on this server";
				"for geoip-whois to work.";
				"Detailed documentation is available on https://github.com/pirc-pl/unrealircd-modules/blob/master/README.md#geoip-whois";
        }
}
*** <<<MODULE MANAGER END>>>
*/

/*
// Config example:
geoip-whois {
	display-name; // Poland
	display-code; // PL
	display-continent; // EU
	info-string "connected from "; // remember the trailing space!
};
*/

#include "unrealircd.h"

#define BUFLEN 8191

#define MYCONF "geoip-whois"

struct country {
	char code[10];
	char name[100];
	char continent[25];
	int id;
	struct country *next;
};

int display_name = 0, display_code = 0, display_continent = 0;
int have_config = 0;
char *info_string = NULL;
int suppress_null_warning = 0;
ModuleInfo *geoip_modinfo;

// function declarations here
static int geoip_whois_userconnect(Client *);
static char *get_country_text(Client *);
int geoip_whois_configtest(ConfigFile *cf, ConfigEntry *ce, int type, int *errs);
int geoip_whois_configposttest(int *errs);
int geoip_whois_configrun(ConfigFile *cf, ConfigEntry *ce, int type);
EVENT(set_existing_users_evt);
EVENT(allow_next_warning_evt);
CMD_OVERRIDE_FUNC(geoip_whois_overridemd);

ModuleHeader MOD_HEADER = {
	"third/geoip-whois",   /* Name of module */
	"5.0.5", /* Version */
	"Add country info to /whois", /* Short description of module */
	"k4be@PIRC",
	"unrealircd-5"
};
	
// config file stuff, based on Gottem's module
int geoip_whois_configtest(ConfigFile *cf, ConfigEntry *ce, int type, int *errs) {
	ConfigEntry *cep; // For looping through our bl0cc
	int errors = 0; // Error count
	int i; // iter8or m8
	int display_anything = 0;

	// Since we'll add a new top-level block to unrealircd.conf, need to filter on CONFIG_MAIN lmao
	if(type != CONFIG_MAIN)
		return 0; // Returning 0 means idgaf bout dis

	// Check for valid config entries first
	if(!ce || !ce->ce_varname)
		return 0;

	// If it isn't our bl0ck, idc
	if(strcmp(ce->ce_varname, MYCONF))
		return 0;

	have_config = 1;

	// Loop dat shyte fam
	for(cep = ce->ce_entries; cep; cep = cep->ce_next) {
		// Do we even have a valid name l0l?
		if(!cep->ce_varname) {
			config_error("%s:%i: blank %s item", cep->ce_fileptr->cf_filename, cep->ce_varlinenum, MYCONF); // Rep0t error
			errors++; // Increment err0r count fam
			continue; // Next iteration imo tbh
		}

		if(!strcmp(cep->ce_varname, "display-name")) { // no value expected
			display_anything = 1;
			continue;
		}
		if(!strcmp(cep->ce_varname, "display-code")) {
			display_anything = 1;
			continue;
		}
		if(!strcmp(cep->ce_varname, "display-continent")) {
			display_anything = 1;
			continue;
		}
		
		if(!strcmp(cep->ce_varname, "info-string")) {
			if(!cep->ce_vardata) {
				config_error("%s:%i: %s::%s must be a string", cep->ce_fileptr->cf_filename, cep->ce_varlinenum, MYCONF, cep->ce_varname);
				errors++; // Increment err0r count fam
			}
			continue;
		}

		// Anything else is unknown to us =]
		config_warn("%s:%i: unknown item %s::%s", cep->ce_fileptr->cf_filename, cep->ce_varlinenum, MYCONF, cep->ce_varname); // So display just a warning
	}
	
	// note that we will get here only if the config block is present
	if(!display_anything){
		config_error("geoip-whois: configured not to display anything! Specify at least one of: display-name, display-code, display-continent.");
		errors++;
	}
	
	*errs = errors;
	return errors ? -1 : 1; // Returning 1 means "all good", -1 means we shat our panties
}

int geoip_whois_configposttest(int *errs) {
	if(!have_config){
		config_warn("geoip-whois: no \"%s\" config block found, using default options", MYCONF);
		display_name = 1;
		display_code = 1;
	}
	if(info_string == NULL) info_string = strdup("connected from ");
	return 1;
}

// "Run" the config (everything should be valid at this point)
int geoip_whois_configrun(ConfigFile *cf, ConfigEntry *ce, int type) {
	ConfigEntry *cep; // For looping through our bl0cc

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

		if(!strcmp(cep->ce_varname, "display-name")) {
			display_name = 1;
			continue;
		}
		if(!strcmp(cep->ce_varname, "display-code")) {
			display_code = 1;
			continue;
		}
		if(!strcmp(cep->ce_varname, "display-continent")) {
			display_continent = 1;
			continue;
		}
		
		if(cep->ce_vardata && !strcmp(cep->ce_varname, "info-string")) {
			info_string = strdup(cep->ce_vardata);
			continue;
		}
	}
	return 1; // We good
}

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
			sendto_realops("geoip-whois: curr_country is NULL for %s (%s), perhaps no geoip-base module available on this server, or incomplete/outdated database?", cptr->name, GetIP(cptr));
			sendto_realops("geoip-whois: Please note that the warning won't reappear for the next %d hours.", WARNING_SUPPRESS_TIME);
			suppress_null_warning = 1;
			EventAdd(geoip_modinfo->handle, "allow_next_warning", allow_next_warning_evt, NULL, (long)WARNING_SUPPRESS_TIME * 60 * 60 * 1000, 1);
		}
		return NULL;
	}
	if(display_name) strcpy(buf, curr_country->name);
	if(display_code) sprintf(buf + strlen(buf), "%s(%s)", display_name?" ":"", curr_country->code);
	if(display_continent) sprintf(buf + strlen(buf), "%s%s", (display_name || display_code)?", ":"", curr_country->continent);
	return buf;
}

MOD_TEST(){
	have_config = 0;
	HookAdd(modinfo->handle, HOOKTYPE_CONFIGTEST, 0, geoip_whois_configtest);
	HookAdd(modinfo->handle, HOOKTYPE_CONFIGPOSTTEST, 0, geoip_whois_configposttest);
	return MOD_SUCCESS;
}

MOD_INIT(){
	HookAdd(modinfo->handle, HOOKTYPE_CONFIGRUN, 0, geoip_whois_configrun);
	HookAdd(modinfo->handle, HOOKTYPE_REMOTE_CONNECT, 0, geoip_whois_userconnect);
	HookAdd(modinfo->handle, HOOKTYPE_LOCAL_CONNECT, 0, geoip_whois_userconnect);
	return MOD_SUCCESS;
}

MOD_LOAD(){
	geoip_modinfo = modinfo;

	EventAdd(geoip_modinfo->handle, "set_existing_users", set_existing_users_evt, NULL, 1000, 1);
	if(!CommandOverrideAddEx(modinfo->handle, "MD", 0, geoip_whois_overridemd)){
		config_error("%s: CommandOverrideAddEx failed: %s", MOD_HEADER.name, ModuleGetErrorStr(modinfo->handle));
		return MOD_FAILED;
	}
	return MOD_SUCCESS;
}

MOD_UNLOAD(){
	Client *acptr;
	list_for_each_entry(acptr, &client_list, client_node){
		if (!IsUser(acptr)) continue;
		swhois_delete(acptr, "geoip", "*", &me, NULL); // delete info when unloading 
	}
	safe_free(info_string);
	return MOD_SUCCESS;
}

static int geoip_whois_userconnect(Client *cptr) {
	if(!cptr) return HOOK_CONTINUE; // is it possible?
	char *cdata = get_country_text(cptr);
	if(!cdata) return HOOK_CONTINUE; // no country data for this user
	char buf[BUFLEN+1];
	sprintf(buf, "%s%s", info_string, cdata);
	swhois_add(cptr, "geoip", 0, buf, &me, NULL);
	return HOOK_CONTINUE;
}

EVENT(set_existing_users_evt){
	Client *acptr;
	list_for_each_entry(acptr, &client_list, client_node){
		if (!IsUser(acptr)) continue;
		geoip_whois_userconnect(acptr); // add info for all users upon module loading
	}
}

EVENT(allow_next_warning_evt){
	suppress_null_warning = 0;
}

CMD_OVERRIDE_FUNC(geoip_whois_overridemd){ // this is needed when the local db is outdated or someone installed -transfer instead of -base
	int i;

	CallCommandOverride(ovr, client, recv_mtags, parc, parv); // Run original function

	if(parc == 5 && !strcasecmp(parv[1], "client") && !strcasecmp(parv[3], "geoip")){
		Client *target = find_client(parv[2], NULL);
		geoip_whois_userconnect(target); // we try again if other server is sending a geoip moddata
	}
}

