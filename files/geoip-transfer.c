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
                "The module is installed. Now all you need to do is add a loadmodule line:";
                "loadmodule \"third/geoip-transfer\";";
  				"And /REHASH the IRCd.";
				"The module does not need any other configuration.";
				"Remember that you need other \"geoip\" module to make a real use of this one,";
				"and the \"geoip-base\" module on at least one server to provide the data.";
        }
}
*** <<<MODULE MANAGER END>>>
*/

#include "unrealircd.h"

struct country {
	char code[10];
	char name[100];
	char continent[25];
	int id;
	struct country *next;
};

ModDataInfo *geoipMD;

// function declarations here
void geoip_moddata_free(ModData *m);
char *geoip_moddata_serialize(ModData *m);
void geoip_moddata_unserialize(char *str, ModData *m);

ModuleHeader MOD_HEADER = {
	"third/geoip-transfer",   /* Name of module */
	"5.0", /* Version */
	"GeoIP data provider / data transfer module", /* Short description of module */
	"k4be@PIRC",
	"unrealircd-5"
};

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

MOD_INIT(){
	ModDataInfo mreq;
	memset(&mreq, 0, sizeof(mreq));
	mreq.type = MODDATATYPE_CLIENT;
	mreq.name = "geoip";
	mreq.sync = 1;
	mreq.free = geoip_moddata_free;
	mreq.serialize = geoip_moddata_serialize;
	mreq.unserialize = geoip_moddata_unserialize;
	geoipMD = ModDataAdd(modinfo->handle, mreq);
	if(!geoipMD){
		config_error("%s: critical error for ModDataAdd: %s", MOD_HEADER.name, ModuleGetErrorStr(modinfo->handle));
		return MOD_FAILED;
	}
	return MOD_SUCCESS;
}

MOD_LOAD(){
	return MOD_SUCCESS;
}

MOD_UNLOAD(){
	return MOD_SUCCESS;
}

