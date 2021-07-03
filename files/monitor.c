/* Copyright (C) All Rights Reserved
** Written by k4be
** Website: https://github.com/pirc-pl/unrealircd-modules/
** License: GPLv3 https://www.gnu.org/licenses/gpl-3.0.html
*/
 
 /*** <<<MODULE MANAGER START>>>
module
{
        documentation "https://github.com/pirc-pl/unrealircd-modules/blob/master/README.md#monitor";
        troubleshooting "In case of problems, contact k4be on irc.pirc.pl.";
        min-unrealircd-version "5.*";
        post-install-text {
                "The module is installed. Now all you need to do is add a loadmodule line:";
                "loadmodule \"third/monitor\";";
  				"And /REHASH the IRCd.";
				"The module does not need any configuration.";
        }
}
*** <<<MODULE MANAGER END>>>
*/

#include "unrealircd.h"

CMD_FUNC(cmd_monitor);
char *monitor_isupport_param(void);
void monitorMD_free(ModData *md);
#if UNREAL_VERSION_TIME<202115
int monitor_nickchange(Client *client, char *newnick);
#else // unrealircd-5.2.0
int monitor_nickchange(Client *client, MessageTag *mtags, char *newnick);
#endif
int monitor_quit(Client *client, MessageTag *mtags, char *comment);
int monitor_connect(Client *client);
void monitor_showall(Client *client, MessageTag *recv_mtags);

void monitor_clear(Client *client);
void monitor_list(Client *client, MessageTag *recv_mtags);
void monitor_remove(Client *client, char *nick);
int monitor_add(Client *client, MessageTag *recv_mtags, char *nick);
void monitor_offline(char *nick);
void monitor_online(Client *client, char *nick);
int is_monitoring(Client *client, char *nick);
void send_status(Client *client, MessageTag *recv_mtags, char *nick);

#define MSG_MONITOR	"MONITOR"	
#define STR_HELPER(x) #x
#define STR(x) STR_HELPER(x)

#define RPL_MONONLINE	730
#define RPL_MONOFFLINE	731
#define RPL_MONLIST	732
#define RPL_ENDOFMONLIST	733
#define ERR_MONLISTFULL	734

ModuleHeader MOD_HEADER = {
	"third/monitor",
	"5.2",
	"Command /monitor (IRCv3)", 
	"k4be@PIRC",
	"unrealircd-5",
};

ModDataInfo *monitorMD;

MOD_INIT(){
	ModDataInfo mreq;
	memset(&mreq, 0 , sizeof(mreq));
	mreq.type = MODDATATYPE_CLIENT;
	mreq.name = "monitor",
	mreq.free = monitorMD_free;
	monitorMD = ModDataAdd(modinfo->handle, mreq);
	if(!monitorMD){
		config_error("[%s] Failed to request monitor moddata: %s", MOD_HEADER.name, ModuleGetErrorStr(modinfo->handle));
		return MOD_FAILED;
	}

	CommandAdd(modinfo->handle, MSG_MONITOR, cmd_monitor, 2, CMD_USER);

	HookAdd(modinfo->handle, HOOKTYPE_LOCAL_NICKCHANGE, 0, monitor_nickchange);
	HookAdd(modinfo->handle, HOOKTYPE_REMOTE_NICKCHANGE, 0, monitor_nickchange);
	HookAdd(modinfo->handle, HOOKTYPE_REMOTE_QUIT, 0, monitor_quit);
	HookAdd(modinfo->handle, HOOKTYPE_LOCAL_QUIT, 0, monitor_quit);
	HookAdd(modinfo->handle, HOOKTYPE_LOCAL_CONNECT, 0, monitor_connect);
	HookAdd(modinfo->handle, HOOKTYPE_REMOTE_CONNECT, 0, monitor_connect);

	return MOD_SUCCESS;
}

MOD_LOAD(){
	ISupportAdd(modinfo->handle, "MONITOR", monitor_isupport_param());
	return MOD_SUCCESS;
}

MOD_UNLOAD(){
	return MOD_SUCCESS;
}

