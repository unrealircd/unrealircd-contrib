/* OJOIN
 * GPLv3 or later
 * Copyright Ⓒ 2022 Valerie Pond
 *
 * Inspircd by InspIRCd's third module of the same name
 */

/*** <<<MODULE MANAGER START>>>
module {
	documentation "https://github.com/ValwareIRC/valware-unrealircd-mods/blob/main/ojoin/README.md";
	troubleshooting "In case of problems, email me at v.a.pond@outlook.com";
	min-unrealircd-version "6.*";
	//max-unrealircd-version "6.*";
	post-install-text {
		"The module is installed, now all you need to do is add a 'loadmodule' line to your config file:";
		"loadmodule \"third/ojoin\";";
		"Then /rehash the IRCd.";
		"For usage information, refer to the module's documentation found at: https://github.com/ValwareIRC/valware-unrealircd-mods/blob/main/ojoin/";
		"Provides the server-admin only command /OJOIN. Requires operclass permission 'ojoin'";
	}
}
*** <<<MODULE MANAGER END>>>
*/

#include "unrealircd.h"

/* Defines */
#define RANK_SOPMODE 5000
#define CMD_OJOIN "OJOIN"
#define MODE_SOPMODE 'Y'
#define PREFIX_SOPMODE '!'

ModuleHeader MOD_HEADER = {
	"third/ojoin",
	"1.1",
	"/OJOIN Command and Channel Mode +Y (Server Operator)",
	"Valware",
	"unrealircd-6",
};

/* Forward declarations */
CMD_FUNC(ojoin);
int cmode_sopmode_is_ok(Client *client, Channel *channel, char mode, const char *para, int type, int what);
int ojoin_kick_check(Client *client, Client *target, Channel *channel, const char *comment, const char *client_member_modes, const char *target_member_modes, const char **reject_reason);

MOD_INIT()
{
	CmodeInfo creq;

	MARK_AS_GLOBAL_MODULE(modinfo);

	/* Channel mode +Y */
	memset(&creq, 0, sizeof(creq));
	creq.paracount = 1;
	creq.is_ok = cmode_sopmode_is_ok;
	creq.letter = MODE_SOPMODE;
	creq.prefix = PREFIX_SOPMODE;
	creq.sjoin_prefix = '^';
	creq.rank = RANK_SOPMODE;
	creq.unset_with_param = 1;
	creq.type = CMODE_MEMBER;
	CmodeAdd(modinfo->handle, creq, NULL);
	CommandAdd(modinfo->handle, CMD_OJOIN, ojoin, MAXPARA, CMD_USER);
	HookAdd(modinfo->handle, HOOKTYPE_CAN_KICK, 0, ojoin_kick_check);

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

int cmode_sopmode_is_ok(Client *client, Channel *channel, char mode, const char *param, int type, int what)
{
	Client *target;
	if (!(target = find_user(param, NULL)))
		return EX_DENY;

	int can_ojoin = ValidatePermissionsForPath("ojoin", target, NULL, channel, NULL);

	if (what == MODE_DEL && client == target && can_ojoin) // allow them to -Y themselves
		return EX_ALLOW;
	else if (what == MODE_DEL && client != target) // if someone else is trying to -Y you
	{
		if (!IsServer(client) && !IsULine(client)) // if they're not a server or ULine
		{
			if (type == EXCHK_ACCESS_ERR)
				sendto_one(client, NULL, ":%s %d %s %s :%s", me.name, ERR_CANNOTDOCOMMAND, target->name, "MODE", "Permission denied!"); // DENIED
			return EX_ALWAYS_DENY;
		}
	}
	else if (!can_ojoin)
	{
		if (type == EXCHK_ACCESS_ERR)
			sendto_one(client, NULL, ":%s %d %s %s :%s", me.name, ERR_CANNOTDOCOMMAND, target->name, "MODE", "Permission denied!");
		return EX_DENY;
	}

	if (what == MODE_ADD && !IsServer(client))
	{
		if (type == EXCHK_ACCESS_ERR)
			sendto_one(client, NULL, ":%s %d %s %s :%s", me.name, ERR_CANNOTDOCOMMAND, target->name, "MODE", "Mode +Y is reserved for the command /OJOIN");
		return EX_ALWAYS_DENY;
	}

	return EX_ALWAYS_DENY;
}

/* Make the user unkickable */
int ojoin_kick_check(Client *client, Client *target, Channel *channel, const char *comment, const char *client_member_modes, const char *target_member_modes, const char **reject_reason)
{
	static char errmsg[256];
	char *p;
	int has_sop = 0;
	if (strstr(target_member_modes, "Y"))
	{
		ircsnprintf(errmsg, sizeof(errmsg), ":%s %d %s %s :%s",
					me.name, ERR_CANNOTDOCOMMAND, client->name,
					"KICK", "Permission denied!");
		*reject_reason = errmsg;
		has_sop = 1;
	}
	if (has_sop)
		return EX_DENY;
	else
		return EX_ALLOW;
}

CMD_FUNC(ojoin)
{
	Channel *chan;
	if (!IsULine(client) && !ValidatePermissionsForPath("ojoin", client, NULL, NULL, NULL))
	{
		sendnumeric(client, ERR_CANNOTDOCOMMAND, CMD_OJOIN, "Permission denied!");
		return;
	}
	if (parc < 1)
	{
		sendnumeric(client, ERR_NEEDMOREPARAMS, CMD_OJOIN);
		return;
	}
	if (!valid_channelname(parv[1]))
	{
		send_invalid_channelname(client, parv[1]);
		return;
	}
	chan = make_channel(parv[1]);
	if (!IsULine(client) && !ValidatePermissionsForPath("ojoin", client, NULL, chan, NULL))
	{
		sendnumeric(client, ERR_NOPRIVILEGES);
		return;
	}
	const char *parv2[3];
	parv2[0] = client->name;
	parv2[1] = parv[1];
	parv2[2] = NULL;

	do_join(client, 2, parv2);

	char *modes;
	const char *mode_args[3];

	modes = safe_alloc(2);
	modes[0] = 'Y';

	mode_args[0] = modes;
	mode_args[1] = client->name;
	mode_args[2] = 0;

	Client *us = find_client(me.name, NULL); // make this ACTUALLY sent by the server for the mode_is_ok check
	do_mode(chan, us, NULL, 3, mode_args, 0, 0);
	sendto_channel(chan, &me, client, "oaqY", 0, SEND_ALL, NULL, ":%s NOTICE @%s :%s entering this channel on official network business.", me.name, chan->name, client->name);

	safe_free(modes);
}
