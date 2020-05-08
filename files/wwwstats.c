/* Copyright (C) All Rights Reserved
** Written by rocket & k4be
** Website: https://github.com/pirc-pl/unrealircd-modules/
** License: GPLv3 https://www.gnu.org/licenses/gpl-3.0.html
*/

/*** <<<MODULE MANAGER START>>>
module
{
        documentation "https://github.com/pirc-pl/unrealircd-modules/blob/master/README.md";
        troubleshooting "In case of problems, contact k4be on irc.pirc.pl.";
        min-unrealircd-version "5.*";
        post-install-text {
                "The module is installed. Now you need to add a loadmodule line:";
                "loadmodule \"third/wwwstats\";";
                "then create a valid configuration block as in the example below:";
                "wwwstats {";
				" socket-path \"/tmp/wwwstats.sock\"; // do not specify if you don't want the socket";
				"};";
				"And /REHASH the IRCd.";
				"";
				"If you want a version with MySQL/MariaDB support, look for 'wwwstats-mysql'";
				"on https://github.com/pirc-pl/unrealircd-modules - unfortunately it can't";
				"be installed by the module manager.";
				"Detailed documentation is available on https://github.com/pirc-pl/unrealircd-modules/blob/master/README.md#wwwstats";
        }
}
*** <<<MODULE MANAGER END>>>
*/

#define MYCONF "wwwstats"

#include "unrealircd.h"
#include <sys/socket.h>
#include <sys/un.h>
#include <pthread.h>

#ifndef TOPICLEN
#define TOPICLEN MAXTOPICLEN
#endif

#if (UNREAL_VERSION_GENERATION == 5 && UNREAL_VERSION_MAJOR == 0 && UNREAL_VERSION_MINOR < 5)
#define MESSAGE_SENDTYPE int
#else
#define MESSAGE_SENDTYPE SendType
#endif

struct chanStats_s {
	Channel *chan;
	char chname[2*CHANNELLEN+1];
	int msg;
	int exists;
	struct chanStats_s *next;
};

struct channelInfo_s {
	int hashnum;
	Channel *chan;
	int messages;
};

struct asendInfo_s {
	int sock;
	char *buf;
	int bufsize;
	char *tmpbuf;
};

typedef struct chanStats_s chanStats;
typedef struct channelInfo_s channelInfo;
typedef struct asendInfo_s asendInfo;

int counter;
time_t init_time;

int stats_socket;
char send_buf[4096];
struct sockaddr_un stats_addr;

int wwwstats_msg(Client *sptr, Channel *chptr, MessageTag *mtags, char *msg, MESSAGE_SENDTYPE sendtype);
EVENT(wwwstats_socket_evt);
void asend_sprintf(asendInfo *info, char *fmt, ...);
void append_int_param(asendInfo *info, char *param, int value);
int getChannelInfo(channelInfo *prev);
Channel *getChanByName(char *name);
void removeExpiredChannels();
char *tmp_escape(char *d, const char *a);
char *json_escape(char *d, const char *a);
void appendChannel(Channel *ch, int messages);

int wwwstats_configtest(ConfigFile *cf, ConfigEntry *ce, int type, int *errs);
int wwwstats_configposttest(int *errs);
int wwwstats_configrun(ConfigFile *cf, ConfigEntry *ce, int type);

chanStats *chans, *chans_last;

// config file stuff, based on Gottem's module

static char *socket_path;
int socket_hpath=0;

