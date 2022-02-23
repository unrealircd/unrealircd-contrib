/* Copyright (C) All Rights Reserved
** Written by Gottem <support@gottem.nl>
** Website: https://gottem.nl/unreal
** License: https://gottem.nl/unreal/license
*/

/*** <<<MODULE MANAGER START>>>
module {
	documentation "https://gottem.nl/unreal/man/sacmds";
	troubleshooting "In case of problems, check the FAQ at https://gottem.nl/unreal/halp or e-mail me at support@gottem.nl";
	min-unrealircd-version "6.*";
	//max-unrealircd-version "6.*";
	post-install-text {
		"The module is installed, now all you need to do is add a 'loadmodule' line to your config file:";
		"loadmodule \"third/sacmds\";";
		"Then /rehash the IRCd.";
		"For usage information, refer to the module's documentation found at: https://gottem.nl/unreal/man/sacmds";
	}
}
*** <<<MODULE MANAGER END>>>
*/

// One include for all cross-platform compatibility thangs
#include "unrealircd.h"

#define MSG_SANICK "SANICK"
#define MSG_SAUMODE "SAUMODE"

#define UMODE_DENY 0
#define UMODE_ALLOW 1

// Dem macros yo
#define CheckAPIError(apistr, apiobj) \
	do { \
		if(!(apiobj)) { \
			config_error("A critical error occurred on %s for %s: %s", (apistr), MOD_HEADER.name, ModuleGetErrorStr(modinfo->handle)); \
			return MOD_FAILED; \
		} \
	} while(0)

CMD_FUNC(cmd_sanick);
CMD_FUNC(cmd_saumode);

// Quality fowod declarations
static void dumpit(Client *client, char **p);

// Help strings in case someone does just /SA<CMD>
/* Special characters:
** \002 = bold -- \x02
** \037 = underlined -- \x1F
*/
static char *sanickhelp[] = {
	"*** \002Help on /SANICK\002 ***",
	"Forces a user to take a different nick.",
	" ",
	"Syntax:",
	"    \002/SANICK\002 \037orig\037 \037new\037",
	" ",
	"Examples:",
	"    \002/SANICK test test2\002",
	"        Changes the nick \037test\037 to \037test2\037.",
	NULL
};

static char *saumodehelp[] = {
	"*** \002Help on /SAUMODE\002 ***",
	"Forcibly changes someone's u(ser)modes",
	" ",
	"Syntax:",
	"    \002/SAUMODE\002 \037nick\037 \037modes\037",
	" ",
	"Examples:",
	"    \002/SAUMODE someone +Z\002",
	"        Enables secureonlymsg on \037someone\037.",
	"    \002/SAUMODE someone -x\002",
	"        Remove \037someone\037's cloaked host.",
	"    \002/SAUMODE someone +Z-x\002",
	"        Combination of the two.",
	NULL
};

// Dat dere module header
ModuleHeader MOD_HEADER = {
	"third/sacmds", // Module name
	"2.2.0", // Version
	"Implements SA* commands for privileged opers", // Description
	"Gottem", // Author
	"unrealircd-6", // Modversion
};

MOD_INIT() {
	CheckAPIError("CommandAdd(SANICK)", CommandAdd(modinfo->handle, MSG_SANICK, cmd_sanick, 2, CMD_USER));
	CheckAPIError("CommandAdd(SAUMODE)", CommandAdd(modinfo->handle, MSG_SAUMODE, cmd_saumode, 2, CMD_USER));

	MARK_AS_GLOBAL_MODULE(modinfo);
	return MOD_SUCCESS;
}

MOD_LOAD() {
	return MOD_SUCCESS;
}

// Called on unload/rehash lol
MOD_UNLOAD() {
	return MOD_SUCCESS;
}

// Dump a NULL-terminated array of strings to the user (taken from DarkFire IRCd)
static void dumpit(Client *client, char **p) {
	if(IsServer(client)) // Bail out early and silently if it's a server =]
		return;

	// Using sendto_one() instead of sendnumericfmt() because the latter strips indentation and stuff ;]
	for(; *p != NULL; p++)
		sendto_one(client, NULL, ":%s %03d %s :%s", me.name, RPL_TEXT, client->name, *p);
}

