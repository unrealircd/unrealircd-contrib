/* Copyright © 2025 Valware
 * License: GPLv3
 * Name: third/relaymsg
 */
/*** <<<MODULE MANAGER START>>>
module
{
		documentation "https://github.com/ValwareIRC/valware-unrealircd-mods/blob/main/relaymsg/README.md";
		troubleshooting "In case of problems, check the documentation or e-mail me at v.a.pond@outlook.com";
		min-unrealircd-version "6.1.0";
		max-unrealircd-version "6.*";
		post-install-text {
				"The module is installed. Now all you need to do is add a loadmodule line:";
				"loadmodule \"third/relaymsg\";";
				"The module needs no other configuration.";
				"Once you're good to go, you can finally type in your shell: ./unrealircd rehash";
		}
}
*** <<<MODULE MANAGER END>>>

*/

/* One include for all */
#include "unrealircd.h"

#define CONF_BLOCK_NAME "relaymsg"
#define NAME_RELAYMSG "draft/relaymsg"

long CAP_RELAYMSG = 0L;

/* Forward declarations */
void set_config(void);
void free_config(void);
int hookfunc_configtest(ConfigFile *cf, ConfigEntry *ce, int type, int *errs);
int hookfunc_configrun(ConfigFile *cf, ConfigEntry *ce, int type);

int relaymsg_tag_is_ok(Client *client, const char *name, const char *value);
const char *relay_msg_cap_parameter(Client *client);

CMD_FUNC(cmd_relaymsg);
CMD_FUNC(cmd_rrelaymsg);

struct MyConfStruct
{
	char *hostmask;

	bool got_hostmask;
};
static struct MyConfStruct MyConf;

ModuleHeader MOD_HEADER
={
		"third/relaymsg", /* Name of module */
		"1.0.0", /* Version */
		"Implements draft/relaymsg", /* Short description of module */
		"Valware", /* Author */
		"unrealircd-6", /* Version of UnrealIRCd */
};

// Module initialization
MOD_INIT()
{
	ClientCapabilityInfo c;
	ClientCapability *c2;
	MessageTagHandlerInfo mtag;

	MARK_AS_GLOBAL_MODULE(modinfo);

	set_config(); // Set defaults
	HookAdd(modinfo->handle, HOOKTYPE_CONFIGRUN, 0, hookfunc_configrun); // Run through the config and set the values
	
	memset(&c, 0, sizeof(c));
	c.name = NAME_RELAYMSG;
	c.parameter = relay_msg_cap_parameter;
	c2 = ClientCapabilityAdd(modinfo->handle, &c, &CAP_RELAYMSG);

	memset(&mtag, 0, sizeof(mtag));
	mtag.name = NAME_RELAYMSG;
	mtag.is_ok = relaymsg_tag_is_ok;
	mtag.clicap_handler = c2;
	MessageTagHandlerAdd(modinfo->handle, &mtag);

	CommandAdd(modinfo->handle, "RELAYMSG", cmd_relaymsg, 4, CMD_USER|CMD_SERVER|CMD_NOLAG); // Add the command
	CommandAdd(modinfo->handle, "RRELAYMSG", cmd_rrelaymsg, 5, CMD_SERVER|CMD_NOLAG|CMD_BIGLINES); // Add the command

	return MOD_SUCCESS;
}

// Module load
MOD_LOAD()
{
	return MOD_SUCCESS;
}

// Module unload
MOD_UNLOAD()
{
	free_config(); // Free memory
	return MOD_SUCCESS;
}

// Module test
MOD_TEST()
{
   memset(&MyConf, 0, sizeof(MyConf)); // Clear it out

   HookAdd(modinfo->handle, HOOKTYPE_CONFIGTEST, 0, hookfunc_configtest); // Test the config
   return MOD_SUCCESS;
}

// Set defaults for the configuration settings here (called in MOD_INIT)
void set_config(void)
{
	safe_strdup(MyConf.hostmask, "unreal@localhost"); // String
}

// Free the memory allocated for the configuration settings here (called in MOD_UNLOAD)
void free_config(void)
{
	safe_free(MyConf.hostmask);
}

