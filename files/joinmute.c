/* Copyright (C) All Rights Reserved
** Written by Gottem <support@gottem.nl>
** Website: https://gottem.nl/unreal
** License: https://gottem.nl/unreal/license
*/

/*** <<<MODULE MANAGER START>>>
module {
	documentation "https://gottem.nl/unreal/man/joinmute";
	troubleshooting "In case of problems, check the FAQ at https://gottem.nl/unreal/halp or e-mail me at support@gottem.nl";
	min-unrealircd-version "5.*";
	//max-unrealircd-version "5.*";
	post-install-text {
		"The module is installed, now all you need to do is add a 'loadmodule' line to your config file:";
		"loadmodule \"third/joinmute\";";
		"Then /rehash the IRCd.";
		"For usage information, refer to the module's documentation found at: https://gottem.nl/unreal/man/joinmute";
	}
}
*** <<<MODULE MANAGER END>>>
*/

// One include for all cross-platform compatibility thangs
#include "unrealircd.h"

#define CheckAPIError(apistr, apiobj) \
	do { \
		if(!(apiobj)) { \
			config_error("A critical error occurred on %s for %s: %s", (apistr), MOD_HEADER.name, ModuleGetErrorStr(modinfo->handle)); \
			return MOD_FAILED; \
		} \
	} while(0)

// Big hecks go here
typedef struct _channels_users_ {
	struct _channel_users *prev, *next;
	Client *source;
	Channel *channel;
	int joined_since;
} UsersM;

typedef struct _joinmute {
	char flag;
	int seconds;
} JoinMute;

// Quality fowod declarations
void add_user_to_memory(Client *client, Channel *channel);
void del_user_from_memory(UsersM *u);
void clear_matching_entries(Client *client);
UsersM *FindUserInMemory(Client *client, Channel *channel);
int joinmute_hook_join(Client *client, Channel *channel, MessageTag *mtags, char *parv[]);
int joinmute_hook_part(Client *client, Channel *channel, MessageTag *mtags, char *comment);
int joinmute_hook_quit(Client *client, MessageTag *recv_mtags, char *comment);
int joinmute_hook_kick(Client *client, Client *victim, Channel *channel, MessageTag *mtags, char *comment);
int joinmute_hook_cansend_chan(Client *client, Channel *channel, Membership *lp, char **text, char **errmsg, int notice);
int modeJ_is_ok(Client *client, Channel *channel, char mode, char *para, int checkt, int what);
void *modeJ_put_param(void *lst, char *para);
char *modeJ_get_param(void *lst);
char *modeJ_conv_param(char *param, Client *client);
void modeJ_free_param(void *lst);
void *modeJ_dup_struct(void *src);
int modeJ_sjoin_check(Channel *channel, void *ourx, void *theirx);

UsersM *muted_users = NULL; // List of data
Cmode_t extcmode_joinmute = 0L; // To store the bit flag latur

ModuleHeader MOD_HEADER = {
	"third/joinmute",
	"2.0",
	"Adds +J chmode: Mute newly joined people for +J X seconds",
	"Gottem", // Author
	"unrealircd-5", // Modversion
};

MOD_INIT() {
	CmodeInfo req;
	memset(&req, 0, sizeof(req));
	req.paracount = 1;
	req.flag = 'J';
	req.is_ok = modeJ_is_ok;
	req.put_param = modeJ_put_param;
	req.get_param = modeJ_get_param;
	req.conv_param = modeJ_conv_param;
	req.free_param = modeJ_free_param;
	req.dup_struct = modeJ_dup_struct;
	req.sjoin_check = modeJ_sjoin_check;
	CheckAPIError("CmodeAdd(extcmode_joinmute)", CmodeAdd(modinfo->handle, req, &extcmode_joinmute));

	MARK_AS_GLOBAL_MODULE(modinfo);

	// High priority hewks, just in case lol
	HookAdd(modinfo->handle, HOOKTYPE_LOCAL_JOIN, -100, joinmute_hook_join);
	HookAdd(modinfo->handle, HOOKTYPE_LOCAL_PART, -100, joinmute_hook_part);
	HookAdd(modinfo->handle, HOOKTYPE_CAN_SEND_TO_CHANNEL, -100, joinmute_hook_cansend_chan);
	HookAdd(modinfo->handle, HOOKTYPE_LOCAL_QUIT, -100, joinmute_hook_quit);
	HookAdd(modinfo->handle, HOOKTYPE_LOCAL_KICK, -100, joinmute_hook_kick);
	return MOD_SUCCESS;
}

