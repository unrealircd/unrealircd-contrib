/* Copyright (C) All Rights Reserved
** Written by Gottem <support@gottem.nl>
** Contains edits by jesopo to implement notices for HOOKTYPE_TKL_ADD
** Website: https://gottem.nl/unreal
** License: https://gottem.nl/unreal/license
*/

/*** <<<MODULE MANAGER START>>>
module {
	documentation "https://gottem.nl/unreal/man/chansno";
	troubleshooting "In case of problems, check the FAQ at https://gottem.nl/unreal/halp or e-mail me at support@gottem.nl";
	min-unrealircd-version "5.*";
	//max-unrealircd-version "5.*";
	post-install-text {
		"The module is installed, now all you need to do is add a 'loadmodule' line to your config file:";
		"loadmodule \"third/chansno\";";
		"Then /rehash the IRCd.";
		"For usage information, refer to the module's documentation found at: https://gottem.nl/unreal/man/chansno";
	}
}
*** <<<MODULE MANAGER END>>>
*/

#include "unrealircd.h"

typedef struct _conf_operflag OperFlag;
typedef struct _chansnoflag ChanSnoFlag;

struct _conf_operflag {
	long flag;
	char *name;
};

struct _chansnoflag {
	ChanSnoFlag *prev, *next;
	char *channel;
	long flags;
};

extern OperFlag *config_binary_flags_search(OperFlag *table, char *cmd, int size);

#define MSG_CHANSNO "CHANSNO"
#define CHSNO_TABLESIZE sizeof(_ChanSnoFlags) / sizeof(_ChanSnoFlags[0])
#define MaxSize(x) (sizeof(x) - strlen(x) - 1)

#define IsParam(x) (parc > (x) && !BadPtr(parv[(x)]))
#define IsNotParam(x) (parc <= (x) || BadPtr(parv[(x)]))

/* Some helpful abbreviations */
#define UserName(client) ((client)->user->username)
#define RealHost(client) ((client)->user->realhost)

/* Messages types */
#define MT_PRIVMSG 0x00
#define MT_NOTICE 0x01
#define MsgType (msgtype == MT_PRIVMSG ? "PRIVMSG" : "NOTICE")

/* Channel server notice masks */
#define CHSNO_CONNECT 0x0001
#define CHSNO_DISCONNECT 0x0002
#define CHSNO_NICKCHANGE 0x0004
#define CHSNO_JOIN 0x0008
#define CHSNO_PART 0x0010
#define CHSNO_KICK 0x0020
#define CHSNO_CHANMODE 0x0040
#define CHSNO_SCONNECT 0x0080
#define CHSNO_SQUIT 0x0100
#define CHSNO_TOPIC 0x0200
#define CHSNO_UNKUSER_QUIT 0x0400
#define CHSNO_CHANNEL_CREATE 0x0800
#define CHSNO_CHANNEL_DESTROY 0x1000
#define CHSNO_OPER 0x2000
#define CHSNO_SPAMFILTER 0x4000
#define CHSNO_TKL_ADD 0x8000

#define CheckAPIError(apistr, apiobj) \
	do { \
		if(!(apiobj)) { \
			config_error("A critical error occurred on %s for %s: %s", (apistr), MOD_HEADER.name, ModuleGetErrorStr(modinfo->handle)); \
			return MOD_FAILED; \
		} \
	} while(0)

// This MUST be alphabetised
OperFlag _ChanSnoFlags[] = {
	{ CHSNO_CHANNEL_CREATE,  "channel-creations" },
	{ CHSNO_CHANNEL_DESTROY, "channel-destructions" },
	{ CHSNO_CONNECT, "connects" },
	{ CHSNO_DISCONNECT, "disconnects" },
	{ CHSNO_JOIN, "joins" },
	{ CHSNO_KICK, "kicks" },
	{ CHSNO_CHANMODE, "mode-changes" },
	{ CHSNO_NICKCHANGE, "nick-changes" },
	{ CHSNO_OPER, "oper-ups" },
	{ CHSNO_PART, "parts" },
	{ CHSNO_SCONNECT, "server-connects" },
	{ CHSNO_SPAMFILTER, "spamfilter-hits" },
	{ CHSNO_SQUIT, "squits" },
	{ CHSNO_TKL_ADD, "tkl-add" },
	{ CHSNO_TOPIC, "topics" },
	{ CHSNO_UNKUSER_QUIT, "unknown-users" }
};