int wwwstats_configtest(ConfigFile *cf, ConfigEntry *ce, int type, int *errs) {
	ConfigEntry *cep; // For looping through our bl0cc
	int errors = 0; // Error count
	int i; // iter8or m8

	// Since we'll add a new top-level block to unrealircd.conf, need to filter on CONFIG_MAIN lmao
	if(type != CONFIG_MAIN)
		return 0; // Returning 0 means idgaf bout dis

	// Check for valid config entries first
	if(!ce || !ce->ce_varname)
		return 0;

	// If it isn't our bl0ck, idc
	if(strcmp(ce->ce_varname, MYCONF))
		return 0;

	// Loop dat shyte fam
	for(cep = ce->ce_entries; cep; cep = cep->ce_next) {
		// Do we even have a valid name l0l?
		if(!cep->ce_varname) {
			config_error("%s:%i: blank %s item", cep->ce_fileptr->cf_filename, cep->ce_varlinenum, MYCONF); // Rep0t error
			errors++; // Increment err0r count fam
			continue; // Next iteration imo tbh
		}

		if(!strcmp(cep->ce_varname, "socket-path")) {
			if(!cep->ce_vardata) {
				config_error("%s:%i: %s::%s must be a path", cep->ce_fileptr->cf_filename, cep->ce_varlinenum, MYCONF, cep->ce_varname);
				errors++; // Increment err0r count fam
				continue;
			}
			socket_hpath = 1;
			continue;
		}

		// Anything else is unknown to us =]
		config_warn("%s:%i: unknown item %s::%s", cep->ce_fileptr->cf_filename, cep->ce_varlinenum, MYCONF, cep->ce_varname); // So display just a warning
	}

	*errs = errors;
	return errors ? -1 : 1; // Returning 1 means "all good", -1 means we shat our panties
}

int wwwstats_configposttest(int *errs) {
	if(!socket_hpath){
		config_warn("m_wwwstats: warning: socket path not specified! Socket won't be created.");
	}
	return 1;
}

// "Run" the config (everything should be valid at this point)
int wwwstats_configrun(ConfigFile *cf, ConfigEntry *ce, int type) {
	ConfigEntry *cep; // For looping through our bl0cc

	// Since we'll add a new top-level block to unrealircd.conf, need to filter on CONFIG_MAIN lmao
	if(type != CONFIG_MAIN)
		return 0; // Returning 0 means idgaf bout dis

	// Check for valid config entries first
	if(!ce || !ce->ce_varname)
		return 0;

	// If it isn't our bl0cc, idc
	if(strcmp(ce->ce_varname, MYCONF))
		return 0;

	// Loop dat shyte fam
	for(cep = ce->ce_entries; cep; cep = cep->ce_next) {
		// Do we even have a valid name l0l?
		if(!cep->ce_varname)
			continue; // Next iteration imo tbh

		if(cep->ce_vardata && !strcmp(cep->ce_varname, "socket-path")) {
			socket_path = strdup(cep->ce_vardata);
			continue;
		}
	}
	return 1; // We good
}

ModuleHeader MOD_HEADER = {
	"third/wwwstats",   /* Name of module */
	"5.0", /* Version */
	"Provides data for network stats", /* Short description of module */
	"rocket, k4be@PIRC",
	"unrealircd-5"
};

// Configuration testing-related hewks go in testing phase obv
MOD_TEST(){
	// We have our own config block so we need to checkem config obv m9
	// Priorities don't really matter here
	HookAdd(modinfo->handle, HOOKTYPE_CONFIGTEST, 0, wwwstats_configtest);
	HookAdd(modinfo->handle, HOOKTYPE_CONFIGPOSTTEST, 0, wwwstats_configposttest);
	return MOD_SUCCESS;
}

/* This is called on module init, before Server Ready */
MOD_INIT(){
	/*
	 * We call our add_Command crap here
	*/
	HookAdd(modinfo->handle, HOOKTYPE_CONFIGRUN, 0, wwwstats_configrun);
	HookAdd(modinfo->handle, HOOKTYPE_PRE_CHANMSG, 0, wwwstats_msg);

	return MOD_SUCCESS;
}

/* Is first run when server is 100% ready */
MOD_LOAD(){
	if(socket_path){
		stats_addr.sun_family = AF_UNIX;
		strcpy(stats_addr.sun_path, socket_path);
		unlink(stats_addr.sun_path);	// remove old socket if exists
	}

	counter = 0;

	chans = NULL;
	chans_last = NULL;

	if(socket_path){
		stats_socket = socket(PF_UNIX, SOCK_STREAM, 0);
		bind(stats_socket, (struct sockaddr*) &stats_addr, SUN_LEN(&stats_addr));
		chmod(socket_path, 0777);
		listen(stats_socket, 5); // open socket
		fcntl(stats_socket, F_SETFL, O_NONBLOCK);
	}

	EventAdd(modinfo->handle, "wwwstats_socket", wwwstats_socket_evt, NULL, 100, 0);

	return MOD_SUCCESS;
}

