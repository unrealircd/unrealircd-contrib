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
                "loadmodule \"third/geoip-whois\";";
  				"And /REHASH the IRCd.";
				"Remember that you need \"geoip-base\" module installed on this server";
				"for geoip-whois to work.";
				"Detailed documentation is available on https://github.com/pirc-pl/unrealircd-modules/blob/master/README.md";
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
ModuleInfo *geoip_modinfo;

// function declarations here
static int geoip_whois_userconnect(Client *);
static char *get_country_text(Client *);
int geoip_whois_configtest(ConfigFile *cf, ConfigEntry *ce, int type, int *errs);
int geoip_whois_configposttest(int *errs);
int geoip_whois_configrun(ConfigFile *cf, ConfigEntry *ce, int type);
void geoip_moddata_free(ModData *m);
char *geoip_moddata_serialize(ModData *m);
void geoip_moddata_unserialize(char *str, ModData *m);
EVENT(set_existing_users_evt);
EVENT(set_new_user_evt);

ModuleHeader MOD_HEADER = {
	"third/geoip-whois",   /* Name of module */
	"5.0", /* Version */
	"add country info to /whois", /* Short description of module */
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

// required for some reason
int geoip_whois_configposttest(int *errs) {
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

	have_config = 1;
	
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

static char *get_country_text(Client *cptr){
	static char buf[BUFLEN];
	struct country *curr_country;
	
	if(!cptr) return NULL;
	
	ModDataInfo *md = findmoddata_byname("geoip", MODDATATYPE_CLIENT);
	
	if(!md){
		sendto_realops("geoip-whois: no ModData field, perhaps no geoip-base nor geoip-transfer module available on this server? It won't work.");
		return NULL;
	}
	
	if(IsULine(cptr)) return NULL; // service bots won't have the valid geoip location anyway, hence we suppress the error message
	
	curr_country = moddata_client(cptr, md).ptr;
	if(!curr_country){
		sendto_realops("geoip-whois: curr_country is NULL for %s, perhaps no geoip-base module available on the network, or incomplete/outdated database?", cptr->name);
		return NULL;
	}
	if(display_name) strcpy(buf, curr_country->name);
	if(display_code) sprintf(buf + strlen(buf), "%s(%s)", display_name?" ":"", curr_country->code);
	if(display_continent) sprintf(buf + strlen(buf), "%s%s", (display_name || display_code)?", ":"", curr_country->continent);
	return buf;
}

void geoip_moddata_free(ModData *m){
	if(m->ptr) safe_free(m->ptr);
	m->ptr = NULL;
}

char *geoip_moddata_serialize(ModData *m){
	static char buf[140];
	if(!m->ptr) return NULL;
	struct country *country = (struct country *)m->ptr;
	ircsnprintf(buf, 140, "%s!%s!%s", country->code, country->name, country->continent);
	return buf;
}

void geoip_moddata_unserialize(char *str, ModData *m){
	if(m->ptr) safe_free(m->ptr);
	struct country *country = safe_alloc(sizeof(struct country));
	if(sscanf(str, "%[^!]!%[^!]!%[^!]", country->code, country->name, country->continent) != 3){ // invalid argument
		safe_free(country);
		m->ptr = NULL;
	} else {
		m->ptr = country;
	}
}

MOD_TEST(){
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
	if(!have_config){
		sendto_realops("Warning: no configuration for geoip-whois, using default options");
		display_name = 1;
		display_code = 1;
	}
	if(info_string == NULL) info_string = strdup("connected from ");
	
	geoip_modinfo = modinfo;

	EventAdd(geoip_modinfo->handle, "set_existing_users", set_existing_users_evt, NULL, 1000, 1);
	return MOD_SUCCESS;
}

MOD_UNLOAD(){
	Client *acptr;
	list_for_each_entry(acptr, &client_list, client_node){
		if (!IsUser(acptr)) continue;
		swhois_delete(acptr, "geoip", "*", &me, NULL); // delete info when unloading 
	}
	return MOD_SUCCESS;
}

static int geoip_whois_userconnect(Client *cptr) {
	EventAdd(geoip_modinfo->handle, "set_new_user", set_new_user_evt, cptr, 1000, 1);
	return HOOK_CONTINUE;
}

EVENT(set_existing_users_evt){
	Client *acptr;
	list_for_each_entry(acptr, &client_list, client_node){
		if (!IsUser(acptr)) continue;
		geoip_whois_userconnect(acptr); // add info for all users upon module loading
	}
}

EVENT(set_new_user_evt){
	Client *cptr = (Client *) data;
	if(!cptr) return;
	char *cdata = get_country_text(cptr);
	if(!cdata) return;
	char buf[BUFLEN+1];
	sprintf(buf, "%s%s", info_string, cdata);
	swhois_delete(cptr, "geoip", "*", &me, NULL); //somehow has it already set
	swhois_add(cptr, "geoip", 0, buf, &me, NULL);
	return;
}