CMD_FUNC(chansno);
int chansno_configtest(ConfigFile *, ConfigEntry *, int, int *);
int chansno_configrun(ConfigFile *, ConfigEntry *, int);
int chansno_rehash(void);

int chansno_hook_chanmode(Client *client, Channel *channel, MessageTag *mtags, char *modebuf, char *parabuf, time_t sendts, int samode);
int chansno_hook_connect(Client *client);
int chansno_hook_quit(Client *client, MessageTag *mtags, char *comment);
int chansno_hook_join(Client *client, Channel *channel, MessageTag *mtags, char *parv[]);
int chansno_hook_kick(Client *client, Client *victim, Channel *channel, MessageTag *mtags, char *comment);
int chansno_hook_nickchange(Client *client, char *newnick);
int chansno_hook_part(Client *client, Channel *channel, MessageTag *mtags, char *comment);
int chansno_hook_serverconnect(Client *client);
int chansno_hook_serverquit(Client *client, MessageTag *mtags);
int chansno_hook_topic(Client *client, Channel *channel, MessageTag *mtags, char *topic);
int chansno_hook_unkuserquit(Client *client, MessageTag *mtags, char *comment);
int chansno_hook_channelcreate(Client *client, Channel *channel);
int chansno_hook_channeldestroy(Channel *channel, int *should_destroy);
int chansno_hook_spamfilter(Client *acptr, char *str, char *str_in, int type, char *target, TKL *tkl);
CMD_OVERRIDE_FUNC(chansno_override_oper);
int chansno_hook_tkladd(Client *client, TKL *tkl);

static void InitConf(void);
static void FreeConf(void);
static char *get_flag_names(long flags);
static void stats_chansno_channels(Client *client);
static void stats_chansno_config(Client *client);
static u_int find_sno_channel(Channel *channel);
static void SendNotice_simple(long type, int local);
static void SendNotice_channel(Channel *channel, long type, int local);

ChanSnoFlag *ConfChanSno;

u_int msgtype = MT_PRIVMSG;
char msgbuf[BUFSIZE];

ModuleHeader MOD_HEADER = {
	"third/chansno",
	"2.0",
	"Allows opers to assign channels for specific server notifications (sort of like snomasks)",
	"Gottem / jesopo", // Author
	"unrealircd-5", // Modversion
};

// =================================================================
// Module functions
// =================================================================

MOD_TEST() {
	HookAdd(modinfo->handle, HOOKTYPE_CONFIGTEST, 0, chansno_configtest);
	return MOD_SUCCESS;
}

