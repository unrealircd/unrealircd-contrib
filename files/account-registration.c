/* 
	LICENSE: GPLv3-or-later
  	Copyright Ⓒ 2022 Valerie Pond

	IRCv3 `account-registration` capability

	Spec author information:
	Copyright © 2020 Ed Kellett <e@kellett.im>
	Copyright © 2021 Val Lorentz <progval+ircv3@progval.net>

	Documentation: https://ircv3.net/specs/extensions/account-registration
 
*/

/*** <<<MODULE MANAGER START>>>
module
{
		documentation "https://github.com/ValwareIRC/valware-unrealircd-mods/blob/main/account-registration/README.md";
		troubleshooting "In case of problems, documentation or e-mail me at v.a.pond@outlook.com";
		min-unrealircd-version "6.*";
		max-unrealircd-version "6.*";
		post-install-text {
				"The module is installed. Now all you need to do is add a loadmodule line:";
				"loadmodule \"third/account-registration\";";
				"And /REHASH the IRCd.";
				"The module does not need any other configuration.";
		}
}
*** <<<MODULE MANAGER END>>>
*/
#include "unrealircd.h"

ModuleHeader MOD_HEADER
  = {
	"third/account-registration",
	"0.1",
	"Account registration functionality (IRCv3)", 
	"Valware",
	"unrealircd-6",
};

typedef struct ARUser ARUser;
struct ARUser {
	long accreg_out;
	int accreg_complete;
	char registrar[255];
	time_t accreg_sent_time;
};

struct RegServ RegServ;
struct RegServ {
	char name[100];
};

ModDataInfo *accreg_md = NULL;
static int abort_accreg(Client *client);
void regkeylist_free(ModData *m);
const char *regkeylist_serialize(ModData *m);
void regkeylist_unserialize(const char *str, ModData *m);
const char *accreg_capability_parameter(Client *client);
int registration_server_synced(Client *client);
void accreg_md_free(ModData *md);
int has_reg_key(Client *client, char *check);
EVENT(account_registration_timeout);

#define REGCAP_NAME "draft/account-registration"
#define MSG_REGISTER "REGISTER"
#define MSG_REGISTRATION "REGISTRATION"
#define MSG_VERIFY "VERIFY"
#define SetARUser(x, y) do { moddata_client(x, accreg_md).ptr = y; } while(0)
#define ARUSER(x)	   ((ARUser *)moddata_client(x, accreg_md).ptr)
#define AGENT_SID(agent_p)      (agent_p->user != NULL ? agent_p->user->server : agent_p->name)

/* Variables */
long CAP_ACCOUNTREGISTRATION = 0L;


/*
 * REGISTRATION message
 *
 * parv[1]: distribution mask
 * parv[2]: target
 * parv[3]: ip
 * parv[4]: state
 * parv[5]: data
 * parv[6]: additional information
 * parv[7]: out-of-bound data
 */
