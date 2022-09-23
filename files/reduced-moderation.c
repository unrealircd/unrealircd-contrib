/*
  Licence: GPLv3
  Copyright Ⓒ 2022 Valerie Pond
  
  Permissions: By using this module, you agree that it is Free, and you are allowed to make copies
  and redistrubite this at your own free will, so long as in doing so, the original author and license remain in-tact. 


 Reduced moderation: Requested by Mahjong

*/
/*** <<<MODULE MANAGER START>>>
module
{
		documentation "https://github.com/ValwareIRC/valware-unrealircd-mods/blob/main/reduced-moderation/README.md";
		troubleshooting "In case of problems, documentation or e-mail me at v.a.pond@outlook.com";
		min-unrealircd-version "6.*";
		max-unrealircd-version "6.*";
		post-install-text {
				"The module is installed. Now all you need to do is add a loadmodule line:";
				"loadmodule \"third/reduced-moderation\";";
				"And /REHASH the IRCd.";
				"The module does not need any other configuration.";
		}
}
*** <<<MODULE MANAGER END>>>
*/

#include "unrealircd.h"


ModuleHeader MOD_HEADER
  = {
	"third/reduced-moderation",
	"1.0",
	"Reduced Moderation mode (+x)",
	"Valware",
	"unrealircd-6",
	};

/* Global variables */
Cmode_t EXTCMODE_REDMOD;

/* Forward declarations */
int redmod_can_send_to_channel(Client *client, Channel *channel, Membership *lp, const char **msg, const char **errmsg, SendType sendtype);
const char *redmod_pre_local_part(Client *client, Channel *channel, const char *text);

/* Macros */
#define IsRedMod(channel) (channel->mode.mode & EXTCMODE_REDMOD)

MOD_INIT()
{
	CmodeInfo req;

	MARK_AS_GLOBAL_MODULE(modinfo);

	memset(&req, 0, sizeof(req));
	req.paracount = 0;
	req.letter = 'x';
	req.is_ok = extcmode_default_requirechop;
	CmodeAdd(modinfo->handle, req, &EXTCMODE_REDMOD);

	HookAdd(modinfo->handle, HOOKTYPE_CAN_SEND_TO_CHANNEL, 123, redmod_can_send_to_channel);
	HookAddConstString(modinfo->handle, HOOKTYPE_PRE_LOCAL_PART, 123, redmod_pre_local_part);

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

/* Overrides for +m also will override +x */
int redmod_can_send_to_channel(Client *client, Channel *channel, Membership *m, const char **text, const char **errmsg, SendType sendtype)
{
	MessageTag *mtags = NULL;
	if (IsRedMod(channel) && (!m || !check_channel_access_membership(m, "vhoaq")) &&
		!op_can_override("channel:override:message:moderated",client,channel,NULL))
	{
		Hook *h;
		for (h = Hooks[HOOKTYPE_CAN_BYPASS_CHANNEL_MESSAGE_RESTRICTION]; h; h = h->next)
		{
			int i = (*(h->func.intfunc))(client, channel, BYPASS_CHANMSG_MODERATED);
			if (i == HOOK_ALLOW)
				return HOOK_CONTINUE;
			if (i != HOOK_CONTINUE)
				break;
		}

		int notice = (sendtype == SEND_TYPE_NOTICE);

		new_message(client, NULL, &mtags);
		sendto_channel(channel, client, client, "oaq", 0, SEND_ALL, mtags, ":%s %s %s :%s", client->name, (notice ? "NOTICE" : "PRIVMSG"), channel->name, *text);
		free_message_tags(mtags);
		*text = NULL;

	}

	return HOOK_CONTINUE;
}

const char *redmod_pre_local_part(Client *client, Channel *channel, const char *text)
{
	if (IsRedMod(channel) && !check_channel_access(client, channel, "v") && !check_channel_access(client, channel, "h"))
		return NULL;
	return text;
}