MOD_INIT() {
	InitConf();

	CheckAPIError("CommandAdd(CHANSNO)", CommandAdd(modinfo->handle, MSG_CHANSNO, chansno, MAXPARA, CMD_USER));

	MARK_AS_GLOBAL_MODULE(modinfo);

	HookAdd(modinfo->handle, HOOKTYPE_CONFIGRUN, 0, chansno_configrun);
	HookAdd(modinfo->handle, HOOKTYPE_REHASH, 0, chansno_rehash);

	HookAdd(modinfo->handle, HOOKTYPE_LOCAL_CHANMODE, 0, chansno_hook_chanmode);
	HookAdd(modinfo->handle, HOOKTYPE_LOCAL_CONNECT, 0, chansno_hook_connect);
	HookAdd(modinfo->handle, HOOKTYPE_LOCAL_QUIT, 0, chansno_hook_quit);
	HookAdd(modinfo->handle, HOOKTYPE_LOCAL_JOIN, 0, chansno_hook_join);
	HookAdd(modinfo->handle, HOOKTYPE_LOCAL_KICK, 0, chansno_hook_kick);
	HookAdd(modinfo->handle, HOOKTYPE_LOCAL_NICKCHANGE, 0, chansno_hook_nickchange);
	HookAdd(modinfo->handle, HOOKTYPE_LOCAL_PART, 0, chansno_hook_part);
	HookAdd(modinfo->handle, HOOKTYPE_SERVER_CONNECT, 0, chansno_hook_serverconnect);
	HookAdd(modinfo->handle, HOOKTYPE_SERVER_QUIT, 0, chansno_hook_serverquit);
	HookAdd(modinfo->handle, HOOKTYPE_TOPIC, 0, chansno_hook_topic);
	HookAdd(modinfo->handle, HOOKTYPE_UNKUSER_QUIT, 0, chansno_hook_unkuserquit);
	HookAdd(modinfo->handle, HOOKTYPE_CHANNEL_CREATE, 0, chansno_hook_channelcreate);
	HookAdd(modinfo->handle, HOOKTYPE_CHANNEL_DESTROY, 0, chansno_hook_channeldestroy);
	HookAdd(modinfo->handle, HOOKTYPE_LOCAL_SPAMFILTER, 0, chansno_hook_spamfilter);
	HookAdd(modinfo->handle, HOOKTYPE_TKL_ADD, 0, chansno_hook_tkladd);

	return MOD_SUCCESS;
}

MOD_LOAD() {
	CheckAPIError("CommandOverrideAdd(OPER)", CommandOverrideAdd(modinfo->handle, "OPER", chansno_override_oper));
	return MOD_SUCCESS;
}

MOD_UNLOAD() {
	FreeConf();
	return MOD_SUCCESS;
}

// =================================================================
// Functions related to loading/unloading configuration
// =================================================================

static void InitConf(void) {
	ConfChanSno	= NULL;
	msgtype = MT_PRIVMSG;
}

static void FreeConf(void) {
	ChanSnoFlag *c;
	ListStruct *next;

	for(c = ConfChanSno; c; c = (ChanSnoFlag *) next) {
		next = (ListStruct *)c->next;
		DelListItem(c, ConfChanSno);
		safe_free(c->channel);
		safe_free(c);
	}
}

// =================================================================
// Config file interfacing
// =================================================================

int chansno_rehash(void) {
	FreeConf();
	InitConf();
	return HOOK_CONTINUE;
}

int chansno_configtest(ConfigFile *cf, ConfigEntry *ce, int type, int *errs) {
	ConfigEntry *cep, *cep2;
	int errors = 0;

	if(type != CONFIG_MAIN)
		return 0;

	if(!ce->ce_varname || strcmp(ce->ce_varname, "chansno"))
		return 0;

	for(cep = ce->ce_entries; cep; cep = cep->ce_next) {
		if(!cep->ce_varname) {
			config_error("%s:%i: blank chansno item", cep->ce_fileptr->cf_filename, cep->ce_varlinenum);
			errors++;
			continue;
		}
		if(!cep->ce_vardata) {
			config_error("%s:%i: chansno::%s item without value", cep->ce_fileptr->cf_filename, cep->ce_varlinenum, cep->ce_varname);
			errors++;
			continue;
		}
		if(!strcmp(cep->ce_varname, "channel")) {
			if(!cep->ce_entries) {
				config_error("%s:%i: chansno::channel without contents", cep->ce_fileptr->cf_filename, cep->ce_varlinenum);
				errors++;
				continue;
			}
			for(cep2 = cep->ce_entries; cep2; cep2 = cep2->ce_next) {
				if(!cep2->ce_varname) {
					config_error("%s:%i: chansno::channel item without variable name", cep2->ce_fileptr->cf_filename, cep2->ce_varlinenum);
					errors++;
					continue;
				}
				if(!config_binary_flags_search(_ChanSnoFlags, cep2->ce_varname, CHSNO_TABLESIZE)) {
					config_error("%s:%i: unknown chansno::channel flag '%s'", cep2->ce_fileptr->cf_filename, cep2->ce_varlinenum, cep2->ce_varname);
					errors++;
				}
			}
		}
		else if(!strcmp(cep->ce_varname, "msgtype")) {
			if(strcmp(cep->ce_vardata, "privmsg") && strcmp(cep->ce_vardata, "notice")) {
				config_error("%s:%i: unknown chansno::msgtype '%s'", cep->ce_fileptr->cf_filename, cep->ce_varlinenum, cep->ce_vardata);
				errors++;
			}
		}
		else {
			config_error("%s:%i: unknown directive chansno::%s", cep->ce_fileptr->cf_filename, cep->ce_varlinenum, cep->ce_varname);
			errors++;
		}
	}
	*errs = errors;
	return errors ? -1 : 1;
}