CMD_FUNC(cmd_registration)
{
	if (!strlen(RegServ.name) || MyUser(client) || (parc < 6) || !parv[6])
		return;

	char p[150] = "\0";
	int i;
	for (i = 6; i < parc && !BadPtr(parv[i]); i++)
	{
		strlcat(p, parv[i], sizeof(p));
		if (!BadPtr(parv[i + 1]))
			strlcat(p, i == 7 ? " :" : " ", sizeof(p));
	}
	const char *txt = p;

	if (!strcasecmp(parv[1], me.name) || !strncmp(parv[1], me.id, 3))
	{
		Client *target;

		target = find_client(parv[2], NULL);
		if (!target || !MyConnect(target))
			return;

		if (target->user == NULL)
			make_user(target);

		/* Reject if someone else answers */
		if (!ARUSER(target) && *ARUSER(target)->registrar && strcasecmp(client->name, ARUSER(target)->registrar))
			return;
		else
			strlcpy(ARUSER(target)->registrar, client->name, sizeof(ARUSER(target)->registrar));

		char cmd[10] = "\0";
		/* V = VERIFY command was used */
		if (*parv[4] == 'V')
			strlcpy(cmd, "VERIFY", sizeof(cmd));

		/* R = REGISTER command was used */
		else if (*parv[4] == 'R')
			strlcpy(cmd, "REGISTER", sizeof(cmd));

		*ARUSER(target)->registrar = '\0';
		if (*parv[5] == 'F')
		{
			ARUSER(target)->accreg_sent_time = 0;
			add_fake_lag(target, 7000); /* bump fakelag due to failed registration attempt lol */
			sendto_one(target, NULL, "FAIL %s %s", cmd, txt);
		}
		else if (*parv[5] == 'S')
		{
			ARUSER(target)->accreg_sent_time = 0;
			ARUSER(target)->accreg_complete++;
			sendto_one(target, NULL, "%s SUCCESS %s", cmd, txt);
		}
		else if (*parv[5] == 'N')
			sendto_one(target, NULL, "NOTE %s %s", cmd, txt);
		
		else if (*parv[5] == 'W')
			sendto_one(target, NULL, "WARN %s %s", cmd, txt);
			
		else if (*parv[5] == 'A')
		{
			abort_accreg(client);
		}
	
		return;
	}

	/* not for us; propagate. */
	sendto_server(NULL, 0, 0, NULL, ":%s REGISTRATION %s %s %s %c %s %s",
		client->name, parv[1], parv[2], parv[3], *parv[4], parv[5], txt);
}

/*
 * REGISTER message
 *
 * parv[1]: account name or '*'
 * parv[2]: email or '*'
 * parv[3]: password
 */
CMD_FUNC(cmd_register)
{
	Client *agent_p = NULL;

	if (!strlen(RegServ.name) || !MyConnect(client) || BadPtr(parv[1]) || BadPtr(parv[2]) || BadPtr(parv[3]))
		return;

	/* Failing to use CAP REQ for account-register is a protocol violation. */
	if (!HasCapability(client, "account-registration") && !HasCapability(client, "draft/account-registration"))
		return;
		
	/* they have the cap, good, set them as such */
	if (!ARUSER(client))
		SetARUser(client, safe_alloc(sizeof(ARUser)));

	/* if we can't find the server, back out and tell the client */
	Client *server = find_server(RegServ.name, NULL);
	if (!server)
	{
		sendto_one(client, NULL, "FAIL REGISTER TEMPORARILY_UNVAILABLE :Registration server unreachable");
		return;
	}

	/* if the client is trying to register in the connection phase but we don't allow it */
	if (!has_reg_key(server, "before-connect") && !IsRegistered(client))
	{
		sendto_one(client, NULL, "FAIL REGISTER COMPLETE_CONNECTION_REQUIRED :You must connect before you may use REGISTER");
		return;
	}

	char nick[HOSTLEN+1] = "\0";
	/* If they are registering their own nick, convert it for services because we're nice like that */
	if (!strcmp(parv[1],"*") || !strcmp(parv[1], client->name))
		strcpy(nick,client->name);

	/* if not, and if we don't allow custom account names, reject them */
	else if (!has_reg_key(server, "custom-account-name"))
	{
		sendto_one(client, NULL, "FAIL REGISTER ACCOUNT_NAME_MUST_BE_NICK * :Your desired account name must match your nick");
		return;
	}
	else
		strcpy(nick,parv[1]);
	/* basic checking, let services do the real checks and go from there */
	if (!strcmp(parv[2],"*") && has_reg_key(server, "email-required"))
	{
		sendto_one(client, NULL, "FAIL REGISTER NEED_EMAIL * :You must provide an email address in order to register");
		return;
	}
	
	/* invalidity checking */
	if ((parv[1][0] == ':') || strchr(parv[1], ' '))
	{
		sendto_one(client, NULL, "FAIL REGISTER BAD_ACCOUNT_NAME :Invalid parameter");
		return;
	}
	if (parc > 400)
	{
		sendto_one(client, NULL, "FAIL REGISTER BAD_ACCOUNT_NAME :Message too long");
		return;
	}
	if (!do_nick_name(nick))
	{
		sendto_one(client, NULL, "FAIL REGISTER BAD_ACCOUNT_NAME :Erroneous account name");
		return;
	}
	/* get our registration server */
	if (*ARUSER(client)->registrar)
		agent_p = find_client(ARUSER(client)->registrar, NULL);

	/* cReDeMtIaLs */
	const char *addr = BadPtr(client->ip) ? "0" : client->ip;
	const char *account = nick;
	const char *email = parv[2];
	char p[150] = "\0";
	int i;


	/* jam their password into a single thing lmao */
	for (i = 3; i < parc && !BadPtr(parv[i]); i++)
	{
		strlcat(p, parv[i], sizeof(p));
		if (!BadPtr(parv[i + 1]))
			strlcat(p, " ", sizeof(p));
	}
	const char *pass = p;

	/* NULL terminator */
	parv[4] = NULL;

	/* and... *clicks send*
	 * Using a magic colon ":" here because we can finally properly allow whitespaces in passwords (securitah!)
	*/
	if (agent_p == NULL)
		sendto_server(NULL, 0, 0, NULL, ":%s REGISTRATION %s %s %s R R %s %s :%s",
			me.name, RegServ.name, client->id, addr, account, email, pass);

	else
		sendto_server(NULL, 0, 0, NULL, ":%s REGISTRATION %s %s %s R R %s %s :%s",
			me.name, AGENT_SID(agent_p), client->id, addr, account, email, pass);

	ARUSER(client)->accreg_out++;
	ARUSER(client)->accreg_sent_time = TStime();
}
/*
 * REGISTER message
 *
 * parv[1]: account name or '*'
 * parv[2]: email or '*'
 * parv[3]: password
 */