/* Called when module is unloaded */
MOD_UNLOAD(){
	time_t act_time;
	chanStats *next;

	close(stats_socket);
	unlink(stats_addr.sun_path);

	act_time = time(NULL);

	for(;chans;chans=next) {
		next=chans->next;
		free(chans);
	}

	if(socket_path) free(socket_path);
	
	return MOD_SUCCESS;
}

int wwwstats_msg(Client *sptr, Channel *chptr, MessageTag *mtags, char *msg, MESSAGE_SENDTYPE sendtype) { // called on channel messages
	chanStats *lp;
	int c_msg;
	counter++;
	for(lp=chans; lp; lp=lp->next) if(lp->chan==chptr) break;

	if(lp) lp->msg++; // if channel found, increase msg count
	else { // create new channel
		c_msg = 1;
//	    sendto_realops("wwwstats: added channel %s, %d msgs", name, c_msg);
		appendChannel(chptr, c_msg);
	}
	return HOOK_CONTINUE;
}

EVENT(wwwstats_socket_evt){
	char buf[2000];
	char topic[6*TOPICLEN+1];
	char name[6*CHANNELLEN+1];
	int i;
	int sock;
	channelInfo chinfo;
	asendInfo asinfo;
	struct sockaddr_un cli_addr;
	socklen_t slen;
	Client *acptr;

	sock = accept(stats_socket, (struct sockaddr*) &cli_addr, &slen); // wait for a connection
	
	slen = sizeof(cli_addr);
	asinfo.buf = send_buf;
	asinfo.bufsize = sizeof(send_buf);
	asinfo.tmpbuf = buf;
	
	if(sock<0){
		if(errno == EWOULDBLOCK || errno == EAGAIN) return;
		sendto_realops("wwwstats: accept error: %s", strerror(errno));
		return;
	}
	asinfo.sock = sock;
	send_buf[0] = 0;
	asend_sprintf(&asinfo, "{"); // generate JSON data
	append_int_param(&asinfo, "clients", irccounts.clients);
	append_int_param(&asinfo, "channels", irccounts.channels);
	append_int_param(&asinfo, "operators", irccounts.operators);
	append_int_param(&asinfo, "servers", irccounts.servers);
	append_int_param(&asinfo, "messages", counter);

	i=0;

	asend_sprintf(&asinfo, "\"serv\":[");
	list_for_each_entry(acptr, &global_server_list, client_node){
		if (IsULine(acptr) && HIDE_ULINES)
			continue;
		asend_sprintf(&asinfo, "%s{\"name\":\"%s\",\"users\":%1d}", i?",":"", acptr->name, acptr->serv->users);
		i++;
	}
	
	asend_sprintf(&asinfo, "],\"chan\":[");

	removeExpiredChannels();
	chinfo.chan = NULL;

	i=0;
	while(getChannelInfo(&chinfo)) {
		if(!PubChannel(chinfo.chan)) continue;
		asend_sprintf(&asinfo, "%s{\"name\":\"%s\",\"users\":%d,\"messages\":%d", i?",":"",
			json_escape(name, chinfo.chan->chname), chinfo.chan->users, chinfo.messages);
		if(chinfo.chan->topic)
			asend_sprintf(&asinfo, ",\"topic\":\"%s\"", json_escape(topic, chinfo.chan->topic));
		asend_sprintf(&asinfo, "}");
	
		i++;
	}
	
	asend_sprintf(&asinfo, "]}");

	if(send_buf[0]) {
		send(sock, send_buf, strlen(send_buf), 0);
		send_buf[0] = 0;
	}

	close(sock);
}

