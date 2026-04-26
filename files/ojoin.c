/* OJOIN
 * GPLv3 or later
 * Copyright Ⓒ 2022-2025 Valerie Pond
 *
 * Inspircd by InspIRCd's third module of the same name
 */

/*** <<<MODULE MANAGER START>>>
module {
	documentation "https://github.com/ValwareIRC/valware-unrealircd-mods/blob/main/ojoin/README.md";
	troubleshooting "In case of problems, please file a bug report at https://github.com/ValwareIRC/valware-unrealircd-mods/issues/new?template=bug_report.md";
	min-unrealircd-version "6.*";
	//max-unrealircd-version "6.*";
	post-install-text {
		"The module is installed, now all you need to do is add a 'loadmodule' line to your config file:";
		"loadmodule \"third/ojoin\";";
		"Then /rehash the IRCd.";
		"For usage information, refer to the module's documentation found at: https://github.com/ValwareIRC/valware-unrealircd-mods/blob/main/ojoin/";
		"Provides the server-admin only command /OJOIN. Requires operclass permission 'ojoin'";
	}
}
*** <<<MODULE MANAGER END>>>
*/

#include "unrealircd.h"

/* Defines */
#define RANK_SOPMODE 5000
#define CMD_OJOIN "OJOIN"
#define MODE_SOPMODE 'Y'
#define PREFIX_SOPMODE '!'

int ojoin_configtest(ConfigFile *cf, ConfigEntry *ce, int type, int *errs);
int queue_configrun(ConfigFile *cf, ConfigEntry *ce, int type);
struct configstruct
{
	int show_entry_message;
	char *entry_message;
	char *show_to;
	
	unsigned short int got_entry_message;
	unsigned short int got_show_entry_message;
	unsigned short int got_show_to;
};
static struct configstruct conf;


ModuleHeader MOD_HEADER = {
	"third/ojoin",
	"2.1",
	"/OJOIN Command and Channel Mode +Y (Server Operator)",
	"Valware",
	"unrealircd-6",
};



/* Forward declarations */
CMD_FUNC(ojoin);
int cmode_sopmode_is_ok(Client *client, Channel *channel, char mode, const char *para, int type, int what);
int ojoin_kick_check(Client *client, Client *target, Channel *channel, const char *comment, const char *client_member_modes, const char *target_member_modes, const char **reject_reason);

/* Helper function to convert prefixes to mode letters (can return NULL for everyone) */
char *convert_prefixes_to_modes(const char *prefixes)
{
	static char modes[32];
	char *p = modes;
	const char *s;
	
	if (!prefixes)
		return NULL; // NULL means show to everyone (default)
	
	memset(modes, 0, sizeof(modes));
	
	for (s = prefixes; *s; s++)
	{
		switch (*s)
		{
			case '+': *p++ = 'v'; break; // voice
			case '%': *p++ = 'h'; break; // halfop
			case '@': *p++ = 'o'; break; // op
			case '&': *p++ = 'a'; break; // admin/protect
			case '~': *p++ = 'q'; break; // owner
			case '!': *p++ = 'Y'; break; // server operator
			case 'v': *p++ = 'v'; break; // voice (mode letter)
			case 'h': *p++ = 'h'; break; // halfop (mode letter)
			case 'o': *p++ = 'o'; break; // op (mode letter)
			case 'a': *p++ = 'a'; break; // admin (mode letter)
			case 'q': *p++ = 'q'; break; // owner (mode letter)
			case 'Y': *p++ = 'Y'; break; // server operator (mode letter)
		}
	}
	
	// If no valid modes found, show to everyone
	if (modes[0] == '\0')
		return NULL;
	
	return modes;
}

/* Helper function to substitute $nick in the entry message */
char *substitute_entry_message(const char *message, const char *nickname)
{
	static char result[512];
	const char *src;
	char *dst;
	
	if (!message || !nickname)
		return (char *)message;
	
	src = message;
	dst = result;
	
	while (*src && (dst - result) < (sizeof(result) - 1))
	{
		if (*src == '$' && strncmp(src, "$nick", 5) == 0)
		{
			// Replace $nick with actual nickname
			const char *nick_ptr = nickname;
			while (*nick_ptr && (dst - result) < (sizeof(result) - 1))
			{
				*dst++ = *nick_ptr++;
			}
			src += 5; // Skip past "$nick"
		}
		else
		{
			*dst++ = *src++;
		}
	}
	*dst = '\0';
	
	return result;
}


