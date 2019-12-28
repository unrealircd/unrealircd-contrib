/* Copyright (C) All Rights Reserved
** Written by Gottem <support@gottem.nl>
** Website: https://gottem.nl/unreal
** License: https://gottem.nl/unreal/license
*/

/*** <<<MODULE MANAGER START>>>
module {
	documentation "https://gottem.nl/unreal/man/topicgreeting";
	troubleshooting "In case of problems, check the FAQ at https://gottem.nl/unreal/halp or e-mail me at support@gottem.nl";
	min-unrealircd-version "5.*";
	//max-unrealircd-version "5.*";
	post-install-text {
		"The module is installed, now all you need to do is add a 'loadmodule' line to your config file:";
		"loadmodule \"third/topicgreeting\";";
		"Then /rehash the IRCd.";
		"For usage information, refer to the module's documentation found at: https://gottem.nl/unreal/man/topicgreeting";
	}
}
*** <<<MODULE MANAGER END>>>
*/

// One include for all cross-platform compatibility thangs
#include "unrealircd.h"

#define CHMODE_FLAG 'g' // As in greet obv =]

// Dem macros yo
#define HasTopicgreet(x) ((x) && (x)->mode.extmode & extcmode_topicgreeting)

#define CheckAPIError(apistr, apiobj) \
	do { \
		if(!(apiobj)) { \
			config_error("A critical error occurred on %s for %s: %s", (apistr), MOD_HEADER.name, ModuleGetErrorStr(modinfo->handle)); \
			return MOD_FAILED; \
		} \
	} while(0)

// Quality fowod declarations
int topicgreeting_localjoin(Client *client, Channel *channel, MessageTag *recv_mtags, char *parv[]);

// Muh globals
Cmode_t extcmode_topicgreeting = 0L; // Store bitwise value latur

// Dat dere module header
ModuleHeader MOD_HEADER = {
	"third/topicgreeting", // Module name
	"2.0", // Version
	"Greet users who join a channel by changing the topic (channel mode +g)", // Description
	"Gottem", // Author
	"unrealircd-5", // Modversion
};

// Initialisation routine (register hooks, commands and modes or create structs etc)
MOD_INIT() {
	// Request the mode flag
	CmodeInfo cmodereq;
	memset(&cmodereq, 0, sizeof(cmodereq));
	cmodereq.flag = CHMODE_FLAG; // Flag yo
	cmodereq.paracount = 0; // How many params?
	cmodereq.is_ok = extcmode_default_requirechop; // For paramless modes that simply require +o/+a/+q etc
	CheckAPIError("CmodeAdd(extcmode_topicgreeting)", CmodeAdd(modinfo->handle, cmodereq, &extcmode_topicgreeting));

	MARK_AS_GLOBAL_MODULE(modinfo);

	// Add a hook with priority 0 (i.e. normal) that returns an int
	HookAdd(modinfo->handle, HOOKTYPE_LOCAL_JOIN, 0, topicgreeting_localjoin);
	return MOD_SUCCESS;
}

// Actually load the module here (also command overrides as they may not exist in MOD_INIT yet)
MOD_LOAD() {
	return MOD_SUCCESS; // We good
}

// Called on unload/rehash obv
MOD_UNLOAD() {
	return MOD_SUCCESS; // We good
}

int topicgreeting_localjoin(Client *client, Channel *channel, MessageTag *recv_mtags, char *parv[]) {
	// Let's make sure we don't greet ButtServ and co lol
	if(channel && (channel->mode.extmode & extcmode_topicgreeting) && !IsULine(client)) {
		char str[BUFSIZE];
		int tlen, nlen;
		time_t ttime;
		ircsnprintf(str, sizeof(str), "Hey %s, thanks for joining!", client->name);
		if(!channel->topic || strcasecmp(channel->topic, str)) { // Only if no topic or if it would differ (to prevent """funny""" business with /cycle)
			MessageTag *mtags = NULL;
			tlen = strlen(str);
			nlen = strlen(me.name);
			ttime = TStime();

			// Make sure we don't exceed the maximum allowed lengths ;]
			if(tlen > iConf.topic_length)
				tlen = iConf.topic_length;
			if(nlen > (NICKLEN + USERLEN + HOSTLEN + 5))
				nlen = (NICKLEN + USERLEN + HOSTLEN + 5);

			// Set new topic
			safe_free(channel->topic);
			channel->topic = safe_alloc(tlen + 1);
			strlcpy(channel->topic, str, tlen + 1);
			channel->topic_time = ttime;

			// Also use this server's name for source nick
			safe_free(channel->topic_nick);
			channel->topic_nick = safe_alloc(nlen + 1);
			strlcpy(channel->topic_nick, me.name, nlen + 1);

			// Broadcast that shit twice; once to local users using server *name* and another for remote users with em SID
			new_message(&me, NULL, &mtags);
			sendto_channel(channel, &me, NULL, 0, 0, SEND_LOCAL, mtags, ":%s TOPIC %s :%s", me.name, channel->chname, channel->topic);
			sendto_channel(channel, &me, NULL, 0, 0, SEND_REMOTE, mtags, ":%s TOPIC %s :%s", me.id, channel->chname, channel->topic);
			free_message_tags(mtags);
		}
	}
	return HOOK_CONTINUE;
}