CMD_FUNC(cmd_sanick) {
	// Gets args: Client *client, MessageTag *recv_mtags, int parc, char *parv[]
	char oldnick[NICKLEN + 1], newnick[NICKLEN + 1];
	Client *acptr; // "Orig" check
	Client *ocptr; // "New" check
	long tiem; // Nickchange timestamp etc
	MessageTag *mtags;
	char descbuf[BUFSIZE];
	unsigned char removemoder;

	// Prevent non-privileged opers from using this command
	if(!ValidatePermissionsForPath("sanick", client, NULL, NULL, NULL) && !IsULine(client)) {
		sendnumeric(client, ERR_NOPRIVILEGES); // Send error lol
		return;
	}

	if(BadPtr(parv[1])) { // If first argument is a bad pointer, don't proceed
		dumpit(client, sanickhelp); // Return help string instead
		return;
	}

	if(parc < 3) { // Need at least 3 params yo
		sendnumeric(client, ERR_NEEDMOREPARAMS, MSG_SANICK); // Send error lol
		return;
	}

	strlcpy(oldnick, parv[1], sizeof(oldnick));
	strlcpy(newnick, parv[2], sizeof(newnick));

	if(!strcasecmp(oldnick, newnick)) // If orig and new are the same, gtfo =]
		return;

	if(!(acptr = find_user(oldnick, NULL))) { // Ensure that the target nick is actually in use
		sendnumeric(client, ERR_NOSUCHNICK, oldnick); // Send error lol
		return;
	}

	if(IsULine(acptr)) {
		sendnotice(client, "[sacmd/nick] You can't change nicknames of U-Lined users");
		return;
	}

	if(strlen(newnick) > NICKLEN) {
		sendnotice(client, "*** [sacmd/nick] Target nick can not exceed NICKLEN (max. %d chars)", NICKLEN);
		return;
	}

	if(do_nick_name(newnick) == 0) { // Validate new nick
		sendnotice(client, "*** [sacmd/nick] Invalid target nick: %s", newnick);
		return;
	}

	if((ocptr = find_client(newnick, NULL))) { // Prevent nick collisions (no kill)
		sendnotice(client, "*** [sacmd/nick] Changing the nick %s to %s would cause a collision, aborting", oldnick, newnick);
		return;
	}

	// If the target user is on another server, forward the command to that server (doing this only now to save a tiny bit of bandwidth)
	if(!MyUser(acptr)) {
		sendto_one(acptr->direction, NULL, ":%s %s %s %s", client->id, MSG_SANICK, oldnick, newnick);
		return; // Forwarded
	}

	// Enact the nick change
	removemoder = ((acptr->umodes & UMODE_REGNICK) ? 1 : 0);
	if(client->user) { // Just in case a server calls this shit
		unreal_log(ULOG_INFO, "sacmds", "SACMDS_NICK_USAGE", client, "$client.details used $cmd to change $target to $new",
			log_data_string("cmd", MSG_SANICK),
			log_data_string("target", acptr->name),
			log_data_string("new", newnick)
		);
	}

	mtags = NULL;
	tiem = TStime(); // Get timestamp
	new_message(acptr, recv_mtags, &mtags);
	RunHook(HOOKTYPE_LOCAL_NICKCHANGE, acptr, mtags, newnick);
	acptr->lastnick = tiem; // Set the timestamp of the last nick change for the target user to the current time.
	add_history(acptr, 1); // Add nick history for whowas etc
	sendto_server(acptr, 0, 0, mtags, ":%s NICK %s %lld", acptr->id, newnick, (long long)acptr->lastnick); // Send to the rest of el netw0rkerin0 ;]
	sendto_local_common_channels(acptr, acptr, 0, mtags, ":%s NICK :%s", acptr->name, newnick); // And to local users in common channels
	sendto_one(acptr, mtags, ":%s NICK :%s", acptr->name, newnick); // And the user itself
	free_message_tags(mtags);

	if(removemoder)
		acptr->umodes &= ~UMODE_REGNICK; // Remove +r umode (registered nick)

	del_from_client_hash_table(acptr->name, acptr); // Remove old name from lclient_list
	strlcpy(acptr->name, newnick, sizeof(acptr->name)); // Actually change the nick the pointer is using here
	add_to_client_hash_table(newnick, acptr); // Re-add to lclient_list

	snprintf(descbuf, sizeof(descbuf), "Client: %s", newnick);
	fd_desc(acptr->local->fd, descbuf);
	if(removemoder)
		sendto_one(acptr, NULL, ":%s MODE %s :-r", me.name, acptr->name);

	RunHook(HOOKTYPE_POST_LOCAL_NICKCHANGE, acptr, recv_mtags, oldnick);
}

