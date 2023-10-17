/* Copyright (C) All Rights Reserved
** Written by Gottem <support@gottem.nl>
** Website: https://gottem.nl/unreal
** License: https://gottem.nl/unreal/license
*/

/*** <<<MODULE MANAGER START>>>
module {
	documentation "https://gottem.nl/unreal/man/fixhop";
	troubleshooting "In case of problems, check the FAQ at https://gottem.nl/unreal/halp or e-mail me at support@gottem.nl";
	min-unrealircd-version "6.*";
	//max-unrealircd-version "6.*";
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

#define CLIENT_INVITES(client) (moddata_local_client(client, userInvitesMD).ptr)
#define CHANNEL_INVITES(channel) (moddata_channel(channel, channelInvitesMD).ptr)

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
int is_chmode_denied(char mode, char direction);
CMD_OVERRIDE_FUNC(fixhop_inviteoverride);
CMD_OVERRIDE_FUNC(fixhop_modeoverride);

// Ripped functions (from src/modules/invite.c) =]
void add_invite(Client *from, Client *to, Channel *channel, MessageTag *mtags);
void del_invite(Client *client, Channel *channel);

ModDataInfo *userInvitesMD, *channelInvitesMD;

// Set config defaults here
int allowInvite = 0; // Allow hops to /invite
int denyWidemasks = 0; // Prevent them from banning/exempting *!*@* etc
int widemaskNotif = 0; // Display notifications to hops why something was denied/stripped
char *denyChmodes = NULL; // Prevent hops changing certain channel modes
int chmodeNotif = 0; // Notification to go wit it

// Dat dere module header
ModuleHeader MOD_HEADER = {
	"third/fixhop", // Module name
	"2.3.2", // Version
	"The +h access mode seems to be a little borked/limited, this module implements some tweaks for it", // Description
	"Gottem", // Author
	"unrealircd-6", // Modversion
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
	if(!(userInvitesMD = findmoddata_byname("invite", MODDATATYPE_LOCAL_CLIENT))) {
		config_error("A critical error occurred for %s: unable to find client moddata for the 'invite' module", MOD_HEADER.name);
		return MOD_FAILED;
	}

	if(!(channelInvitesMD = findmoddata_byname("invite", MODDATATYPE_CHANNEL))) {
		config_error("A critical error occurred for %s: unable to find channel moddata for the 'invite' module", MOD_HEADER.name);
		return MOD_FAILED;
	}

	// Since our invite override is a lil weird, it should run after all other modules have done their part :>
	CheckAPIError("CommandOverrideAdd(INVITE)", CommandOverrideAdd(modinfo->handle, OVR_INVITE, 99, fixhop_inviteoverride));
	CheckAPIError("CommandOverrideAdd(MODE)", CommandOverrideAdd(modinfo->handle, OVR_MODE, 0, fixhop_modeoverride));
	return MOD_SUCCESS; // We good
}

// Called on unload/rehash obv
MOD_UNLOAD() {
	safe_free(denyChmodes);
	return MOD_SUCCESS; // We good
}

int fixhop_configtest(ConfigFile *cf, ConfigEntry *ce, int type, int *errs) {
	int errors = 0; // Error count
	int i;
	ConfigEntry *cep; // To store the current variable/value pair etc

	// Since we'll add a top-level block to unrealircd.conf, need to filter on CONFIG_MAIN lmao
	if(type != CONFIG_MAIN)
		return 0; // Returning 0 means idgaf bout dis

	// Check for valid config entries first
	if(!ce || !ce->name)
		return 0;

	// If it isn't our block, idc
	if(strcmp(ce->name, MYCONF))
		return 0;

	// Loop dat shyte fam
	for(cep = ce->items; cep; cep = cep->next) {
		// Do we even have a valid name l0l?
		if(!cep->name) {
			config_error("%s:%i: blank %s item", cep->file->filename, cep->line_number, MYCONF); // Rep0t error
			errors++; // Increment err0r count fam
			continue; // Next iteration imo tbh
		}

		if(!strcmp(cep->name, "allow_invite") || !strcmp(cep->name, "deny_widemasks") || !strcmp(cep->name, "widemask_notif") ||
			!strcmp(cep->name, "chmode_notif"))
			continue;

		if(!strcmp(cep->name, "deny_chmodes")) {
			if(!cep->value || strlen(cep->value) < 1) {
				config_error("%s:%i: no modes specified for %s::deny_chmodes", cep->file->filename, cep->line_number, MYCONF); // Rep0t error
				errors++; // Increment err0r count fam
				continue;
			}
			for(i = 0; i < strlen(cep->value); i++) {
				if(!isalpha(cep->value[i]) && !strchr("+-", cep->value[i])) {
					config_error("%s:%i: invalid mode character '%c' in %s::deny_chmodes", cep->file->filename, cep->line_number, cep->value[i], MYCONF); // Rep0t error
					errors++;
				}
			}
			continue;
		}

		config_error_unknown(cep->file->filename, cep->line_number, MYCONF, cep->name); // Shit out error lol
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
	if(!ce || !ce->name)
		return 0;

	// If it isn't fixhop, idc
	if(strcmp(ce->name, MYCONF))
		return 0;

		// Loop dat shyte fam
	for(cep = ce->items; cep; cep = cep->next) {
		// Do we even have a valid name l0l?
		if(!cep->name)
			continue; // Next iteration imo tbh

		if(!strcmp(cep->name, "allow_invite")) {
			allowInvite = 1;
			continue;
		}

		if(!strcmp(cep->name, "deny_widemasks")) {
			denyWidemasks = 1;
			continue;
		}

		if(!strcmp(cep->name, "widemask_notif")) {
			widemaskNotif = 1;
			continue;
		}

		if(!strcmp(cep->name, "deny_chmodes") && cep->value) {
			safe_strdup(denyChmodes, cep->value);
			continue;
		}

		if(!strcmp(cep->name, "chmode_notif")) {
			chmodeNotif = 1;
			continue;
		}
	}

	if(!denyWidemasks && widemaskNotif)
		config_warn("[fixhop] You've enabled widemask_notif but not deny_widemasks"); // Rep0t warn

	if(!denyChmodes && chmodeNotif)
		config_warn("[fixhop] You've enabled chmode_notif but not deny_chmodes"); // Rep0t warn

	return 1; // We good
}

int fixhop_rehash(void) {
	allowInvite = denyWidemasks = widemaskNotif = chmodeNotif = 0;
	return HOOK_CONTINUE;
}

int is_chmode_denied(char mode, char direction) {
	char curdir;
	char *p;

	if(!denyChmodes || !strchr(denyChmodes, mode))
		return 0;

	curdir = 0;
	for(p = denyChmodes; *p; p++) {
		if(strchr("+-", *p)) {
			curdir = *p;
			continue;
		}

		if((curdir == 0 || curdir == direction) && *p == mode)
			return 1;
	}

	return 0;
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
	if(parc < 3 || *parv[1] == '\0' || !(target = find_user(parv[1], NULL))) {
		CallCommandOverride(ovr, client, recv_mtags, parc, parv);
		return ;
	}

	if(MyConnect(client) && !valid_channelname(parv[2])) {
		CallCommandOverride(ovr, client, recv_mtags, parc, parv);
		return;
	}

	if(!(channel = find_channel(parv[2]))) {
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

	if(has_channel_mode(channel, 'i')) {
		// Hecks
		if(!check_channel_access(client, channel, "ho") && !IsULine(client)) {
			if(ValidatePermissionsForPath("channel:override:invite:invite-only", client, NULL, channel, NULL) && client == target)
				override = 1;
			else {
				sendnumeric(client, ERR_CHANOPRIVSNEEDED, channel->name);
				return;
			}
		}
		else if(!IsMember(client, channel) && !IsULine(client)) {
			if(ValidatePermissionsForPath("channel:override:invite:invite-only", client, NULL, channel, NULL) && client == target)
				override = 1;
			else {
				sendnumeric(client, ERR_CHANOPRIVSNEEDED, channel->name);
				return;
			}
		}
	}

	if(SPAMFILTER_VIRUSCHANDENY && SPAMFILTER_VIRUSCHAN && !strcasecmp(channel->name, SPAMFILTER_VIRUSCHAN) && !check_channel_access(client, channel, "o") && !ValidatePermissionsForPath("immune:server-ban:viruschan", client, NULL, NULL, NULL)) {
		sendnumeric(client, ERR_CHANOPRIVSNEEDED, channel->name);
		return;
	}

	if(MyUser(client)) {
		if(target_limit_exceeded(client, target, target->name))
			return;

		if(!ValidatePermissionsForPath("immune:invite-flood", client, NULL, NULL, NULL) && flood_limit_exceeded(client, FLD_INVITE)) {
			sendnumeric(client, RPL_TRYAGAIN, "INVITE");
			return;
		}

		if(!override) {
			sendnumeric(client, RPL_INVITING, target->name, channel->name);
			if(target->user->away)
				sendnumeric(client, RPL_AWAY, target->name, target->user->away);
		}
	}

	/* Send OperOverride messages */
	if(override && MyConnect(target)) {
		if(is_banned(client, channel, BANCHK_JOIN, NULL, NULL)) {
			unreal_log(ULOG_INFO, "operoverride", "OPEROVERRIDE_INVITE", client, "OperOverride -- $client.details invited themself into $channel (overriding +b).",
				log_data_channel("channel", channel)
			);
		}
		else if(has_channel_mode(channel, 'i')) {
			unreal_log(ULOG_INFO, "operoverride", "OPEROVERRIDE_INVITE", client, "OperOverride -- $client.details invited themself into $channel (overriding +i).",
				log_data_channel("channel", channel)
			);
		}
		else if(has_channel_mode(channel, 'l')) {
			unreal_log(ULOG_INFO, "operoverride", "OPEROVERRIDE_INVITE", client, "OperOverride -- $client.details invited themself into $channel (overriding +l).",
				log_data_channel("channel", channel)
			);
		}

		else if(has_channel_mode(channel, 'k')) {
			unreal_log(ULOG_INFO, "operoverride", "OPEROVERRIDE_INVITE", client, "OperOverride -- $client.details invited themself into $channel (overriding +k).",
				log_data_channel("channel", channel)
			);
		}
		else if(has_channel_mode(channel, 'z')) {
			unreal_log(ULOG_INFO, "operoverride", "OPEROVERRIDE_INVITE", client, "OperOverride -- $client.details invited themself into $channel (overriding +z).",
				log_data_channel("channel", channel)
			);
		}
	#ifdef OPEROVERRIDE_VERIFY
		else if(has_channel_mode(channel, 's') || has_channel_mode(channel, 'p'))
			override = -1;
	#endif
		else
			return;
	}

	if(MyConnect(target)) {
		// Hecks
		if(IsUser(client) && (check_channel_access(client, channel, "ho") || IsULine(client) || ValidatePermissionsForPath("channel:override:invite:self", client, NULL, channel, NULL))) {
			MessageTag *mtags = NULL;

			new_message(&me, NULL, &mtags);
			if(override == 1)
				sendto_channel(channel, &me, NULL, "oaq", 0, SEND_ALL, mtags, ":%s NOTICE @%s :OperOverride -- %s invited themself into the channel.", me.name, channel->name, client->name);
			else if(override == 0) {
				sendto_channel(channel, &me, NULL, "oaq", 0, SEND_ALL, mtags, ":%s NOTICE @%s :%s invited %s into the channel.", me.name, channel->name, client->name, target->name);
			}
			add_invite(client, target, channel, mtags);
			free_message_tags(mtags);
		}
	}

	/* Notify the person who got invited */
	if(!is_silenced(client, target))
		sendto_prefix_one(target, client, NULL, ":%s INVITE %s :%s", client->name, target->name, channel->name);
}