#if UNREAL_VERSION_TIME<202115
int monitor_nickchange(Client *client, char *newnick){
#else // unrealircd-5.2.0
int monitor_nickchange(Client *client, MessageTag *mtags, char *newnick){
#endif
	if(!smycmp(client->name, newnick)) // new nick is same as old one, maybe the case changed
		return 0;
	monitor_offline(client->name);
	monitor_online(client, newnick);
	return 0;
}

int monitor_quit(Client *client, MessageTag *mtags, char *comment){
	monitor_offline(client->name);
	return 0;
}

int monitor_connect(Client *client){
	monitor_online(client, client->name);
	return 0;
}

void monitor_offline(char *nick){
	Client *acptr;

	if(!nick) return;
	list_for_each_entry(acptr, &lclient_list, lclient_node){ // notifications for local subscribers
		if(!IsUser(acptr))
			continue;
		if(is_monitoring(acptr, nick))
			sendto_one(acptr, NULL, ":%s %.3d %s :%s", me.name, RPL_MONOFFLINE, acptr->name, nick);
	}
}

void monitor_online(Client *client, char *nick){
	Client *acptr;

	if(!IsUser(client))
		return;
	list_for_each_entry(acptr, &lclient_list, lclient_node){ // notifications for local subscribers
		if(!IsUser(acptr))
			continue;
		if(is_monitoring(acptr, nick))
			sendto_one(acptr, NULL, ":%s %.3d %s :%s!%s@%s", me.name, RPL_MONONLINE, acptr->name, nick, client->user->username, GetHost(client));
	}
}

int is_monitoring(Client *client, char *nick){
	char **monitor;
	int i;
	monitor = moddata_client(client, monitorMD).ptr;
	if(monitor)	for(i=0; i<MAXWATCH; i++){
		if(monitor[i] && !smycmp(monitor[i], nick))
			return 1;
	}
	return 0;
}

void monitorMD_free(ModData *md){
	int i;
	char **monitor = md->ptr;
	if(!monitor) return;
	for(i=0; i<MAXWATCH; i++){
		safe_free(monitor[i]);
	}
	safe_free(monitor);
}

char *monitor_isupport_param(void){
	return STR(MAXWATCH);
}

void monitor_clear(Client *client){
	char **monitor;
	int i;
	monitor = moddata_client(client, monitorMD).ptr;
	if(!monitor)
		return; // none is set
	for(i=0; i<MAXWATCH; i++){
		safe_free(monitor[i]);
	}
}

void send_status(Client *client, MessageTag *recv_mtags, char *nick){
	MessageTag *mtags = NULL;
	Client *user;
	user = find_person(nick, NULL);
	new_message(client, recv_mtags, &mtags);
	if(!user){
		sendto_one(client, mtags, ":%s %.3d %s :%s", me.name, RPL_MONOFFLINE, client->name, nick);
	} else {
		sendto_one(client, mtags, ":%s %.3d %s :%s!%s@%s", me.name, RPL_MONONLINE, client->name, nick, client->user->username, GetHost(client));
	}
	free_message_tags(mtags);
}

void monitor_list(Client *client, MessageTag *recv_mtags){
	char **monitor;
	int i;
	MessageTag *mtags;
	monitor = moddata_client(client, monitorMD).ptr;
	if(monitor)	for(i=0; i<MAXWATCH; i++){
		if(!monitor[i])
			continue;
		mtags = NULL;
		new_message(client, recv_mtags, &mtags);
		sendto_one(client, mtags, ":%s %.3d %s :%s", me.name, RPL_MONLIST, client->name, monitor[i]);
		free_message_tags(mtags);
	}
	mtags = NULL;
	new_message(client, recv_mtags, &mtags);
	sendto_one(client, mtags, ":%s %.3d %s :End of MONITOR list", me.name, RPL_ENDOFMONLIST, client->name);
	free_message_tags(mtags);
}

void monitor_showall(Client *client, MessageTag *recv_mtags){
	char **monitor;
	int i;
	monitor = moddata_client(client, monitorMD).ptr;
	if(monitor)	for(i=0; i<MAXWATCH; i++){
		if(!monitor[i])
			continue;
		send_status(client, recv_mtags, monitor[i]);
	}
}

void monitor_remove(Client *client, char *nick){
	char **monitor;
	int i;
	monitor = moddata_client(client, monitorMD).ptr;
	if(monitor)	for(i=0; i<MAXWATCH; i++){
		if(monitor[i] && !strcasecmp(monitor[i], nick))
			safe_free(monitor[i]);
	}
}

int monitor_add(Client *client, MessageTag *recv_mtags, char *nick){
	char **monitor;
	int i;
	int added = 0;
	if(!do_nick_name(nick)) // invalid nick
		return 1; // silently ignoring
	monitor = moddata_client(client, monitorMD).ptr;
	if(!monitor){
		monitor = safe_alloc(sizeof(char *) * MAXWATCH);
		moddata_client(client, monitorMD).ptr = monitor;
	}
	for(i=0; i<MAXWATCH; i++){
		if(monitor[i]){
			if(!strcasecmp(monitor[i], nick))
				added = 1; // was already added
		}
	}
	if(!added) for(i=0; i<MAXWATCH; i++){
		if(monitor[i])
			continue;
		added = 1;
		monitor[i] = strdup(nick);
		break;
	}
	if(!added)
		return 0; // failed
	send_status(client, recv_mtags, nick);
	return 1;
}

CMD_FUNC(cmd_monitor){
	char cmd;
	char *s, *p = NULL;
	int toomany = 0;
	MessageTag *mtags = NULL;

	if(parc < 2 || BadPtr(parv[1])){
		cmd = 'l';
	} else {
		cmd = tolower(*parv[1]);
	}
	
	int i;
	
	switch(cmd){
		case 'c':
			monitor_clear(client);
			break;
		case 'l':
			monitor_list(client, recv_mtags);
			break;
		case 's':
			monitor_showall(client, recv_mtags);
			break;
		case '-':
		case '+':
			if(parc < 3 || BadPtr(parv[2]))
				return;
			new_message(client, recv_mtags, &mtags);
			for(s = strtoken(&p, parv[2], ","); s; s = strtoken(&p, NULL, ",")){
				if(cmd == '-'){
					monitor_remove(client, s);
				} else {
					if(!toomany){
						if(!monitor_add(client, recv_mtags, s))
							toomany = 1;
					}
					if(toomany)
						sendto_one(client, mtags, ":%s %.3d %s %d %s :Monitor list is full.", me.name, ERR_MONLISTFULL, client->name, MAXWATCH, s);
				}
			}
			free_message_tags(mtags);
			break;
	}
}