CMD_FUNC(cmd_verify)
{
	Client *agent_p = NULL;

	if (!strlen(RegServ.name) || !MyConnect(client) || BadPtr(parv[1]) || BadPtr(parv[2]))
		return;

	/* Failing to use CAP REQ for account-register is a protocol violation. */
	if (!HasCapability(client, "account-registration") && !HasCapability(client, "draft/account-registration"))
		return;
		
	/* they have the cap, good, set them as such */
	if (!ARUSER(client))
		SetARUser(client, safe_alloc(sizeof(ARUser)));

	/* if we can't find the server, back out and tell the client */
	Client *server = find_server(RegServ.name, NULL);
	if (!server)
	{
		sendto_one(client, NULL, "FAIL VERIFY TEMPORARILY_UNVAILABLE :Registration server unreachable");
		return;
	}

	/* if the client is trying to register in the connection phase but we don't allow it */
	if (!has_reg_key(server, "before-connect") && !IsRegistered(client))
	{
		sendto_one(client, NULL, "FAIL VERIFY COMPLETE_CONNECTION_REQUIRED :You must connect before you may use VERIFY");
		return;
	}
	
	/* invalidity checking */
	if ((parv[1][0] == ':') || strchr(parv[1], ' '))
	{
		sendto_one(client, NULL, "FAIL VERIFY BAD_ACCOUNT_NAME :Invalid parameter");
		return;
	}
	if (parc > 400)
	{
		sendto_one(client, NULL, "FAIL VERIFY BAD_ACCOUNT_NAME :Message too long");
		return;
	}

	/* get our registration server */
	if (*ARUSER(client)->registrar)
		agent_p = find_client(ARUSER(client)->registrar, NULL);

	/* cReDeMtIaLs */
	const char *addr = BadPtr(client->ip) ? "0" : client->ip;
	const char *account = parv[1];
	const char *filler = "*";
	char p[150] = "\0";
	int i;

	/* jam their code into a single lmao */
	for (i = 2; i < parc && !BadPtr(parv[i]); i++)
	{
		strlcat(p, parv[i], sizeof(p));
		if (!BadPtr(parv[i + 1]))
			strlcat(p, " ", sizeof(p));
	}
	const char *code = p;

	/* NULL terminator */
	parv[4] = NULL;
	if (agent_p == NULL)
		sendto_server(NULL, 0, 0, NULL, ":%s REGISTRATION %s %s %s V R %s %s :%s",
			me.name, RegServ.name, client->id, addr, account, filler, code);

	else
		sendto_server(NULL, 0, 0, NULL, ":%s REGISTRATION %s %s %s V R %s %s :%s",
			me.name, AGENT_SID(agent_p), client->id, addr, account, filler, code);

	ARUSER(client)->accreg_out++;
	ARUSER(client)->accreg_sent_time = TStime();
}

