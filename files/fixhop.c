/* Copyright (C) All Rights Reserved
** Written by Gottem <support@gottem.nl>
** Website: https://gottem.nl/unreal
** License: https://gottem.nl/unreal/license
*/

/*** <<<MODULE MANAGER START>>>
module {
	documentation "https://gottem.nl/unreal/man/fixhop";
	troubleshooting "In case of problems, check the FAQ at https://gottem.nl/unreal/halp or e-mail me at support@gottem.nl";
	min-unrealircd-version "5.*";
	//max-unrealircd-version "5.*";
	post-install-text {
		"The module is installed, now all you need to do is add a 'loadmodule' line to your config file:";
		"loadmodule \"third/fixhop\";";
		"Then /rehash the IRCd.";
		"For usage information, refer to the module's documentation found at: https://gottem.nl/unreal/man/fixhop";
	}
}
*** <<<MODULE MANAGER END>>>
*/

// One include for all cross-platform compatibility thangs
#include "unrealircd.h"

// Config block
#define MYCONF "fixhop"

// Commands to override
#define OVR_INVITE "INVITE"
#define OVR_MODE "MODE"

#define CheckAPIError(apistr, apiobj) \
	do { \
		if(!(apiobj)) { \
			config_error("A critical error occurred on %s for %s: %s", (apistr), MOD_HEADER.name, ModuleGetErrorStr(modinfo->handle)); \
			return MOD_FAILED; \
		} \
	} while(0)

// Quality fowod declarations
int fixhop_configtest(ConfigFile *cf, ConfigEntry *ce, int type, int *errs);
int fixhop_configrun(ConfigFile *cf, ConfigEntry *ce, int type);
int fixhop_rehash(void);
CMD_OVERRIDE_FUNC(fixhop_inviteoverride);
CMD_OVERRIDE_FUNC(fixhop_modeoverride);

// Ripped functions lol
void send_invite_list(Client *client); // From src/modules/invite.c =]

// Set config defaults here
int allowInvite = 0; // Allow hops to /invite
int disallowWidemasks = 0; // Disallow them from banning/exempting *!*@* etc
int widemaskNotif = 0; // Display notifications to hops why something was disallowed/stripped
char *disallowChmodes = NULL; // Disallow hops changing +t channel mode
int chmodeNotif = 0; // Notification to go wit it

// Dat dere module header
ModuleHeader MOD_HEADER = {
	"third/fixhop", // Module name
	"2.0.1", // Version
	"The +h access mode seems to be a little borked/limited, this module implements some tweaks for it", // Description
	"Gottem", // Author
	"unrealircd-5", // Modversion
};

// Configuration testing-related hewks go in testing phase obv
MOD_TEST() {
	// We have our own config block so we need to checkem config obv m9
	// Priorities don't really matter here
	HookAdd(modinfo->handle, HOOKTYPE_CONFIGTEST, 0, fixhop_configtest);
	return MOD_SUCCESS;
}

// Initialisation routine (register hooks, commands and modes or create structs etc)
MOD_INIT() {
	MARK_AS_GLOBAL_MODULE(modinfo);

	HookAdd(modinfo->handle, HOOKTYPE_REHASH, 0, fixhop_rehash);
	HookAdd(modinfo->handle, HOOKTYPE_CONFIGRUN, 0, fixhop_configrun);
	return MOD_SUCCESS;
}

// Actually load the module here (also command overrides as they may not exist in MOD_INIT yet)
MOD_LOAD() {
	// Since our invite override is a lil weird, it should run after all other modules have done their part :>
	CheckAPIError("CommandOverrideAddEx(INVITE)", CommandOverrideAddEx(modinfo->handle, OVR_INVITE, 99, fixhop_inviteoverride));
	CheckAPIError("CommandOverrideAddEx(MODE)", CommandOverrideAdd(modinfo->handle, OVR_MODE, fixhop_modeoverride));
	return MOD_SUCCESS; // We good
}

