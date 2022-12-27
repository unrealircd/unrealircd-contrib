/*
  Licence: GPLv3 or later
  Copyright Ⓒ 2022 Valerie Pond
  draft/display-name

*/
/*** <<<MODULE MANAGER START>>>
module
{
		documentation "https://github.com/ValwareIRC/valware-unrealircd-mods/blob/main/display-name/README.md";
		troubleshooting "In case of problems, documentation or e-mail me at v.a.pond@outlook.com";
		min-unrealircd-version "6.*";
		max-unrealircd-version "6.*";
		post-install-text {
				"The module is installed. Now all you need to do is add a loadmodule line:";
				"loadmodule \"third/display-name\";";
				"You need to restart your server for this to show up in CLIENTTAGDENY";
				"The module does not need any other configuration.";
		}
}
*** <<<MODULE MANAGER END>>>
*/

#define MTAG_DISPLAYNAME "+draft/display-name"
#include "unrealircd.h"

ModuleHeader MOD_HEADER =
{
	"third/display-name",
	"1.0",
	"+draft/display-name (IRCv3)",
	"Valware",
	"unrealircd-6",
};
int i3display_name_mtag_is_ok(Client *client, const char *name, const char *value);
void mtag_add_i3_display_name(Client *client, MessageTag *recv_mtags, MessageTag **mtag_list, const char *signature);
int IsValidDisplayName(Client *client, const char *value);

MOD_INIT()
{
	MessageTagHandlerInfo mtag;

	MARK_AS_GLOBAL_MODULE(modinfo);

	memset(&mtag, 0, sizeof(mtag));
	mtag.is_ok = i3display_name_mtag_is_ok;
	mtag.flags = MTAG_HANDLER_FLAGS_NO_CAP_NEEDED;
	mtag.name = MTAG_DISPLAYNAME;
	MessageTagHandlerAdd(modinfo->handle, &mtag);

	HookAddVoid(modinfo->handle, HOOKTYPE_NEW_MESSAGE, 0, mtag_add_i3_display_name);
	

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

int i3display_name_mtag_is_ok(Client *client, const char *name, const char *value)
{
	/* we COULD return IsValidDisplayName() direcly but I don't like that. */
	int IsValid = IsValidDisplayName(client,value);
	return IsValid;
}

void mtag_add_i3_display_name(Client *client, MessageTag *recv_mtags, MessageTag **mtag_list, const char *signature)
{
	MessageTag *m;

	if (IsUser(client))
	{
		m = find_mtag(recv_mtags, MTAG_DISPLAYNAME);
		if (m)
		{
			m = duplicate_mtag(m);
			AddListItem(m, *mtag_list);
		}
	}
}

int IsValidDisplayName(Client *client, const char *value)
{
	if (BadPtr(value))
	{
		sendto_one(client, NULL, "FAIL * DISPLAY_NAME_ERRONEOUS :Your display-name cannot be empty.");
		return 0;
	}
	const char *illegalchars = "!+%@&~#$:'\"?*,.";
	const char *p;
	if (strstr(value,"\n") || strstr(value,"\r"))
	{
		sendto_one(client, NULL, "FAIL * DISPLAY_NAME_ERRONEOUS :The display-name you used contained an illegal character");
		return 0;
	}
	for (p = value; *p; p++)
	{
		if (strchr(illegalchars, *p))
		{
			sendto_one(client, NULL, "FAIL * DISPLAY_NAME_ERRONEOUS :The display-name you used contained an illegal character (%s).",illegalchars);
			return 0;
		}
	}
	if (strlen(value) > NICKLEN)
	{
		sendto_one(client, NULL, "FAIL * DISPLAY_NAME_TOO_LONG :The display-name you used exceeded the maximum length and was not included. (Maximum length: %d)", NICKLEN);
		return 0;
	}
	return 1;
}
