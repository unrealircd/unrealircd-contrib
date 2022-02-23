/* Copyright (C) All Rights Reserved
** Written by Gottem <support@gottem.nl>
** Website: https://gottem.nl/unreal
** License: https://gottem.nl/unreal/license
*/

/*** <<<MODULE MANAGER START>>>
module {
	documentation "https://gottem.nl/unreal/man/portsifresi";
	troubleshooting "In case of problems, check the FAQ at https://gottem.nl/unreal/halp or e-mail me at support@gottem.nl";
	min-unrealircd-version "6.*";
	//max-unrealircd-version "6.*";
	post-install-text {
		"The module is installed, now all you need to do is add a 'loadmodule' line to your config file:";
		"loadmodule \"third/portsifresi\";";
		"Then /rehash the IRCd.";
		"For usage information, refer to the module's documentation found at: https://gottem.nl/unreal/man/portsifresi";
	}
}
*** <<<MODULE MANAGER END>>>
*/

// One include for all cross-platform compatibility thangs
#include "unrealircd.h"

// Big hecks go here
typedef struct t_psifre PSIFre;
struct t_psifre {
	PSIFre *prev;
	PSIFre *next;
	int port;
	char *pass;
};

enum {
	PSI_RET_UNHANDLED = 0,
	PSI_RET_NOPASS = 1,
	PSI_RET_INCORRECTPASS = 2,
	PSI_RET_ACCEPTEDPASS = 3,
};

/* Prototypes */
static void addPortPassword(char *port, char *pass);
int findPortPassword(Client *client);
int portsifresi_hook_prelocalconnect(Client *);
int portsifresi_hook_configrun(ConfigFile *cf, ConfigEntry *ce, int type);
int portsifresi_hook_configtest(ConfigFile *cf, ConfigEntry *ce, int type, int *errs);
int portsifresi_hook_rehash(void);

// Globals
static PSIFre *portsifresiList = NULL;

/* Main Module Header For UnrealIRCd */
ModuleHeader MOD_HEADER = {
	"third/portsifresi",
	"2.1.0", // Version
	"Protect specific ports with a password",
	"Gottem", // Author
	"unrealircd-6", // Modversion
};

/* MOD_TEST Function */
MOD_TEST() {
	HookAdd(modinfo->handle, HOOKTYPE_CONFIGTEST, 0, portsifresi_hook_configtest);
	return MOD_SUCCESS;
}

/* MOD_INIT Function */
MOD_INIT() {
	MARK_AS_GLOBAL_MODULE(modinfo);

	HookAdd(modinfo->handle, HOOKTYPE_PRE_LOCAL_CONNECT, 0, portsifresi_hook_prelocalconnect);
	HookAdd(modinfo->handle, HOOKTYPE_CONFIGRUN, 0, portsifresi_hook_configrun);
	HookAdd(modinfo->handle, HOOKTYPE_REHASH, 0, portsifresi_hook_rehash);
	return MOD_SUCCESS;
}

/* MOD_LOAD Function */
MOD_LOAD() {
	return MOD_SUCCESS;
}

/* MOD_UNLOAD Function */
MOD_UNLOAD() {
	return MOD_SUCCESS;
}

/* Local User Connect Hook */
int portsifresi_hook_prelocalconnect(Client *client) {
	int ret = findPortPassword(client);
	switch(ret) {
		case PSI_RET_UNHANDLED:
			break;

		case PSI_RET_ACCEPTEDPASS:
			//sendnotice(client, "*** [portsifresi] Password accepted");
			break;

		case PSI_RET_NOPASS:
			exit_client(client, NULL, "No password given");
			return HOOK_DENY;

		case PSI_RET_INCORRECTPASS:
			exit_client(client, NULL, "Incorrect password");
			return HOOK_DENY;

		default:
			break;
	}
	return HOOK_CONTINUE;
}

/* Adding passwords to memory from confs reads */
static void addPortPassword(char *port, char *pass) {
	PSIFre *e;
	e = safe_alloc(sizeof(PSIFre));
	e->port = atoi(port);
	e->pass = strdup(pass);
	AddListItem(e, portsifresiList);
}

/* Read passwords from memory and return 1 if there is match password. */
int findPortPassword(Client *client) {
	PSIFre *e;

	// Shouldn't be possible but let's be certain =]
	if(!client->local || !client->local->listener || !client->local->listener->port)
		return PSI_RET_UNHANDLED;

	for(e = portsifresiList; e; e = e->next) {
		if(e->port != client->local->listener->port)
			continue;

		if(client->local->passwd == NULL)
			return PSI_RET_NOPASS;

		if(!strcmp(client->local->passwd, e->pass))
			return PSI_RET_ACCEPTEDPASS;
		return PSI_RET_INCORRECTPASS;
	}
	return PSI_RET_UNHANDLED;
}

/* On Rehash Hook? :) */
int portsifresi_hook_rehash(void) {
	PSIFre *e, *next;
	for(e = portsifresiList; e; e = next) {
		next = e->next;
		DelListItem(e, portsifresiList);
		safe_free(e->pass);
		safe_free(e);
	}
	return HOOK_CONTINUE;
}

/* On Conf Test Hook. */
int portsifresi_hook_configtest(ConfigFile *cf, ConfigEntry *ce, int type, int *errs) {
	int errors = 0;
	ConfigEntry	*cep;
	char *p;
	//int foundsep;
	//char port[6]; // Max port is 65535 obv ;]
	//int pi;
	int port;
	int validport;
	size_t portlen, passlen;

	if(type != CONFIG_MAIN)
		return 0;

	if(!ce || !ce->name || strcmp(ce->name, "psifre"))
		return 0;

	for(cep = ce->items; cep; cep = cep->next) {
		if(!cep->name || !(portlen = strlen(cep->name)) || !cep->value || !(passlen = strlen(cep->value))) {
			config_error("%s:%i: empty/incomplete psifre entry (should be of the format: <port> \"<password>\";)", cep->file->filename, cep->line_number);
			errors++;
			continue;
		}

		validport = 1;
		p = cep->name;
		while(*p) {
			if(!isdigit(*p)) {
				config_error("%s:%i: invalid psifre port number '%s' (not an actual number)", cep->file->filename, cep->line_number, cep->name);
				errors++;
				validport = 0;
				break;
			}
			p++;
		}

		if(!validport)
			continue;

		port = atoi(cep->name);
		if(portlen > 5 || port <= 1024 || port > 65535) {
			config_error("%s:%i: psifre port number %s is out of range (minimum is 1025 and maximum is 65535)", cep->file->filename, cep->line_number, cep->name);
			errors++;
			continue;
		}

		if(passlen > PASSWDLEN) {
			config_error("%s:%i: psifre password for port %d is too long (exceeds internal max length of %d)", cep->file->filename, cep->line_number, port, PASSWDLEN);
			errors++;
			continue;
		}
	}
	*errs = errors;
	return errors ? -1 : 1;
}

/* On Conf Run Hook. */
int portsifresi_hook_configrun(ConfigFile *cf, ConfigEntry *ce, int type) {
	ConfigEntry	*cep;

	if(type != CONFIG_MAIN)
		return 0;

	if(!ce || !ce->name || strcmp(ce->name, "psifre"))
		return 0;

	for(cep = ce->items; cep; cep = cep->next) {
		// Shouldn't really be possible but whatevs =]
		if(!cep->name || !strlen(cep->name) || !cep->value || !strlen(cep->value))
			continue;
		addPortPassword(cep->name, cep->value);
	}
	return 1;
}