// Called on unload/rehash obv
MOD_UNLOAD() {
	safe_free(disallowChmodes);
	return MOD_SUCCESS; // We good
}

void send_invite_list(Client *client) {
	Link *inv;
	for(inv = client->user->invited; inv; inv = inv->next)
		sendnumeric(client, RPL_INVITELIST, inv->value.channel->chname);
	sendnumeric(client, RPL_ENDOFINVITELIST);
}

int fixhop_configtest(ConfigFile *cf, ConfigEntry *ce, int type, int *errs) {
	int errors = 0; // Error count
	int i;
	ConfigEntry *cep; // To store the current variable/value pair etc

	// Since we'll add a top-level block to unrealircd.conf, need to filter on CONFIG_MAIN lmao
	if(type != CONFIG_MAIN)
		return 0; // Returning 0 means idgaf bout dis

	// Check for valid config entries first
	if(!ce || !ce->ce_varname)
		return 0;

	// If it isn't our block, idc
	if(strcmp(ce->ce_varname, MYCONF))
		return 0;

	// Loop dat shyte fam
	for(cep = ce->ce_entries; cep; cep = cep->ce_next) {
		// Do we even have a valid name l0l?
		if(!cep->ce_varname) {
			config_error("%s:%i: blank %s item", cep->ce_fileptr->cf_filename, cep->ce_varlinenum, MYCONF); // Rep0t error
			errors++; // Increment err0r count fam
			continue; // Next iteration imo tbh
		}

		if(!strcmp(cep->ce_varname, "allow_invite") || !strcmp(cep->ce_varname, "disallow_widemasks") || !strcmp(cep->ce_varname, "widemask_notif") ||
			!strcmp(cep->ce_varname, "chmode_notif"))
			continue;

		if(!strcmp(cep->ce_varname, "disallow_chmodes")) {
			if(!cep->ce_vardata || strlen(cep->ce_vardata) < 1) {
				config_error("%s:%i: no modes specified for %s::disallow_chmodes", cep->ce_fileptr->cf_filename, cep->ce_varlinenum, MYCONF); // Rep0t error
				errors++; // Increment err0r count fam
				continue;
			}
			for(i = 0; i < strlen(cep->ce_vardata); i++) {
				if(!isalpha(cep->ce_vardata[i])) {
					config_error("%s:%i: invalid mode character '%c' in %s::disallow_chmodes", cep->ce_fileptr->cf_filename, cep->ce_varlinenum, cep->ce_vardata[i], MYCONF); // Rep0t error
					errors++;
				}
			}
			continue;
		}

		config_error_unknown(cep->ce_fileptr->cf_filename, cep->ce_varlinenum, MYCONF, cep->ce_varname); // Shit out error lol
		errors++; // Increment err0r count fam
	}

	*errs = errors;
	// Returning 1 means "all good", -1 means we shat our panties
	return errors ? -1 : 1;
}

// "Run" the config (everything should be valid at this point)
int fixhop_configrun(ConfigFile *cf, ConfigEntry *ce, int type) {
	ConfigEntry *cep; // To store the current variable/value pair etc

	// Since we'll add a top-level block to unrealircd.conf, need to filter on CONFIG_MAIN lmao
	if(type != CONFIG_MAIN)
		return 0; // Returning 0 means idgaf bout dis

	// Check for valid config entries first
	if(!ce || !ce->ce_varname)
		return 0;

	// If it isn't fixhop, idc
	if(strcmp(ce->ce_varname, MYCONF))
		return 0;

		// Loop dat shyte fam
	for(cep = ce->ce_entries; cep; cep = cep->ce_next) {
		// Do we even have a valid name l0l?
		if(!cep->ce_varname)
			continue; // Next iteration imo tbh

		if(!strcmp(cep->ce_varname, "allow_invite")) {
			allowInvite = 1;
			continue;
		}

		if(!strcmp(cep->ce_varname, "disallow_widemasks")) {
			disallowWidemasks = 1;
			continue;
		}

		if(!strcmp(cep->ce_varname, "widemask_notif")) {
			widemaskNotif = 1;
			continue;
		}

		if(!strcmp(cep->ce_varname, "disallow_chmodes") && cep->ce_vardata) {
			safe_strdup(disallowChmodes, cep->ce_vardata);
			continue;
		}

		if(!strcmp(cep->ce_varname, "chmode_notif")) {
			chmodeNotif = 1;
			continue;
		}
	}

	if(!disallowWidemasks && widemaskNotif)
		config_warn("fixhop: You've enabled widemask_notif but not disallow_widemasks"); // Rep0t warn

	if(!disallowChmodes && chmodeNotif)
		config_warn("fixhop: You've enabled chmode_notif but not disallow_chmodes"); // Rep0t warn

	return 1; // We good
}

