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
        min-unrealircd-version "6.*";
        post-install-text {
                "The module is installed. Now you need to add a loadmodule line:";
                "loadmodule \"third/wwwstats\";";
                "then create a valid configuration block as in the example below:";
                "wwwstats {";
				" socket-path \"/tmp/wwwstats.sock\"; // this option is REQUIRED";
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

#ifndef TOPICLEN
#define TOPICLEN MAXTOPICLEN
#endif

#if (UNREAL_VERSION_GENERATION == 5 && UNREAL_VERSION_MAJOR == 0 && UNREAL_VERSION_MINOR < 5)
#define MESSAGE_SENDTYPE int
#else
#define MESSAGE_SENDTYPE SendType
#endif

#define CHANNEL_MESSAGE_COUNT(channel) moddata_channel(channel, message_count_md).i

int counter;
time_t init_time;

int stats_socket;
char send_buf[4096];
struct sockaddr_un stats_addr;
ModDataInfo *message_count_md;

#if UNREAL_VERSION_TIME<202340
int wwwstats_msg(Client *sptr, Channel *chptr, MessageTag *mtags, const char *msg, MESSAGE_SENDTYPE sendtype);
#else
int wwwstats_msg(Client *sptr, Channel *chptr, MessageTag **mtags, const char *msg, MESSAGE_SENDTYPE sendtype);
#endif
EVENT(wwwstats_socket_evt);
char *json_escape(char *d, const char *a);
void md_free(ModData *md);
int wwwstats_configtest(ConfigFile *cf, ConfigEntry *ce, int type, int *errs);
int wwwstats_configposttest(int *errs);
int wwwstats_configrun(ConfigFile *cf, ConfigEntry *ce, int type);

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
	if(!ce || !ce->name)
		return 0;

	// If it isn't our bl0ck, idc
	if(strcmp(ce->name, MYCONF))
		return 0;

	// Loop dat shyte fam
	for(cep = ce->items; cep; cep = cep->next) {
		// Do we even have a valid name l0l?
		if(!cep->name) {
			config_error("%s:%i: blank %s item", cep->file->filename, cep->line_number, MYCONF); // Rep0t error
			errors++; // Increment err0r count fam
			continue; // Next iteration imo tbh
		}

		if(!strcmp(cep->name, "socket-path")) {
			if(!cep->value) {
				config_error("%s:%i: %s::%s must be a path", cep->file->filename, cep->line_number, MYCONF, cep->name);
				errors++; // Increment err0r count fam
				continue;
			}
			socket_hpath = 1;
			continue;
		}

		// Anything else is unknown to us =]
		config_warn("%s:%i: unknown item %s::%s", cep->file->filename, cep->line_number, MYCONF, cep->name); // So display just a warning
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
	if(!ce || !ce->name)
		return 0;

	// If it isn't our bl0cc, idc
	if(strcmp(ce->name, MYCONF))
		return 0;

	// Loop dat shyte fam
	for(cep = ce->items; cep; cep = cep->next) {
		// Do we even have a valid name l0l?
		if(!cep->name)
			continue; // Next iteration imo tbh

		if(cep->value && !strcmp(cep->name, "socket-path")) {
			socket_path = strdup(cep->value);
			continue;
		}
	}
	return 1; // We good
}

ModuleHeader MOD_HEADER = {
	"third/wwwstats",   /* Name of module */
	"6.0", /* Version */
	"Provides data for network stats", /* Short description of module */
	"rocket, k4be",
	"unrealircd-6"
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

#if UNREAL_VERSION_TIME<202340
int wwwstats_msg(Client *sptr, Channel *chptr, MessageTag *mtags, const char *msg, MESSAGE_SENDTYPE sendtype)
#else
int wwwstats_msg(Client *sptr, Channel *chptr, MessageTag **mtags, const char *msg, MESSAGE_SENDTYPE sendtype)
#endif
{ // called on channel messages
	counter++;
	CHANNEL_MESSAGE_COUNT(chptr)++;
	return HOOK_CONTINUE;
}

EVENT(wwwstats_socket_evt){
	char topic[6*TOPICLEN+1];
	char name[6*CHANNELLEN+1];
	int sock;
	struct sockaddr_un cli_addr;
	socklen_t slen;
	Client *acptr;
	Channel *channel;
	unsigned int hashnum;
	json_t *output = NULL;
	json_t *servers = NULL;
	json_t *channels = NULL;
	json_t *server_j = NULL;
	json_t *channel_j = NULL;
	char *result;

	if(!socket_hpath) return; // nothing to do

	sock = accept(stats_socket, (struct sockaddr*) &cli_addr, &slen); // wait for a connection
	
	slen = sizeof(cli_addr);
	
	if(sock<0){
		if(errno == EWOULDBLOCK || errno == EAGAIN) return;
		unreal_log(ULOG_ERROR, "wwwstats", "WWWSTATS_ACCEPT_ERROR", NULL, "Socket accept error: $error", log_data_string("error", strerror(errno)));
		return;
	}
	
	output = json_object();
	servers = json_array();
	channels = json_array();

	json_object_set_new(output, "clients", json_integer(irccounts.clients));
	json_object_set_new(output, "channels", json_integer(irccounts.channels));
	json_object_set_new(output, "operators", json_integer(irccounts.operators));
	json_object_set_new(output, "servers", json_integer(irccounts.servers));
	json_object_set_new(output, "messages", json_integer(counter));

	list_for_each_entry(acptr, &global_server_list, client_node){
		if (IsULine(acptr) && HIDE_ULINES)
			continue;
		server_j = json_object();
		json_object_set_new(server_j, "name", json_string_unreal(acptr->name));
		json_object_set_new(server_j, "users", json_integer(acptr->server->users));
		json_array_append_new(servers, server_j);
	}
	json_object_set_new(output, "serv", servers);

	for(hashnum = 0; hashnum < CHAN_HASH_TABLE_SIZE; hashnum++){
		for(channel = hash_get_chan_bucket(hashnum); channel; channel = channel->hnextch){
			if(!PubChannel(channel)) continue;
			channel_j = json_object();
			json_object_set_new(channel_j, "name", json_string_unreal(channel->name));
			json_object_set_new(channel_j, "users", json_integer(channel->users));
			json_object_set_new(channel_j, "messages", json_integer(CHANNEL_MESSAGE_COUNT(channel)));
			if(channel->topic)
				json_object_set_new(channel_j, "topic", json_string_unreal(channel->topic));
			json_array_append_new(channels, channel_j);
		}
	}
	json_object_set_new(output, "chan", channels);
	result = json_dumps(output, JSON_COMPACT);
	
	send(sock, result, strlen(result), 0);
	json_decref(output);
	safe_free(result);
	close(sock);
}

