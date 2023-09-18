/*
	Licence: GPLv3 or later
	Copyright Ⓒ 2022 Valerie Pond
	draft/react

	VERSION
	0.1:
		-Test-run
	
	1.0:
		-Added backwards compatibility, in the sense that if someone doesn't support message-tags,
		 then they will instead see the equivalent of the user saying /me reacted with "so and so"
		-Added a configurable allowed reacts. These can be a string of up to 11 chars long, which
		 includes most if not all emojis and "ellemayo", "roflcopter", and most of the other fun
		 acronyms and symbols.

  React to a message
*/
/*** <<<MODULE MANAGER START>>>
module
{
		documentation "https://github.com/ValwareIRC/valware-unrealircd-mods/blob/main/react/README.md";
		troubleshooting "v.a.pond@outlook.com";
		min-unrealircd-version "6.*";
		max-unrealircd-version "6.*";
		post-install-text {
				"The module is installed. Now all you need to do is add a loadmodule line:";
				"loadmodule \"third/react\";";
				"You need to restart your server for this to show up in CLIENTTAGDENY";
				"Please see the README for configuration syntax";
		}
}
*** <<<MODULE MANAGER END>>>
*/


#include "unrealircd.h"

/* Maximim length of allowed reacts.
 * This is the limit I impose to cater for all emojis, but also
 * make it so that
 */

ModuleHeader MOD_HEADER
  = {
	"third/react-tag",
	"1.0",
	"Provides +draft/react (IRCv3) related functionality",
	"Valware",
	"unrealircd-6",
};

#define REACT_ENTRY_LIMIT 11 // allowed length of each comma delimited string
#define CONF_REACT_ALLOW "allowed-reacts" // the config item name
#define REACT_CAP "valware.uk/react" // the CAP to request to use this functionality
#define CMD_REACTSLIST "REACTSLIST"

int react_is_ok(Client *client, const char *name, const char *value);
void react_new_message(Client *client, MessageTag *recv_mtags, MessageTag **mtag_list, const char *signature);

int reactlist_configtest(ConfigFile *cf, ConfigEntry *ce, int type, int *errs);
int reactlist_configrun(ConfigFile *cf, ConfigEntry *ce, int type);
int chan_react_backwards_compat(Client *client, Channel *channel, int sendflags, const char *prefix, const char *target, MessageTag *mtags, const char *text, SendType sendtype);
int user_react_backwards_compat(Client *client, Client *to, MessageTag *mtags, const char *text, SendType sendtype);
int react_welcome(Client *client, int n);
long CAP_REACT = 0L;
CMD_FUNC(cmd_reactslist);

typedef struct
{
	char *fullstring;
	MultiLine *reacts_list;
	unsigned short int empty;
} react_list;

static react_list ourconf;


void freeconf(void)
{
	freemultiline(ourconf.reacts_list);
	ourconf.empty = 0;
}