int fixhop_rehash(void) {
	allowInvite = disallowWidemasks = widemaskNotif = chmodeNotif = 0;
	return HOOK_CONTINUE;
}

// Now for the actual override
// This function is simply ripped from src/modules/invite.c with a little adjustment
// Seems to be necessary to make this module werk pr0perly
CMD_OVERRIDE_FUNC(fixhop_inviteoverride) {
	// Gets args: CommandOverride *ovr, Client *client, MessageTag *recv_mtags, int parc, char *parv[]
	Client *target;
	Channel *channel;
	int override = 0;
	int i = 0;
	Hook *h;

	// Don't even bother if fixhop::allow_invite isn't even set
	if(!allowInvite) {
		CallCommandOverride(ovr, client, recv_mtags, parc, parv); // Run original function yo
		return;
	}

	// Mabe just pass the command back to invite.c =]
	if(parc < 3 || *parv[1] == '\0' || !(target = find_person(parv[1], NULL))) {
		CallCommandOverride(ovr, client, recv_mtags, parc, parv);
		return ;
	}

	if(MyConnect(client) && !valid_channelname(parv[2])) {
		CallCommandOverride(ovr, client, recv_mtags, parc, parv);
		return;
	}

	if(!(channel = find_channel(parv[2], NULL))) {
		CallCommandOverride(ovr, client, recv_mtags, parc, parv);
		return;
	}

	for(h = Hooks[HOOKTYPE_PRE_INVITE]; h; h = h->next) {
		i = (*(h->func.intfunc))(client, target, channel, &override);
		if(i == HOOK_DENY)
			return;
		if(i == HOOK_ALLOW)
			break;
	}

	if(!IsMember(client, channel) && !IsULine(client)) {
		if(ValidatePermissionsForPath("channel:override:invite:notinchannel", client, NULL, channel, NULL) && client == target)
			override = 1;
		else {
			sendnumeric(client, ERR_NOTONCHANNEL, parv[2]);
			return;
		}
	}

	if(IsMember(target, channel)) {
		sendnumeric(client, ERR_USERONCHANNEL, parv[1], parv[2]);
		return;
	}

	if(channel->mode.mode & MODE_INVITEONLY) {
		// Hecks
		if(!is_chan_op(client, channel) && !is_half_op(client, channel) && !IsULine(client)) {
			if(ValidatePermissionsForPath("channel:override:invite:invite-only", client, NULL, channel, NULL) && client == target)
				override = 1;
			else {
				sendnumeric(client, ERR_CHANOPRIVSNEEDED, channel->chname);
				return;
			}
		}
		else if(!IsMember(client, channel) && !IsULine(client)) {
			if(ValidatePermissionsForPath("channel:override:invite:invite-only", client, NULL, channel, NULL) && client == target)
				override = 1;
			else {
				sendnumeric(client, ERR_CHANOPRIVSNEEDED, channel->chname);
				return;
			}
		}
	}

	if(SPAMFILTER_VIRUSCHANDENY && SPAMFILTER_VIRUSCHAN && !strcasecmp(channel->chname, SPAMFILTER_VIRUSCHAN) && !is_chan_op(client, channel) && !ValidatePermissionsForPath("immune:server-ban:viruschan", client, NULL, NULL, NULL)) {
		sendnumeric(client, ERR_CHANOPRIVSNEEDED, channel->chname);
		return;
	}

	if(MyConnect(client)) {
		if(target_limit_exceeded(client, target, target->name))
			return;

		if(!ValidatePermissionsForPath("immune:invite-flood", client, NULL, NULL, NULL)) {
			if((client->user->flood.invite_t + INVITE_PERIOD) <= timeofday) {
				client->user->flood.invite_c = 0;
				client->user->flood.invite_t = timeofday;
			}
			if(client->user->flood.invite_c <= INVITE_COUNT)
				client->user->flood.invite_c++;
			if(client->user->flood.invite_c > INVITE_COUNT) {
				sendnumeric(client, RPL_TRYAGAIN, "INVITE");
				return;
			}
		}

		if(!override) {
			sendnumeric(client, RPL_INVITING, target->name, channel->chname);
			if(target->user->away)
				sendnumeric(client, RPL_AWAY, target->name, target->user->away);
		}
	}

	/* Send OperOverride messages */
	if(override && MyConnect(target)) {
		if(is_banned(client, channel, BANCHK_JOIN, NULL, NULL)) {
			sendto_snomask_global(SNO_EYES, "*** OperOverride -- %s (%s@%s) invited him/herself into %s (overriding +b).", client->name, client->user->username, client->user->realhost, channel->chname);

			/* Logging implementation added by XeRXeS */
			ircd_log(LOG_OVERRIDE,"OVERRIDE: %s (%s@%s) invited him/herself into %s (Overriding Ban).", client->name, client->user->username, client->user->realhost, channel->chname);
		}
		else if(channel->mode.mode & MODE_INVITEONLY) {
			sendto_snomask_global(SNO_EYES, "*** OperOverride -- %s (%s@%s) invited him/herself into %s (overriding +i).", client->name, client->user->username, client->user->realhost, channel->chname);

			/* Logging implementation added by XeRXeS */
			ircd_log(LOG_OVERRIDE,"OVERRIDE: %s (%s@%s) invited him/herself into %s (Overriding Invite Only)", client->name, client->user->username, client->user->realhost, channel->chname);
		}
		else if(channel->mode.limit) {
			sendto_snomask_global(SNO_EYES, "*** OperOverride -- %s (%s@%s) invited him/herself into %s (overriding +l).", client->name, client->user->username, client->user->realhost, channel->chname);

			/* Logging implementation added by XeRXeS */
			ircd_log(LOG_OVERRIDE,"OVERRIDE: %s (%s@%s) invited him/herself into %s (Overriding Limit)", client->name, client->user->username, client->user->realhost, channel->chname);
		}

		else if(*channel->mode.key) {
			sendto_snomask_global(SNO_EYES, "*** OperOverride -- %s (%s@%s) invited him/herself into %s (overriding +k).", client->name, client->user->username, client->user->realhost, channel->chname);

			/* Logging implementation added by XeRXeS */
			ircd_log(LOG_OVERRIDE,"OVERRIDE: %s (%s@%s) invited him/herself into %s (Overriding Key)", client->name, client->user->username, client->user->realhost, channel->chname);
		}
		else if(has_channel_mode(channel, 'z')) {
			sendto_snomask_global(SNO_EYES, "*** OperOverride -- %s (%s@%s) invited him/herself into %s (overriding +z).", client->name, client->user->username, client->user->realhost, channel->chname);

			/* Logging implementation added by XeRXeS */
			ircd_log(LOG_OVERRIDE,"OVERRIDE: %s (%s@%s) invited him/herself into %s (Overriding SSL/TLS-Only)", client->name, client->user->username, client->user->realhost, channel->chname);
		}
	#ifdef OPEROVERRIDE_VERIFY
		else if(channel->mode.mode & MODE_SECRET || channel->mode.mode & MODE_PRIVATE)
			override = -1;
	#endif
		else
			return;
	}

	if(MyConnect(target)) {
		// Hecks
		if(IsUser(client) && (is_chan_op(client, channel) || is_half_op(client, channel) || IsULine(client) || ValidatePermissionsForPath("channel:override:invite:self", client, NULL, channel, NULL))) {
			MessageTag *mtags = NULL;

			new_message(&me, NULL, &mtags);
			if(override == 1)
				sendto_channel(channel, &me, NULL, PREFIX_OP|PREFIX_ADMIN|PREFIX_OWNER, 0, SEND_ALL, mtags, ":%s NOTICE @%s :OperOverride -- %s invited him/herself into the channel.", me.name, channel->chname, client->name);
			else if(override == 0) {
				sendto_channel(channel, &me, NULL, PREFIX_OP|PREFIX_ADMIN|PREFIX_OWNER, 0, SEND_ALL, mtags, ":%s NOTICE @%s :%s invited %s into the channel.", me.name, channel->chname, client->name, target->name);
			}
			add_invite(client, target, channel, mtags);
			free_message_tags(mtags);
		}
	}

	/* Notify the person who got invited */
	if(!is_silenced(client, target))
		sendto_prefix_one(target, client, NULL, ":%s INVITE %s :%s", client->name, target->name, channel->chname);
}