MOD_LOAD() {
	return MOD_SUCCESS;
}

MOD_UNLOAD() {
	return MOD_SUCCESS;
}

int modeJ_is_ok(Client *client, Channel *channel, char mode, char *para, int checkt, int what) {
	int seconds = 0;
	if(what == MODE_ADD)
		seconds = atoi(para);

	if((checkt == EXCHK_ACCESS) || (checkt == EXCHK_ACCESS_ERR)) {
		if(IsUser(client) && is_chan_op(client, channel))
			return EX_ALLOW;

		if(checkt == EXCHK_ACCESS_ERR) /* can only be due to being halfop */
			sendnumeric(client, ERR_NOTFORHALFOPS, 'J');

		return EX_DENY;
	}
	else if(checkt == EXCHK_PARAM) {
		if(seconds <= 0 || seconds > 65536) {
			sendnotice(client, "*** [joinmute] Error: Seconds value is out of range!");
			return EX_DENY;
		}
		return EX_ALLOW;
	}
	return 0;
}

void *modeJ_put_param(void *r_in, char *param) {
	JoinMute *r = (JoinMute *)r_in;
	int seconds = atoi(param);
	// Gottem entry already?
	if(!r) {
		/* Need to create one */
		r = (JoinMute *)safe_alloc(sizeof(JoinMute));
		memset(r, 0, sizeof(JoinMute));
		r->flag = 'J';
	}
	if(seconds < 0 || seconds > 65536)
		seconds = 0;
	r->seconds = seconds;
	return (void *)r;
}

char *modeJ_get_param(void *r_in) {
	JoinMute *r = (JoinMute *)r_in;
	static char retbuf[16];
	if(!r)
		return NULL;

	snprintf(retbuf, sizeof(retbuf), "%d", r->seconds);
	return retbuf;
}

char *modeJ_conv_param(char *param, Client *client) {
	static char retbuf[32];
	int num = atoi(param);

	if(num < 0)
		num = 0;
	else if(num > 65536)
		num = 255;
	else if(!num)
		num = 0;

	snprintf(retbuf, sizeof(retbuf), "%d", num);

	return retbuf;
}

void modeJ_free_param(void *r) {
	JoinMute *n = (JoinMute *)r;
	safe_free(n);
}

void *modeJ_dup_struct(void *src) {
	JoinMute *n = (JoinMute *)safe_alloc(sizeof(JoinMute));
	memcpy(n, src, sizeof(JoinMute));
	return (void *)n;
}

int modeJ_sjoin_check(Channel *channel, void *ourx, void *theirx) {
	JoinMute *our = (JoinMute *)ourx;
	JoinMute *their = (JoinMute *)theirx;
	if(our->seconds == their->seconds)
		return EXSJ_SAME;
	if(our->seconds > their->seconds) /* Server with more seconds winning */
		return EXSJ_WEWON;
	else
		return EXSJ_THEYWON;
}

/** add_user_to_memory
* Adds user to currently muted users list
* Allocates the memory for data
*/

void add_user_to_memory(Client *client, Channel *channel) {
	UsersM *u = (UsersM *)safe_alloc(sizeof(UsersM));
	if(!u)
		return;
	u->channel = channel;
	u->source = client;
	u->joined_since = TStime();
	AddListItem(u, muted_users);
	return;
}

