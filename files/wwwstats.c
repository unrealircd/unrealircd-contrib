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

#define CHANNEL_MESSAGE_COUNT(channel) moddata_channel(channel, message_count_md).i

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
ModDataInfo *message_count_md;

int wwwstats_msg(Client *sptr, Channel *chptr, MessageTag *mtags, char *msg, MESSAGE_SENDTYPE sendtype);
EVENT(wwwstats_socket_evt);
void asend_sprintf(asendInfo *info, char *fmt, ...);
void append_int_param(asendInfo *info, char *param, int value);
char *json_escape(char *d, const char *a);
void md_free(ModData *md);
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
		config_warn("[wwwstats] warning: socket path not specified! Socket won't be created. This module will not be useful.");
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
	socket_hpath = 0;

	HookAdd(modinfo->handle, HOOKTYPE_CONFIGTEST, 0, wwwstats_configtest);
	HookAdd(modinfo->handle, HOOKTYPE_CONFIGPOSTTEST, 0, wwwstats_configposttest);
	return MOD_SUCCESS;
}

/* This is called on module init, before Server Ready */
MOD_INIT(){
	ModDataInfo mreq;
	/*
	 * We call our add_Command crap here
	*/
	HookAdd(modinfo->handle, HOOKTYPE_CONFIGRUN, 0, wwwstats_configrun);
	HookAdd(modinfo->handle, HOOKTYPE_PRE_CHANMSG, 0, wwwstats_msg);

	memset(&mreq, 0 , sizeof(mreq));
	mreq.type = MODDATATYPE_CHANNEL;
	mreq.name = "message_count",
	mreq.free = md_free;
	message_count_md = ModDataAdd(modinfo->handle, mreq);
	if(!message_count_md){
		config_error("[%s] Failed to request message_count moddata: %s", MOD_HEADER.name, ModuleGetErrorStr(modinfo->handle));
		return MOD_FAILED;
	}

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
	close(stats_socket);
	unlink(stats_addr.sun_path);

	if(socket_path) free(socket_path);
	
	return MOD_SUCCESS;
}

void md_free(ModData *md){
	md->i = 0;
}

int wwwstats_msg(Client *sptr, Channel *chptr, MessageTag *mtags, char *msg, MESSAGE_SENDTYPE sendtype) { // called on channel messages
	counter++;
	CHANNEL_MESSAGE_COUNT(chptr)++;
	return HOOK_CONTINUE;
}

EVENT(wwwstats_socket_evt){
	char buf[2000];
	char topic[6*TOPICLEN+1];
	char name[6*CHANNELLEN+1];
	int i;
	int sock;
	asendInfo asinfo;
	struct sockaddr_un cli_addr;
	socklen_t slen;
	Client *acptr;
	Channel *channel;
	unsigned int hashnum;

	if(!socket_hpath) return; // nothing to do

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

	i=0;
	for(hashnum = 0; hashnum < CHAN_HASH_TABLE_SIZE; hashnum++){
		for(channel = hash_get_chan_bucket(hashnum); channel; channel = channel->hnextch){
			if(!PubChannel(channel)) continue;
			asend_sprintf(&asinfo, "%s{\"name\":\"%s\",\"users\":%d,\"messages\":%d", i?",":"",
				json_escape(name, channel->chname), channel->users, CHANNEL_MESSAGE_COUNT(channel));
			if(channel->topic)
				asend_sprintf(&asinfo, ",\"topic\":\"%s\"", json_escape(topic, channel->topic));
			asend_sprintf(&asinfo, "}");
			i++;
		}
	}
	
	asend_sprintf(&asinfo, "]}");

	if(send_buf[0]) {
		send(sock, send_buf, strlen(send_buf), 0);
		send_buf[0] = 0;
	}

	close(sock);
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

