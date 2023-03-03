/*
  Licence: GPLv3 or later
  Copyright Ⓒ 2022 Valerie Pond

  Provides support for KiwiIRC-related tags
*/
/*** <<<MODULE MANAGER START>>>
module
{
		documentation "https://github.com/ValwareIRC/valware-unrealircd-mods/blob/main/kiwiirc-tags/README.md";
		troubleshooting "In case of problems, documentation or e-mail me at v.a.pond@outlook.com";
		min-unrealircd-version "6.*";
		max-unrealircd-version "6.*";
		post-install-text {
				"The module is installed. Now all you need to do is add a loadmodule line:";
				"loadmodule \"third/kiwiirc-tags\";";
				"The module does not need any other configuration.";
		}
}
*** <<<MODULE MANAGER END>>>
*/

#define MTAG_FILEUPLOAD "+kiwiirc.com/fileuploader"
#define MTAG_CONFERENCE "+kiwiirc.com/conference"
#define MTAG_TICTACTOE_OLD "+data" /* current */
#define MTAG_TICTACTOE "+kiwiirc.com/ttt" /* supported in near future */
#include "unrealircd.h"

ModuleHeader MOD_HEADER =
{
	"third/kiwiirc-tags",
	"1.0",
	"Provides support for KiwiIRC's MessageTags",
	"Valware",
	"unrealircd-6",
};
int kiwiirc_tag(Client *client, const char *name, const char *value);
void mtag_add_kiwiirc_tag(Client *client, MessageTag *recv_mtags, MessageTag **mtag_list, const char *signature);

MOD_INIT()
{
	MessageTagHandlerInfo mtag;

	MARK_AS_GLOBAL_MODULE(modinfo);

	memset(&mtag, 0, sizeof(mtag));
	mtag.is_ok = kiwiirc_tag;
	mtag.flags = MTAG_HANDLER_FLAGS_NO_CAP_NEEDED;
	mtag.name = MTAG_FILEUPLOAD;
	MessageTagHandlerAdd(modinfo->handle, &mtag);

	memset(&mtag, 0, sizeof(mtag));
	mtag.is_ok = kiwiirc_tag;
	mtag.flags = MTAG_HANDLER_FLAGS_NO_CAP_NEEDED;
	mtag.name = MTAG_CONFERENCE;
	MessageTagHandlerAdd(modinfo->handle, &mtag);

	memset(&mtag, 0, sizeof(mtag));
	mtag.is_ok = kiwiirc_tag;
	mtag.flags = MTAG_HANDLER_FLAGS_NO_CAP_NEEDED;
	mtag.name = MTAG_TICTACTOE_OLD;
	MessageTagHandlerAdd(modinfo->handle, &mtag);

	memset(&mtag, 0, sizeof(mtag));
	mtag.is_ok = kiwiirc_tag;
	mtag.flags = MTAG_HANDLER_FLAGS_NO_CAP_NEEDED;
	mtag.name = MTAG_TICTACTOE;
	MessageTagHandlerAdd(modinfo->handle, &mtag);

	HookAddVoid(modinfo->handle, HOOKTYPE_NEW_MESSAGE, 0, mtag_add_kiwiirc_tag);
	

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

int kiwiirc_tag(Client *client, const char *name, const char *value)
{
	if (!strlen(value))
	{
		sendto_one(client, NULL, "FAIL * MESSAGE_TAG_TOO_SHORT %s :That message tag must contain a value.", name);
		return 0;
	}
	return 1;
}

void mtag_add_kiwiirc_tag(Client *client, MessageTag *recv_mtags, MessageTag **mtag_list, const char *signature)
{
	MessageTag *m;

	if (IsUser(client))
	{
		m = find_mtag(recv_mtags, MTAG_FILEUPLOAD);
		if (m)
		{
			m = duplicate_mtag(m);
			AddListItem(m, *mtag_list);
		}
		m = find_mtag(recv_mtags, MTAG_CONFERENCE);
		if (m)
		{
			m = duplicate_mtag(m);
			AddListItem(m, *mtag_list);
		}
		m = find_mtag(recv_mtags, MTAG_TICTACTOE_OLD);
		if (m)
		{
			m = duplicate_mtag(m);
			AddListItem(m, *mtag_list);
		}
		m = find_mtag(recv_mtags, MTAG_TICTACTOE);
		if (m)
		{
			m = duplicate_mtag(m);
			AddListItem(m, *mtag_list);
		}
	}
}
