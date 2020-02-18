/*
 *   Created by k4be, based on src/modules/setname.c
 *   IRC - Internet Relay Chat
 *   (c) 1999-2001 Dominick Meglio (codemastr) <codemastr@unrealircd.com>
 *
 *   See file AUTHORS in IRC package for additional names of
 *   the programmers. 
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 1, or (at your option)
 *   any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

/*** <<<MODULE MANAGER START>>>
module
{
        documentation "https://github.com/pirc-pl/unrealircd-modules/blob/master/README.md#setname";
        troubleshooting "In case of problems, contact k4be on irc.pirc.pl.";
        min-unrealircd-version "5.*";
        post-install-text {
                "The module is installed. Now all you need to do is add a loadmodule line:";
                "loadmodule \"third/setname\";";
  				"And /REHASH the IRCd.";
				"The module does not need any configuration.";
        }
}
*** <<<MODULE MANAGER END>>>
*/

#include "unrealircd.h"

CMD_OVERRIDE_FUNC(cmd_setname);
char *setname_isupport_param(void);

long CAP_SETNAME = 0L;

#define MSG_SETNAME 	"SETNAME"	/* setname */
#define STR_HELPER(x) #x
#define STR(x) STR_HELPER(x)

ModuleHeader MOD_HEADER
  = {
	"third/setname",	/* Name of module */
	"5.0", /* Version */
	"IRCv3-compatible command /setname (CAP setname)", /* Short description of module */
	"k4be@PIRC",
	"unrealircd-5",
    };

MOD_INIT(){
	ClientCapabilityInfo cap;
	ClientCapability *c;

	memset(&cap, 0, sizeof(cap));
	cap.name = "setname";
	c = ClientCapabilityAdd(modinfo->handle, &cap, &CAP_SETNAME);

	return MOD_SUCCESS;
}

MOD_LOAD(){
	if(!CommandOverrideAddEx(modinfo->handle, MSG_SETNAME, 0, cmd_setname)){
		config_error("[%s] Crritical: Failed to request command override for SETNAME: %s", MOD_HEADER.name, ModuleGetErrorStr(modinfo->handle));
	}
	ISupportAdd(modinfo->handle, "NAMELEN", setname_isupport_param());
	return MOD_SUCCESS;
}

char *setname_isupport_param(void){
	return STR(REALLEN);
}

MOD_UNLOAD(){
	return MOD_SUCCESS;
}

CMD_OVERRIDE_FUNC(cmd_setname){
	int xx;
	char tmpinfo[REALLEN + 1];
	char spamfilter_user[NICKLEN + USERLEN + HOSTLEN + REALLEN + 64];
	ConfigItem_ban *bconf;
	MessageTag *mtags = NULL;

	if(!MyUser(client) || !HasCapabilityFast(client, CAP_SETNAME)){
		CallCommandOverride(ovr, client, recv_mtags, parc, parv); // the original command
		new_message(client, recv_mtags, &mtags);
		sendto_local_common_channels(client, client, CAP_SETNAME, mtags, ":%s!%s@%s SETNAME :%s", client->name, client->user->username, GetHost(client), client->info);
		free_message_tags(mtags);
		return;
	}

	if ((parc < 2) || BadPtr(parv[1])){
		sendnumeric(client, ERR_NEEDMOREPARAMS, "SETNAME");
		return;
	}

	if (strlen(parv[1]) > REALLEN){
		if (MyConnect(client)){
			new_message(client, recv_mtags, &mtags);
			sendto_one(client, mtags, ":%s FAIL SETNAME INVALID_REALNAME :\"Real names\" may maximum be %i characters of length", me.name, REALLEN);
			free_message_tags(mtags);
		}
		return;
	}

	if (MyUser(client)){
		/* set temp info for spamfilter check*/
		strcpy(tmpinfo, client->info);
		/* set the new name before we check, but don't send to servers unless it is ok */
		strcpy(client->info, parv[1]);
		spamfilter_build_user_string(spamfilter_user, client->name, client);
		if (match_spamfilter(client, spamfilter_user, SPAMF_USER, NULL, 0, NULL)){
			/* Was rejected by spamfilter, restore the realname */
			strcpy(client->info, tmpinfo);
			return;
		}

		/* Check for realname bans here too */
		if (!ValidatePermissionsForPath("immune:server-ban:ban-realname",client,NULL,NULL,NULL) &&
		    ((bconf = find_ban(NULL, client->info, CONF_BAN_REALNAME)))){
			banned_client(client, "realname", bconf->reason?bconf->reason:"", 0, 0);
			return;
		}
	} else {
		/* remote user */
		strcpy(client->info, parv[1]);
	}

	sendto_server(client, 0, 0, NULL, ":%s SETNAME :%s", client->id, parv[1]);

	if(MyConnect(client)){
		new_message(client, recv_mtags, &mtags);
		sendto_one(client, mtags, ":%s!%s@%s SETNAME :%s", client->name, client->user->username, GetHost(client), parv[1]);
		free_message_tags(mtags);
	}
	sendto_local_common_channels(client, client, CAP_SETNAME, recv_mtags, ":%s!%s@%s SETNAME :%s", client->name, client->user->username, GetHost(client), parv[1]);
}

