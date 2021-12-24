/* Copyright (C) All Rights Reserved
** Written by k4be
** Website: https://github.com/pirc-pl/unrealircd-modules/
** License: GPLv3 https://www.gnu.org/licenses/gpl-3.0.html
*/

/*** <<<MODULE MANAGER START>>>
module
{
        documentation "https://github.com/pirc-pl/unrealircd-modules/blob/master/README.md#inchannelban-enforce";
        troubleshooting "In case of problems, contact k4be on irc.pirc.pl.";
        min-unrealircd-version "6.*";
        post-install-text {
                "The module is installed. Now all you need to do is add a loadmodule line:";
                "loadmodule \"third/inchannelban-enforce\";";
  				"And /REHASH the IRCd.";
  				"Optionally, you can configure this module. See documentation for help.";
        }
}
*** <<<MODULE MANAGER END>>>
*/

#include "unrealircd.h"

#define ACTION_NONE 0
#define ACTION_KICK 1
#define ACTION_PART 2

struct {
	int action;
	char *kick_text;
	char *notice_text;
	int have_config;
	int have_action;
	int have_kick_text;
	int have_notice_text;
} settings;

int ice_local_join(Client *client, Channel *joined_channel, MessageTag *mtags);

/*
// if the config block is missing, the following settings are assumed
inchannelban-enforce {
	action kick; // must be "kick" or "part"
	notice-text "*** Restrictions set on $ban prevent you from being on $joined at the same time. Leaving $ban"; // If missing, no notice will be sent.
	kick-text "Enforcing channel ban for $joined"; // Required for "kick" action.
	// In notice-text and kick-text you can use variables $joined (the channel that is banned and the user just joined) and $ban (the channel that has $joined on its banlist).
};
*/

#define MYCONF "inchannelban-enforce"

int ice_configtest(ConfigFile *cf, ConfigEntry *ce, int type, int *errs) {
	ConfigEntry *cep;
	int errors = 0;
	int i;
	
	if(type != CONFIG_MAIN)
		return 0;

	if(!ce || !ce->name)
		return 0;

	if(strcmp(ce->name, MYCONF))
		return 0;
	
	settings.have_config = 1;

	for(cep = ce->items; cep; cep = cep->next) {
		if(!cep->name) {
			config_error("%s:%i: blank %s item", cep->file->filename, cep->line_number, MYCONF);
			errors++;
			continue;
		}

		if(!cep->value || !strlen(cep->value)) {
			config_error("%s:%i: %s::%s must be non-empty", cep->file->filename, cep->line_number, MYCONF, cep->name);
			errors++;
			continue;
		}
	
		if(!strcmp(cep->name, "action")) {
			settings.have_action = ACTION_NONE;
			if(!strcmp(cep->value, "kick"))
				settings.have_action = ACTION_KICK;
			if(!strcmp(cep->value, "part"))
				settings.have_action = ACTION_PART;
			if(settings.have_action == ACTION_NONE){
				config_error("%s:%i: %s::%s must be either \"kick\" or \"part\"", cep->file->filename, cep->line_number, MYCONF, cep->name);
				errors++;
				break;
			}
			continue;
		}

		if(!strcmp(cep->name, "notice-text")) {
			settings.have_notice_text = 1;
			continue;
		}

		if(!strcmp(cep->name, "kick-text")) {
			settings.have_kick_text = 1;
			continue;
		}

		config_warn("%s:%i: unknown item %s::%s", cep->file->filename, cep->line_number, MYCONF, cep->name);
	}
	
	*errs = errors;
	return errors ? -1 : 1;
}

int ice_configposttest(int *errs) {
	int errors = 0;
	if(settings.have_config && !settings.have_action){
		config_error("No %s::action specfied!", MYCONF);
		errors++;
	}
	if(settings.have_action == ACTION_KICK && !settings.have_kick_text){
		config_error("%s::kick-text is required if %s::method is set to \"kick\"!", MYCONF, MYCONF);
		errors++;
	}
	if(errors){
		*errs = errors;
		return -1;
	}
	/* config ok, proceed to apply defaults */
	settings.kick_text = NULL;
	settings.notice_text = NULL;
	if(!settings.have_config){
		safe_strdup(settings.kick_text, "Enforcing channel ban for $joined");
		safe_strdup(settings.notice_text, "*** Restrictions set on $ban prevent you from being on $joined at the same time. Leaving $ban");
		settings.action = ACTION_KICK;
	}
	return 1;
}