// Hecks
CMD_OVERRIDE_FUNC(fixhop_modeoverride) {
	Channel *channel; // Channel pointer
	int fc, mc, cc, pc; // Flag count, mask count and char counters respectively
	int i, j; // Just s0em iterators fam
	int stripped_wide; // Count 'em
	int stripped_deny; // Count 'em
	int skip[MAXPARA + 1]; // Skippem
	int newparc; // Keep track of proper param count
	int cont, dironly;
	char newflags[MODEBUFLEN + 3]; // Store cleaned up flags
	const char *newparv[MAXPARA + 1]; // Ditto for masks etc
	char c; // Current character lol, can be '+', '-' or any lettur
	char curdir; // Current direction (add/del etc)
	const char *p; // To check the non-wildcard length shit
	const char *mask; // Store "cleaned" ban mask
	Cmode *chanmode;
	int chanmode_max;

	// May not be anything to do =]
	if((!denyWidemasks && !denyChmodes) || !MyUser(client) || IsOper(client) || parc < 3) {
		CallCommandOverride(ovr, client, recv_mtags, parc, parv); // Run original function yo
		return;
	}

	// You need to have hops on a channel for this to kicc in obv (or U-Line, to prevent bypassing this module with '/cs mode')
	if(!(channel = find_channel(parv[1])) || !(check_channel_access(client, channel, "h") || IsULine(client))) {
		CallCommandOverride(ovr, client, recv_mtags, parc, parv); // Run original function yo
		return;
	}

	newparc = 3; // Initialise new param count, starting at 3 lol (MODE #chan +something)
	fc = mc = cc = pc = 0; // Ditto for em other counters
	curdir = '+'; // Set "direction" ('+' or '-')
	stripped_wide = stripped_deny = 0;
	newparv[0] = parv[0];
	newparv[1] = parv[1];
	memset(newflags, '\0', sizeof(newflags)); // Set 'em
	memset(&skip, 0, sizeof(skip));

	// Loop over every mode character
	for(i = 0; i < strlen(parv[2]); i++) {
		c = parv[2][i];
		mask = NULL; // Aye elemao
		cont = 0;

		// Since we check for denied modes early we also need to know the "direction" early
		if(strchr("+-", c)) {
			curdir = c;
			newflags[cc++] = c;
			continue;
		}

		if(is_chmode_denied(c, curdir)) {
			stripped_deny++; // Increment counter

			// Some modes take an argument, let's skip that too so we don't fall through to listing ban masks in case of shit like '+bb foo!bar@ke.ks'
			if(strchr("beIvhoaqfkLljJH", c)) {
				fc++;
				j = mc + 3; // In parv[] the first mask is found at index 3
				if(parc > j && !BadPtr(parv[j])) {
					mc++;
					skip[j] = 1;
				}
			}
			continue;
		}

		// Check if we need to verify somethang else
		switch(c) {
			// El list stuff
			case 'b': // Ban
			case 'e': // Ban exempts
			case 'I': // Invite exempts
				fc++;
				j = mc + 3;
				if(parc <= j || BadPtr(parv[j])) {
					if(fc > 1)
						cont = 1;
					break;
				}
				mc++;
				newparc++;

				if(!denyWidemasks)
					break;

				// On error getting dis, just let CallCommandOverride handle it
				mask = clean_ban_mask(parv[j], (curdir == '+' ? MODE_ADD : MODE_DEL), client, 0); // Turns "+b *" into "+b *!*@*" so we can easily check bel0w =]
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

			// Let's also account for ne third-party m0ds we don't know about, which should always have ->paracount set I think
			default:
				fc++;
				chanmode = find_channel_mode_handler(c);
				if(!chanmode || !chanmode->paracount || (!chanmode->unset_with_param && curdir == '-'))
					break;

				j = mc + 3;
				chanmode_max = j + chanmode->paracount;
				while(j < chanmode_max) {
					if(parc <= j || BadPtr(parv[j])) {
						if(fc > 1)
							cont = 1;
						break;
					}
					mc++;
					newparc++;
					j++;
				}
				break;
		}

		if(cont)
			continue;
		newflags[cc++] = c; // Seems to be a sane character, append it
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

	if(stripped_deny && chmodeNotif)
		sendto_one(client, NULL, ":%s NOTICE %s :[FH] Stripped %d mode(s) (denied)", me.name, channel->name, stripped_deny);

	if(stripped_wide && widemaskNotif)
		sendto_one(client, NULL, ":%s NOTICE %s :[FH] Stripped %d mask(s) (too wide)", me.name, channel->name, stripped_wide);

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

void add_invite(Client *from, Client *to, Channel *channel, MessageTag *mtags) {
	Link *inv, *tmp;

	// This bit is not ripped from the original, but essa some backwards compatibility shit =]
	int maxchans;
#ifdef MAXCHANNELSPERUSER // Older versions
	maxchans = MAXCHANNELSPERUSER;
#else
	maxchans = get_setting_for_user_number(from, SET_MAX_CHANNELS_PER_USER);
#endif

	del_invite(to, channel);
	/* If too many invite entries then delete the oldest one */
	if(link_list_length(CLIENT_INVITES(to)) >= maxchans) {
		for(tmp = CLIENT_INVITES(to); tmp->next; tmp = tmp->next)
			;
		del_invite(to, tmp->value.channel);

	}

	/* We get pissy over too many invites per channel as well now,
	 * since otherwise mass-inviters could take up some major
	 * resources -Donwulff
	 */
	if(link_list_length(CHANNEL_INVITES(channel)) >= maxchans) {
		for(tmp = CHANNEL_INVITES(channel); tmp->next; tmp = tmp->next)
			;
		del_invite(tmp->value.client, channel);
	}
	/*
	 * add client to the beginning of the channel invite list
	 */
	inv = make_link();
	inv->value.client = to;
	inv->next = CHANNEL_INVITES(channel);
	CHANNEL_INVITES(channel) = inv;
	/*
	 * add channel to the beginning of the client invite list
	 */
	inv = make_link();
	inv->value.channel = channel;
	inv->next = CLIENT_INVITES(to);
	CLIENT_INVITES(to) = inv;

	RunHook(HOOKTYPE_INVITE, from, to, channel, mtags);
}

void del_invite(Client *client, Channel *channel) {
	Link **inv, *tmp;

	for(inv = (Link **)&CHANNEL_INVITES(channel); (tmp = *inv); inv = &tmp->next) {
		if(tmp->value.client == client) {
			*inv = tmp->next;
			free_link(tmp);
			break;
		}
	}

	for(inv = (Link **)&CLIENT_INVITES(client); (tmp = *inv); inv = &tmp->next) {
		if(tmp->value.channel == channel) {
			*inv = tmp->next;
			free_link(tmp);
			break;
		}
	}
}
