/* Recheck blacklists after a certain time
 * (C) Copyright 2023 Bram Matthys and The UnrealIRCd Team
 * License: GPLv2
 */

/*** <<<MODULE MANAGER START>>>
module
{
	documentation "https://www.unrealircd.org/docs/Set_block#set::blacklist";

	// This is displayed in './unrealircd module info ..' and also if compilation of the module fails:
	troubleshooting "Please report at https://bugs.unrealircd.org/ if this module fails to compile";

	// Minimum version necessary for this module to work:
	min-unrealircd-version "6.*";

	// Maximum version (actually this module is redundant in 6.1.2+):
	max-unrealircd-version "6.*";

	post-install-text {
		"The module is installed. Now all you need to do is add a loadmodule line:";
		"loadmodule \"third/blacklistrecheck\";";
		"And /REHASH the IRCd.";
		"By default the module will re-check blacklists blocks, first 1 minute after the user has connected, and after that every 5 minutes.";
		"This can be tweaked via https://www.unrealircd.org/docs/Set_block#set::blacklist";
	}
}
*** <<<MODULE MANAGER END>>>
*/

#include "unrealircd.h"

ModuleHeader MOD_HEADER
  = {
	"third/blacklistrecheck",
	"1.0.0",
	"Recheck blacklists after a certain time",
	"UnrealIRCd Team",
	"unrealircd-6",
    };

/* The first "quick" recheck: */
#define BLACKLIST_RECHECK_TIME_FIRST 60
/* After that, check every <this>: */
long BLACKLIST_RECHECK_TIME = 300;

//#define LastBLCheck(x)	(moddata_client(x, blacklistrecheck_md).l ? moddata_client(x, blacklistrecheck_md).l : client->local->creationtime)
#define LastBLCheck(x)	(moddata_client(x, blacklistrecheck_md).l)
#define SetLastBLCheck(x, y)	do { moddata_client(x, blacklistrecheck_md).l = y; } while(0)

ModDataInfo *blacklistrecheck_md = NULL;

/* Forward declarations */
EVENT(blacklist_recheck);
int blr_config_test(ConfigFile *cf, ConfigEntry *ce, int type, int *errs);
int blr_config_posttest(int *errs);
int blr_config_run(ConfigFile *cf, ConfigEntry *ce, int type);

MOD_TEST()
{
#if UNREAL_VERSION_TIME >= 202338
	config_error("The blacklist recheck functionality is already included in UnrealIRCd 6.1.2 and later, no need to load third/blacklistrecheck.");
	config_error("Please remove the loadmodule third/blacklistrecheck line from your config file.");
	return MOD_FAILED;
#endif
	HookAdd(modinfo->handle, HOOKTYPE_CONFIGTEST, 0, blr_config_test);
	return MOD_SUCCESS;
}

MOD_INIT()
{
	ModDataInfo mreq;
	memset(&mreq, 0, sizeof(mreq));
	mreq.name = "blacklistrecheck";
	mreq.type = MODDATATYPE_CLIENT;
	blacklistrecheck_md = ModDataAdd(modinfo->handle, mreq);
	if (!blacklistrecheck_md)
	{
		config_error("[blacklistrecheck] failed adding moddata");
		return MOD_FAILED;
	}
	EventAdd(modinfo->handle, "blacklist_recheck", blacklist_recheck, NULL, 2000, 0);
	HookAdd(modinfo->handle, HOOKTYPE_CONFIGRUN, 0, blr_config_run);
	return MOD_SUCCESS;
}

MOD_LOAD()
{
	return MOD_SUCCESS;
}

MOD_UNLOAD()
{
	return MOD_SUCCESS;
}

/** Test the set::blacklist configuration */
int blr_config_test(ConfigFile *cf, ConfigEntry *ce, int type, int *errs)
{
	int errors = 0;
	ConfigEntry *cep, *cepp;

	if (type != CONFIG_SET)
		return 0;
	
	/* We are only interrested in set::blacklist.. */
	if (!ce || !ce->name || strcmp(ce->name, "blacklist"))
		return 0;
	
	for (cep = ce->items; cep; cep = cep->next)
	{
		if (!strcmp(cep->name, "recheck-time"))
		{
			int v;
			if (!cep->value)
			{
				config_error("%s:%i: set::blacklist::recheck-time with no value",
					cep->file->filename, cep->line_number);
				errors++;
			}
			v = config_checkval(cep->value, CFG_TIME);
			if (v < 60)
			{
				config_error("%s:%i: set::blacklist::recheck-time must be more than 60 seconds",
					cep->file->filename, cep->line_number);
				errors++;
			}
		} else
		{
			config_error("%s:%i: unknown directive set::blacklist::%s",
				cep->file->filename, cep->line_number, cep->name);
			errors++;
			continue;
		}
	}
	
	*errs = errors;
	return errors ? -1 : 1;
}

/* Configure ourselves based on the set::blacklist settings */
int blr_config_run(ConfigFile *cf, ConfigEntry *ce, int type)
{
	ConfigEntry *cep, *cepp;

	if (type != CONFIG_SET)
		return 0;
	
	/* We are only interrested in set::blacklist.. */
	if (!ce || !ce->name || strcmp(ce->name, "blacklist"))
		return 0;
	
	for (cep = ce->items; cep; cep = cep->next)
	{
		if (!strcmp(cep->name, "recheck-time"))
			BLACKLIST_RECHECK_TIME = config_checkval(cep->value, CFG_TIME);
	}
	return 1;
}


void blacklist_recheck_user(Client *client)
{
	SetLastBLCheck(client, TStime());
	if (!RCallbacks[CALLBACKTYPE_BLACKLIST_CHECK])
		return; /* blacklist module not loaded */
	RCallbacks[CALLBACKTYPE_BLACKLIST_CHECK]->func.intfunc(client);
}

EVENT(blacklist_recheck)
{
	Client *client;
	time_t last_check;

	list_for_each_entry(client, &lclient_list, lclient_node)
	{
		/* Only check connected users */
		if (!IsUser(client))
			continue;

		last_check = LastBLCheck(client);
		if ((last_check == 0) && (TStime() - client->local->creationtime >= BLACKLIST_RECHECK_TIME_FIRST))
		{
			/* First time: check after 60 seconds already */
			blacklist_recheck_user(client);
		} else /* After that, check every <...> seconds */
		if (last_check && (TStime() - last_check) >= BLACKLIST_RECHECK_TIME)
		{
			blacklist_recheck_user(client);
		}
	}
}