static int abort_accreg(Client *client)
{
	if (!ARUSER(client))
		return 0;
	ARUSER(client)->accreg_sent_time = 0;

	if (ARUSER(client)->accreg_out == 0 || ARUSER(client)->accreg_complete)
		return 0;

	const char *addr = BadPtr(client->ip) ? "0" : client->ip;
	ARUSER(client)->accreg_out = ARUSER(client)->accreg_complete = 0;

	if (*ARUSER(client)->registrar)
	{
		Client *agent_p = find_client(ARUSER(client)->registrar, NULL);

		if (agent_p != NULL)
		{
			sendto_server(NULL, 0, 0, NULL, ":%s REGISTRATION %s %s %s R A REGISTER TIMED_OUT :Timed out.",
		   		me.name, AGENT_SID(agent_p), client->id, addr);
			return 0;
		}
	}

	sendto_server(NULL, 0, 0, NULL, ":%s REGISTRATION * %s %s R A REGISTER TIMED_OUT :Timed out.", me.name, client->id, addr);
	return 0;
}

/** Is this capability visible?
 * Note that 'client' may be NULL when queried from CAP DEL / CAP NEW
 */
int accreg_capability_visible(Client *client)
{
	if (!strlen(RegServ.name) || !find_server(RegServ.name, NULL))
		return 0;

	/* Don't advertise capability if we are going to reject the
	 * user anyway due to set::plaintext-policy. This way the client
	 * won't attempt to register over plaintext connection.
	 */
	if (client && !IsSecure(client) && !IsLocalhost(client) && (iConf.plaintext_policy_user == POLICY_DENY))
		return 0;

	/* Similarly, don't advertise when we are going to reject the user
	 * due to set::outdated-tls-policy.
	 */
	if (IsSecure(client) && (iConf.outdated_tls_policy_user == POLICY_DENY) && outdated_tls_client(client))
		return 0;

	return 1;
}

int accreg_connect(Client *client)
{
	return abort_accreg(client);
}

int accreg_quit(Client *client, MessageTag *mtags, const char *comment)
{
	return abort_accreg(client);
}

int registration_server_quit(Client *client, MessageTag *mtags)
{
	if (!strlen(RegServ.name))
		return 0;

	if (!strcasecmp(client->name, RegServ.name))
		send_cap_notify(0, REGCAP_NAME);

	return 0;
}

void auto_discover_registration_server(int justlinked)
{
	
	if (!strlen(RegServ.name) && SERVICES_NAME)
	{
		Client *client = find_server(SERVICES_NAME, NULL);
		if (client && moddata_client_get(client, "regkeylist"))
		{
			/* REGISTER server found */
			if (justlinked)
			{
				unreal_log(ULOG_INFO, "server", "REGISTRATION_SERVER_AUTODETECT", client,
						   "Services server $client provides account registration, good!");
			}
			strlcpy(RegServ.name, SERVICES_NAME, sizeof(RegServ.name));
			if (justlinked)
				registration_server_synced(client);
		}
	}
}

int registration_server_synced(Client *client)
{

	if (!strlen(RegServ.name))
	{
		auto_discover_registration_server(1);
		return 0;
	}
	if (!strcasecmp(client->name, RegServ.name))
		send_cap_notify(1, REGCAP_NAME);

	return 0;
}