int ice_configrun(ConfigFile *cf, ConfigEntry *ce, int type) {
	ConfigEntry *cep;

	if(type != CONFIG_MAIN)
		return 0;

	if(!ce || !ce->name)
		return 0;

	if(strcmp(ce->name, MYCONF))
		return 0;

	for(cep = ce->items; cep; cep = cep->next) {
		if(!cep->name)
			continue;

		if(!strcmp(cep->name, "action")) {
			if(!strcmp(cep->value, "kick"))
				settings.action = ACTION_KICK;
			if(!strcmp(cep->value, "part"))
				settings.action = ACTION_PART;
			continue;
		}

		if(!strcmp(cep->name, "notice-text")) {
			safe_strdup(settings.notice_text, cep->value);
			continue;
		}

		if(!strcmp(cep->name, "kick-text")) {
			safe_strdup(settings.kick_text, cep->value);
			continue;
		}
	}
	return 1;
}

ModuleHeader MOD_HEADER = {
	"third/inchannelban-enforce",
	"6.0",
	"Enforce ~c bans so they can't be circumvented",
	"k4be",
	"unrealircd-6",
};

MOD_INIT(){
	HookAdd(modinfo->handle, HOOKTYPE_CONFIGRUN, 0, ice_configrun);
	return MOD_SUCCESS;
}

MOD_TEST(){
	settings.have_config = 0;
	settings.have_action = ACTION_NONE;
	settings.have_kick_text = 0;
	settings.have_notice_text = 0;
	HookAdd(modinfo->handle, HOOKTYPE_CONFIGTEST, 0, ice_configtest);
	HookAdd(modinfo->handle, HOOKTYPE_CONFIGPOSTTEST, 0, ice_configposttest);
	return MOD_SUCCESS;
}

MOD_LOAD(){
	HookAdd(modinfo->handle, HOOKTYPE_LOCAL_JOIN, 0, ice_local_join);
	return MOD_SUCCESS;
}

MOD_UNLOAD(){
	return MOD_SUCCESS;
}

#define BUFLEN 256

int ice_local_join(Client *client, Channel *joined_channel, MessageTag *mtags){
	Ban *ban;
	Membership *lp;
	Channel *channel;
	char buf[BUFLEN];
	const char *banned_channel;
	const char *parv[2];
	parv[0] = client->name;
	const char *name[3], *value[3];

	for(lp = client->user->channel; lp;){
		channel = lp->channel;
		lp = lp->next;
		banned_channel = NULL;
		ban = is_banned_with_nick(client, channel, BANCHK_JOIN, NULL, NULL, NULL);
		if(ban){
			banned_channel = strstr(ban->banstr, "~channel:#"); /* extract ~c ban argument */
			if(banned_channel){
				banned_channel += 9;
			} else {
				banned_channel = strstr(ban->banstr, "~c:#"); /* old version */
				if(banned_channel){
					banned_channel += 3;
				}
			}
			if(banned_channel && *banned_channel != '#') /* get rid of accessmode modifier */
				banned_channel++;
		}
		if(!BadPtr(banned_channel) && match_esc(banned_channel, joined_channel->name)){ /* is banned and the ban matches just joined channel */
			name[0] = "ban";
			value[0] = channel->name;
			name[1] = "joined";
			value[1] = joined_channel->name;
			name[2] = NULL;
			value[2] = NULL;
			if(settings.notice_text){
				buildvarstring(settings.notice_text, buf, sizeof(buf), name, value);
				sendnotice(client, "%s", buf);
			}
			if(settings.action == ACTION_KICK){
				buildvarstring(settings.kick_text, buf, sizeof(buf), name, value);
				kick_user(NULL, channel, &me, client, buf);
			} else {
				parv[1] = channel->name;
				do_cmd(client, NULL, "PART", 2, parv); /* we can't specify a PART reason here, because we're already banned :( would need a significant amount of code to avoid that */
				if(IsDead(client))
					return HOOK_CONTINUE;
			}
		}
	}
	return HOOK_CONTINUE;
}

