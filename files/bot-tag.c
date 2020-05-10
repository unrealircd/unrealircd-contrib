/* Copyright (C) All Rights Reserved
** Written by k4be
** Website: https://github.com/pirc-pl/unrealircd-modules/
** License: GPLv3 https://www.gnu.org/licenses/gpl-3.0.html
*/

/*** <<<MODULE MANAGER START>>>
module
{
        documentation "https://github.com/pirc-pl/unrealircd-modules/blob/master/README.md#bot-tag";
        troubleshooting "In case of problems, contact k4be on irc.pirc.pl.";
        min-unrealircd-version "5.*";
        post-install-text {
                "The module is installed. Now all you need to do is add a loadmodule line:";
                "loadmodule \"third/bot-tag\";";
  				"And /REHASH the IRCd.";
        }
}
*** <<<MODULE MANAGER END>>>
*/

#include "unrealircd.h"

#define IsBot(cptr)    (cptr->umodes & UMODE_BOT)
long UMODE_BOT = 0L;

int bot_mtag_is_ok(Client *client, char *name, char *value);
void mtag_add_bot(Client *client, MessageTag *recv_mtags, MessageTag **mtag_list, char *signature);

ModuleHeader MOD_HEADER = {
	"third/bot-tag",
	"5.0",
	"Add inspircd.org/bot tag",
	"k4be@PIRC",
	"unrealircd-5",
};

MOD_INIT(){
	MessageTagHandlerInfo mtag;

	memset(&mtag, 0, sizeof(mtag));
	mtag.name = "inspircd.org/bot";
	mtag.is_ok = bot_mtag_is_ok;
	mtag.flags = MTAG_HANDLER_FLAGS_NO_CAP_NEEDED;
	if(!MessageTagHandlerAdd(modinfo->handle, &mtag)){
		config_error("[%s] Failed to request bot client tag: %s.", MOD_HEADER.name, ModuleGetErrorStr(modinfo->handle));
		return MOD_FAILED;
	}

	HookAddVoid(modinfo->handle, HOOKTYPE_NEW_MESSAGE, 0, mtag_add_bot);
	return MOD_SUCCESS;
}

MOD_LOAD(){
	UMODE_BOT = find_user_mode('B');
	if(!UMODE_BOT){
		config_warn("[%s] Mode +B not found. You need to load 'usermodes/bot' module to use bot tags.", MOD_HEADER.name);
	}
	return MOD_SUCCESS;
}

MOD_UNLOAD(){
	return MOD_SUCCESS;
}

int bot_mtag_is_ok(Client *client, char *name, char *value){
	if (IsServer(client))
		return 1;

	return 0;
}

void mtag_add_bot(Client *client, MessageTag *recv_mtags, MessageTag **mtag_list, char *signature){
	MessageTag *m;

	if (IsUser(client) && IsBot(client))
	{
		MessageTag *m = find_mtag(recv_mtags, "inspircd.org/bot");
		if (m)
		{
			m = duplicate_mtag(m);
		} else {
			m = safe_alloc(sizeof(MessageTag));
			safe_strdup(m->name, "inspircd.org/bot");
		}
		AddListItem(m, *mtag_list);
	}
}

