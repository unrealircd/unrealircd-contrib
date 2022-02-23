/* Copyright (C) All Rights Reserved
** Written by Gottem <support@gottem.nl>
** Website: https://gottem.nl/unreal
** License: https://gottem.nl/unreal/license
*/

/*** <<<MODULE MANAGER START>>>
module {
	documentation "https://gottem.nl/unreal/man/operpasswd";
	troubleshooting "In case of problems, check the FAQ at https://gottem.nl/unreal/halp or e-mail me at support@gottem.nl";
	min-unrealircd-version "6.*";
	//max-unrealircd-version "6.*";
	post-install-text {
		"The module is installed, now all you need to do is add a 'loadmodule' line to your config file:";
		"loadmodule \"third/operpasswd\";";
		"Then /rehash the IRCd.";
		"For usage information, refer to the module's documentation found at: https://gottem.nl/unreal/man/operpasswd";
	}
}
*** <<<MODULE MANAGER END>>>
*/

// One include for all cross-platform compatibility thangs
#include "unrealircd.h"

typedef struct _fcount FCount;

struct _fcount {
	FCount *prev, *next;
	Client *client;
	u_int count;
};

#define CheckAPIError(apistr, apiobj) \
	do { \
		if(!(apiobj)) { \
			config_error("A critical error occurred on %s for %s: %s", (apistr), MOD_HEADER.name, ModuleGetErrorStr(modinfo->handle)); \
			return MOD_FAILED; \
		} \
	} while(0)

static void InitConf(void);
static void FreeConf(void);

CMD_OVERRIDE_FUNC(operpasswd_ovr_oper);
int operpasswd_configtest(ConfigFile *cf, ConfigEntry *ce, int type, int *errs);
int operpasswd_configposttest(int *errs);
int operpasswd_configrun(ConfigFile *cf, ConfigEntry *ce, int type);
int operpasswd_hook_quit(Client *client, MessageTag *recv_mtags, const char *comment);
int operpasswd_hook_rehash(void);
static void del_failop_counts(void);

static FCount *FailedOperups = NULL;

struct {
	u_int max_failed_operups;
	char *failop_kill_reason;

	u_int has_max_failed_operups;
	u_int has_failop_kill_reason;
} Settings;

ModuleHeader MOD_HEADER = {
	"third/operpasswd",
	"2.1.0", // Version
	"Kill users with too many failed OPER attempts",
	"Gottem", // Author
	"unrealircd-6", // Modversion
};

MOD_TEST() {
	HookAdd(modinfo->handle, HOOKTYPE_CONFIGTEST, 0, operpasswd_configtest);
	HookAdd(modinfo->handle, HOOKTYPE_CONFIGPOSTTEST, 0, operpasswd_configposttest);
	return MOD_SUCCESS;
}

MOD_INIT() {
	FailedOperups = NULL;
	InitConf();

	MARK_AS_GLOBAL_MODULE(modinfo);

	HookAdd(modinfo->handle, HOOKTYPE_REHASH, 0, operpasswd_hook_rehash);
	HookAdd(modinfo->handle, HOOKTYPE_CONFIGRUN, 0, operpasswd_configrun);
	HookAdd(modinfo->handle, HOOKTYPE_LOCAL_QUIT, 0, operpasswd_hook_quit);
	return MOD_SUCCESS;
}

MOD_LOAD() {
	// Non-default (lower) priority so we can go *after* set::restrict-commands (if someone decides to override OPER, that is =])
	CheckAPIError("CommandOverrideAdd(OPER)", CommandOverrideAdd(modinfo->handle, "OPER", 10, operpasswd_ovr_oper));
	return MOD_SUCCESS;
}

MOD_UNLOAD() {
	FreeConf();
	del_failop_counts();
	return MOD_SUCCESS;
}

static void InitConf(void) {
	memset(&Settings, 0, sizeof(Settings));
}

static void FreeConf(void) {
	safe_free(Settings.failop_kill_reason);
}

int operpasswd_hook_rehash() {
	FreeConf();
	InitConf();
	return HOOK_CONTINUE;
}

