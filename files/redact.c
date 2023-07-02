/*
** Copyright (C) 2023 Valentin Lorentz
** License: GPLv3 https://www.gnu.org/licenses/gpl-3.0.html
*/

/*** <<<MODULE MANAGER START>>>
module
{
		documentation "https://github.com/ircv3/ircv3-specifications/pull/524";
		troubleshooting "In case of problems, contact val on irc.unrealircd.org.";
		min-unrealircd-version "6.1.2";
		post-install-text {
				"The module is installed. Now all you need to do is add a loadmodule line:";
				"loadmodule \"third/redact\";";
  				"And /REHASH the IRCd.";
				"The module may be additionaly configured to change the defaults.";
				"See documentation for help.";
				"Please note that the implemented feature is still \"Work In Progress\".";
		}
}
*** <<<MODULE MANAGER END>>>
*/

#include "unrealircd.h"


ModuleHeader MOD_HEADER = {
	"third/redact",
	"6.0",
	"Implements the draft IRCv3 message-redaction specification",
	"val",
	"unrealircd-6"
};

/* Forward declarations */
CMD_FUNC(cmd_redact);

/* Global variables */
long CAP_MESSAGE_REDACTION = 0L;
bool sender_can_redact = 0;
char *chan_access_pattern = "";


int redact_config_run(ConfigFile *cf, ConfigEntry *ce, int type) {
	int errors = 0;
	ConfigEntry *cep, *cep2;

	if (type != CONFIG_SET)
		return 0;

	if (!ce || !ce->name || strcmp(ce->name, "redacters"))
		return 0;

	bool owners_can_redact = 0;
	bool admins_can_redact = 0;
	bool ops_can_redact = 0;
	bool halfops_can_redact = 0;

	for (cep = ce->items; cep; cep = cep->next)
	{
		if (!cep->name)
		{
			config_error("%s:%i: blank set::redacters item",
				cep->file->filename, cep->line_number);
			errors++;
			continue;
		}
		else if (!strcmp(cep->name, "owner"))
			owners_can_redact = 1;
		else if (!strcmp(cep->name, "admin"))
			admins_can_redact = 1;
		else if (!strcmp(cep->name, "op"))
			ops_can_redact = 1;
		else if (!strcmp(cep->name, "halfop"))
			halfops_can_redact = 1;
		else if (!strcmp(cep->name, "sender"))
			sender_can_redact = 1;
		else {
			/* Should have been caught in redact_config_test */
			continue;
		}
	}

	if (halfops_can_redact)
		chan_access_pattern = "hoaq";
	else if (ops_can_redact)
		chan_access_pattern = "oaq";
	else if (admins_can_redact)
		chan_access_pattern = "aq";
	else if (owners_can_redact)
		chan_access_pattern = "q";
	else
		chan_access_pattern = "";

	return 1;
}

int redact_config_test(ConfigFile *cf, ConfigEntry *ce, int type, int *errs) {
	int errors = 0;
	ConfigEntry *cep, *cep2;

	if (type != CONFIG_SET)
		return 0;

	if (!ce || !ce->name || strcmp(ce->name, "redacters"))
		return 0;

	for (cep = ce->items; cep; cep = cep->next)
	{
		if (!cep->name)
		{
			config_error("%s:%i: blank set::redacters item",
				cep->file->filename, cep->line_number);
			errors++;
			continue;
		}
		else if (!strcmp(cep->name, "owner") && !strcmp(cep->name, "admin") && !strcmp(cep->name, "op") && !strcmp(cep->name, "halfop") && !strcmp(cep->name, "sender")) {
			config_error("%s:%i: invalid set::redacters item: %s",
				cep->file->filename, cep->line_number, cep->name);
			errors++;
			continue;
		}
	}

	*errs = errors;
	return errors ? -1 : 1;
}

MOD_INIT()
{
	ClientCapabilityInfo c;

	/* Will be set when reading the config */
	chan_access_pattern = "";
	sender_can_redact = 0;

	CommandAdd(modinfo->handle, "REDACT", cmd_redact, MAXPARA, CMD_USER|CMD_SERVER);
	HookAdd(modinfo->handle, HOOKTYPE_CONFIGRUN, 0, redact_config_run);

	memset(&c, 0, sizeof(c));
	c.name = "draft/message-redaction";
	ClientCapabilityAdd(modinfo->handle, &c, &CAP_MESSAGE_REDACTION);
	return MOD_SUCCESS;
}

