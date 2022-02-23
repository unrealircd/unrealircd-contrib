/* Copyright (C) All Rights Reserved
** Written by Gottem <support@gottem.nl>
** Website: https://gottem.nl/unreal
** License: https://gottem.nl/unreal/license
*/

/*** <<<MODULE MANAGER START>>>
module {
	documentation "https://gottem.nl/unreal/man/commandsno";
	troubleshooting "In case of problems, check the FAQ at https://gottem.nl/unreal/halp or e-mail me at support@gottem.nl";
	min-unrealircd-version "6.*";
	//max-unrealircd-version "6.*";
	post-install-text {
		"The module is installed, now all you need to do is add a 'loadmodule' line to your config file:";
		"loadmodule \"third/commandsno\";";
		"Then /rehash the IRCd.";
		"For usage information, refer to the module's documentation found at: https://gottem.nl/unreal/man/commandsno";
	}
}
*** <<<MODULE MANAGER END>>>
*/

// One include for all cross-platform compatibility thangs
#include "unrealircd.h"

typedef struct _cmdovr CmdOvr;

struct _cmdovr {
	CmdOvr *prev, *next;
	char *cmd;
};

#define FLAG_CMD 'C'
#define MaxSize (sizeof(mybuf) - strlen(mybuf) - 1)

#define CheckAPIError(apistr, apiobj) \
	do { \
		if(!(apiobj)) { \
			config_error("A critical error occurred on %s for %s: %s", (apistr), MOD_HEADER.name, ModuleGetErrorStr(modinfo->handle)); \
			return MOD_FAILED; \
		} \
	} while(0)

CMD_OVERRIDE_FUNC(commandsno_override_cmd);
int commandsno_configtest(ConfigFile *cf, ConfigEntry *ce, int type, int *errs);
int commandsno_configrun(ConfigFile *cf, ConfigEntry *ce, int type);
int commandsno_hook_stats(Client *client, const char *stats);
int commandsno_hook_rehash(void);
static void InitConf(void);
static void FreeConf(void);

char *cmdlist;

ModuleHeader MOD_HEADER = {
	"third/commandsno",
	"2.1.0", // Version
	"Lets IRC operators see command usages",
	"Gottem", // Author
	"unrealircd-6", // Modversion
};

MOD_TEST() {
	HookAdd(modinfo->handle, HOOKTYPE_CONFIGTEST, 0, commandsno_configtest);
	return MOD_SUCCESS;
}

MOD_INIT() {
	InitConf();

	MARK_AS_GLOBAL_MODULE(modinfo);

	HookAdd(modinfo->handle, HOOKTYPE_REHASH, 0, commandsno_hook_rehash);
	HookAdd(modinfo->handle, HOOKTYPE_CONFIGRUN, 0, commandsno_configrun);
	HookAdd(modinfo->handle, HOOKTYPE_STATS, 0, commandsno_hook_stats);
	return MOD_SUCCESS;
}

MOD_LOAD() {
	if(cmdlist) {
		char *cmd, *p;
		char buf[64]; // Should be plenty, n0?
		char apibuf[96];

		strlcpy(buf, cmdlist, sizeof(buf));
		for(cmd = strtoken(&p, buf, ","); cmd; cmd = strtoken(&p, NULL, ",")) {
			if(!strcasecmp(cmd, "PRIVMSG") || !strcasecmp(cmd, "NOTICE"))
				continue;

			snprintf(apibuf, sizeof(apibuf), "CommandOverrideAdd(%s)", cmd);
			CheckAPIError(apibuf, CommandOverrideAdd(modinfo->handle, cmd, 0, commandsno_override_cmd));
		}
	}
	return MOD_SUCCESS;
}

MOD_UNLOAD() {
	FreeConf();
	return MOD_SUCCESS;
}

static void InitConf(void) {
	cmdlist = NULL;
}

static void FreeConf(void) {
	safe_free(cmdlist);
}

int commandsno_hook_rehash(void) {
	FreeConf();
	InitConf();
	return HOOK_CONTINUE;
}

int commandsno_configtest(ConfigFile *cf, ConfigEntry *ce, int type, int *errs) {
	int errors = 0;

	if(type != CONFIG_SET)
		return 0;

	if(strcmp(ce->name, "commandsno"))
		return 0;

	if(!ce->value || !strlen(ce->value)) {
		config_error("%s:%i: set::%s without contents", ce->file->filename, ce->line_number, ce->name);
		errors++;
	}
	*errs = errors;
	return errors ? -1 : 1;
}

int commandsno_configrun(ConfigFile *cf, ConfigEntry *ce, int type) {
	if(type != CONFIG_SET)
		return 0;

	if(strcmp(ce->name, "commandsno"))
		return 0;

	safe_strdup(cmdlist, ce->value);
	return 1;
}

int commandsno_hook_stats(Client *client, const char *stats) {
	if(*stats == 'S') // Corresponds to "set" stats
		sendnumericfmt(client, RPL_TEXT, ":commandsno: %s", cmdlist ? cmdlist : "<none>");
	return HOOK_CONTINUE;
}

CMD_OVERRIDE_FUNC(commandsno_override_cmd) {
	if(MyUser(client) && !IsULine(client)) {
		char mybuf[BUFSIZE];
		int i;
		mybuf[0] = 0;

		for(i = 1; i < parc && !BadPtr(parv[i]); i++) {
			if(mybuf[0])
				strncat(mybuf, " ", MaxSize);
			strncat(mybuf, parv[i], MaxSize);
		}
		if(!mybuf[0])
			strcpy(mybuf, "<none>");

		unreal_log(ULOG_INFO, "commandsno", "COMMANDSNO_USAGE", client, "$client.details used command $command (params: $params)",
			log_data_string("command", ovr->command->cmd),
			log_data_string("params", mybuf)
		);
	}

	CallCommandOverride(ovr, client, recv_mtags, parc, parv); // Run original function yo
}
