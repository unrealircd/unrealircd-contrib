/*
  Licence: GPLv3
  Copyright Ⓒ 2022 Valerie Pond

  Ban mtags from being used in a channel
 */

/*** <<<MODULE MANAGER START>>>
module
{
		documentation "https://github.com/ValwareIRC/valware-unrealircd-mods/blob/main/mtag-extban/README.md";
		troubleshooting "In case of problems, documentation or e-mail me at v.a.pond@outlook.com";
		min-unrealircd-version "6.1.2";
		max-unrealircd-version "6.*";
		post-install-text {
				"The module is installed. Now all you need to do is add a loadmodule line:";
				"loadmodule \"third/mtag-extban\";";
				"The module does not need any other configuration.";
		}
}
*** <<<MODULE MANAGER END>>>
*/
#include "unrealircd.h"

#define MAX_MTAG_BANS_PER_CHAN	 15 /* Max number of ~T bans in a channel. */

ModuleHeader MOD_HEADER
= {
	"third/mtag-extban",
	"1.0",
	"ExtBan ~mtag - Prevent usage of certain IRCv3 message-tags per channel",
	"Valware",
	"unrealircd-6",
};
/* Forward declarations */
CMD_OVERRIDE_FUNC(extban_mtag_check);

int mtag_check_ban(Client *client, Channel *channel, char *ban, char *msg, const char **errmsg);
const char *extban_mtag_conv_param(BanContext *b, Extban *extban);
int extban_mtag_is_ok(BanContext *b);
void mtag_search_and_destroy(MessageTag **mtags, Channel *chan);
int mtag_extban_match(MessageTag *m, char *banstr);
int count_mtag_bans(Ban *ban);

/** Called upon module init */
MOD_INIT()
{
	ExtbanInfo req;
	
	memset(&req, 0, sizeof(req));
	req.letter = 'M'; //needed for some reason in u6
	req.name = "mtag";
	req.is_ok = extban_is_ok_nuh_extban;
	req.options = EXTBOPT_NOSTACKCHILD; /* disallow things like ~n:~T, as we only affect mtags */
	req.conv_param = extban_mtag_conv_param;
	req.is_ok = extban_mtag_is_ok;
	if (!ExtbanAdd(modinfo->handle, req))
	{
		config_error("could not register extended ban type");
		return MOD_FAILED;
	}

	// overriding these commands seeeems to be the only way to do this..
	CommandOverrideAdd(modinfo->handle, "PRIVMSG", 0, extban_mtag_check);
	CommandOverrideAdd(modinfo->handle, "TAGMSG", 0, extban_mtag_check);
	
	MARK_AS_GLOBAL_MODULE(modinfo);
	
	return MOD_SUCCESS;
}

/** Called upon module load */
MOD_LOAD()
{
	return MOD_SUCCESS;
}

/** Called upon unload */
MOD_UNLOAD()
{
	return MOD_SUCCESS;
}

int extban_mtag_is_ok(BanContext *b)
{
	int bans = count_mtag_bans(b->channel->banlist);
	int excepts = count_mtag_bans(b->channel->exlist);
	if (b->what != MODE_ADD)
		return 1;
	if ((b->ban_type == EXBTYPE_BAN && bans > MAX_MTAG_BANS_PER_CHAN)
	 || (b->ban_type == EXBTYPE_EXCEPT && excepts > MAX_MTAG_BANS_PER_CHAN))
	{
		sendnumeric(b->client, ERR_BANLISTFULL, b->channel->name, b->banstr);
		sendnotice(b->client, "Too many message-tag ban%s for this channel", b->ban_type == EXBTYPE_BAN ? "s" : " exceptions");
		return 0;
	}
	return 1;
}


CMD_OVERRIDE_FUNC(extban_mtag_check)
{
	MessageTag *m;
	Channel *target;
	Ban *ban;
	// satisfy the requirements of our check (has mtag list, has target, local user)
	if (parc < 1 || BadPtr(parv[1]) || !recv_mtags || !MyUser(client))
	{
		CallCommandOverride(ovr, client, recv_mtags, parc, parv);
		return;
	}
	// make sure our channel exists and has a bans list to check
	if (!(target = find_channel(parv[1])) || !target->banlist)
	{
		CallCommandOverride(ovr, client, recv_mtags, parc, parv);
		return;
	}
	mtag_search_and_destroy(&recv_mtags, target);
	CallCommandOverride(ovr, client, recv_mtags, parc, parv);
}


const char *extban_mtag_conv_param(BanContext *b, Extban *extban)
{
	static char retbuf[151];
	snprintf(retbuf, sizeof(retbuf), "%s%s", *b->banstr == '+' ? "" : "+", b->banstr);
	if (strlen(retbuf) == 1)
		strncat(retbuf, "*", sizeof(retbuf));
		
	return retbuf;
}

/** Check and remove messagetags
*/
void mtag_search_and_destroy(MessageTag **mtags, Channel *chan)
{
	MessageTag *m;
	Ban *b, *e; // chmode +b, +e
	Ban *except = chan->exlist;
	int do_drop_mtag = 0;
	for (m = *mtags; m; m = m->next)
	{
		if (*m->name != '+') // if it's not a client tag, we don't care
			continue;

		for (b = chan->banlist; b; b=b->next) // check the bans list
			if (mtag_extban_match(m, b->banstr))
				do_drop_mtag = 1;

		if (chan->exlist)
			for (e = chan->exlist; e; e = e->next) // check the exceptions list
				if (mtag_extban_match(m, e->banstr))
					do_drop_mtag = 0;

		if (do_drop_mtag) // silently drop it so as not to (potentially) spam the user (case of typing tags)
			DelListItem(m, *mtags);
	}
}

int mtag_extban_match(MessageTag *m, char *banstr)
{
	/* Pretend time does not exist...
		taken from textban.c by syzop */
	if (!strncmp(banstr, "~t:", 3))
	{
		banstr = strchr(banstr+3, ':');
		if (!banstr)
			return 0;
		banstr++;
	}

	else if (!strncmp(banstr, "~time:", 6))
	{
		banstr = strchr(banstr+6, ':');
		if (!banstr)
			return 0;
		banstr++;
	}

	// check if the ban is for message tags
	if ( (!strncmp(banstr, "~M:", 3) && ( match_simple(banstr+3, m->name) || !strcmp(banstr+3, "+*") ) )
			|| ( !strncmp(banstr, "~mtag:", 6) && ( match_simple(banstr+6, m->name) || !strcmp(banstr+6, "+*") ) ))
	{
		return 1;
	}
	return 0;
}


int count_mtag_bans(Ban *ban)
{
	int i = 0;
	Ban *b;
	for (b = ban; b; b=b->next)
		if (strstr(b->banstr,"~mtag:"))
			i++;
	return i;
}