CMD_FUNC(cmd_saumode) {
	Umode *um;
	const char *m; // For checkin em mode string
	Client *acptr; // Target nick
	int what; // Direction flag
	long lastflags; // Current umodes
	char mbuf[128]; // For storing the required mode string to pass along to the MODE command

	// Prevent non-privileged opers from using this command
	if(!ValidatePermissionsForPath("saumode", client, NULL, NULL, NULL) && !IsULine(client)) {
		sendnumeric(client, ERR_NOPRIVILEGES); // Send error lol
		return;
	}

	if(BadPtr(parv[1])) { // If first argument is a bad pointer, don't proceed
		dumpit(client, saumodehelp); // Return help string instead
		return;
	}

	if(parc < 3) { // Need at least 3 params yo
		sendnumeric(client, ERR_NEEDMOREPARAMS, MSG_SAUMODE); // Send error lol
		return;
	}

	if(!(acptr = find_user(parv[1], NULL))) { // Ensure that the target nick is actually in use
		sendnumeric(client, ERR_NOSUCHNICK, parv[1]); // Send error lol
		return;
	}

	if(IsULine(acptr)) {
		sendnotice(client, "[sacmd/umode] You can't change usermodes for U-Lined users");
		return;
	}

	// If the target user is on another server, forward the command to that server (doing this only now to save a tiny bit of bandwidth)
	if(!MyUser(acptr)) {
		sendto_one(acptr->direction, NULL, ":%s %s %s %s", client->id, MSG_SAUMODE, parv[1], parv[2]);
		return;
	}

	userhost_save_current(acptr);

	what = MODE_ADD;
	lastflags = acptr->umodes;

	// Ayy thx Syzop ;];]
	for(m = parv[2]; *m; m++) {
		switch (*m) {
			case '+':
				what = MODE_ADD;
				break;
			case '-':
				what = MODE_DEL;
				break;

			// We may not get these, but they shouldnt be in default =]
			case ' ':
			case '\n':
			case '\r':
			case '\t':
				break;

			case 'i':
				if((what == MODE_ADD) && !(acptr->umodes & UMODE_INVISIBLE))
					irccounts.invisible++;
				if((what == MODE_DEL) && (acptr->umodes & UMODE_INVISIBLE))
					irccounts.invisible--;
				goto setmodex;
			case 'o':
				if((what == MODE_ADD) && !(acptr->umodes & UMODE_OPER)) {
					if(!IsOper(acptr) && MyUser(acptr))
						list_add(&acptr->special_node, &oper_list);
					irccounts.operators++;
				}
				if((what == MODE_DEL) && (acptr->umodes & UMODE_OPER)) {
					if(acptr->umodes & UMODE_HIDEOPER)
						acptr->umodes &= ~UMODE_HIDEOPER; // clear 'H' too, and opercount stays the same
					else
						irccounts.operators--;

					if(MyUser(acptr) && !list_empty(&acptr->special_node))
						list_del(&acptr->special_node);

					// User is no longer oper (after the goto below), so remove all oper-only modes and snomasks.
					remove_oper_privileges(acptr, 0);
				}
				goto setmodex;
			case 'H':
				if(what == MODE_ADD && !(acptr->umodes & UMODE_HIDEOPER)) {
					if(!IsOper(acptr) && !strchr(parv[2], 'o')) // Yes, this strchr() is flawed xd
						break; // Isn't an oper and would not become one either, gtfo
					irccounts.operators--;
				}
				if(what == MODE_DEL && (acptr->umodes & UMODE_HIDEOPER))
					irccounts.operators++;
				goto setmodex;
			case 'd':
				goto setmodex;
			case 'x':
				if(what == MODE_DEL) {
					if(acptr->user->virthost) // If user has a virtual host and we got -x...
						safe_strdup(acptr->user->virthost, acptr->user->cloakedhost); // ...change it to the cloaked host ;]
				}
				else {
					if(!acptr->user->virthost)
						safe_strdup(acptr->user->virthost, acptr->user->cloakedhost);

					if(MyUser(acptr) && !strcasecmp(acptr->user->virthost, acptr->user->cloakedhost))
						sendto_server(NULL, PROTO_VHP, 0, NULL, ":%s SETHOST :%s", acptr->id, acptr->user->virthost);
				}
				goto setmodex;
			case 't':
				// We support -t nowadays, which means we remove the vhost and set the cloaked host (note that +t is a NOOP, that code is in +x)
				if(what == MODE_DEL) {
					if(acptr->user->virthost && *acptr->user->cloakedhost && strcasecmp(acptr->user->cloakedhost, GetHost(acptr))) {
						safe_strdup(acptr->user->virthost, acptr->user->cloakedhost);

						if(MyUser(acptr))
							sendto_server(NULL, PROTO_VHP, 0, NULL, ":%s SETHOST :%s", acptr->id, acptr->user->virthost);
					}
				goto setmodex;
			}
			case 'r': // Let's not fuck with registration status lel
			case 'z': // Setting and unsetting user mode 'z' is not supported
				break;
			default:
				setmodex: // Actually gonna change the mode here =]
				for(um = usermodes; um; um = um->next) {
					if(um->letter == *m) {
						if(what == MODE_ADD)
							acptr->umodes |= um->mode;
						else
							acptr->umodes &= ~um->mode;
						break;
					}
				}
				break;
		}
	}

	if(lastflags != acptr->umodes) { // If the flags end up different ;]
		RunHook(HOOKTYPE_UMODE_CHANGE, acptr, lastflags, acptr->umodes); // Let's runnem hewks lel
		build_umode_string(acptr, lastflags, ALL_UMODES, mbuf); // Figure out the resulting mode string ;]
		if(MyUser(acptr) && *mbuf)
			sendto_one(acptr, NULL, ":%s MODE %s :%s", client->name, acptr->name, mbuf);

		if(client->user) {
			unreal_log(ULOG_INFO, "sacmds", "SACMDS_UMODE_USAGE", client, "$client.details used $cmd $args to change $target's umodes",
				log_data_string("cmd", MSG_SAUMODE),
				log_data_string("args", mbuf),
				log_data_string("target", acptr->name)
			);
		}
	}

	userhost_changed(acptr); // We can safely call this, even if nothing changed
	VERIFY_OPERCOUNT(acptr, "saumode");
}
