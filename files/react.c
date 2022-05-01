/*
  Licence: GPLv3 or later
  Copyright Ⓒ 2022 Valerie Pond
  draft/react

  React to a message
*/
/*** <<<MODULE MANAGER START>>>
module
{
		documentation "https://github.com/ValwareIRC/valware-unrealircd-mods/blob/main/react/README.md";
		troubleshooting "In case of problems, documentation or e-mail me at v.a.pond@outlook.com";
		min-unrealircd-version "6.*";
		max-unrealircd-version "6.*";
		post-install-text {
				"The module is installed. Now all you need to do is add a loadmodule line:";
				"loadmodule \"third/react\";";
				"You need to restart your server for this to show up in CLIENTTAGDENY";
				"The module does not need any other configuration."
		}
}
*** <<<MODULE MANAGER END>>>
*/


#include "unrealircd.h"

ModuleHeader MOD_HEADER
  = {
	"third/react",
	"0.1",
	"+draft/react (IRCv3)",
	"Valware",
	"unrealircd-6",
	};

int i3react_mtag_is_ok(Client *client, const char *name, const char *value);
void mtag_add_i3react(Client *client, MessageTag *recv_mtags, MessageTag **mtag_list, const char *signature);

MOD_INIT()
{
	MessageTagHandlerInfo mtag;

	MARK_AS_GLOBAL_MODULE(modinfo);

	memset(&mtag, 0, sizeof(mtag));
	mtag.is_ok = i3react_mtag_is_ok;
	mtag.flags = MTAG_HANDLER_FLAGS_NO_CAP_NEEDED;
	mtag.name = "+draft/react";
	MessageTagHandlerAdd(modinfo->handle, &mtag);

	HookAddVoid(modinfo->handle, HOOKTYPE_NEW_MESSAGE, 0, mtag_add_i3react);

	return MOD_SUCCESS;
}

MOD_LOAD()
{
	return MOD_SUCCESS;
}

MOD_UNLOAD()
{
	return MOD_SUCCESS;
}

int i3react_mtag_is_ok(Client *client, const char *name, const char *value)
{
	if (BadPtr(value) || !strlen(value) || strlen(value) < 10)
		return 0;

	return 1;
}

void mtag_add_i3react(Client *client, MessageTag *recv_mtags, MessageTag **mtag_list, const char *signature)
{
	MessageTag *m;

	if (IsUser(client))
	{
		m = find_mtag(recv_mtags, "+draft/react");
		if (m)
		{
			m = duplicate_mtag(m);
			AddListItem(m, *mtag_list);
		}
	}
}
