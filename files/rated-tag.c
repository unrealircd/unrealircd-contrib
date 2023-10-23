/*
  Licence: GPLv3 or later
  Copyright Ⓒ 2023 Valerie Pond
  draft/rated

*/
/*** <<<MODULE MANAGER START>>>
module
{
		documentation "https://github.com/ValwareIRC/valware-unrealircd-mods/blob/main/rated/README.md";
		troubleshooting "In case of problems, documentation or e-mail me at v.a.pond@outlook.com";
		min-unrealircd-version "6.*";
		max-unrealircd-version "6.*";
		post-install-text {
				"The module is installed. Now all you need to do is add a loadmodule line:";
				"loadmodule \"third/rated\";";
				"The module does not need any other configuration.";
		}
}
*** <<<MODULE MANAGER END>>>
*/


#include "unrealircd.h"
#define MTAG_RATED "+draft/rated" // can be changed at a later date

ModuleHeader MOD_HEADER = {
	"third/rated-tag",
	"1.0",
	"+draft/rated tag, allowing clients to implement ratings on their message",
	"Valware",
	"unrealircd-6",
};

int rated_mtag_is_ok(Client *client, const char *name, const char *value);
void mtag_add_rated(Client *client, MessageTag *recv_mtags, MessageTag **mtag_list, const char *signature);

MOD_INIT()
{
	MessageTagHandlerInfo mtag;

	MARK_AS_GLOBAL_MODULE(modinfo);

	memset(&mtag, 0, sizeof(mtag));
	mtag.is_ok = rated_mtag_is_ok;
	mtag.flags = MTAG_HANDLER_FLAGS_NO_CAP_NEEDED;
	mtag.name = MTAG_RATED;
	MessageTagHandlerAdd(modinfo->handle, &mtag);

	HookAddVoid(modinfo->handle, HOOKTYPE_NEW_MESSAGE, 0, mtag_add_rated);

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

int rated_mtag_is_ok(Client *client, const char *name, const char *value)
{
	if (strlen(value) && strlen(value) >= 100) // allow null values but cap long values to 100 chars.
		return 0;
	return 1;
}

void mtag_add_rated(Client *client, MessageTag *recv_mtags, MessageTag **mtag_list, const char *signature)
{
	MessageTag *m;

	if (IsUser(client))
	{
		m = find_mtag(recv_mtags, MTAG_RATED);
		if (m)
		{
			m = duplicate_mtag(m);
			AddListItem(m, *mtag_list);
		}
	}
}
