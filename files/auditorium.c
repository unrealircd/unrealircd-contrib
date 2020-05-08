/* Copyright (C) All Rights Reserved
** Written by Gottem <support@gottem.nl>
** Website: https://gottem.nl/unreal
** License: https://gottem.nl/unreal/license
*/

/*** <<<MODULE MANAGER START>>>
module {
	documentation "https://gottem.nl/unreal/man/auditorium";
	troubleshooting "In case of problems, check the FAQ at https://gottem.nl/unreal/halp or e-mail me at support@gottem.nl";
	min-unrealircd-version "5.*";
	//max-unrealircd-version "5.*";
	post-install-text {
		"The module is installed, now all you need to do is add a 'loadmodule' line to your config file:";
		"loadmodule \"third/auditorium\";";
		"Then /rehash the IRCd.";
		"For usage information, refer to the module's documentation found at: https://gottem.nl/unreal/man/auditorium";
	}
}
*** <<<MODULE MANAGER END>>>
*/

// One include for all cross-platform compatibility thangs
#include "unrealircd.h"

// Since v5.0.5 some hooks now include a SendType
#define BACKPORT_HOOK_SENDTYPE (UNREAL_VERSION_GENERATION == 5 && UNREAL_VERSION_MAJOR == 0 && UNREAL_VERSION_MINOR < 5)

#define CheckAPIError(apistr, apiobj) \
	do { \
		if(!(apiobj)) { \
			config_error("A critical error occurred on %s for %s: %s", (apistr), MOD_HEADER.name, ModuleGetErrorStr(modinfo->handle)); \
			return MOD_FAILED; \
		} \
	} while(0)

// Quality fowod declarations
int auditorium_chmode_isok(Client *client, Channel *channel, char mode, char *para, int checkt, int what);
int auditorium_hook_visibleinchan(Client *target, Channel *channel);

#if BACKPORT_HOOK_SENDTYPE
	int auditorium_hook_cansend_chan(Client *client, Channel *channel, Membership *lp, char **text, char **errmsg, int notice);
#else
	int auditorium_hook_cansend_chan(Client *client, Channel *channel, Membership *lp, char **text, char **errmsg, SendType sendtype);
#endif

#define CHMODE_FLAG 'u' // Good ol' +u ;];]
#define IsAudit(x) ((x) && (x)->mode.extmode & extcmode_auditorium)

// Muh globals
Cmode_t extcmode_auditorium = 0L; // Store bitwise value latur

// Dat dere module header
ModuleHeader MOD_HEADER = {
	"third/auditorium", // Module name
	"2.0.2", // Version
	"Channel mode +u to show channel events/messages to/from people with +o/+a/+q only", // Description
	"Gottem", // Author
	"unrealircd-5", // Modversion
};

// Initialisation routine (register hooks, commands and modes or create structs etc)
MOD_INIT() {
	// Request the mode flag
	CmodeInfo cmodereq;
	memset(&cmodereq, 0, sizeof(cmodereq));
	cmodereq.flag = CHMODE_FLAG; // Flag yo
	cmodereq.paracount = 0; // No params required chico
	cmodereq.is_ok = auditorium_chmode_isok; // Custom verification function
	CheckAPIError("CmodeAdd(extcmode_auditorium)", CmodeAdd(modinfo->handle, cmodereq, &extcmode_auditorium));

	MARK_AS_GLOBAL_MODULE(modinfo);

	HookAdd(modinfo->handle, HOOKTYPE_VISIBLE_IN_CHANNEL, 0, auditorium_hook_visibleinchan);
	HookAdd(modinfo->handle, HOOKTYPE_CAN_SEND_TO_CHANNEL, 999, auditorium_hook_cansend_chan); // Low prio hook to make sure we go after everything else (like anticaps etc)
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

int auditorium_chmode_isok(Client *client, Channel *channel, char mode, char *para, int checkt, int what) {
	/* Args:
	** client: Client who issues the MODE change
	** channel: Channel to which the MODE change applies
	** mode: The mode character for completeness
	** para: Parameter to the channel mode (will be NULL for paramless modes)
	** checkt: Check type, one of EXCHK_*. Explained later.
	** what: Used to differentiate between adding and removing the mode, one of MODE_ADD or MODE_DEL
	*/

	/* Access types:
	** EXCHK_ACCESS: Verify if the user may (un)set the mode, do NOT send error messages for this (just check access)
	** EXCHK_ACCESS_ERR: Similar to above, but you SHOULD send an error message here
	** EXCHK_PARAM: Check the sanity of the parameter(s)
	*/

	/* Return values:
	** EX_ALLOW: Allow it
	** EX_DENY: Deny for most people (only IRC opers w/ override may use it)
	** EX_ALWAYS_DENY: Even prevent IRC opers from overriding shit
	*/
	if((checkt == EXCHK_ACCESS) || (checkt == EXCHK_ACCESS_ERR)) { // Access check lol
		// Check if the user has +a or +q (OperOverride automajikally overrides this bit ;])
		if(!is_chanadmin(client, channel) && !is_chanowner(client, channel)) {
			if(checkt == EXCHK_ACCESS_ERR)
				sendnumeric(client, ERR_CHANOWNPRIVNEEDED, channel->chname);
			return EX_DENY;
		}
		return EX_ALLOW;
	}
	return EX_ALLOW; // Fallthrough, like when someone attempts +u 10 it'll simply do +u
}

int auditorium_hook_visibleinchan(Client *target, Channel *channel) {
	if(IsAudit(channel) && !is_chan_op(target, channel) && !IsULine(target)) // If channel has +u and the checked user (not you) doesn't have +o or higher...
		return HOOK_DENY; // ...don't show in /names etc
	return HOOK_CONTINUE;
}

#if BACKPORT_HOOK_SENDTYPE
	int auditorium_hook_cansend_chan(Client *client, Channel *channel, Membership *lp, char **text, char **errmsg, int notice) {
#else
	int auditorium_hook_cansend_chan(Client *client, Channel *channel, Membership *lp, char **text, char **errmsg, SendType sendtype) {
		// Let's not act on TAGMSG for the time being :>
		if(sendtype != SEND_TYPE_PRIVMSG && sendtype != SEND_TYPE_NOTICE)
			return HOOK_CONTINUE;
		int notice = (sendtype == SEND_TYPE_NOTICE);
#endif

	MessageTag *mtags = NULL;
	if(IsAudit(channel) && IsUser(client) && !is_chan_op(client, channel) && !IsOper(client) && !IsULine(client)) { // If channel has +u and you don't have +o or higher...
		// In case the user is banned just keep processing the hooks as usual, since one of them will finally interrupt and (prolly) emit a message =]
		if(is_banned(client, channel, BANCHK_MSG, text, NULL))
			return HOOK_CONTINUE;

		// ..."relay" the message to +o etc only
		new_message(client, NULL, &mtags);
		sendto_channel(channel, client, NULL, PREFIX_OP|PREFIX_ADMIN|PREFIX_OWNER, 0, SEND_ALL, mtags, ":%s %s @%s :%s", client->name, (notice ? "NOTICE" : "PRIVMSG"), channel->chname, *text);
		*text = NULL;
		free_message_tags(mtags);
		// Can't return HOOK_DENY here cuz Unreal will abort() in that case :D
	}
	return HOOK_CONTINUE;
}
