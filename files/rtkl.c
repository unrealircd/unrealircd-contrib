/* Copyright (C) All Rights Reserved
** Written by Gottem <support@gottem.nl>
** Website: https://gottem.nl/unreal
** License: https://gottem.nl/unreal/license
*/

/*** <<<MODULE MANAGER START>>>
module {
	documentation "https://gottem.nl/unreal/man/rtkl";
	troubleshooting "In case of problems, check the FAQ at https://gottem.nl/unreal/halp or e-mail me at support@gottem.nl";
	min-unrealircd-version "6.*";
	//max-unrealircd-version "6.*";
	post-install-text {
		"The module is installed, now all you need to do is add a 'loadmodule' line to your config file:";
		"loadmodule \"third/rtkl\";";
		"Then /rehash the IRCd.";
		"For usage information, refer to the module's documentation found at: https://gottem.nl/unreal/man/rtkl";
	}
}
*** <<<MODULE MANAGER END>>>
*/

// One include for all cross-platform compatibility thangs
#include "unrealircd.h"

// Command strings
#define MSG_RKLINE "RKLINE"
#define MSG_RZLINE "RZLINE"

#define CheckAPIError(apistr, apiobj) \
	do { \
		if(!(apiobj)) { \
			config_error("A critical error occurred on %s for %s: %s", (apistr), MOD_HEADER.name, ModuleGetErrorStr(modinfo->handle)); \
			return MOD_FAILED; \
		} \
	} while(0)

// Register command functions
CMD_FUNC(cmd_rkline);
CMD_FUNC(cmd_rzline);

// Quality fowod declarations
static void dumpit(Client *client, char **p);
void rtkl_main(Client *client, int parc, const char *parv[], char *cmd, char *operperm);
int hook_tkl_add(Client *client, TKL *tkl);
int hook_tkl_del(Client *client, TKL *tkl);
int hook_tkl_main(Client *client, TKL *tkl, char flag);

// Help string in case someone does just /RKLINE
static char *rtklhelp[] = {
	/* Special characters:
	** \002 = bold -- \x02
	** \037 = underlined -- \x1F
	*/
	"*** \002Help on /RKLINE and /RZLINE\002 ***",
	"Both of those commands allow you to remove \037local\037",
	"K-Lines and Z-Lines from remote servers. This might be useful",
	"if for example a spamfilter triggered a K-Line and you want to",
	"remove the ban without reconnecting.",
	" ",
	"Syntax:",
	"    \002/RKLINE\002 \037server\037 \037[-]user@host\037 [duration] [reason]",
	"    \002/RZLINE\002 \037server\037 \037[-]user@IP\037 [duration] [reason]",
	"        Everything after the \037server\037 arg is the same stuff",
	         "as for regular K/Z-Lines.",
	" ",
	"Examples:",
	"    \002/RKLINE myleaf.dom.tld guest*@* 0 na d00d\002",
	"    \002/RZLINE myleaf.dom.tld guest*@* 0 na d00d\002",
	"        Place a ban on all guests",
	"    \002/RKLINE myleaf.dom.tld -guest*@*\002",
	"        Remove the same ban",
	" ",
	"There's no function for listing them in this module, as you can already use",
	"\002/stats K <server>\002 for that.",
	NULL
};

// Dat dere module header
ModuleHeader MOD_HEADER = {
	"third/rtkl", // Module name
	"2.1.0", // Version
	"Allows privileged opers to remove remote servers' local K/Z-Lines", // Description
	"Gottem", // Author
	"unrealircd-6", // Modversion
};

// Initialisation routine (register hooks, commands and modes or create structs etc)
MOD_INIT() {
	// Only users may use this module's commands =]
	CheckAPIError("CommandAdd(RKLINE)", CommandAdd(modinfo->handle, MSG_RKLINE, cmd_rkline, MAXPARA, CMD_USER));
	CheckAPIError("CommandAdd(RZLINE)", CommandAdd(modinfo->handle, MSG_RZLINE, cmd_rzline, MAXPARA, CMD_USER));

	MARK_AS_GLOBAL_MODULE(modinfo);

	HookAdd(modinfo->handle, HOOKTYPE_TKL_ADD, 0, hook_tkl_add);
	HookAdd(modinfo->handle, HOOKTYPE_TKL_DEL, 0, hook_tkl_del);
	return MOD_SUCCESS;
}

// Actually load the module here (also command overrides as they may not exist in MOD_INIT yet)
MOD_LOAD() {
	return MOD_SUCCESS; // We good
}

// Called on unload/rehash obv
MOD_UNLOAD() {
	return MOD_SUCCESS; // We good
}

// Dump a NULL-terminated array of strings to the user (taken from DarkFire IRCd)
static void dumpit(Client *client, char **p) {
	if(IsServer(client)) // Bail out early and silently if it's a server =]
		return;

	// Using sendto_one() instead of sendnumericfmt() because the latter strips indentation and stuff ;]
	for(; *p != NULL; p++)
		sendto_one(client, NULL, ":%s %03d %s :%s", me.name, RPL_TEXT, client->name, *p);
}

