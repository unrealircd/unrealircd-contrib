/* Copyright (C) All Rights Reserved
** Written by Gottem <support@gottem.nl>
** Website: https://gottem.nl/unreal
** License: https://gottem.nl/unreal/license
*/

/*** <<<MODULE MANAGER START>>>
module {
	documentation "https://gottem.nl/unreal/man/auditorium";
	troubleshooting "In case of problems, check the FAQ at https://gottem.nl/unreal/halp or e-mail me at support@gottem.nl";
	min-unrealircd-version "6.*";
	//max-unrealircd-version "6.*";
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

#define CheckAPIError(apistr, apiobj) \
	do { \
		if(!(apiobj)) { \
			config_error("A critical error occurred on %s for %s: %s", (apistr), MOD_HEADER.name, ModuleGetErrorStr(modinfo->handle)); \
			return MOD_FAILED; \
		} \
	} while(0)

// Quality fowod declarations
int auditorium_chmode_isok(Client *client, Channel *channel, char mode, const char *para, int checkt, int what);

#if UNREAL_VERSION <= 0x06010000
	int auditorium_hook_visibleinchan(Client *target, Channel *channel);
#elif UNREAL_VERSION < 0x06020100
	int auditorium_hook_visibleinchan(Client *target, Channel *channel, Member *client_member);
#else
	int auditorium_hook_join_data(Client *client, Channel *channel);
	int auditorium_hook_chanmode(Client *client, Channel *channel, MessageTag *mtags, const char *modebuf, const char *parabuf, time_t sendts, int samode, int *destroy_channel);
#endif

#if UNREAL_VERSION >= 0x06020000
	int auditorium_hook_cansend_chan(Client *client, Channel *channel, Membership *lp, const char **text, const char **errmsg, SendType sendtype, ClientContext *clictx);
#else
	int auditorium_hook_cansend_chan(Client *client, Channel *channel, Membership *lp, const char **text, const char **errmsg, SendType sendtype);
#endif

#define CHMODE_FLAG 'u' // Good ol' +u ;];]
#define IsAudit(x) ((x) && has_channel_mode((x), CHMODE_FLAG))
#define CanBeVisible(cl, ch) (check_channel_access(cl, ch, "oaq") || IsULine(cl))
#define IsMemberInvisible(x) ((x)->memb_flags & MEMB_FLAG_INVISIBLE)

// Muh globals
Cmode_t extcmode_auditorium = 0L; // Store bitwise value latur

// Dat dere module header
ModuleHeader MOD_HEADER = {
	"third/auditorium", // Module name
	"2.1.5", // Version
	"Channel mode +u to show channel events/messages to/from people with +o/+a/+q only", // Description
	"Gottem", // Author
	"unrealircd-6", // Modversion
};

// Initialisation routine (register hooks, commands and modes or create structs etc)
MOD_INIT() {
	// Request the mode flag
	CmodeInfo cmodereq;
	memset(&cmodereq, 0, sizeof(cmodereq));
	cmodereq.letter = CHMODE_FLAG; // Flag yo
	cmodereq.paracount = 0; // No params required chico
	cmodereq.is_ok = auditorium_chmode_isok; // Custom verification function
	CheckAPIError("CmodeAdd(extcmode_auditorium)", CmodeAdd(modinfo->handle, cmodereq, &extcmode_auditorium));

	MARK_AS_GLOBAL_MODULE(modinfo);

	#if UNREAL_VERSION < 0x06020100
		HookAdd(modinfo->handle, HOOKTYPE_VISIBLE_IN_CHANNEL, 0, auditorium_hook_visibleinchan);
	#else
		HookAdd(modinfo->handle, HOOKTYPE_JOIN_DATA, 0, auditorium_hook_join_data);
		HookAdd(modinfo->handle, HOOKTYPE_LOCAL_CHANMODE, 0, auditorium_hook_chanmode);
		HookAdd(modinfo->handle, HOOKTYPE_REMOTE_CHANMODE, 0, auditorium_hook_chanmode);
	#endif

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

int auditorium_chmode_isok(Client *client, Channel *channel, char mode, const char *para, int checkt, int what) {
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
		if(!check_channel_access(client, channel, "aq")) {
			if(checkt == EXCHK_ACCESS_ERR)
				sendnumeric(client, ERR_CHANOWNPRIVNEEDED, channel->name);
			return EX_DENY;
		}
		return EX_ALLOW;
	}
	return EX_ALLOW; // Fallthrough, like when someone attempts +u 10 it'll simply do +u
}

#if UNREAL_VERSION < 0x06020100
	#if UNREAL_VERSION <= 0x06010000
		int auditorium_hook_visibleinchan(Client *target, Channel *channel)
	#elif UNREAL_VERSION < 0x06020100
		int auditorium_hook_visibleinchan(Client *target, Channel *channel, Member *client_member)
	#endif
	{
		if(IsAudit(channel) && !CanBeVisible(client, channel)) // If channel has +u and the checked user (not you) doesn't have +o or higher...
			return HOOK_DENY; // ...don't show in /names etc
		return HOOK_CONTINUE;
	}
#else
	int auditorium_hook_join_data(Client *client, Channel *channel) {
		if(IsAudit(channel) && !CanBeVisible(client, channel)) // If channel has +u and the joining user doesn't have +o or higher (in this case it's really only about being U-Lined or n0)...
			set_user_invisible(client, channel, 1); // ...don't show in /names etc
		return HOOK_CONTINUE;
	}

	int auditorium_hook_chanmode(Client *client, Channel *channel, MessageTag *mtags, const char *modebuf, const char *parabuf, time_t sendts, int samode, int *destroy_channel) {
		ParseMode pm;
		Client *target;
		Member* mb;
		int ret;
		int mode_u_changed;

		mode_u_changed = 0;
		for(ret = parse_chanmode(&pm, modebuf, parabuf); ret; ret = parse_chanmode(&pm, NULL, NULL)) {
			// Gotta adjust the visibility of all users when changing +u itself, but we'll do this outside the loop to ensure we change all the members only once
			if(pm.modechar == CHMODE_FLAG) {
				mode_u_changed = pm.what == MODE_DEL ? -1 : 1;
				continue;
			}

			// We'll also adjust the visibility of users that get or lose +oaq while +u is in effect
			if(!IsAudit(channel))
				continue;
			if(pm.modechar != 'o' && pm.modechar != 'a' && pm.modechar != 'q')
				continue;

			// Since this is a regular chanmode hook (i.e. not a PRE_* variant), the modes are actually already applied so we can keep it quite simple
			// We don't use a check like IsMemberInvisible() here since there shouldn't be very many nicks in one /MODE anyway, so letting set_user_invisible() look up the membership link every time shouldn't be a real problem
			target = find_client(pm.param, NULL);
			if(target)
				set_user_invisible(target, channel, !CanBeVisible(target, channel));
		}

		if(mode_u_changed == 0)
			return HOOK_CONTINUE;

		for(mb = channel->members; mb; mb = mb->next) {
			// We'll need to make everyone visible when doing -u
			// Here we do use IsMemberInvisible(), mostly to prevent having many pointless lookups of membership links (we already have members here, so we can just check the flag directly)
			if(mode_u_changed == -1) {
				if(IsMemberInvisible(mb))
					set_user_invisible(mb->client, channel, 0);
				continue;
			}

			// And when +u is set we'll need to unhide everyone with +oaq and hide the rest
			if(CanBeVisible(mb->client, channel)) {
				if(IsMemberInvisible(mb))
					set_user_invisible(mb->client, channel, 0);
			}
			else if(!IsMemberInvisible(mb)) {
				set_user_invisible(mb->client, channel, 1);
			}
		}

		return HOOK_CONTINUE;
	}
#endif

#if UNREAL_VERSION >= 0x06020000
	int auditorium_hook_cansend_chan(Client *client, Channel *channel, Membership *lp, const char **text, const char **errmsg, SendType sendtype, ClientContext *clictx)
#else
	int auditorium_hook_cansend_chan(Client *client, Channel *channel, Membership *lp, const char **text, const char **errmsg, SendType sendtype)
#endif
{
	// Let's not act on TAGMSG for the time being :>
	if(sendtype != SEND_TYPE_PRIVMSG && sendtype != SEND_TYPE_NOTICE)
		return HOOK_CONTINUE;
	if(!text || !*text) // If there's no text then the message is already blocked :>
		return HOOK_CONTINUE;

	int notice = sendtype == SEND_TYPE_NOTICE;
	const char *cmd = notice ? "NOTICE" : "PRIVMSG";
	int cap_echo = HasCapability(client, "echo-message");
	MessageTag *mtags = NULL;

	if(IsAudit(channel) && IsUser(client) && !CanBeVisible(client, channel)) { // If channel has +u and you don't have +o or higher
		// In case the user is banned we'll just keep processing the hooks as usual, since one of them will finally interrupt and (prolly) emit a message =]
		if(is_banned(client, channel, BANCHK_MSG, text, NULL))
			return HOOK_CONTINUE;

		// "Relay" the message to `+o` etc only
		// Note that some clients supporting `echo-message` may or may not display messages sent by the user by themselves, so they could be excluded by the `+oaq` check and never see their own shit
		// To prevent this we'll skip the initial multicast for these clients and echo the message back separately, which should still correctly prevent d00plicates (they wanna receive an echo in the first place)
		new_message(client, NULL, &mtags);
		sendto_channel(channel, client, (cap_echo ? client : NULL), "oaq", 0, SEND_ALL, mtags, ":%s %s @%s :%s", client->name, cmd, channel->name, *text);
		if(cap_echo)
			sendto_one(client, mtags, ":%s %s @%s :%s", client->name, cmd, channel->name, *text);

		*text = NULL;
		free_message_tags(mtags);
		// Can't return HOOK_DENY here cuz Unreal might abort() in that case :D
	}

	return HOOK_CONTINUE;
}
