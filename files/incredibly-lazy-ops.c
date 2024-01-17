/*
  Licence: GPLv3
  Copyright Ⓒ 2024 Valerie Pond

  Provides some easy commands for using extbans for lazy chanops who can't be bothered to learn extbans 
*/
/*** <<<MODULE MANAGER START>>>
module
{
        documentation "https://github.com/ValwareIRC/valware-unrealircd-mods/blob/main/incredibly-lazy-ops/README.md";
		troubleshooting "In case of problems, documentation or e-mail me at v.a.pond@outlook.com";
        min-unrealircd-version "6.*";
        max-unrealircd-version "6.*";
        post-install-text {
                "The module is installed. Now all you need to do is add a loadmodule line:";
                "loadmodule \"third/incredibly-lazy-ops\";";
                "And /REHASH the IRCd.";
                "The module does not need any other configuration.";
        }
}
*** <<<MODULE MANAGER END>>>
*/

#include "unrealircd.h"

CMD_FUNC(cmd_tban);
CMD_FUNC(cmd_qban);

ModuleHeader MOD_HEADER = {
	"third/incredibly-lazy-ops",
	"1.0",
	"Provides some easy commands for using extbans for lazy chanops who can't be bothered to learn extbans",
	"Valware",
	"unrealircd-6",
};


MOD_INIT() {

	CommandAdd(modinfo->handle, "TBAN", cmd_tban, 4, CMD_USER);
	CommandAdd(modinfo->handle, "TIMEBAN", cmd_tban, 4, CMD_USER);
	CommandAdd(modinfo->handle, "QBAN", cmd_qban, 3, CMD_USER);
	CommandAdd(modinfo->handle, "QUIETBAN", cmd_qban, 3, CMD_USER);
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

/**
	TBAN <chan> <time> <nick> <reason>
	Kick and bans a nick from a channel with time and reason.

	Since you are such a lazy channel moderator and don't wanna use extbans,
	you won't wanna specify a mask since you can just use extbans there,
	but this is LAZY COMMAND so you must choose a valid nick of someone in that channel.
	But don't worry! It'll automatically convert it to be a ban against their hostmask, not their nick.
	Except if your channel is set as +R (registered users only) in which case it'll ban them by their account.
	So lazy!
	The nick is just so you can kick them by their nick at the same time, because we are
	JUST. SO. LAZY. why should we have to kick to kick them?
*/
CMD_FUNC(cmd_tban)
{
	Client *target;
	Channel *chan;
	const char *kick_parv[5], *mode_args[3];
	int time = 0;
	char *modes;

	if (BadPtr(parv[1]) || BadPtr(parv[2]) || BadPtr(parv[3]))
	{
		sendnumeric(client, ERR_NEEDMOREPARAMS, "TBAN");
		return;
	}

	chan = find_channel(parv[1]);
	if (!chan)
	{
		sendnumeric(client, ERR_NOSUCHNICK, parv[1]);
		return;
	}
	
	time = atoi(parv[2]);

	/* Fix any out of bounds value */
	if (time < 1)
		time = 1;
	else if (time > 9999)
		time = 9999;

	target = find_user(parv[3], NULL);
	if (!target)
	{
		sendnumeric(client, ERR_NOSUCHNICK, parv[3]);
		return;
	}

	if (!check_channel_access(client, chan, "hoaq"))
	{
		sendnumeric(client, ERR_CHANOPRIVSNEEDED, chan->name);
		return;
	}

	// build the "mask"
	int ban_by_account = (has_channel_mode(chan, 'R') && IsLoggedIn(target)) ? 1 : 0;
	char str[512];
	ircsnprintf(str, sizeof(str), "~time:%s:%s%s", my_itoa(time),
		(ban_by_account) ? "~account:" : "*!*@",
		(ban_by_account) ? target->user->account : target->user->cloakedhost);


	modes = safe_alloc(2);
	modes[0] = 'b';

	mode_args[0] = modes;
	mode_args[1] = str;
	mode_args[2] = 0;

	kick_parv[0] = NULL;
	kick_parv[1] = chan->name;
	kick_parv[2] = target->name;
	kick_parv[4] = NULL;

	char reason[MAXKICKLEN];
	ircsnprintf(reason, sizeof(reason), "Banned from %s for %s minute%s: %s", chan->name, my_itoa(time), (time == 1) ? "" : "s", (BadPtr(parv[4])) ? "No reason" : parv[4]);
	kick_parv[3] = reason;

	if (IsMember(target, chan)) // if they're on the channel, kick them
		do_cmd(client, recv_mtags, "KICK", 3, kick_parv); // kick them first so they don't see how the ban against them bans them lol

	do_mode(chan, client, recv_mtags, 3, mode_args, 0, 0);

	safe_free(modes);
	
}


CMD_FUNC(cmd_qban)
{
	Client *target;
	Channel *chan;
	const char *mode_args[3];
	char *modes;

	if (BadPtr(parv[1]) || BadPtr(parv[2]))
	{
		sendnumeric(client, ERR_NEEDMOREPARAMS, "QBAN");
		return;
	}

	chan = find_channel(parv[1]);
	if (!chan)
	{
		sendnumeric(client, ERR_NOSUCHNICK, parv[1]);
		return;
	}
	
	target = find_user(parv[2], NULL);
	if (!target)
	{
		sendnumeric(client, ERR_NOSUCHNICK, parv[2]);
		return;
	}

	if (!check_channel_access(client, chan, "hoaq"))
	{
		sendnumeric(client, ERR_CHANOPRIVSNEEDED, chan->name);
		return;
	}

	// build the "mask"
	int ban_by_account = (has_channel_mode(chan, 'R') && IsLoggedIn(target)) ? 1 : 0;
	char str[512];
	ircsnprintf(str, sizeof(str), "~quiet:%s%s",
		(ban_by_account) ? "~account:" : "*!*@",
		(ban_by_account) ? target->user->account : target->user->cloakedhost);


	modes = safe_alloc(2);
	modes[0] = 'b';

	mode_args[0] = modes;
	mode_args[1] = str;
	mode_args[2] = 0;

	char reason[MAXKICKLEN];
	ircsnprintf(reason, sizeof(reason), "You were muted from %s (quiet-banned) by %s: %s", chan->name, client->name, (BadPtr(parv[3])) ? "No reason" : parv[3]);
	do_mode(chan, client, recv_mtags, 3, mode_args, 0, 0);
	
	if (IsMember(target, chan)) // if they're on the channel, let them know they were m00ted
		sendto_one(client, NULL, ":%s NOTICE %s :%s", me.name, chan->name, reason);

	safe_free(modes);
	
}