MOD_INIT()
{

	/* reset our struct */
	freeconf();
	safe_free(ourconf.fullstring);
	memset(&ourconf, 0, sizeof(ourconf));

	/* Add the message tag */
	MessageTagHandlerInfo mtag;
	memset(&mtag, 0, sizeof(mtag));

	ClientCapabilityInfo cap;
	memset(&cap, 0, sizeof(cap));
	cap.name = REACT_CAP;
	ClientCapabilityAdd(modinfo->handle, &cap, &CAP_REACT);

	mtag.is_ok = react_is_ok;
	mtag.flags = MTAG_HANDLER_FLAGS_NO_CAP_NEEDED;
	mtag.name = "+draft/react";
	MessageTagHandlerAdd(modinfo->handle, &mtag);

	/* Command for getting reacts list */
	CommandAdd(modinfo->handle, CMD_REACTSLIST, cmd_reactslist, 0, CMD_USER);

	/* find a hooker */
	HookAdd(modinfo->handle, HOOKTYPE_CONFIGRUN, 0, reactlist_configrun);
	HookAdd(modinfo->handle, HOOKTYPE_CHANMSG, 0, chan_react_backwards_compat);
	HookAdd(modinfo->handle, HOOKTYPE_USERMSG, 0, user_react_backwards_compat);
	HookAdd(modinfo->handle, HOOKTYPE_WELCOME, 0, react_welcome);
	HookAddVoid(modinfo->handle, HOOKTYPE_NEW_MESSAGE, 0, react_new_message);

	/* ;) */
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

MOD_TEST()
{
	ourconf.empty = 1;
	HookAdd(modinfo->handle, HOOKTYPE_CONFIGTEST, 0, reactlist_configtest);
	return MOD_SUCCESS;
}

int react_is_ok(Client *client, const char *name, const char *value)
{
	if (!HasCapabilityFast(client, CAP_REACT))
	{
		sendto_one(client, NULL, "FAIL * CANNOT_DO_REACT :Your react \"%s\" was not sent. This server requires you to `/CAP REQ valware.uk/react` to react to messages.", value);
		return 0;
	}
	MultiLine *ml;
	if (BadPtr(value) || strlen(value) > REACT_ENTRY_LIMIT) // longest emoji is 👩‍👩‍👧‍👧 with 11 unicode code points
		return 0;

	if (!strcmp(ourconf.fullstring,"*"))
		return 1;

	for (ml = ourconf.reacts_list; ml; ml = ml->next)
		if (!strcmp(ml->line, value))
			return 1;

	return 0;
}

void react_new_message(Client *client, MessageTag *recv_mtags, MessageTag **mtag_list, const char *signature)
{
	MessageTag *m;

	if (IsUser(client))
	{
		m = find_mtag(recv_mtags, "+draft/react");
		if (m)
		{
			m = duplicate_mtag(m);
			AddListItem(m, *mtag_list);
			add_fake_lag(client, 1000); // try to limit tag spamming
		}
	}
}


int reactlist_configtest(ConfigFile *cf, ConfigEntry *ce, int type, int *errs)
{
	freeconf();
	int errors = 0;

	if (type != CONFIG_SET)
		return 0;

	if (!ce || !ce->name)
		return 0;

	if (strcmp(ce->name, CONF_REACT_ALLOW))
		return 0;

	if (BadPtr(ce->value))
	{
		config_warn("%s:%i: \"%s\" is defined but is empty", ce->file->filename, ce->line_number, CONF_REACT_ALLOW);
		return 1;
	}
	if (strlen(ce->value) > 400)
	{
		config_error("%s:%i: \"%s\" is too long. May not be over 400 chars in length.", ce->file->filename, ce->line_number, CONF_REACT_ALLOW);
		config_error("If you wish to allow everything, just set the value of %s to be \"*\" an asterisk.", CONF_REACT_ALLOW);
		errors++;
		return -1;
	}


	/* dup string */
	char *config_entry_value = NULL;
	config_entry_value = (char *)malloc(strlen(ce->value) + 1);
	strcpy(config_entry_value, ce->value);

	/* NOTE: we don't really validate the existence of emojis
	* we only evaluate length of each entry because it could be a series
	* of too many expected string configurations
	* we just know the longest emoji is 11 chars! 👩‍👩‍👧‍👧
	*/
	for (char *tok = strtok(config_entry_value, ","); tok != NULL; tok = strtok(NULL, ","))
	{
		if (strlen(tok) > REACT_ENTRY_LIMIT)
		{
			config_error("%s:%i: \"%s\" invalid entry, each string may not be more than %d characters long: %s", ce->file->filename, ce->line_number, CONF_REACT_ALLOW, REACT_ENTRY_LIMIT, tok);
			errors++;
			continue;
		}
		else ourconf.empty = 0;
	}
	free(config_entry_value);

	*errs = errors;
	return errors ? -1 : 1;
}

int reactlist_configrun(ConfigFile *cf, ConfigEntry *ce, int type)
{
	if (type != CONFIG_SET) // we needa set{} blocc
		return 0;

	/* do some checks ...*/
	if (!ce || !ce->name || BadPtr(ce->value) || strcmp(ce->name, CONF_REACT_ALLOW) || ourconf.empty == 1 || strlen(ce->value) > 400)
		return 0;
	
	/* dup string */
	char *config_entry_value = NULL;
	config_entry_value = (char *)malloc(strlen(ce->value) + 1);
	strcpy(config_entry_value, ce->value);

	char *tok = strtok(config_entry_value, ",");
	while (tok != NULL)
	{
		addmultiline(&ourconf.reacts_list, tok);
		tok = strtok(NULL, ",");
	}
	safe_strdup(ourconf.fullstring, ce->value);
	free(config_entry_value);
	return 1; // We good
}

int chan_react_backwards_compat(Client *client, Channel *channel, int sendflags, const char *prefix, const char *target, MessageTag *mtags, const char *text, SendType sendtype)
{
	Member *member;
	MessageTag *m;
	m = find_mtag(mtags, "+draft/react");
	if (m)
		for (member = channel->members; member; member = member->next)
			if (!HasCapabilityFast(member->client, CAP_REACT))
				sendto_one(member->client, NULL, ":%s!%s@%s PRIVMSG %s :\1ACTION reacted with \"%s\"\1", client->name, client->user->username,
							strlen(client->user->virthost) ? client->user->virthost : client->user->cloakedhost, channel->name, m->value); // who cares if it's a cloak this one time
										/* drifting awaaay, slowly drifting, wave after wave */
	return 0;
}

int user_react_backwards_compat(Client *client, Client *to, MessageTag *mtags, const char *text, SendType sendtype)
{
	MessageTag *m;
	m = find_mtag(mtags, "+draft/react");
	if (!m)
		return 0;

	if (!HasCapabilityFast(to, CAP_REACT))
		sendto_one(to, NULL, ":%s!%s@%s PRIVMSG %s :\1ACTION reacted with \"%s\"\1", client->name, client->user->username,
					strlen(client->user->virthost) ? client->user->virthost : client->user->cloakedhost, to->name, m->value); // who cares if it's a cloak this one time

	return 0;
}

int react_welcome(Client *client, int n)
{
	// MORE ISUPPORT?!?!?!?!
	if (n == 5 && ourconf.fullstring)
		sendto_one(client, NULL, ":%s REACTS %s :%s", me.name, client->name, ourconf.fullstring);
	return 0;
}

CMD_FUNC(cmd_reactslist)
{
	sendto_one(client, NULL, ":%s REACTS %s :%s", me.name, client->name, ourconf.fullstring);
	add_fake_lag(client, 3000);
}