CMD_FUNC(cmd_rkline) {
	// Gets args: Client *client, MessageTag *recv_mtags, int parc, char *parv[]
	rtkl_main(client, parc, parv, MSG_RKLINE, "server-ban:kline:remove");
}

CMD_FUNC(cmd_rzline) {
	rtkl_main(client, parc, parv, MSG_RZLINE, "server-ban:zline:local:remove");
}

void rtkl_main(Client *client, int parc, const char *parv[], char *cmd, char *operperm) {
	int i; // Iterator to shift parv in a bit ;]
	Client *srv; // Server pointer kek
	char buf[BUFSIZE];
	size_t len;

	if(!MyUser(client) || IsULine(client)) // Only regular local users (well, opers) can use this =]
		return;

	if(!IsOper(client) || !ValidatePermissionsForPath(operperm, client, NULL, NULL, NULL)) {
		sendnumeric(client, ERR_NOPRIVILEGES); // Not gonna happen
		return;
	}

	if(BadPtr(parv[1]) || !strcasecmp(parv[1], "help") || !strcasecmp(parv[1], "halp")) { // If first argument is a bad pointer or "help", don't proceed
		dumpit(client, rtklhelp); // Return help string instead
		return;
	}

	// Need at least 2 args before we pass it on to the server
	if(BadPtr(parv[2])) {
		sendnumeric(client, ERR_NEEDMOREPARAMS, cmd); // Show "needs more parameters" error string
		return;
	}

	if(!(srv = find_server(parv[1], NULL))) {
		sendnotice(client, "[rtkl] Invalid server name: %s (server not found)", parv[1]);
		return;
	}

	if(srv == &me) {
		sendnotice(client, "[rtkl] Invalid server name: %s (you're currently connected to this server, so use the regular /ZLINE and /KLINE commands)", parv[1]);
		return;
	}

	memset(buf, '\0', sizeof(buf));
	len = 0;
	for(i = 2; i < parc && parv[i]; i++) {
		if(i == 2) {
			ircsnprintf(buf, sizeof(buf), "%s", parv[i]);
			if(*parv[i] == '-') // No need to process any more args for deletions =]
				break;
		}
		else {
			ircsnprintf(buf + len, sizeof(buf) - len, " %s", parv[i]);
			len++; // Add one for el space ;]
		}
		len += strlen(parv[i]);
	}

	unreal_log(ULOG_INFO, "rtkl", "RTKL_USAGE", client, "$client.details used $cmd for server $target [args = $args]",
		log_data_string("cmd", cmd),
		log_data_string("target", parv[1]),
		log_data_string("args", buf)
	);

	cmd++; // Skip the R in RKLINE etc =]
	sendto_one(srv, NULL, ":%s %s %s", client->name, cmd, buf);
}

int hook_tkl_add(Client *client, TKL *tkl) {
	return hook_tkl_main(client, tkl, '+');
}

int hook_tkl_del(Client *client, TKL *tkl) {
	return hook_tkl_main(client, tkl, '-');
}

int hook_tkl_main(Client *client, TKL *tkl, char flag) {
	char buf[BUFSIZE]; // Output buffer lol
	size_t len;
	char gmt[128];
	char tkltype;
	Client *setter;
	char setby[NICKLEN + USERLEN + HOSTLEN + 6];
	char *nick;

	// May have been (un)set by el server timer, no need to do shit with it
	if(!client || !tkl)
		return HOOK_CONTINUE;

	// Only respond to non-local clients and _local_ K/Z-Lines ;];]
	tkltype = tkl_typetochar(tkl->type);
	strncpy(setby, tkl->set_by, sizeof(setby));
	nick = strtok(setby, "!");
	if(!nick || !(setter = find_user(nick, NULL)) || MyUser(setter) || !strchr("kz", tkltype))
		return HOOK_CONTINUE; // kbye

	// Let's build that fucking message
	ircsnprintf(buf, sizeof(buf), "[rtkl] %s%s (local) %c-Line for %s@%s",
		(flag == '+' ? "Added" : "Removed"),
		(tkl->expire_at == 0 ? " permanent" : ""),
		toupper(tkltype), tkl->ptr.serverban->usermask, tkl->ptr.serverban->hostmask
	);

	if(flag == '-') {
		len = strlen(buf);
		*gmt = '\0';
		short_date(tkl->set_at, gmt);
		ircsnprintf(buf + len, sizeof(buf) - len, " set by %s at %s GMT", tkl->set_by, gmt);
	}

	if(tkl->expire_at > 0) {
		len = strlen(buf);
		*gmt = '\0';
		short_date(tkl->expire_at, gmt);
		ircsnprintf(buf + len, sizeof(buf) - len, " (to expire at %s GMT)", gmt);
	}

	len = strlen(buf);
	ircsnprintf(buf + len, sizeof(buf) - len, " [reason: %s]", tkl->ptr.serverban->reason);

	sendnotice(setter, buf); // Send that fucking abomination
	return HOOK_CONTINUE; // Can't do shit anyways =]
}