int chansno_configrun(ConfigFile *cf, ConfigEntry *ce, int type) {
	ConfigEntry *cep, *cep2;
	OperFlag *ofp;
	ChanSnoFlag *ca;

	if(type != CONFIG_MAIN)
		return 0;

	if(!ce->ce_varname || strcmp(ce->ce_varname, "chansno"))
		return 0;

	for(cep = ce->ce_entries; cep; cep = cep->ce_next) {
		if(!strcmp(cep->ce_varname, "channel")) {
			ca = safe_alloc(sizeof(ChanSnoFlag));
			safe_strdup(ca->channel, cep->ce_vardata);

			for(cep2 = cep->ce_entries; cep2; cep2 = cep2->ce_next) {
				if((ofp = config_binary_flags_search(_ChanSnoFlags, cep2->ce_varname, CHSNO_TABLESIZE)))
					ca->flags |= ofp->flag;
			}

			AddListItem(ca, ConfChanSno);
		}
		else if(!strcmp(cep->ce_varname, "msgtype")) {
			if(!strcmp(cep->ce_vardata, "privmsg"))
				msgtype = MT_PRIVMSG;
			else if(!strcmp(cep->ce_vardata, "notice"))
				msgtype = MT_NOTICE;
		}
	}
	return 1;
}

// ===============================================================
// Functions used by chansno
// ===============================================================

static char *get_flag_names(long flags) {
	u_int i, found = 0;
	memset(&msgbuf, 0, sizeof(msgbuf));

	for(i = 0; i < CHSNO_TABLESIZE; i++) {
		if(flags & _ChanSnoFlags[i].flag) {
			if(found)
				strncat(msgbuf, ", ", MaxSize(msgbuf));
			else
				found = 1;

			strncat(msgbuf, _ChanSnoFlags[i].name, MaxSize(msgbuf));
		}
	}

	if(!strlen(msgbuf))
		strcpy(msgbuf, "<None>");

	return msgbuf;
}

static void stats_chansno_channels(Client *client) {
	ChanSnoFlag *c;
	for(c = ConfChanSno; c; c = c->next)
		sendnumericfmt(client, RPL_TEXT, ":channel %s: %s", c->channel, get_flag_names(c->flags));
	sendnumeric(client, RPL_ENDOFSTATS, 'S');
}

static void stats_chansno_config(Client *client) {
	sendnumericfmt(client, RPL_TEXT, "msgtype: %s", MsgType);
	sendnumeric(client, RPL_ENDOFSTATS, 'S');
}

// ===============================================================
// chansno
//      parv[0]: sender prefix
//      parv[1]: option
//      parv[2]: server name (optional)
// ===============================================================

