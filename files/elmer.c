/*
  Licence: GPLv3
  Elmer
  Force a user to speak like Elmer Fudd
  
  Special thanks to Jobe for helping me with casting =]
*/
/*** <<<MODULE MANAGER START>>>
module
{
        documentation "https://github.com/ValwareIRC/valware-unrealircd-mods/blob/main/elmer/README.md";
	troubleshooting "In case of problems, documentation or e-mail me at v.a.pond@outlook.com";
        min-unrealircd-version "6.*";
        max-unrealircd-version "6.*";
        post-install-text {
                "The module is installed. Now all you need to do is add a loadmodule line:";
                "loadmodule \"third/elmer\";";
                "And /REHASH the IRCd.";
                "The module does not need any other configuration.";
        }
}
*** <<<MODULE MANAGER END>>>
*/

#include "unrealircd.h"

ModuleHeader MOD_HEADER = {
	"third/elmer",
	"1.0",
	"Make people talk like Elmer",
	"Valware",
	"unrealircd-6",
};


#define IsElmer(x)			(moddata_client(x, elmer_md).i)
#define SetElmer(x)		do { moddata_client(x, elmer_md).i = 1; } while(0)
#define ClearElmer(x)		do { moddata_client(x, elmer_md).i = 0; } while(0)

#define ADDELM "ELMER"
#define DELELM "DELMER"

CMD_FUNC(ADDELMER);
CMD_FUNC(DELELMER);

void elmer_free(ModData *m);
const char *elmer_serialize(ModData *m);
void elmer_unserialize(const char *str, ModData *m);

static char *convert_to_elmer(const char **line);

int elmer_chanmsg(Client *client, Channel *channel, Membership *lp, const char **msg, const char **errmsg, SendType sendtype);
int elmer_usermsg(Client *client, Client *target, const char **msg, const char **errmsg, SendType sendtype);

ModDataInfo *elmer_md;

MOD_INIT() {
	ModDataInfo mreq;

	MARK_AS_GLOBAL_MODULE(modinfo);
	
	memset(&mreq, 0, sizeof(mreq));
	mreq.name = "elmer";
	mreq.free = elmer_free;
	mreq.serialize = elmer_serialize;
	mreq.unserialize = elmer_unserialize;
	mreq.sync = MODDATA_SYNC_EARLY;
	mreq.type = MODDATATYPE_CLIENT;
	elmer_md = ModDataAdd(modinfo->handle, mreq);
	if (!elmer_md)
		abort();
	
	CommandAdd(modinfo->handle, ADDELM, ADDELMER, 1, CMD_OPER);
	CommandAdd(modinfo->handle, DELELM, DELELMER, 1, CMD_OPER);
	HookAdd(modinfo->handle, HOOKTYPE_CAN_SEND_TO_CHANNEL, 0, elmer_chanmsg);
	HookAdd(modinfo->handle, HOOKTYPE_CAN_SEND_TO_USER, 0, elmer_usermsg);
	
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
const char *elmer_serialize(ModData *m)
{
	static char buf[32];
	if (m->i == 0)
		return NULL; /* not set */
	snprintf(buf, sizeof(buf), "%d", m->i);
	return buf;
}
void elmer_free(ModData *m)
{
    m->i = 0;
}
void elmer_unserialize(const char *str, ModData *m)
{
    m->i = atoi(str);
}
CMD_FUNC(ADDELMER)
{
	Client *target;
	
	if (!IsOper(client))
	{
		sendnumeric(client, ERR_NOPRIVILEGES);
		return;	
	}
	else if (parc < 2) {
		sendnumeric(client, ERR_NEEDMOREPARAMS, ADDELM);
		return;
	}
	else if (!(target = find_user(parv[1], NULL))) {
		sendnumeric(client, ERR_NOSUCHNICK, parv[1]);
		return;
	}
	if (IsOper(target))
	{
		sendnumeric(client, ERR_NOPRIVILEGES);
		return;	
	}
	if (IsElmer(target))
	{
		sendnotice(client,"%s is already talking like Elmer!",target->name);
		return;
	}
	SetElmer(target);
	sendnotice(client,"%s is now talking like Elmer.",target->name);
	return;
}

CMD_FUNC(DELELMER)
{
	Client *target;
	

	if (!IsOper(client))
	{
		sendnumeric(client, ERR_NOPRIVILEGES);
		return;	
	}
	else if (parc < 2) {
		sendnumeric(client, ERR_NEEDMOREPARAMS, DELELM);
		return;
	}
	else if (!(target = find_user(parv[1], NULL))) {
		sendnumeric(client, ERR_NOSUCHNICK, parv[1]);
		return;
	}
	if (IsOper(target))
	{
		sendnumeric(client, ERR_NOPRIVILEGES);
		return;	
	}
	if (!IsElmer(target))
	{
		sendnotice(client,"%s was not talking like Elmer anyway.",target->name);
		return;
	}
	ClearElmer(target);
	sendnotice(client,"%s is no longer talking like Elmer.",target->name);
	return;
}

int elmer_chanmsg(Client *client, Channel *channel, Membership *lp, const char **msg, const char **errmsg, SendType sendtype)
{
	static char retbuf[512];
	if (IsElmer(client))
	{
		strlcpy(retbuf, convert_to_elmer(msg), sizeof(retbuf));
		*msg = retbuf;
	}
	return 0;
}

int elmer_usermsg(Client *client, Client *target, const char **msg, const char **errmsg, SendType sendtype)
{
	static char retbuf[512];
	if (IsElmer(client))
	{
		strlcpy(retbuf, convert_to_elmer(msg), sizeof(retbuf));
		*msg = retbuf;
	}
	return 0;
}


static char *convert_to_elmer(const char **line)
{
	char *p;
	for (p = (char *)*line; *p; p++)
		switch(*p)
		{
			case 'l':
			case 'r':
				*p = 'w';
				break;
			case 'L':
			case 'R':
				*p = 'W';
		}

	return (char *)*line;
}