void appendChannel(Channel *ch, int messages) {
	chanStats *lp;

	lp = malloc(sizeof(chanStats));
	lp->chan = ch;
	lp->msg = messages;
	strcpy(lp->chname, ch->chname);
	lp->next = NULL;
	if(chans_last) chans_last->next = lp;
	chans_last = lp;
	if(!chans) chans = lp;
}

void removeExpiredChannels() {
	int hashnum;
	Channel *c;
	chanStats *lp, *lpprev, *lpnext;
	
	for(lp=chans; lp; lp=lp->next) lp->exists = 0;

	for(hashnum=0; hashnum<CHAN_HASH_TABLE_SIZE; hashnum++) {
		c = (Channel*) hash_get_chan_bucket(hashnum);
		while(c) {
			for(lp=chans; lp; lp=lp->next) if(lp->chan==c) break;
			if(lp) lp->exists = 1;
			c = c->hnextch;
		}
	}

	lpprev = NULL;
	lpnext = NULL;
	for(lp=chans; lp; lp=lpnext) {
		if(!lp->exists) {
//			sendto_realops("wwwstats: deleted channel %s", lp->chname);
			if(lpprev) lpprev->next = lp->next;
				else chans = lp->next;
			if(!lp->next) chans_last = lpprev;
			lpnext = lp->next;
			free(lp);
			continue;
		}
		lpnext = lp->next;
		lpprev = lp;
	}
}

Channel *getChanByName(char *name) {
	channelInfo chinfo;

	chinfo.chan = NULL;
	while(getChannelInfo(&chinfo)) { 
		if(strcmp(chinfo.chan->chname, name)==0) return chinfo.chan;
	}
	return NULL;
}

int getChannelInfo(channelInfo *prev) {
	int hashnum = 0;
	int messages = 0;
	Channel *c = NULL;
	chanStats *lp;

	if(prev->chan) {
		hashnum = prev->hashnum;
		c = prev->chan->hnextch;
		if(!c) hashnum++;
	}

	if(!c) for(; hashnum<CHAN_HASH_TABLE_SIZE; hashnum++) {
		c = (Channel*) hash_get_chan_bucket(hashnum);
		if(c) break;
	}
	if(!c) return 0;

	for(lp=chans; lp; lp=lp->next) if(lp->chan==c) break;
	if(lp) messages = lp->msg;

	prev->hashnum = hashnum;
	prev->chan = c;
	prev->messages = messages;
	return 1;
}

void asend_sprintf(asendInfo *info, char *fmt, ...) {
	int bl, tl;
	va_list list;
	va_start(list, fmt);
	vsprintf(info->tmpbuf, fmt, list);
	bl = strlen(info->tmpbuf);
	tl = strlen(info->buf);
	if((bl+tl)>=info->bufsize) {
		send(info->sock, info->buf, tl, 0);
		info->buf[0] = 0;
	}

	strcat(info->buf, info->tmpbuf);
	va_end(list);
}

void append_int_param(asendInfo *info, char *param, int value) {
	asend_sprintf(info, "\"%s\":%d,", param, value);
}

char *tmp_escape(char *d, const char *a) { // now only for sql queries
	int diff=0;
	int i;
	for(i=0; a[i]; i++) {
		if((a[i]=='"') || (a[i]=='\\')) {
			d[diff+i] = '\\';
			diff++;
		}
		d[diff+i] = a[i];
	}
	d[diff+i] = 0;
	return d;
}

char *json_escape(char *d, const char *a) {
	int diff=0;
	int i, j;
	char buf[7];
    for(i=0; a[i]; i++) {
        if(a[i] == '"' || a[i] == '\\' || a[i] <= '\x1f') { // unicode chars don't need to be escaped
        	sprintf(buf, "\\u%04x", (int)a[i]);
        	for(j=0; j<6; j++){
	        	d[diff+i] = buf[j];
	        	diff++;
	        }
	        diff--;
        } else {
            d[diff+i] = a[i];
        }
    }
	d[diff+i] = 0;
	return d;
}