CMD_FUNC(chansno) {
	if(!IsUser(client))
		return;

	if(!IsOper(client)) {
		sendnumeric(client, ERR_NOPRIVILEGES);
		return;
	}

	if(!IsParam(1)) {
		sendto_one(client, NULL, ":%s NOTICE %s :Usage:", me.name, client->name);
		sendto_one(client, NULL, ":%s NOTICE %s :    /chansno <option> [<servername>]", me.name, client->name);
		sendto_one(client, NULL, ":%s NOTICE %s :Options:", me.name, client->name);
		sendto_one(client, NULL, ":%s NOTICE %s :    list: displays the chansno::channel block list", me.name, client->name);
		sendto_one(client, NULL, ":%s NOTICE %s :    config: shows the rest of chansno configuration", me.name, client->name);
		return;
	}

	if(IsParam(2)) {
		if(hunt_server(client, NULL, ":%s CHANSNO %s %s", 2, parc, parv) != HUNTED_ISME)
			return;
	}

	if(!strcasecmp(parv[1], "list"))
		stats_chansno_channels(client);
	else if(!strcasecmp(parv[1], "config"))
		stats_chansno_config(client);
	else {
		sendto_one(client, NULL, ":%s NOTICE %s :Unknown option %s." " Valid options are: list, config", me.name, client->name, parv[1]);
		return;
	}
}

// ===============================================================
// Interface for sending notifications
// ===============================================================

static u_int find_sno_channel(Channel *channel) {
	ChanSnoFlag *c;

	for(c = ConfChanSno; c; c = c->next) {
		if(!strcasecmp(channel->chname, c->channel))
			return 1;
	}

	return 0;
}

static void SendNotice_simple(long type, int local) {
	ChanSnoFlag *c;
	Channel *channel;
	MessageTag *mtags = NULL;

	for(c = ConfChanSno; c; c = c->next) {
		if(c->flags & type) {
			if((channel = find_channel(c->channel, NULL)) && msgbuf[1]) {
				new_message(&me, NULL, &mtags);
				if(local) {
					// The following call sends the message to locally connected users in the channel only ;]
					// Currently only applies to channel destructions, as every server keeps track of that themselves
					sendto_channel(channel, &me, NULL, 0, 0, SEND_LOCAL, mtags, ":%s %s %s :%s", me.name, MsgType, channel->chname, msgbuf);
				}
				else {
					// This call sends the message to all clients in the particular channel ;]
					sendto_channel(channel, &me, NULL, 0, 0, SEND_ALL, mtags, ":%s %s %s :%s", me.name, MsgType, channel->chname, msgbuf);
				}
				free_message_tags(mtags);
			}
		}
	}
}

static void SendNotice_channel(Channel *channel, long type, int local) {
	ChanSnoFlag *c;
	Channel *channel2;
	MessageTag *mtags = NULL;

	for(c = ConfChanSno; c; c = c->next) {
		if(c->flags & type) {
			if(channel && !find_sno_channel(channel) && (channel2 = find_channel(c->channel, NULL)) && msgbuf[1]) {
				new_message(&me, NULL, &mtags);
				if(local) {
					// The following call sends the message to locally connected users in the channel only ;]
					// Currently only applies to channel destructions, as every server keeps track of that themselves
					sendto_channel(channel2, &me, NULL, 0, 0, SEND_LOCAL, mtags, ":%s %s %s :[%s] %s", me.name, MsgType, channel2->chname, channel->chname, msgbuf);
				}
				else {
					// This call sends the message to all clients in the particular channel ;]
					sendto_channel(channel2, &me, NULL, 0, 0, SEND_LOCAL, mtags, ":%s %s %s :[%s] %s", me.name, MsgType, channel2->chname, channel->chname, msgbuf);
				}
				free_message_tags(mtags);
			}
		}
	}
}

int chansno_hook_chanmode(Client *client, Channel *channel, MessageTag *mtags, char *modebuf, char *parabuf, time_t sendts, int samode) {
	snprintf(msgbuf, sizeof(msgbuf), "%s sets mode: %s%s%s", client->name, modebuf, BadPtr(parabuf) ? "" : " ", BadPtr(parabuf) ? "" : parabuf);
	SendNotice_channel(channel, CHSNO_CHANMODE, 0);
	return HOOK_CONTINUE;
}

