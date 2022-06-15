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
#define IsJoiningAsSop(x)			(moddata_client(x, ojoin_md).i)
#define SetJoiningAsSop(x)		do { moddata_client(x, ojoin_md).i = 1; } while(0)
#define ClearJoiningAsSop(x)		do { moddata_client(x, ojoin_md).i = 0; } while(0)

ModuleHeader MOD_HEADER
  = {
	"third/ojoin",
	"1.0",
	"/OJOIN Command and Channel Mode +Y (Server Operator)",
	"Valware",
	"unrealircd-6",
};

/* Forward declarations */
void ojoin_free(ModData *m);
const char *ojoin_serialize(ModData *m);
void ojoin_unserialize(const char *str, ModData *m);
ModDataInfo *ojoin_md;
CMD_FUNC(ojoin);
int cmode_sopmode_is_ok(Client *client, Channel *channel, char mode, const char *para, int type, int what);
int ojoin_kick_check(Client *client, Client *target, Channel *channel, const char *comment, const char *client_member_modes, const char *target_member_modes, const char **reject_reason);

MOD_INIT()
{
	CmodeInfo creq;
	ModDataInfo mreq;

	MARK_AS_GLOBAL_MODULE(modinfo);

	/* some module data for restricting setting +Y to only settable using `/OJOIN` */
	memset(&mreq, 0, sizeof(mreq));
	mreq.name = "ojoin_md";
	mreq.free = ojoin_free;
	mreq.serialize = ojoin_serialize;
	mreq.unserialize = ojoin_unserialize;
	mreq.sync = 1;
	mreq.type = MODDATATYPE_CLIENT;
	ojoin_md = ModDataAdd(modinfo->handle, mreq);
	if (!ojoin_md)
		abort();

	/* Channel mode +Y */
	memset(&creq, 0, sizeof(creq));
	creq.paracount = 1;
	creq.is_ok = cmode_sopmode_is_ok;
	creq.letter = MODE_SOPMODE;
	creq.prefix = PREFIX_SOPMODE;
	creq.sjoin_prefix = PREFIX_SOPMODE;
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

	int can_ojoin = ValidatePermissionsForPath("ojoin",target,NULL,channel,NULL);

	if (what == MODE_DEL && client == target && can_ojoin) // allow them to -Y themselves 
		return EX_ALLOW;
	else if (what == MODE_DEL && client != target) // if someone else is trying to -Y you
	{
		if (!IsServer(client) && !IsULine(client)) // if they're not a server or ULine
		{
			sendto_one(target, NULL, ":%s %d %s %s :%s", me.name, ERR_CANNOTDOCOMMAND, target->name, "MODE", "Permission denied!"); // DENIED
			return EX_ALWAYS_DENY; // DENIED even if you have override AHHAHA
		}
	}
	if (!can_ojoin)
	 {
		sendto_one(target, NULL, ":%s %d %s %s :%s", me.name, ERR_CANNOTDOCOMMAND, target->name, "MODE", "Permission denied!");
		return EX_DENY;
	}
	
	if (what == MODE_ADD && !IsJoiningAsSop(target))
	{
		sendto_one(target, NULL, ":%s %d %s %s :%s", me.name, ERR_CANNOTDOCOMMAND, target->name, "MODE", "Mode +Y is reserved for the command /OJOIN");
		return EX_ALWAYS_DENY;
	}

	if (IsJoiningAsSop(target))
		ClearJoiningAsSop(target);
	return EX_ALLOW;

}

/* Make the user unkickable */
int ojoin_kick_check(Client *client, Client *target, Channel *channel, const char *comment, const char *client_member_modes, const char *target_member_modes, const char **reject_reason)
{
	static char errmsg[256];
	char *p;
	int has_sop = 0;
	if (strstr(target_member_modes,"Y"))
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

const char *ojoin_serialize(ModData *m)
{
	static char buf[32];
	if (m->i == 0)
		return NULL; /* not set */
	snprintf(buf, sizeof(buf), "%d", m->i);
	return buf;
}

void ojoin_free(ModData *m)
{
	m->i = 0;
}

void ojoin_unserialize(const char *str, ModData *m)
{
	m->i = atoi(str);
}

CMD_FUNC(ojoin)
{
	char *modes;
	char *name = '\0';
	char *p = '\0';
	char *m0de;
	char request[BUFSIZE];
	const char *member_modes;
	const char *parv_stuff_lol[3];
	int ntargets = 0;
	int maxtargets = max_targets_for_command("JOIN");
	Membership *membership;
	Channel *chan;
	MessageTag *mtags = NULL;
   
	if (!IsULine(client) && !ValidatePermissionsForPath("ojoin", client, NULL, NULL, NULL))
	{
		sendnumeric(client, ERR_CANNOTDOCOMMAND, CMD_OJOIN, "Permission denied!");
		return;
	}
 
	if (!maxtargets)
	{
		unreal_log(ULOG_INFO, "ojoin", "OJOIN_COMMAND", client, "OJOIN: $client tried to use OJOIN but our `maxtargets` was none. This is a serious problem and means nobody can join channels.");
		return;
	}

	if (parc < 2 || BadPtr(parv[1]))
	{
		sendnumeric(client, ERR_NEEDMOREPARAMS, CMD_OJOIN);
		return;
	}
	
	strlcpy(request, parv[1], sizeof(request));
	

	if (!strlen(request) || !valid_channelname(request))
	{
		sendnotice(client,"Invalid channel name: %s", request);
		return;
	}

	if (++ntargets > maxtargets)
	{
		sendnumeric(client, ERR_TOOMANYTARGETS, request, maxtargets, CMD_OJOIN);
		return;
	}

	if (strlen(request) > CHANNELLEN)
	{
		sendnotice(client, "Channel name too long: %s", request);
		return;
	}

	member_modes = (ChannelExists(request)) ? "" : LEVEL_ON_JOIN;
	chan = make_channel(request);

	if (!chan) // shouldn't really happen because we just created it, but if something went wrong...
	{
		sendnumeric(client, ERR_NOSUCHCHANNEL, request);
		return;
	}

	member_modes = (ChannelExists(request)) ? "" : LEVEL_ON_JOIN;
	chan = make_channel(request);

	/* if they not on that channel we join 'em first */
	if (!(membership = find_membership_link(client->user->channel, chan)))
	{
		new_message(client, NULL, &mtags);
		join_channel(chan, client, mtags, member_modes);
	}

	m0de = safe_alloc(3);
	m0de[0] = MODE_SOPMODE;
	parv_stuff_lol[0] = m0de;
	parv_stuff_lol[1] = client->name;
	parv_stuff_lol[2] = 0;
	sendto_channel(chan, &me, NULL, NULL, 0, SEND_ALL, mtags, ":%s NOTICE %s :%s entering this channel on official network business.", me.name, chan->name, client->name);
	SetJoiningAsSop(client);
	unreal_log(ULOG_INFO, "ojoin", "OJOIN_COMMAND", client, "OJOIN: $client used OJOIN to join $channel on official network business.",
		log_data_string("channel", chan->name));
	do_mode(chan, &me, mtags, 3, parv_stuff_lol, 0, 0);
	m0de[0] = 'o';
	parv_stuff_lol[0] = m0de;
	do_mode(chan, &me, mtags, 3, parv_stuff_lol, 0, 0);
	safe_free(m0de);
	
	free_message_tags(mtags);

}
