/*
  Licence: GPLv3 or later
  Copyright Ⓒ 2022 Valerie Pond
  Channel Context

  Sends a message tag with the channel for which the message itself is associated with
  i.e. if someone types in a channel "!help", the bot will reply in private to the user
  but with the channel sent as a mtag, so that the user may know which channel the response
  is associated with
*/
/*** <<<MODULE MANAGER START>>>
module
{
		documentation "https://github.com/ValwareIRC/valware-unrealircd-mods/blob/main/channel-context/README.md";
		troubleshooting "In case of problems, documentation or e-mail me at v.a.pond@outlook.com";
		min-unrealircd-version "6.*";
		max-unrealircd-version "6.*";
		post-install-text {
				"The module is installed. Now all you need to do is add a loadmodule line:";
				"loadmodule \"third/channel-context\";";
				"And /REHASH the IRCd.";
				"The module does not need any other configuration.";
		}
}
*** <<<MODULE MANAGER END>>>
*/


#include "unrealircd.h"

ModuleHeader MOD_HEADER
  = {
	"third/channel-context",
	"0.1",
	"Channel Context (IRCv3)",
	"Valware",
	"unrealircd-6",
	};

int chancontext_mtag_is_ok(Client *client, const char *name, const char *value);
void mtag_add_chancontext(Client *client, MessageTag *recv_mtags, MessageTag **mtag_list, const char *signature);

MOD_INIT()
{
	MessageTagHandlerInfo mtag;

	MARK_AS_GLOBAL_MODULE(modinfo);

	memset(&mtag, 0, sizeof(mtag));
	mtag.is_ok = chancontext_mtag_is_ok;
	mtag.flags = MTAG_HANDLER_FLAGS_NO_CAP_NEEDED;
	mtag.name = "+draft/channel-context";
	MessageTagHandlerAdd(modinfo->handle, &mtag);

	HookAddVoid(modinfo->handle, HOOKTYPE_NEW_MESSAGE, 0, mtag_add_chancontext);

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

int chancontext_mtag_is_ok(Client *client, const char *name, const char *value)
{
	if (BadPtr(value))
		return 0;

	/* if the channel doesn't exist, gtfo */
	if (!find_channel(value))
		return 0;

	return 1;
}

void mtag_add_chancontext(Client *client, MessageTag *recv_mtags, MessageTag **mtag_list, const char *signature)
{
	MessageTag *m;

	if (IsUser(client))
	{
		m = find_mtag(recv_mtags, "+draft/channel-context");
		if (m)
		{
			m = duplicate_mtag(m);
			AddListItem(m, *mtag_list);
		}
	}
}