MOD_INIT()
{
	CmodeInfo creq;
	MARK_AS_GLOBAL_MODULE(modinfo);

	/* Channel mode +Y */
	memset(&creq, 0, sizeof(creq));
	creq.paracount = 1;
	creq.is_ok = cmode_sopmode_is_ok;
	creq.letter = MODE_SOPMODE;
	creq.prefix = PREFIX_SOPMODE;
	creq.sjoin_prefix = '^';
	creq.rank = RANK_SOPMODE;
	creq.unset_with_param = 1;
	creq.type = CMODE_MEMBER;
	CmodeAdd(modinfo->handle, creq, NULL);
	CommandAdd(modinfo->handle, CMD_OJOIN, ojoin, MAXPARA, CMD_USER);
	HookAdd(modinfo->handle, HOOKTYPE_CAN_KICK, 0, ojoin_kick_check);
	HookAdd(modinfo->handle, HOOKTYPE_CONFIGRUN, 0, queue_configrun);

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


MOD_TEST()
{
	memset(&conf, 0, sizeof(conf));
	HookAdd(modinfo->handle, HOOKTYPE_CONFIGTEST, 0, ojoin_configtest);
	return MOD_SUCCESS;
}


int cmode_sopmode_is_ok(Client *client, Channel *channel, char mode, const char *param, int type, int what)
{
	Client *target;
	if (!(target = find_user(param, NULL)))
		return EX_DENY;

	int can_ojoin = ValidatePermissionsForPath("ojoin", target, NULL, channel, NULL);

	if (what == MODE_DEL && client == target && can_ojoin) // allow them to -Y themselves
		return EX_ALLOW;
	else if (what == MODE_DEL && client != target) // if someone else is trying to -Y you
	{
		if (!IsServer(client) && !IsULine(client)) // if they're not a server or ULine
		{
			if (type == EXCHK_ACCESS_ERR)
				sendto_one(client, NULL, ":%s %d %s %s :%s", me.name, ERR_CANNOTDOCOMMAND, target->name, "MODE", "Permission denied!"); // DENIED
			return EX_ALWAYS_DENY;
		}
	}
	else if (!can_ojoin)
	{
		if (type == EXCHK_ACCESS_ERR)
			sendto_one(client, NULL, ":%s %d %s %s :%s", me.name, ERR_CANNOTDOCOMMAND, target->name, "MODE", "Permission denied!");
		return EX_DENY;
	}

	if (what == MODE_ADD && !IsServer(client))
	{
		if (type == EXCHK_ACCESS_ERR)
			sendto_one(client, NULL, ":%s %d %s %s :%s", me.name, ERR_CANNOTDOCOMMAND, target->name, "MODE", "Mode +Y is reserved for the command /OJOIN");
		return EX_ALWAYS_DENY;
	}

	return EX_ALWAYS_DENY;
}

/* Make the user unkickable */
int ojoin_kick_check(Client *client, Client *target, Channel *channel, const char *comment, const char *client_member_modes, const char *target_member_modes, const char **reject_reason)
{
	static char errmsg[256];
	char *p;
	int has_sop = 0;
	if (strstr(target_member_modes, "Y"))
	{
		ircsnprintf(errmsg, sizeof(errmsg), ":%s %d %s %s :%s",
					me.name, ERR_CANNOTDOCOMMAND, client->name,
					"KICK", "Permission denied!");
		*reject_reason = errmsg;
		has_sop = 1;
	}
	if (has_sop)
		return EX_DENY;
	else
		return EX_ALLOW;
}

CMD_FUNC(ojoin)
{
	Channel *chan;
	if (!IsULine(client) && !ValidatePermissionsForPath("ojoin", client, NULL, NULL, NULL))
	{
		sendnumeric(client, ERR_CANNOTDOCOMMAND, CMD_OJOIN, "Permission denied!");
		return;
	}
	if (parc < 2)
	{
		sendnumeric(client, ERR_NEEDMOREPARAMS, CMD_OJOIN);
		return;
	}
	if (!valid_channelname(parv[1]))
	{
		send_invalid_channelname(client, parv[1]);
		return;
	}
	chan = make_channel(parv[1]);
	if (!IsULine(client) && !ValidatePermissionsForPath("ojoin", client, NULL, chan, NULL))
	{
		sendnumeric(client, ERR_NOPRIVILEGES);
		return;
	}
	const char *parv2[3];
	parv2[0] = client->name;
	parv2[1] = parv[1];
	parv2[2] = NULL;

	do_join(client, 2, parv2);

	char *modes;
	const char *mode_args[3];

	modes = safe_alloc(2);
	modes[0] = 'Y';

	mode_args[0] = modes;
	mode_args[1] = client->name;
	mode_args[2] = 0;

	Client *us = find_client(me.name, NULL); // make this ACTUALLY sent by the server for the mode_is_ok check
	do_mode(chan, us, NULL, 3, mode_args, 0, 0);
	
	// show_to_modes can be NULL (show to everyone) - this is the default behavior
	char *show_to_modes = convert_prefixes_to_modes(conf.show_to);
	char *final_message;
	if (conf.show_entry_message && !BadPtr(conf.entry_message))
		final_message = substitute_entry_message(conf.entry_message, client->name);
	else
		final_message = "Joining on official network business.";
	
	sendto_channel(chan, &me, client, show_to_modes, 0, SEND_ALL, NULL, ":%s NOTICE %s :%s",
		me.name, chan->name, final_message);

	unreal_log(ULOG_INFO, "ojoin", "OJOIN", client,
	           "User $client.details used /OJOIN to join channel $chan on official network business.",
	           log_data_string("chan", chan->name));

	safe_free(modes);
}

int ojoin_configtest(ConfigFile *cf, ConfigEntry *ce, int type, int *errs)
{
	int errors = 0;
	int i;
	ConfigEntry *cep;

	if(type != CONFIG_MAIN)
		return 0;

	if(!ce || !ce->name)
		return 0;

	if(strcasecmp(ce->name, "ojoin"))
		return 0;

	for(cep = ce->items; cep; cep = cep->next)
	{
		if(!cep->name)
		{
			config_error("%s:%i: blank %s item", cep->file->filename, cep->line_number, "ojoin");
			errors++;
			continue;
		}

		if(!cep->value)
		{
			config_error("%s:%i: blank %s value", cep->file->filename, cep->line_number, "ojoin");
			errors++;
			continue;
		}

		if(!strcmp(cep->name, "entry-message"))
		{
			if(conf.got_entry_message)
			{
				config_error("%s:%i: duplicate %s::%s directive", cep->file->filename, cep->line_number, "ojoin", cep->name);
				errors++;
				continue;
			}

			conf.got_entry_message = 1;

			if(!strlen(cep->value))
			{
				config_error("%s:%i: %s::%s cannot be empty", cep->file->filename, cep->line_number, "ojoin", cep->name);
				errors++;
			}
			continue;
		}


		if(!strcmp(cep->name, "show-entry-message"))
		{
			if(conf.got_show_entry_message)
			{
				config_error("%s:%i: duplicate %s::%s directive", cep->file->filename, cep->line_number, "ojoin", cep->name);
				errors++;
				continue;
			}

			conf.got_show_entry_message = config_checkval(cep->value, CFG_YESNO);
			continue;
		}

		if(!strcmp(cep->name, "show-to"))
		{
			if(conf.got_show_to)
			{
				config_error("%s:%i: duplicate %s::%s directive", cep->file->filename, cep->line_number, "ojoin", cep->name);
				errors++;
				continue;
			}

			conf.got_show_to = 1;

			if(!strlen(cep->value))
			{
				config_error("%s:%i: %s::%s cannot be empty", cep->file->filename, cep->line_number, "ojoin", cep->name);
				errors++;
				continue;
			}

			/* Check for invalid characters */
			char *p;
			for(p = cep->value; *p; p++)
			{
				if(*p != '+' && *p != '%' && *p != '@' && *p != '&' && *p != '~' && *p != '!' &&
				   *p != 'v' && *p != 'h' && *p != 'o' && *p != 'a' && *p != 'q' && *p != 'Y')
				{
					config_error("%s:%i: %s::%s contains invalid character '%c'. Valid characters are: +%%@&~! or vhoaqY", 
						cep->file->filename, cep->line_number, "ojoin", cep->name, *p);
					errors++;
					break;
				}
			}
			continue;
		}

		config_warn("%s:%i: unknown item %s::%s", cep->file->filename, cep->line_number, "ojoin", cep->name);
	}

	*errs = errors;
	return errors ? -1 : 1;
}
int queue_configrun(ConfigFile *cf, ConfigEntry *ce, int type)
{
	ConfigEntry *cep;

	if(type != CONFIG_MAIN)
		return 0; 

	if(!ce || !ce->name)
		return 0;

	if(strcmp(ce->name, "ojoin"))
		return 0;

	for(cep = ce->items; cep; cep = cep->next)
	{
		if(!cep->name)
			continue;

		if(!strcmp(cep->name, "entry-message"))

		{
			safe_strdup(conf.entry_message, cep->value);
			continue;
		}

		if(!strcmp(cep->name, "show-entry-message"))
		{
			conf.show_entry_message = config_checkval(cep->value, CFG_YESNO);
			continue;
		}

		if(!strcmp(cep->name, "show-to"))
		{
			safe_strdup(conf.show_to, cep->value);
			continue;
		}

	}

	return 1; // We good
}
