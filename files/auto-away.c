/* 
	LICENSE: GPLv3
  	Copyright Ⓒ 2022 Valerie Pond

	Automatically sets a user away if they are idle too long

*/
/*** <<<MODULE MANAGER START>>>
module
{
        documentation "https://github.com/ValwareIRC/valware-unrealircd-mods/blob/main/auto-away/README.md";
		troubleshooting "In case of problems, documentation or e-mail me at v.a.pond@outlook.com";
        min-unrealircd-version "6.*";
        max-unrealircd-version "6.*";
        post-install-text {
                "The module is installed. Now all you need to do is add a loadmodule line:";
                "loadmodule \"third/auto-away\";";
		  		      "Then you specify the time and reason values in the config Here's the defaults to get you started:";
	         			"auto-away {";
				        "	time 2h;";
				        "	reason \"Idle.\";";
		        		"}";
                "And /REHASH the IRCd.";
                "The module does not need any other configuration.";
        }
}
*** <<<MODULE MANAGER END>>>
*/
#include "unrealircd.h"

#define UCONF "auto-away"

EVENT(check_if_away);

void setconf(void);
void freeconf(void);
int autoaway_configtest(ConfigFile *cf, ConfigEntry *ce, int type, int *errs);
int autoaway_configrun(ConfigFile *cf, ConfigEntry *ce, int type);

typedef struct
{
	char *reason;
	long time;

	unsigned short int reason_exists;
	unsigned short int time_exists;
} awaystruct;

ModuleHeader MOD_HEADER =
{
	"third/auto-away",
	"1.0",
	"Automatically set someone away after a certain amount of time",
	"Valware",
	"unrealircd-6",
};
MOD_INIT()
{
	MARK_AS_GLOBAL_MODULE(modinfo);
	
	setconf();
	
	EventAdd(modinfo->handle, "check_if_away", check_if_away, NULL, 1500, 0);
	HookAdd(modinfo->handle, HOOKTYPE_CONFIGRUN, 0, autoaway_configrun);
	return MOD_SUCCESS;
}

MOD_LOAD()
{
	return MOD_SUCCESS;
}
MOD_UNLOAD()
{
	freeconf();
	return MOD_SUCCESS; 
}



static awaystruct ourconf;

MOD_TEST()
{
	memset(&ourconf, 0, sizeof(ourconf));

	HookAdd(modinfo->handle, HOOKTYPE_CONFIGTEST, 0, autoaway_configtest);
	return MOD_SUCCESS;
}


void setconf(void)
{
	safe_strdup(ourconf.reason, "Idle.");
	ourconf.time = 120; /* Default is 2hours inactivity */
}

void freeconf(void)
{
	safe_free(ourconf.reason);
}

/* gottem templaet ellemayo */
int autoaway_configtest(ConfigFile *cf, ConfigEntry *ce, int type, int *errs)
{
	int errors = 0;
	int i;
	ConfigEntry *cep, *cep2;

	if(type != CONFIG_MAIN)
		return 0;

	if(!ce || !ce->name)
		return 0;

	if(strcmp(ce->name, UCONF))
		return 0;

	for(cep = ce->items; cep; cep = cep->next)
	{
		if(!cep->name)
		{
			config_error("%s:%i: blank %s item.", cep->file->filename, cep->line_number, UCONF);
			errors++;
			continue;
		}


		if(!strcmp(cep->name, "reason"))
		{
			if(ourconf.reason_exists)
			{
				config_error("%s:%i: duplicate %s::%s directive.", cep->file->filename, cep->line_number, UCONF, cep->name);
				errors++;
				continue;
			}

			ourconf.reason_exists = 1;
			continue;
		}

		if(!strcmp(cep->name, "time"))
		{
			if(ourconf.time_exists)
			{
				config_error("%s:%i: duplicate %s::%s directive", cep->file->filename, cep->line_number, UCONF, cep->name);
				errors++;
				continue;
			}

			ourconf.time_exists = 1;
			if(config_checkval(cep->value, CFG_TIME) <= 0)
			{
				config_error("%s:%i: %s::%s must be a time string like '7d10m' or simply '20'", cep->file->filename, cep->line_number, UCONF, cep->name);
				errors++;
			}
			continue;
		}
		config_warn("%s:%i: unknown item %s::%s", cep->file->filename, cep->line_number, UCONF, cep->name); 
	}

	*errs = errors;
	return errors ? -1 : 1;
}

int autoaway_configposttest(int *errs)
{
	return 1;
}

int autoaway_configrun(ConfigFile *cf, ConfigEntry *ce, int type)
{
	ConfigEntry *cep;

	if(type != CONFIG_MAIN)
		return 0;

	if(!ce || !ce->name)
		return 0;

	if(strcmp(ce->name, UCONF))
		return 0;

	for(cep = ce->items; cep; cep = cep->next)
	{
		if(!cep->name)
			continue;

		if(!strcmp(cep->name, "reason"))
		{
			safe_strdup(ourconf.reason, cep->value);
			continue;
		}

		if(!strcmp(cep->name, "time"))
		{
			ourconf.time = config_checkval(cep->value, CFG_TIME);
			continue;
		}
	}

	return 1; // We good
}


EVENT(check_if_away)
{
	Client *target;
	list_for_each_entry(target, &lclient_list, lclient_node)
	{
		if (!IsUser(target))
			continue;

		/* if they're already set away.. */
		if (target->user->away)
			continue;

		char tmp[MAXAWAYLEN] = "[Auto-Away] ";
		strlcat(tmp, ourconf.reason, sizeof(tmp));
		const char *parv[3];
		parv[0] = NULL;
		parv[1] = tmp;
		parv[2] = NULL;
		if (target->local->idle_since <= TStime() - ourconf.time)
			do_cmd(target, NULL, "AWAY", 2, parv);
	}
}