MOD_INIT()
{
	ClientCapabilityInfo cap;
	ModDataInfo mreq;

	MARK_AS_GLOBAL_MODULE(modinfo);

	CommandAdd(modinfo->handle, MSG_REGISTRATION, cmd_registration, MAXPARA, CMD_USER|CMD_SERVER);
	CommandAdd(modinfo->handle, MSG_REGISTER, cmd_register, MAXPARA, CMD_UNREGISTERED|CMD_USER);
	CommandAdd(modinfo->handle, MSG_VERIFY, cmd_verify, MAXPARA, CMD_UNREGISTERED|CMD_USER);

	HookAdd(modinfo->handle, HOOKTYPE_LOCAL_CONNECT, 0, accreg_connect);
	HookAdd(modinfo->handle, HOOKTYPE_LOCAL_QUIT, 0, accreg_quit);
	HookAdd(modinfo->handle, HOOKTYPE_SERVER_QUIT, 0, registration_server_quit);
	HookAdd(modinfo->handle, HOOKTYPE_SERVER_SYNCED, 0, registration_server_synced);

	memset(&mreq, 0, sizeof(mreq));
	mreq.name = "accreguser";
	mreq.type = MODDATATYPE_CLIENT;
	mreq.free = accreg_md_free;
	accreg_md = ModDataAdd(modinfo->handle, mreq);
	if (!accreg_md)
	{
		config_error("could not register 'account-registration' data");
		return MOD_FAILED;
	}
	memset(&cap, 0, sizeof(cap));
	cap.name = REGCAP_NAME;
	cap.visible = accreg_capability_visible;
	cap.parameter = accreg_capability_parameter;
	ClientCapabilityAdd(modinfo->handle, &cap, &CAP_ACCOUNTREGISTRATION);

	memset(&mreq, 0, sizeof(mreq));
	mreq.name = "regkeylist";
	mreq.free = regkeylist_free;
	mreq.serialize = regkeylist_serialize;
	mreq.unserialize = regkeylist_unserialize;
	mreq.sync = MODDATA_SYNC_EARLY;
	mreq.self_write = 1;
	mreq.type = MODDATATYPE_CLIENT;
	ModDataAdd(modinfo->handle, mreq);

	EventAdd(modinfo->handle, "account_registration_timeout", account_registration_timeout, NULL, 2000, 0);

	return MOD_SUCCESS;
}

MOD_LOAD()
{
	auto_discover_registration_server(0);
	return MOD_SUCCESS;
}

MOD_UNLOAD()
{
	return MOD_SUCCESS;
}

void regkeylist_free(ModData *m)
{
	safe_free(m->str);
}

const char *regkeylist_serialize(ModData *m)
{
	if (!m->str)
		return NULL;
	return m->str;
}

void regkeylist_unserialize(const char *str, ModData *m)
{
	safe_strdup(m->str, str);
}

const char *accreg_capability_parameter(Client *client)
{
	Client *server;

	if (strlen(RegServ.name))
	{
		server = find_server(RegServ.name, NULL);
		if (server)
			return moddata_client_get(server, "regkeylist"); /* NOTE: could still return NULL */
	}

	return NULL;
}

EVENT(account_registration_timeout)
{
	Client *client;

	list_for_each_entry(client, &unknown_list, lclient_node)
		if (ARUSER(client) && ARUSER(client)->accreg_sent_time &&
			(TStime() - ARUSER(client)->accreg_sent_time > 120))
				abort_accreg(client);
	
}

void accreg_md_free(ModData *md)
{
	ARUser *se = md->ptr;

	if (se)
	{
		safe_free(se);
		md->ptr = se = NULL;
	}
}

int has_reg_key(Client *server, char *check)
{

	int beforeconnect = 0;
	char accregtokens[60] = "\0"; /* SHOULD be enough right? based on current spec. can be changed later.. */
	strlcpy(accregtokens, accreg_capability_parameter(server), sizeof(accregtokens));

	char *tok = strtok(accregtokens, ",");
	while (tok != NULL)
	{
		if (!strcmp(tok, check))
			beforeconnect = 1;

		tok = strtok(NULL,",");
	}
	
	return beforeconnect; 
}