/** del_user_from_memory
* Removes user from currently muted users list
* Frees the memory allocated for data
*/

void del_user_from_memory(UsersM *u) {
	if(!u)
		return;
	DelListItem(u, muted_users);
	safe_free(u);
	return;
}

/* Used on quit - just remove all entries with client as a user */
void clear_matching_entries(Client *client) {
	UsersM *userz, *next;
	for(userz = muted_users; userz; userz = next) {
		next = (UsersM *)userz->next;
		if(userz->source == client)
			del_user_from_memory(userz);
	}
}

/** FindUserInMemory
* Scans through currently muted users
* to find matching one, if the user is found
* its pointer will be returned instantly
*/
UsersM *FindUserInMemory(Client *client, Channel *channel) {
	UsersM *uz, *next;
	for(uz = muted_users; uz; uz = next) {
		next = (UsersM *)uz->next;
		if(!find_person(uz->source->name, NULL)) {
			del_user_from_memory(uz);
			continue;
		}
		if(!IsMember(uz->source, uz->channel)) {
			del_user_from_memory(uz);
			continue;
		}
		if(!(uz->channel->mode.extmode & extcmode_joinmute)) {
			del_user_from_memory(uz);
			continue;
		}
		if((uz->source == client) && (uz->channel == channel))
			return uz;
	}
	return NULL;
}

int joinmute_hook_cansend_chan(Client *client, Channel *channel, Membership *lp, char **text, char **errmsg, int notice) {
	char *para;
	int seconds;
	UsersM *u;
	char errbuf[256];

	if(!MyConnect(client) || !IsUser(client))
		return HOOK_CONTINUE;
	u = FindUserInMemory(client, channel); /* Let's find entry in muted users list :) */
	para = cm_getparameter(channel, 'J');

	/* ops/hops/vops/opers are allowed to send it... */
	if(!(channel->mode.extmode & extcmode_joinmute))
		return HOOK_CONTINUE;
	if(is_chan_op(client, channel) || is_half_op(client, channel) || has_voice(client, channel) || IsOper(client) || IsULine(client))
		return HOOK_CONTINUE;
	if(!u || !para)
		return HOOK_CONTINUE; /* Not found - let it be sent! -- Dvlpr */

	seconds = atoi(para);

	/* How we know if user allowed? we just check if current time - time when user
	* joined the channel is more then seconds and if yes, we allow sending text + deleting
	* user's data from muted users list */
	if((TStime() - u->joined_since) > seconds)
		del_user_from_memory(u);
	else {
		ircsnprintf(errbuf, sizeof(errbuf), "You must wait %d seconds after joining to speak", seconds);
		sendnumeric(client, ERR_CANNOTSENDTOCHAN, channel->chname, errbuf, channel->chname);
		*text = NULL;
		// Can't return HOOK_DENY here cuz Unreal will abort() in that case :D
	}
	return HOOK_CONTINUE;
}

int joinmute_hook_join(Client *client, Channel *channel, MessageTag *mtags, char *parv[]) {
	add_user_to_memory(client, channel);
	return HOOK_CONTINUE;
}

int joinmute_hook_part(Client *client, Channel *channel, MessageTag *mtags, char *comment) {
	UsersM *u = FindUserInMemory(client, channel);
	if(!u)
		return HOOK_CONTINUE;
	del_user_from_memory(u);
	return HOOK_CONTINUE;
}

int joinmute_hook_quit(Client *client, MessageTag *recv_mtags, char *comment) {
	clear_matching_entries(client);
	return HOOK_CONTINUE;
}

int joinmute_hook_kick(Client *client, Client *victim, Channel *channel, MessageTag *mtags, char *comment) {
	UsersM *u = FindUserInMemory(victim, channel);
	if(!u)
		return HOOK_CONTINUE;
	del_user_from_memory(u);
	return HOOK_CONTINUE;
}
