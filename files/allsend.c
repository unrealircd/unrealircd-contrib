/*
  Licence: GPLv3 or later
  Copyright Ⓒ 2024 Valerie Pond

  Requested by OmerAti
*/
/*** <<<MODULE MANAGER START>>>
module
{
		documentation "https://github.com/ValwareIRC/valware-unrealircd-mods/blob/main/allsend/README.md";
		troubleshooting "In case of problems, documentation or e-mail me at v.a.pond@outlook.com";
		min-unrealircd-version "6.*";
		max-unrealircd-version "6.*";
		post-install-text {
				"The module is installed. Now all you need to do is add a loadmodule line:";
				"loadmodule \"third/allsend\";";
				"And /REHASH the IRCd.";
				"Please see the README for operclass requirements";
		}
}
*** <<<MODULE MANAGER END>>>
*/

#include "unrealircd.h"



ModuleHeader MOD_HEADER = {
	"third/allsend",
	"1.00",
	"Adds the /allsend command which allows privileged server operators to send targetted messages",
	"Valware",
	"unrealircd-6",
};


#define AMSG_ALL 1
#define AMSG_USERS 2
#define AMSG_OPERS 3
#define AMSG_NOTICE SEND_TYPE_NOTICE
#define AMSG_PRIVMSG SEND_TYPE_PRIVMSG
#define AMSG_LOCAL 1
#define AMSG_GLOBAL 2

#define MSG_ALLSEND "ALLSEND"
CMD_FUNC(cmd_allsend);


static void allsend_halp(Client *client, char **p);

static char *allsend_help[] =
{
	"***** ALLSEND *****",
	"-",
	"Send a message to a particular group of people.",
	"-",
	"Syntax:",
	"    /allsend <users|opers|all> <notice|private> <local|global> <message>",
	"-",
	"Example:",
	"-",
	"Send notification about a pending server restart or services outage or something =]:",
	"    /allsend all notice global There will be a server restart in 30 minutes.",
	"-",
	NULL
};

MOD_INIT()
{

	CommandAdd(modinfo->handle, MSG_ALLSEND, cmd_allsend, 4, CMD_OPER | CMD_SERVER);
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

static void allsend_halp(Client *client, char **p) {
	if(IsServer(client))
		return;
	for(; *p != NULL; p++)
		sendto_one(client, NULL, ":%s %03d %s :%s", me.name, RPL_TEXT, client->name, *p);
}

CMD_FUNC(cmd_allsend)
{
	Client *target;
	int destination = 0, sendtype = 0, global = 0;

	if (!ValidatePermissionsForPath("allsend", client, NULL, NULL, NULL))
	{
		sendnumeric(client, ERR_NOPRIVILEGES);
		return;	
	}
	else if (BadPtr(parv[1]) || BadPtr(parv[2]) || BadPtr(parv[3]) || BadPtr(parv[4]))
	{
		allsend_halp(client,allsend_help);
		return;
	}

	/* figuring out the options*/
	if (!strcasecmp(parv[1],"users"))
		destination = AMSG_USERS;
	else if (!strcasecmp(parv[1],"all"))
		destination = AMSG_ALL;
	else if (!strcasecmp(parv[1],"opers"))
		destination = AMSG_OPERS;
	if (!strcasecmp(parv[2],"notice"))
		sendtype = AMSG_NOTICE;
	else if (!strcasecmp(parv[2],"private"))
		sendtype = AMSG_PRIVMSG;
	if (!strcasecmp(parv[3],"local"))
		global = AMSG_LOCAL;
	else if (!strcasecmp(parv[3],"global"))
		global = AMSG_GLOBAL;


	if (!destination || !global) // (n)one of these were mentioned correctly
	{
		allsend_halp(client,allsend_help);
		return;
	}

	if (global == AMSG_GLOBAL)
	{
		list_for_each_entry(target, &client_list, client_node)
		{
			if (destination == AMSG_OPERS && !IsOper(target))
				continue;
			else if (destination == AMSG_USERS && IsOper(target))
				continue;
			else if (has_user_mode(target, 'B') || IsULine(target)) // skip bots and ulines
				continue;

			MessageTag *mtags = NULL;
			new_message(client, recv_mtags, &mtags);
			sendto_prefix_one(target, client, mtags, ":%s %s %s :%s", 
				client->name,
				(sendtype == AMSG_NOTICE) ? "NOTICE" : "PRIVMSG",
				target->name,
				parv[4]);
		}
		return;
	}
	if (global == AMSG_LOCAL)
	{
		list_for_each_entry(target, &lclient_list, lclient_node)
		{
			if (destination == AMSG_OPERS && !IsOper(target))
				continue;
			else if (destination == AMSG_USERS && IsOper(target))
				continue;
			else if (has_user_mode(target, 'B') || IsULine(target)) // skip bots and ulines
				continue;
				
			MessageTag *mtags = NULL;
			new_message(client, recv_mtags, &mtags);
			sendto_prefix_one(target, client, mtags, ":%s %s %s :%s", 
				client->name,
				(sendtype == SEND_TYPE_NOTICE) ? "NOTICE" : "PRIVMSG",
				target->name,
				parv[4]);
		}
		return;
	}
}
