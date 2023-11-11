/*
  Licence: GPLv3 or later
  Copyright Ⓒ 2023 Valerie Pond
  +draft/polite

*/
/*** <<<MODULE MANAGER START>>>
module
{
		documentation "https://github.com/ValwareIRC/valware-unrealircd-mods/blob/main/polite-tag/README.md";
		troubleshooting "In case of problems, documentation or e-mail me at v.a.pond@outlook.com";
		min-unrealircd-version "6.*";
		max-unrealircd-version "6.*";
		post-install-text {
				"The module is installed. Now all you need to do is add a loadmodule line:";
				"loadmodule \"third/polite-tag\";";
				"The module does not need any other configuration.";
		}
}
*** <<<MODULE MANAGER END>>>
*/


#include "unrealircd.h"
#define MTAG_POLITE "+draft/polite" // can be changed at a later date

ModuleHeader MOD_HEADER = {
	"third/polite-tag",
	"1.0",
	"+draft/polite tag - Lets a user mark their message as polite (don't highlight)",
	"Valware",
	"unrealircd-6",
};

int polite_mtag_is_ok(Client *client, const char *name, const char *value);
void mtag_add_polite(Client *client, MessageTag *recv_mtags, MessageTag **mtag_list, const char *signature);

MOD_INIT()
{
	MessageTagHandlerInfo mtag;

	MARK_AS_GLOBAL_MODULE(modinfo);

	memset(&mtag, 0, sizeof(mtag));
	mtag.is_ok = polite_mtag_is_ok;
	mtag.flags = MTAG_HANDLER_FLAGS_NO_CAP_NEEDED;
	mtag.name = MTAG_POLITE;
	MessageTagHandlerAdd(modinfo->handle, &mtag);

	HookAddVoid(modinfo->handle, HOOKTYPE_NEW_MESSAGE, 0, mtag_add_polite);

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

int polite_mtag_is_ok(Client *client, const char *name, const char *value)
{
	if (value == NULL) // this tag has no value
        return 1;
	return 0;
}

void mtag_add_polite(Client *client, MessageTag *recv_mtags, MessageTag **mtag_list, const char *signature)
{
	MessageTag *m;

	if (IsUser(client))
	{
		m = find_mtag(recv_mtags, MTAG_POLITE);
		if (m)
		{
			m = duplicate_mtag(m);
			AddListItem(m, *mtag_list);
		}
	}
}