int chansno_hook_connect(Client *client) {
	char secure[256];
	*secure = '\0';
	if(IsSecure(client))
		snprintf(secure, sizeof(secure), " [secure %s]", SSL_get_cipher(client->local->ssl));
	ircsnprintf(msgbuf, sizeof(msgbuf), "*** Client connecting: %s (%s@%s) [%s] [port %d] {%s}%s", client->name, UserName(client), RealHost(client), client->ip, client->local->listener->port, (client->local->class ? client->local->class->name : "0"), secure);
	SendNotice_simple(CHSNO_CONNECT, 0);
	return HOOK_CONTINUE;
}

int chansno_hook_quit(Client *client, MessageTag *mtags, char *comment) {
	if(BadPtr(comment))
		ircsnprintf(msgbuf, sizeof(msgbuf), "*** Client exiting: %s (%s@%s) [%s]", client->name, UserName(client), RealHost(client), client->ip);
	else
		ircsnprintf(msgbuf, sizeof(msgbuf), "*** Client exiting: %s (%s@%s) [%s] (%s)", client->name, UserName(client), RealHost(client), client->ip, comment);
	SendNotice_simple(CHSNO_DISCONNECT, 0);
	return HOOK_CONTINUE;
}

int chansno_hook_unkuserquit(Client *client, MessageTag *mtags, char *comment) {
	if(BadPtr(comment))
		snprintf(msgbuf, sizeof(msgbuf), "Unknown client exiting: %s", client->ip);
	else
		snprintf(msgbuf, sizeof(msgbuf), "Unknown client exiting: %s (%s)", client->ip, comment);

	SendNotice_simple(CHSNO_UNKUSER_QUIT, 0);
	return HOOK_CONTINUE;
}

int chansno_hook_join(Client *client, Channel *channel, MessageTag *mtags, char *parv[]) {
	snprintf(msgbuf, sizeof(msgbuf), "%s (%s@%s) has joined %s", client->name, UserName(client), RealHost(client), channel->chname);
	SendNotice_channel(channel, CHSNO_JOIN, 0);
	return HOOK_CONTINUE;
}

int chansno_hook_kick(Client *client, Client *victim, Channel *channel, MessageTag *mtags, char *comment) {
	snprintf(msgbuf, sizeof(msgbuf), "%s has kicked %s (%s)", client->name, victim->name, comment);
	SendNotice_channel(channel, CHSNO_KICK, 0);
	return HOOK_CONTINUE;
}

int chansno_hook_nickchange(Client *client, char *newnick) {
	snprintf(msgbuf, sizeof(msgbuf), "%s (%s@%s) has changed their nickname to %s", client->name, UserName(client), RealHost(client), newnick);
	SendNotice_simple(CHSNO_NICKCHANGE, 0);
	return HOOK_CONTINUE;
}

int chansno_hook_part(Client *client, Channel *channel, MessageTag *mtags, char *comment) {
	snprintf(msgbuf, sizeof(msgbuf), "%s (%s@%s) has left %s (%s)", client->name, UserName(client), RealHost(client), channel->chname, comment ? comment : client->name);
	SendNotice_channel(channel, CHSNO_PART, 0);
	return HOOK_CONTINUE;
}

int chansno_hook_serverconnect(Client *client) {
	if(!client || !client->local || !client->local->listener) return HOOK_CONTINUE;

	snprintf(msgbuf, sizeof(msgbuf), "Server connecting on port %d: %s (%s) [%s] %s%s%s",
		client->local->listener->port, client->name, client->info,
		client->local->class ? client->local->class->name : "",
		IsSecure(client) ? "[secure " : "",
		IsSecure(client) ? SSL_get_cipher(client->local->ssl) : "",
		IsSecure(client) ? "]" : "");

	SendNotice_simple(CHSNO_SCONNECT, 0);
	return HOOK_CONTINUE;
}

int chansno_hook_serverquit(Client *client, MessageTag *mtags) {
	/* The hook supports no reason :-( */
	snprintf(msgbuf, sizeof(msgbuf), "Server exiting: %s", client->name);
	SendNotice_simple(CHSNO_SQUIT, 0);
	return HOOK_CONTINUE;
}