static void del_failop_counts() {
	FCount *f, *next;
	for(f = FailedOperups; f; f = next) {
		next = f->next;
		DelListItem(f, FailedOperups);
		safe_free(f);
	}
}

static FCount *find_failop_count(Client *client) {
	FCount *f;
	for(f = FailedOperups; f; f = f->next) {
		if(f->client == client)
			break;
	}
	return f;
}

int operpasswd_configtest(ConfigFile *cf, ConfigEntry *ce, int type, int *errs) {
	ConfigEntry *cep;
	int errors = 0;
	int i;

	if(type != CONFIG_MAIN)
		return 0;

	if(!strcmp(ce->name, "operpasswd")) {
		for(cep = ce->items; cep; cep = cep->next) {
			if(!cep->name) {
				config_error("%s:%i: blank %s item", cep->file->filename, cep->line_number, ce->name);
				errors++;
				continue;
			}

			if(!cep->value || !strlen(cep->value)) {
				config_error("%s:%i: %s::%s without value", cep->file->filename, cep->line_number, ce->name, cep->name);
				errors++;
				continue;
			}

			if(!strcmp(cep->name, "max-failed-operups")) {
				if(Settings.has_max_failed_operups) {
					config_error("%s:%i: duplicate value for %s::%s", cep->file->filename, cep->line_number, ce->name, cep->name);
					errors++;
					continue;
				}

				Settings.has_max_failed_operups = 1;
				i = atoi(cep->value);
				if(i < 0 || i > 100) {
					config_error("%s:%i: invalid value for %s::%s (must be an integer from 1-100, inclusive)", cep->file->filename, cep->line_number, ce->name, cep->name);
					errors++;
				}
				continue;
			}

			if(!strcmp(cep->name, "failop-kill-reason")) {
				if(Settings.has_failop_kill_reason) {
					config_error("%s:%i: duplicate value for %s::%s", cep->file->filename, cep->line_number, ce->name, cep->name);
					errors++;
					continue;
				}

				Settings.has_failop_kill_reason = 1;
				continue;
			}

			config_error("%s:%i: unknown directive operpasswd::%s", cep->file->filename, cep->line_number, cep->name);
			errors++;
		}

		*errs = errors;
		return errors ? -1 : 1;
	}

	return 0;
}

int operpasswd_configposttest(int *errs) {
	if(!Settings.has_max_failed_operups) {
		config_error("[operpasswd] max-failed-operups is a required configuration setting");
		(*errs)++;
		return -1;
	}
	return 1;
}

int operpasswd_configrun(ConfigFile *cf, ConfigEntry *ce, int type) {
	ConfigEntry *cep;

	if(type != CONFIG_MAIN)
		return 0;

	if(!strcmp(ce->name, "operpasswd")) {
		for(cep = ce->items; cep; cep = cep->next) {
			if(!strcmp(cep->name, "max-failed-operups"))
				Settings.max_failed_operups = atoi(cep->value);
			else if(!strcmp(cep->name, "failop-kill-reason"))
				safe_strdup(Settings.failop_kill_reason, cep->value);
		}
		return 1;
	}

	return 0;
}

int operpasswd_hook_quit(Client *client, MessageTag *recv_mtags, const char *comment) {
	FCount *f;
	if((f = find_failop_count(client))) {
		DelListItem(f, FailedOperups);
		safe_free(f);
	}
	return HOOK_CONTINUE;
}

CMD_OVERRIDE_FUNC(operpasswd_ovr_oper) {
	FCount *f;

	CallCommandOverride(ovr, client, recv_mtags, parc, parv); // Run original function yo

	/* No need to check if '!MyConnect(client)'. */
	if(!IsUser(client) || IsOper(client) || SVSNOOP || parc < 3)
		return;

	if(!IsOper(client)) {
		if(Settings.max_failed_operups) {
			if(!(f = find_failop_count(client))) {
				f = safe_alloc(sizeof(FCount));
				f->client = client;
				f->count = 1;
				AddListItem(f, FailedOperups);
			}
			else {
				f->count++;
				if(f->count > Settings.max_failed_operups)
					exit_client(client, NULL, (Settings.failop_kill_reason ? Settings.failop_kill_reason : "Too many failed OPER attempts"));
			}
		}
	}
}