MOD_LOAD()
{
	HookAdd(modinfo->handle, HOOKTYPE_CONFIGRUN, 0, redact_config_run);
	return MOD_SUCCESS;
}

MOD_TEST()
{
	HookAdd(modinfo->handle, HOOKTYPE_CONFIGTEST, 0, redact_config_test);
	return MOD_SUCCESS;
}

MOD_UNLOAD()
{
	return MOD_SUCCESS;
}

CMD_FUNC(cmd_redact)
{
	HistoryFilter *filter = NULL;
	HistoryResult *r = NULL;
	Channel *channel;
	char *account;
	char *error;
	int deleted, rejected_deletes;

	if ((parc < 2) || BadPtr(parv[1]))
	{
		sendnumeric(client, ERR_NEEDMOREPARAMS, "REDACT");
		return;
	}

	channel = find_channel(parv[1]);
	if (!channel)
	{
		sendto_one(client, NULL, ":%s FAIL REDACT INVALID_TARGET %s :Chat history is not enabled in PM",
			me.name, parv[1]);
		return;
	}

	int is_oper = ValidatePermissionsForPath("chat:redact",client,NULL,NULL,NULL);
	bool check_sender = 0;

	if (MyUser(client) && !is_oper && (BadPtr(chan_access_pattern) || !check_channel_access(client, channel, chan_access_pattern)))
	{
		if (sender_can_redact)
			/* Provisionally allow the command, until we retrieved the message
			 * they are trying to delete (because we need to check they are its
			 * sender */
			check_sender = 1;
		else {
			error = "sender can't redact, and neither oper nor chanop";
			goto unauthorized;
		}
	}


	filter = safe_alloc(sizeof(HistoryFilter));
	filter->cmd = HFC_AROUND;
	filter->msgid_a = strdup(parv[2]);
	filter->limit = 1;

	if (check_sender) {
		if (IsLoggedIn(client))
			filter->account = strdup(client->user->account);
		else {
			error = "neither oper/chanop nor logged in";
			goto unauthorized;
		}
	}

	deleted = history_delete(channel->name, filter, &rejected_deletes);

	if (deleted > 1) {
		sendto_one(client, NULL, ":%s FAIL REDACT UNKNOWN_ERROR %s :history_delete found more than one result",
			me.name, parv[1]);
		goto end;
	}
	else if (rejected_deletes > 1) {
		sendto_one(client, NULL, ":%s FAIL REDACT UNKNOWN_ERROR %s :history_delete rejected more than one result",
			me.name, parv[1]);
		goto end;
	}
	else if (deleted && rejected_deletes) {
		sendto_one(client, NULL, ":%s FAIL REDACT UNKNOWN_ERROR %s %s :history_delete both deleted and rejected",
			me.name, parv[1], parv[2]);
		goto end;
	}
	else if (rejected_deletes) {
		error = "not sender";
		goto unauthorized;
	}
	else if (!deleted) {
		sendto_one(client, NULL, ":%s FAIL REDACT UNKNOWN_MSGID %s %s :This message does not exist or is too old",
			me.name, parv[1], parv[2]);
		goto end;
	}

	if (parc >= 3) {
		/* Has a reason */
		sendto_channel(channel, client, /* skip */ NULL, /* member_modes */ NULL,
				   CAP_MESSAGE_REDACTION, SEND_ALL, /* mtags */ NULL,
				   ":%s REDACT %s %s :%s",
				   client->name, parv[1], parv[2], parv[3]);
	}
	else {
		sendto_channel(channel, client, /* skip */ NULL, /* member_modes */ NULL,
				   CAP_MESSAGE_REDACTION, SEND_ALL, /* mtags */ NULL,
				   ":%s REDACT %s %s",
				   client->name, parv[1], parv[2]);
	}

	goto end;

unauthorized:
	sendto_one(client, NULL, ":%s FAIL REDACT REDACT_FORBIDDEN %s %s :Your are not authorized to redact messages in %s: %s",
		me.name, parv[1], parv[2], parv[1], error);
end:
	if (filter)
		free_history_filter(filter);
	if (r)
		free_history_result(r);
}
