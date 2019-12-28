/* Copyright (C) All Rights Reserved
** Written by Gottem <support@gottem.nl>
** Website: https://gottem.nl/unreal
** License: https://gottem.nl/unreal/license
*/

/*** <<<MODULE MANAGER START>>>
module {
	documentation "https://gottem.nl/unreal/man/operpasswd";
	troubleshooting "In case of problems, check the FAQ at https://gottem.nl/unreal/halp or e-mail me at support@gottem.nl";
	min-unrealircd-version "5.*";
	//max-unrealircd-version "5.*";
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

#define FlagOperPass 'O'

#define UMODE_DENY 0
#define UMODE_ALLOW 1

#define ENABLE_GLOBAL_NOTICES 0x0001
#define ENABLE_LOGGING 0x0002

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
int operpasswd_configrun(ConfigFile *cf, ConfigEntry *ce, int type);
int operpasswd_hook_quit(Client *client, MessageTag *recv_mtags, char *comment);
int operpasswd_hook_rehash(void);
static void del_failop_counts(void);
int umode_allow_operpriv(Client *client, int what);

static FCount *FailedOperups = NULL;
static char *textbuf = NULL;

long SNO_OPERPASS = 0L;

struct {
	unsigned char options;
	u_int max_failed_operups;
	char *failop_kill_reason;
} Settings;

ModuleHeader MOD_HEADER = {
	"third/operpasswd",
	"2.0",
	"Snomask for failed OPER attempts with the ability to kill",
	"Gottem", // Author
	"unrealircd-5", // Modversion
};

MOD_TEST() {
	HookAdd(modinfo->handle, HOOKTYPE_CONFIGTEST, 0, operpasswd_configtest);
	return MOD_SUCCESS;
}

MOD_INIT() {
	//ModuleSetOptions(modinfo->handle, MOD_OPT_PERM); // May break shit, so commented out
	FailedOperups = NULL;
	textbuf = NULL;
	InitConf();

	CheckAPIError("SnomaskAdd(SNO_OPERPASS)", SnomaskAdd(modinfo->handle, FlagOperPass, umode_allow_operpriv, &SNO_OPERPASS));

	MARK_AS_GLOBAL_MODULE(modinfo);

	HookAdd(modinfo->handle, HOOKTYPE_REHASH, 0, operpasswd_hook_rehash);
	HookAdd(modinfo->handle, HOOKTYPE_CONFIGRUN, 0, operpasswd_configrun);
	HookAdd(modinfo->handle, HOOKTYPE_LOCAL_QUIT, 0, operpasswd_hook_quit);
	return MOD_SUCCESS;
}

MOD_LOAD() {
	// Non-default (lower) priority so we can go *after* set::restrict-commands (if someone decides to override OPER, that is =])
	CheckAPIError("CommandOverrideAddEx(OPER)", CommandOverrideAddEx(modinfo->handle, "OPER", 10, operpasswd_ovr_oper));
	return MOD_SUCCESS;
}

MOD_UNLOAD() {
	FreeConf();
	del_failop_counts();
	safe_free(textbuf);
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
	FCount *f;
	ListStruct *next;

	for(f = FailedOperups; f; f = (FCount *) next) {
		next = (ListStruct *)f->next;
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

int umode_allow_operpriv(Client *client, int what) {
	/* don't check access remotely */
	return(!MyUser(client) || ValidatePermissionsForPath("operpasswd", client, NULL, NULL, NULL)) ? UMODE_ALLOW : UMODE_DENY;
}

int operpasswd_configtest(ConfigFile *cf, ConfigEntry *ce, int type, int *errs) {
	ConfigEntry *cep;
	int errors = 0;

	if(type != CONFIG_MAIN)
		return 0;

	if(!strcmp(ce->ce_varname, "operpasswd")) {
		for(cep = ce->ce_entries; cep; cep = cep->ce_next) {
			if(!cep->ce_varname) {
				config_error("%s:%i: blank %s item", cep->ce_fileptr->cf_filename, cep->ce_varlinenum, ce->ce_varname);
				errors++;
				continue;
			}

			if(!cep->ce_vardata) {
				config_error("%s:%i: %s::%s without value", cep->ce_fileptr->cf_filename, cep->ce_varlinenum, ce->ce_varname, cep->ce_varname);
				errors++;
				continue;
			}

			if(!strcmp(cep->ce_varname, "enable-global-notices"))
				;
			else if(!strcmp(cep->ce_varname, "enable-logging"))
				;
			else if(!strcmp(cep->ce_varname, "max-failed-operups"))
				;
			else if(!strcmp(cep->ce_varname, "failop-kill-reason"))
				;
			else {
				config_error("%s:%i: unknown directive operpasswd::%s", cep->ce_fileptr->cf_filename, cep->ce_varlinenum, cep->ce_varname);
				errors++;
			}

		}
		*errs = errors;
		return errors ? -1 : 1;
	}

	return 0;
}

int operpasswd_configrun(ConfigFile *cf, ConfigEntry *ce, int type) {
	ConfigEntry *cep;

	if(type != CONFIG_MAIN)
		return 0;

	if(!strcmp(ce->ce_varname, "operpasswd")) {
		for(cep = ce->ce_entries; cep; cep = cep->ce_next) {
			if(!strcmp(cep->ce_varname, "enable-global-notices")) {
				if(config_checkval(cep->ce_vardata, CFG_YESNO))
					Settings.options |= ENABLE_GLOBAL_NOTICES;
			}
			else if(!strcmp(cep->ce_varname, "enable-logging")) {
				if(config_checkval(cep->ce_vardata, CFG_YESNO))
					Settings.options |= ENABLE_LOGGING;
			}
			else if(!strcmp(cep->ce_varname, "max-failed-operups"))
				Settings.max_failed_operups = atoi(cep->ce_vardata);
			else if(!strcmp(cep->ce_varname, "failop-kill-reason")) {
				safe_strdup(Settings.failop_kill_reason, cep->ce_vardata);
			}
		}

		if(Settings.options) {
			if(!textbuf)
				textbuf = safe_alloc(BUFSIZE);
		}

		if(!Settings.max_failed_operups)
			del_failop_counts();

		return 1;
	}

	return 0;
}

int operpasswd_hook_quit(Client *client, MessageTag *recv_mtags, char *comment) {
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
		if(Settings.options) {
			snprintf(textbuf, BUFSIZE, "[operpasswd] From: %s, login: %s", client->name, parv[1]);

			if(Settings.options & ENABLE_GLOBAL_NOTICES)
				sendto_snomask_global(SNO_OPERPASS, "*** %s", textbuf);
			else
				sendto_snomask(SNO_OPERPASS, "*** %s", textbuf);

			if(Settings.options & ENABLE_LOGGING)
				ircd_log(LOG_OPER, "%s", textbuf);
		}

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