int chansno_hook_topic(Client *client, Channel *channel, MessageTag *mtags, char *topic) {
	if(MyConnect(client)) {
		snprintf(msgbuf, sizeof(msgbuf), "%s changes topic to: %s", client->name, topic);
		SendNotice_channel(channel, CHSNO_TOPIC, 0);
	}
	return HOOK_CONTINUE;
}

int chansno_hook_channelcreate(Client *client, Channel *channel) {
	if(!IsServer(client) && !find_sno_channel(channel)) {
		snprintf(msgbuf, sizeof(msgbuf), "%s created channel %s", client->name, channel->chname);
		SendNotice_simple(CHSNO_CHANNEL_CREATE, 0);
	}
	return HOOK_CONTINUE;
}

int chansno_hook_channeldestroy(Channel *channel, int *should_destroy) {
	if(!find_sno_channel(channel)) {
		snprintf(msgbuf, sizeof(msgbuf), "Channel %s has been destroyed", channel->chname);
		SendNotice_simple(CHSNO_CHANNEL_DESTROY, 1);
	}
	return HOOK_CONTINUE;
}

int chansno_hook_spamfilter(Client *acptr, char *str, char *str_in, int type, char *target, TKL *tkl) {
	snprintf(msgbuf, sizeof(msgbuf), "[Spamfilter] %s!%s@%s matches filter '%s': [%s%s: '%s'] [%s]",
		acptr->name, acptr->user->username, acptr->user->realhost, tkl->ptr.spamfilter->match->str, cmdname_by_spamftarget(type),
		target, str_in, unreal_decodespace(tkl->ptr.spamfilter->tkl_reason));
	SendNotice_simple(CHSNO_SPAMFILTER, 0);
	return HOOK_CONTINUE;
}

CMD_OVERRIDE_FUNC(chansno_override_oper) {
	char *operclass;
	ConfigItem_oper *oper;

	CallCommandOverride(ovr, client, recv_mtags, parc, parv);

	if(!MyUser(client) || IsOper(client))
		return;

	if(IsOper(client)) {
		oper = find_oper(client->user->operlogin);
		if(oper && oper->operclass)
			operclass = oper->operclass;

		snprintf(msgbuf, sizeof(msgbuf), "Oper-up by %s (%s@%s) [%s] (operclass: %s)", client->name, UserName(client), RealHost(client), parv[1], operclass);
		SendNotice_simple(CHSNO_OPER, 0);
	}
}