// Hecks
CMD_OVERRIDE_FUNC(fixhop_modeoverride) {
	Channel *channel; // Channel pointer
	int fc, mc, cc, pc; // Flag count, mask count and char counters respectively
	int i, j; // Just s0em iterators fam
	int stripped_wide; // Count 'em
	int stripped_disallowed; // Count 'em
	int skip[MAXPARA + 1]; // Skippem
	int newparc; // Keep track of proper param count
	int cont, dironly;
	char newflags[MODEBUFLEN + 3]; // Store cleaned up flags
	char *newparv[MAXPARA + 1]; // Ditto for masks etc
	char c; // Current flag lol, can be '+', '-' or any letturchar
	char curdir; // Current direction (add/del etc)
	char *p; // To check the non-wildcard length shit
	char *mask; // Store "cleaned" ban mask

	// Need to be at least hops or higher on a channel for this to kicc in obv (or U-Line, to prevent bypassing this module with '/cs mode')
	if(!MyUser(client) || IsOper(client) || (!disallowWidemasks && !disallowChmodes) || parc < 2|| !(channel = find_channel(parv[1], NULL)) || !(is_half_op(client, channel) || IsULine(client))) {
		CallCommandOverride(ovr, client, recv_mtags, parc, parv); // Run original function yo
		return;
	}

	newparc = 3; // Initialise new param count, starting at 3 lol (MODE #chan +something)
	fc = mc = cc = pc = 0; // Ditto for em other counters
	curdir = '+'; // Set "direction" (+ or -)
	stripped_wide = stripped_disallowed = 0;
	newparv[0] = parv[0];
	newparv[1] = parv[1];
	memset(newflags, '\0', sizeof(newflags)); // Set 'em
	for(i = 0; i < parc; i++)
		skip[i] = 0; // Set default so the loop doesn't fuck it up as it goes along

	// Loop over every mode flag
	for(i = 0; i < strlen(parv[2]); i++) {
		c = parv[2][i];
		mask = NULL; // Aye elemao
		cont = 0;

		// Check for disallowed modes early
		if(disallowChmodes && strchr(disallowChmodes, c)) {
			stripped_disallowed++; // increment counter

			// Some modes take an argument
			if(strchr("beIvhoaqfkLl", c)) {
				fc++;
				j = mc + 3;
				if(parc <= j || BadPtr(parv[j])) {
					if(fc > 1)
						cont = 1;
				}
				else {
					mc++;
					skip[j] = 1;
				}
				if(!cont)
					newflags[cc++] = c;
			}
			continue;
		}

		// Check if we need to verify somethang
		switch(c) {
			// El list stuff
			case 'b': // Ban
			case 'e': // Ban exempts
			case 'I': // Invite exempts
				fc++;
				j = mc + 3; // In parv[] the first mask is found at index 3
				if(parc <= j || BadPtr(parv[j])) {
					if(fc > 1) // Skip this flag entirely so we don't fall through to listing ban masks in case of shit like '+bb foo!bar@ke.ks'
						cont = 1;
					break;
				}
				mc++;
				newparc++;

				if(!disallowWidemasks)
					break;

				// On error getting dis, just let CallCommandOverride handle it
				mask = clean_ban_mask(parv[j], (curdir == '+' ? MODE_ADD : MODE_DEL), client); // Turns "+b *" into "+b *!*@*" so we can easily check bel0w =]
				if(!mask)
					break;

				// Need at least 4 non-wildcard chars
				pc = 0;
				for(p = mask; *p; p++) {
					if(*p != '*' && *p != '.' && *p != '?')
						pc++;
				}

				// Does it literally match a ban-all mask or is it simply too wide?
				// Or do we have too few non-wildcard chars?
				if(!strcmp(mask, "*!*@*") || !strcmp(mask, "*") || strstr(mask, "@*") || pc < 4) {
					skip[j] = 1; // Skip it lol
					newparc--; // Decrement parc again so Unreal doesn't shit itself =]
					cont = 1;
					stripped_wide++;
				}
				break;

			// Some other modes may have an argument t00
			case 'v': // Access mode, voice
			case 'h': // Hops
			case 'o': // Ops
			case 'a': // Chanadmin
			case 'q': // Chanowner
			case 'f': // Floodprot
			case 'k': // Channel key
			case 'L': // Channel link
			case 'l': // Limit
			case 'j': // Kickjoindelay
			case 'J': // Joinmute
			case 'H': // History
				fc++;
				j = mc + 3;
				if(parc <= j || BadPtr(parv[j])) {
					if(fc > 1)
						cont = 1;
					break;
				}
				mc++;
				newparc++;
				break;

			// Directionals yo
			case '+':
			case '-':
				curdir = c;
				break;

			// Fuck errythang else lol
			default:
				fc++;
				break;
		}

		if(cont)
			continue;
		newflags[cc++] = c; // Seems to be a sane mode, append it
	}

	// Correct parv count due to possibly (now) missing flags/masks lol
	dironly = 1;
	for(i = 0; i < cc && newflags[i]; i++) {
		if(!strchr("+-", newflags[i])) {
			dironly = 0;
			break;
		}
	}
	if(!cc || dironly) {
		newparc--;
		memset(newflags, '\0', sizeof(newflags)); // Reset 'em
	}

	// Now checkem masks, we have to do this separately so we can reliably get the (proper) corresponding mask
	for(i = 3, j = 3; i < parc && j < newparc && !BadPtr(parv[i]); i++) {
		if(skip[i] || !strlen(parv[i]))
			continue;

		// Now store this mask =]
		newparv[j++] = parv[i];
	}

	if(stripped_wide && widemaskNotif)
		sendto_one(client, NULL, ":%s NOTICE %s :[FH] Stripped %d mask(s) (too wide)", me.name, channel->chname, stripped_wide);

	if(stripped_disallowed && chmodeNotif)
		sendto_one(client, NULL, ":%s NOTICE %s :[FH] Stripped %d modes (disallowed)", me.name, channel->chname, stripped_disallowed);

	// Nothing left, don't even bother passing it back =]
	if(!newflags[0])
		return;

	// Heck 'em
	newparv[2] = newflags;
	if(newparc <= MAXPARA)
		newparv[newparc] = NULL;
	else
		newparv[MAXPARA] = NULL;
	CallCommandOverride(ovr, client, recv_mtags, newparc, newparv); // Run original function yo
}
