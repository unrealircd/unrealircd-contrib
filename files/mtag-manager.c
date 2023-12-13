/* 
	LICENSE: GPLv3
  	Copyright Ⓒ 2023 Valerie Pond

	Allows server owners to easily add/allow specific client-to-client tags


*/
/*** <<<MODULE MANAGER START>>>
module
{
		documentation "https://github.com/ValwareIRC/valware-unrealircd-mods/blob/main/mtag-manager/README.md";
		troubleshooting "In case of problems, documentation or e-mail me at v.a.pond@outlook.com";
		min-unrealircd-version "6.*";
		max-unrealircd-version "6.*";
		post-install-text {
				"The module is installed. Now all you need to do is add a loadmodule line:";
				"loadmodule \"third/mtag-manager\";";
				"Then you can add your chosen message-tags to the config file.";
                "Here's an example which provides kiwiirc's tags:";
				"message-tags {";
				"	+kiwiirc.com/fileuploader;";
				"	+kiwiirc.com/conference;";
				"	+kiwiirc.com/ttt;";
				"	+data;";
				"}";
				"And /REHASH the IRCd.";
				"The module does not need any other configuration.";
		}
}
*** <<<MODULE MANAGER END>>>
*/
#include "unrealircd.h"

#define UCONF "message-tags"

void freeconf(void);

void mtag_add_mm_tag(Client *client, MessageTag *recv_mtags, MessageTag **mtag_list, const char *signature);

int mtag_man_configtest(ConfigFile *cf, ConfigEntry *ce, int type, int *errs);
int mtag_man_configrun(ConfigFile *cf, ConfigEntry *ce, int type);

typedef struct
{
	MultiLine *mtags_list;
} mtagstruct;
static mtagstruct ourconf;


ModuleHeader MOD_HEADER =
{
	"third/mtag-manager",
	"1.0",
	"Allows server owners to permit specific client-to-client tags",
	"Valware",
	"unrealircd-6",
};


MOD_INIT()
{
	MARK_AS_GLOBAL_MODULE(modinfo);	

	HookAdd(modinfo->handle, HOOKTYPE_CONFIGRUN, 0, mtag_man_configrun);
	HookAddVoid(modinfo->handle, HOOKTYPE_NEW_MESSAGE, 0, mtag_add_mm_tag);
	return MOD_SUCCESS;
}

MOD_LOAD()
{
	MessageTagHandlerInfo mtag;
	MultiLine *ml;
	if (ourconf.mtags_list)
	{
		for (ml = ourconf.mtags_list; ml; ml = ml->next)
		{
			memset(&mtag, 0, sizeof(mtag));
			mtag.is_ok = 1;
			mtag.flags = MTAG_HANDLER_FLAGS_NO_CAP_NEEDED;
			mtag.name = ml->line;
			MessageTagHandlerAdd(modinfo->handle, &mtag);
		}
	}
	return MOD_SUCCESS;
}
MOD_UNLOAD()
{
	freeconf();
	return MOD_SUCCESS; 
}

MOD_TEST()
{
	HookAdd(modinfo->handle, HOOKTYPE_CONFIGTEST, 0, mtag_man_configtest);
	return MOD_SUCCESS;
}


void freeconf(void)
{
	freemultiline(ourconf.mtags_list);
}

int mtag_man_configtest(ConfigFile *cf, ConfigEntry *ce, int type, int *errs)
{
	int errors = 0;
	int i;
	ConfigEntry *cep, *cep2, *cep3;

	if (type != CONFIG_MAIN)
		return 0;

	if (!ce || !ce->name)
		return 0;

	if (strcmp(ce->name, UCONF))
		return 0;

	for (cep = ce->items; cep; cep = cep->next)
	{
		if (BadPtr(cep->name))
		{
			config_error("%s:%i: blank %s item.", cep->file->filename, cep->line_number, UCONF);
			errors++;
			continue;
		}
		if (cep->name[0] != '+')
		{
			config_error("%s:%i: %s {}: \"%s\" invalid tag - must begin with a plus sign (+)", cep->file->filename, cep->line_number, UCONF, cep->name);
			errors++;
			continue;
		}
		if (strlen(cep->name) > 33)
		{
			config_error("%s:%i: %s {}: \"%s\" invalid tag - tags must be no longer than 32 characters long (not including the leading +)", cep->file->filename, cep->line_number, UCONF, cep->name);
			errors++;
			continue;
		}
		if (cep->value)
		{
			config_error("%s:%i: %s {}: \"%s\" has a value when it should not.", cep->file->filename, cep->line_number, UCONF, cep->name);
			errors++;
			continue;
		}
	}

	*errs = errors;
	return errors ? -1 : 1;
}


int mtag_man_configrun(ConfigFile *cf, ConfigEntry *ce, int type)
{
	ConfigEntry *cep;

	if (type != CONFIG_MAIN)
		return 0;

	if (!ce || !ce->name)
		return 0;

	if (strcmp(ce->name, UCONF))
		return 0;
	freeconf();
	memset(&ourconf, 0, sizeof(ourconf));

	for(cep = ce->items; cep; cep = cep->next)
		addmultiline(&ourconf.mtags_list, cep->name);

	return 1; // We good
}

void mtag_add_mm_tag(Client *client, MessageTag *recv_mtags, MessageTag **mtag_list, const char *signature)
{
	MessageTag *m;
	MultiLine *ml;
	
	if (IsUser(client))
	{
		for (ml = ourconf.mtags_list; ml; ml = ml->next)
		{
			m = find_mtag(recv_mtags, ml->line);
			if (m)
			{
				m = duplicate_mtag(m);
				AddListItem(m, *mtag_list);
			}
		}
	}
}