// Added by jesopo
int chansno_hook_tkladd(Client *client, TKL *tkl) {
	char setby[NICKLEN + USERLEN + HOSTLEN + 6];
	char *name;
	Client *setter;
	char txt[256];
	char set_at[128];
	char expire_at[128];

	strncpy(setby, tkl->set_by, sizeof(setby));
	name = strtok(setby, "!");

	setter = find_client(name, NULL);
	if(setter == NULL || (!IsMe(setter) && !MyUser(setter)))
		return HOOK_CONTINUE;

	switch(tkl->type) {
		case TKL_KILL:
			strlcpy(txt, "K-Line", sizeof(txt));
			break;
		case TKL_ZAP:
			strlcpy(txt, "Z-Line", sizeof(txt));
			break;
		case TKL_KILL | TKL_GLOBAL:
			strlcpy(txt, "G-Line", sizeof(txt));
			break;
		case TKL_ZAP | TKL_GLOBAL:
			strlcpy(txt, "Global Z-Line", sizeof(txt));
			break;
		case TKL_SHUN | TKL_GLOBAL:
			strlcpy(txt, "Shun", sizeof(txt));
			break;
		case TKL_NAME | TKL_GLOBAL:
			strlcpy(txt, "Global Q-Line", sizeof(txt));
			break;
		case TKL_NAME:
			strlcpy(txt, "Q-Line", sizeof(txt));
			break;
		case TKL_SPAMF | TKL_GLOBAL:
			strlcpy(txt, "Global Spamfilter", sizeof(txt));
			break;
		case TKL_EXCEPTION | TKL_GLOBAL:
			strlcpy(txt, "Global TKL Exception (E-Line)", sizeof(txt));
			break;
		default:
			strlcpy(txt, "Unknown *-Line", sizeof(txt));
			break;
	}

	*set_at = '\0';
	short_date(tkl->set_at, set_at);

	if(tkl->expire_at != 0) {
		*expire_at = '\0';
		short_date(tkl->expire_at, expire_at);

		if(TKLIsNameBan(tkl) && tkl->ptr.nameban)
			snprintf(msgbuf, sizeof(msgbuf), "%s added for %s on %s GMT (from %s to expire at %s GMT: %s)", txt, tkl->ptr.nameban->name, set_at, tkl->set_by, expire_at, tkl->ptr.nameban->reason);

		else if(TKLIsSpamfilter(tkl) && tkl->ptr.spamfilter)
			snprintf(msgbuf, sizeof(msgbuf), "%s added for %s on %s GMT (from %s to expire at %s GMT: %s)", txt, tkl->ptr.spamfilter->match->str, set_at, tkl->set_by, expire_at, tkl->ptr.spamfilter->tkl_reason);

		else if(TKLIsServerBan(tkl) && tkl->ptr.serverban)
			snprintf(msgbuf, sizeof(msgbuf), "%s added for %s@%s on %s GMT (from %s to expire at %s GMT: %s)", txt, tkl->ptr.serverban->usermask, tkl->ptr.serverban->hostmask, set_at, tkl->set_by, expire_at, tkl->ptr.serverban->reason);

		else if(TKLIsBanException(tkl) && tkl->ptr.banexception)
			snprintf(msgbuf, sizeof(msgbuf), "%s added for %s@%s on %s GMT (from %s to expire at %s GMT: %s)", txt, tkl->ptr.banexception->usermask, tkl->ptr.banexception->hostmask, set_at, tkl->set_by, expire_at, tkl->ptr.banexception->reason);

		else
			snprintf(msgbuf, sizeof(msgbuf), "%s (%d) added on %s GMT (from %s to expire at %s GMT: UNKNOWN REASON)", txt, tkl->type, set_at, tkl->set_by, expire_at);
	}
	else {
		if(TKLIsNameBan(tkl) && tkl->ptr.nameban)
			snprintf(msgbuf, sizeof(msgbuf), "Permanent %s added for %s on %s GMT (from %s: %s)", txt, tkl->ptr.nameban->name, set_at, tkl->set_by, tkl->ptr.nameban->reason);

		else if(TKLIsSpamfilter(tkl) && tkl->ptr.spamfilter)
			snprintf(msgbuf, sizeof(msgbuf), "Permanent %s added for %s on %s GMT (from %s: %s)", txt, tkl->ptr.spamfilter->match->str, set_at, tkl->set_by, tkl->ptr.spamfilter->tkl_reason);

		else if(TKLIsServerBan(tkl) && tkl->ptr.serverban)
			snprintf(msgbuf, sizeof(msgbuf), "Permanent %s added for %s@%s on %s GMT (from %s: %s)", txt, tkl->ptr.serverban->usermask, tkl->ptr.serverban->hostmask, set_at, tkl->set_by, tkl->ptr.serverban->reason);

		else if(TKLIsBanException(tkl) && tkl->ptr.banexception)
			snprintf(msgbuf, sizeof(msgbuf), "Permanent %s added for %s@%s on %s GMT (from %s: %s)", txt, tkl->ptr.banexception->usermask, tkl->ptr.banexception->hostmask, set_at, tkl->set_by, tkl->ptr.banexception->reason);

		else
			snprintf(msgbuf, sizeof(msgbuf), "Permanent %s (%d) added on %s GMT (from %s: UNKNOWN REASON)", txt, tkl->type, set_at, tkl->set_by);
	}

	SendNotice_simple(CHSNO_TKL_ADD, 0);
	return HOOK_CONTINUE;
}