// Configuration testing function (check for errors in the config block) (called in MOD_TEST)
int hookfunc_configtest(ConfigFile *cf, ConfigEntry *ce, int type, int *errs)
{
	// Keep track of errors, iterators, and the current config entry
	int errors = 0;
	int i;
	ConfigEntry *cep, *cep2;

	// Filter on CONFIG_MAIN only (top-level block)
	if(type != CONFIG_MAIN)
		return 0;

	// Validation check
	if(!ce || !ce->name)
		return 0;

	// Check if it's our block
	if(strcmp(ce->name, CONF_BLOCK_NAME))
		return 0;

	// Look inside the block
	for(cep = ce->items; cep; cep = cep->next)
	{
		if(!cep->value)
		{
			config_error("%s:%i: blank %s value", cep->file->filename, cep->line_number, CONF_BLOCK_NAME); // Rep0t error
			errors++;
			continue;
		}

		// Check for known directives
		// If it's a string, check if it's empty
		if(!strcmp(cep->name, "hostmask"))
		{
			if(MyConf.got_hostmask)
			{
				config_error("%s:%i: duplicate %s::%s directive", cep->file->filename, cep->line_number, CONF_BLOCK_NAME, cep->name);
				errors++;
				continue;
			}

			MyConf.got_hostmask = 1;
			if(!strlen(cep->value) || !strcmp(cep->value, "@"))
			{
				config_error("%s:%i: %s::%s must be non-empty and be in nick@hostmask format", cep->file->filename, cep->line_number, CONF_BLOCK_NAME, cep->name);
				errors++;
			}
			if (!strchr(cep->value, '@'))
			{
				config_error("%s:%i: %s::%s must be in nick@hostmask format", cep->file->filename, cep->line_number, CONF_BLOCK_NAME, cep->name);
				errors++;
			}
			continue;
		}

		// Unknown directive, warn about it
		config_warn("%s:%i: unknown item %s::%s", cep->file->filename, cep->line_number, CONF_BLOCK_NAME, cep->name); // So display just a warning
	}

	// Return a bool int whether or not there were errors. If yes, -1, if no, 1
	*errs = errors;
	return errors ? -1 : 1;
}

// Run through the configuration and set the values (called in MOD_INIT, after MOD_TEST)
int hookfunc_configrun(ConfigFile *cf, ConfigEntry *ce, int type)
{
	ConfigEntry *cep, *cep2;

	if(type != CONFIG_MAIN)
		return 0;

	if(!ce || !ce->name)
		return 0;

	if(strcmp(ce->name, CONF_BLOCK_NAME))
		return 0;

	for(cep = ce->items; cep; cep = cep->next) 
	{
		if(!cep->name)
			continue;

		if(!strcmp(cep->name, "hostmask"))
		{
			safe_strdup(MyConf.hostmask, cep->value);
			continue;
		}
	}

	return 1;
}

// Only allow servers to add this tag
int relaymsg_tag_is_ok(Client *client, const char *name, const char *value)
{
	if (IsServer(client))
		return 1;

	return 0;
}

// Return the parameter for the capability which is always a "/"
// This is the required delimiter for the spoofed nick
const char *relay_msg_cap_parameter(Client *client)
{
	return "/";
}

// Command to send a message to a channel as a spoofed nick
CMD_FUNC(cmd_relaymsg)
{
	MessageTag *mtags = NULL, *m = NULL;

	if (!HasCapability(client, NAME_RELAYMSG))
	{
		return;
	}

	if (!ValidatePermissionsForPath("relaymsg", client, NULL, NULL, NULL))
	{
		sendnumeric(client, ERR_NOPRIVILEGES);
		return;
	}

	if (parc < 3)
	{
		sendnumeric(client, ERR_NEEDMOREPARAMS, "RELAYMSG");
		return;
	}

	// Validate spoofed nick (parv[2])
	const char *invalid_chars = " \t\n\r!+%@&#$:'\"?*,.";
	for (const char *p = parv[2]; *p; p++)
	{
		if (strchr(invalid_chars, *p))
		{
			sendnotice(client, "Invalid characters in spoofed nick");
			return;
		}
	}
	 
	if (!strchr(parv[2], '/'))
	{
		sendnotice(client, "Invalid spoofed nick format");
		return;
	}

    if (strlen(parv[2]) > 35)
    {
        sendnotice(client, "Spoofed nick too long");
        return;
    }

	Channel *channel = find_channel(parv[1]);
	if (!channel)
	{
		sendnumeric(client, ERR_NOSUCHCHANNEL, parv[1]);
		return;
	}

	sendnotice(client, "Sending message to %s", parv[1]);

	m = safe_alloc(sizeof(MessageTag));
	safe_strdup(m->name, NAME_RELAYMSG);
	safe_strdup(m->value, client->name);
	AddListItem(m, mtags);
	new_message(client, recv_mtags, &mtags);


	sendto_channel(channel, &me, NULL, NULL, 0, SEND_LOCAL, mtags,
						":%s!%s PRIVMSG %s :%s", parv[2], MyConf.hostmask, parv[1], parv[3]);
	sendto_server(NULL, 0, 0, mtags,
						 ":%s RRELAYMSG %s %s %s :%s", me.name, client->id, parv[1], parv[2], parv[3]);
}

CMD_FUNC(cmd_rrelaymsg)
{
	if (parc < 4)
		return;

	// We validated before but let's do it again just in case 
	// someone tries to bypass the command and send messed up stuff
	const char *invalid_chars = " \t\n\r!+%@&#$:'\"?*,.";
	for (const char *p = parv[2]; *p; p++)
		if (strchr(invalid_chars, *p))
			return;
	 
	if (!strchr(parv[1], '/'))
		return;

	Channel *channel = find_channel(parv[2]);
	if (!channel)
		return;
	
	sendto_channel(channel, &me, NULL, NULL, 0, SEND_LOCAL, recv_mtags,
				 ":%s!%s PRIVMSG %s :%s", parv[2], MyConf.hostmask, parv[3], parv[4]);
	sendto_server(client, 0, 0, recv_mtags,
				 ":%s RRELAYMSG %s %s %s :%s", me.name, parv[1], parv[2], parv[3], parv[4]);
}
