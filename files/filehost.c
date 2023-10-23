/** === Module Information ===
 * 
 * NAME: draft/FILEHOST
 * LICENCE: GPLv3 or later
 * COPYRIGHT: Ⓒ 2023 Valerie Pond, 2022 Val Lorentz
 * DOCS: https://github.com/progval/ircv3-specifications/blob/filehost/extensions/filehost.md
 * 
*/
/*** <<<MODULE MANAGER START>>>
module
{
		documentation "https://github.com/ValwareIRC/valware-unrealircd-mods/blob/main/filehost/README.md";
		troubleshooting "In case of problems, documentation or e-mail me at v.a.pond@outlook.com";
		min-unrealircd-version "6.*";
		max-unrealircd-version "6.*";
		post-install-text {
				"The module is installed. Now all you need to do is add a loadmodule line:";
				"loadmodule \"third/filehost\";";
				"And /REHASH the IRCd.";
				"The module does not need any other configuration.";
		}
}
*** <<<MODULE MANAGER END>>>
*/
#include "unrealircd.h"

#define CONF_FILEHOST "filehosts"

/* FILEHOST config struct */
struct
{
	char *isupport_line;
	MultiLine *hosts;
	unsigned short int has_hosts;
} cfg;



int filehost_configtest(ConfigFile *cf, ConfigEntry *ce, int type, int *errs);
int filehost_configrun(ConfigFile *cf, ConfigEntry *ce, int type);
void setconf(void);
void freeconf(void);

ModuleHeader MOD_HEADER
  = {
	"third/filehost",
	"1.0",
	"IRCv3 draft/FILEHOST - Provides users with an ISUPPORT token with a URL to your file upload service",
	"Valware",
	"unrealircd-6",
};

MOD_TEST()
{
	setconf();
	HookAdd(modinfo->handle, HOOKTYPE_CONFIGTEST, 0, filehost_configtest);
	return MOD_SUCCESS;
}

MOD_INIT()
{
	HookAdd(modinfo->handle, HOOKTYPE_CONFIGRUN, 0, filehost_configrun);
	return MOD_SUCCESS;
}

MOD_LOAD()
{
	ISupport *is;
    if (!(is = ISupportAdd(modinfo->handle, "draft/FILEHOST", cfg.isupport_line)))
		return MOD_FAILED;
	return MOD_SUCCESS;
}

MOD_UNLOAD()
{
	freeconf();
	return MOD_SUCCESS;
}

void setconf(void)
{
	memset(&cfg, 0, sizeof(cfg));
	cfg.has_hosts = 0;
	safe_strdup(cfg.isupport_line,"");
}

void freeconf(void)
{
	freemultiline(cfg.hosts);
	cfg.has_hosts = 0;
	safe_free(cfg.isupport_line);
	memset(&cfg, 0, sizeof(cfg));
}


int filehost_configtest(ConfigFile *cf, ConfigEntry *ce, int type, int *errs)
{
	int errors = 0;
	ConfigEntry *cep;
    freeconf();
    setconf();
	if (type != CONFIG_MAIN)
		return 0;

	if (!ce || !ce->name)
        return 0;

    if (strcmp(ce->name, CONF_FILEHOST))
		return 0;

	for (cep = ce->items; cep; cep = cep->next)
	{
		if (!cep->name)
		{
			config_error("%s:%i: blank %s item", cep->file->filename, cep->line_number, CONF_FILEHOST);
			++errors;
			continue;
		}
		if (!strcasecmp(cep->name, "host"))
		{
			if (!BadPtr(cep->value))
				cfg.has_hosts = 1;

			else
				config_error("%s:%i: Empty host at %s::%s", cep->file->filename, cep->line_number, CONF_FILEHOST, cep->name);

			continue;
		}

		// Anything else is unknown to us =]
		config_warn("%s:%i: unknown item %s::%s", cep->file->filename, cep->line_number, CONF_FILEHOST, cep->name); // So display just a warning
	}

	*errs = errors;
	return errors ? -1 : 1;
}

int filehost_configrun(ConfigFile *cf, ConfigEntry *ce, int type)
{
	ConfigEntry *cep;
	char buf[BUFSIZE] = "\0";
	
	if (type != CONFIG_MAIN)
		return 0;

	if (!ce || !ce->name)
		return 0;

	if (strcmp(ce->name, "filehosts"))
		return 0;

	for (cep = ce->items; cep; cep = cep->next)
	{
		if (!cep->name)
			continue;

		if (!strcmp(cep->name, "host"))
			addmultiline(&cfg.hosts, cep->value);
		
	}
    
	for (MultiLine *m = cfg.hosts; m; m = m->next)
	{
		strlcat(buf,m->line, sizeof(buf));
		if (m->next)
			strlcat(buf,"\\x20",sizeof(buf));
	}
	if (strlen(buf))
		safe_strdup(cfg.isupport_line, buf);


	return 1; // We good
}
