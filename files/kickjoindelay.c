/* Copyright (C) All Rights Reserved
** Written by Gottem <support@gottem.nl>
** Website: https://gottem.nl/unreal
** License: https://gottem.nl/unreal/license
*/

/*** <<<MODULE MANAGER START>>>
module {
	documentation "https://gottem.nl/unreal/man/kickjoindelay";
	troubleshooting "In case of problems, check the FAQ at https://gottem.nl/unreal/halp or e-mail me at support@gottem.nl";
	min-unrealircd-version "6.*";
	//max-unrealircd-version "6.*";
	post-install-text {
		"The module is installed, now all you need to do is add a 'loadmodule' line to your config file:";
		"loadmodule \"third/kickjoindelay\";";
		"Then /rehash the IRCd.";
		"For usage information, refer to the module's documentation found at: https://gottem.nl/unreal/man/kickjoindelay";
	}
}
*** <<<MODULE MANAGER END>>>
*/

// One include for all cross-platform compatibility thangs
#include "unrealircd.h"

// Channel mode to add
#define CHMODE_FLAG 'j'

#define CheckAPIError(apistr, apiobj) \
	do { \
		if(!(apiobj)) { \
			config_error("A critical error occurred on %s for %s: %s", (apistr), MOD_HEADER.name, ModuleGetErrorStr(modinfo->handle)); \
			return MOD_FAILED; \
		} \
	} while(0)

// Custom mode struct, contains the parameters and possibly other info
typedef struct {
	char flag;
	int p;
} aModej;

typedef struct t_kickTimer kickTimer;
struct t_kickTimer {
	kickTimer *prev, *next;
	Channel *channel;
	time_t lastkick;
};

// Quality fowod declarations
int kickjoindelay_hook_kick(Client *client, Client *victim, Channel *channel, MessageTag *mtags, const char *comment);
int kickjoindelay_hook_pre_localjoin(Client *client, Channel *channel, const char *key);

int kickjoindelay_chmode_isok(Client *client, Channel *channel, char mode, const char *para, int checkt, int what);
void *kickjoindelay_chmode_put_param(void *data, const char *para);
const char *kickjoindelay_chmode_conv_param(const char *para, Client *client, Channel *channel);
const char *kickjoindelay_chmode_get_param(void *data);
void kickjoindelay_chmode_free_param(void *data);
void *kickjoindelay_chmode_dup_struct(void *src);
int kickjoindelay_chmode_sjoin_check(Channel *channel, void *ourx, void *theirx);
void kickjoindelay_md_free(ModData *md);
EVENT(kickjoindelay_event);

Cmode_t extcmode_kickjoindelay = 0L; // Store bitwise value latur
ModDataInfo *kickjoinMDI = NULL; // Persistent st0rage for kick timers

// Dat dere module header
ModuleHeader MOD_HEADER = {
	"third/kickjoindelay", // Module name
	"2.2.0", // Version
	"Chanmode +j to prevent people from rejoining too fast after a kick", // Description
	"Gottem", // Author
	"unrealircd-6", // Modversion
};

// Initialisation routine (register hooks, commands and modes or create structs etc)
MOD_INIT() {
	// Request the module storage thing ;]
	ModDataInfo mreq;
	memset(&mreq, 0, sizeof(mreq));
	mreq.name = "kickjoindelay";
	mreq.type = MODDATATYPE_LOCAL_CLIENT;
	mreq.free = kickjoindelay_md_free;
	kickjoinMDI = ModDataAdd(modinfo->handle, mreq);
	CheckAPIError("ModDataAdd(kickjoindelay)", kickjoinMDI);

	// Request the mode flag
	CmodeInfo cmodereq;
	memset(&cmodereq, 0, sizeof(cmodereq));
	cmodereq.letter = CHMODE_FLAG; // Flag yo
	cmodereq.paracount = 1; // How many params?
	cmodereq.is_ok = kickjoindelay_chmode_isok; // Custom verification function
	cmodereq.conv_param = kickjoindelay_chmode_conv_param; // Transform the parameter(s) if necessary (i.e. +j a1 becomes +j 1)
	cmodereq.put_param = kickjoindelay_chmode_put_param; // Store the param(s) in our struct
	cmodereq.get_param = kickjoindelay_chmode_get_param; // Retrieve it as a char *
	cmodereq.free_param = kickjoindelay_chmode_free_param; // In case of unsetting the mode, free that shit
	cmodereq.dup_struct = kickjoindelay_chmode_dup_struct; // Duplicate the struct, seems to be necessary ;]
	cmodereq.sjoin_check = kickjoindelay_chmode_sjoin_check; // During server synchronisations, check for possibru conflicts
	CheckAPIError("CmodeAdd(extcmode_kickjoindelay)", CmodeAdd(modinfo->handle, cmodereq, &extcmode_kickjoindelay));

	CheckAPIError("EventAdd(kickjoindelay_event)", EventAdd(modinfo->handle, "kickjoindelay_event", kickjoindelay_event, NULL, 115000, 0));

	MARK_AS_GLOBAL_MODULE(modinfo);

	HookAdd(modinfo->handle, HOOKTYPE_LOCAL_KICK, 0, kickjoindelay_hook_kick); // Muh hook
	HookAdd(modinfo->handle, HOOKTYPE_REMOTE_KICK, 0, kickjoindelay_hook_kick); // Muh hook
	HookAdd(modinfo->handle, HOOKTYPE_PRE_LOCAL_JOIN, 0, kickjoindelay_hook_pre_localjoin); // Muh hook
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

// Simply hook into KICK to get a timestamp lol
int kickjoindelay_hook_kick(Client *client, Client *victim, Channel *channel, MessageTag *mtags, const char *comment) {
	kickTimer *cur = NULL;
	kickTimer *newTimer = NULL;
	kickTimer *chanTimers = NULL;
	const char *delayParam = NULL;

	if(!MyUser(victim))
		return HOOK_CONTINUE;

	if(has_channel_mode(channel, CHMODE_FLAG))
		delayParam = cm_getparameter(channel, CHMODE_FLAG);

	if(!delayParam)
		return HOOK_CONTINUE;

	if((chanTimers = moddata_local_client(victim, kickjoinMDI).ptr)) {
		for(cur = chanTimers; cur; cur = cur->next) {
			if(cur->channel == channel)
				break;
		}
	}

	if(!cur) {
		if(!chanTimers) {
			chanTimers = safe_alloc(sizeof(kickTimer));
			chanTimers->channel = channel;
			chanTimers->lastkick = TStime();
		}
		else {
			newTimer = safe_alloc(sizeof(kickTimer));
			newTimer->channel = channel;
			newTimer->lastkick = TStime();
			AddListItem(newTimer, chanTimers);
		}
	}
	else {
		cur->lastkick = TStime();
	}

	moddata_local_client(victim, kickjoinMDI).ptr = chanTimers;
	return HOOK_CONTINUE;
}

// Now for the actual JOIN prevention thingy
int kickjoindelay_hook_pre_localjoin(Client *client, Channel *channel, const char *key) {
	/* Args:
	** client: Pointer to user executing command
	** channel: Pointer to channel struct
	** parv: Contains the args, like a key
	*/
	char out[256]; // For a pretty error string
	const char *delayParam = NULL; // A char * variant of the chmode parameter
	int delay; // The matching int value
	kickTimer *cur = NULL; // For iter8ion m9
	kickTimer *chanTimers; // To store moddata shit

	// Check if the target channel even has the mode set ;]
	if(has_channel_mode(channel, CHMODE_FLAG))
		delayParam = cm_getparameter(channel, CHMODE_FLAG);

	// Just in case lol
	if(!delayParam || !IsUser(client) || IsOper(client) || IsULine(client))
		return HOOK_CONTINUE;

	// Attempt to get moddata
	if((chanTimers = moddata_local_client(client, kickjoinMDI).ptr)) {
		// Find the appropriate Client in it
		for(cur = chanTimers; cur; cur = cur->next) {
			if(cur->channel == channel)
				break;
		}
	}

	// If the loop above got broken, client was kicked at some point
	if(cur) {
		delay = atoi(delayParam); // Convert 'em
		// Just a sanity check for delay, also checkem time diff
		if(delay > 0 && TStime() - cur->lastkick <= delay) {
			ircsnprintf(out, sizeof(out), "You have to wait at least %d seconds before rejoining after a kick", delay);
			sendnumeric(client, ERR_CANNOTSENDTOCHAN, channel->name, out, channel->name); // Send error
			return HOOK_DENY; // Deny 'em
		}
	}

	return HOOK_CONTINUE; // No hit found or is a phre$h af user fam
}

int kickjoindelay_chmode_isok(Client *client, Channel *channel, char mode, const char *para, int checkt, int what) {
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
	if((checkt == EXCHK_ACCESS) || (checkt == EXCHK_ACCESS_ERR)) {
		// Check if the user has at least chanops status
		if(!check_channel_access(client, channel, "oaq")) {
			if(checkt == EXCHK_ACCESS_ERR)
				sendnumeric(client, ERR_CHANOPRIVSNEEDED, channel->name);
			return EX_DENY;
		}
		return EX_ALLOW;
	}
	else if(checkt == EXCHK_PARAM) {
		// A delay above 20 might be a bit much, also check muh sanity
		int v = atoi(para);
		if((v < 1) || (v > 20)) {
			sendnotice(client, "Channel mode +%c: ERROR: Expected a value between 1 and 20", CHMODE_FLAG);
			return EX_DENY;
		}
		return EX_ALLOW;
	}

	return EX_ALLOW; // Fallthrough, normally never reached
}

const char *kickjoindelay_chmode_conv_param(const char *para, Client *client, Channel *channel) {
	/* Args:
	** para: Parameters for the chmode
	** client: Client who issues the MODE change
	*/
	static char convbuf[32]; // Store the conversion result
	int p = atoi(para); // Attempt to convert to integer
	if(p < 1)
		p = 1;
	if(p > 20)
		p = 20;
	ircsnprintf(convbuf, sizeof(convbuf), "%d", p); // Convert em yo
	return convbuf;
}

// Store the parameter in our struct
void *kickjoindelay_chmode_put_param(void *data, const char *para) {
	/* Args:
	** data: A void pointer to the custom aModej struct
	** para: Parameter being set
	*/
	aModej *r = (aModej *)data; // Cast our shit

	// If NULL, allocate some mem0ry ;]
	if(!r) {
		r = (aModej *)safe_alloc(sizeof(aModej));
		r->flag = CHMODE_FLAG;
	}
	r->p = atoi(para); // Set/overwrite value

	return (void *)r; // Return as a void
}

// Convert whatever the parameter(s) are back to a char *
const char *kickjoindelay_chmode_get_param(void *data) {
	/* Args:
	** data: A void pointer to the custom aModej struct
	*/
	aModej *r = (aModej *)data;
	static char buf[32];

	// If no data, just return NULL lol
	if(!r)
		return NULL;

	ircsnprintf(buf, sizeof(buf), "%d", r->p); // Convert the int back
	return buf; // Return 'em char
}

// When unsetting the mode, gotta free our shit
void kickjoindelay_chmode_free_param(void *data) {
	/* Args:
	** data: A void pointer to the custom aModej struct
	*/
	safe_free(data);
}

// Duplicate the struct, seems to be necessary ;]
void *kickjoindelay_chmode_dup_struct(void *src) {
	/* Args:
	** src: A void pointer to our current custom aModej struct
	*/
	aModej *dst = safe_alloc(sizeof(aModej)); // Pointer to duped struct
	memcpy(dst, src, sizeof(aModej)); // Dupe 'em
	return (void *)dst; // Return as void pointer ;]
}

// During server synchronisations, check for possibru conflicts
int kickjoindelay_chmode_sjoin_check(Channel *channel, void *ourx, void *theirx) {
	/* Args:
	** channel: Pointer to the channel struct
	** our: Pointer to our custom mode struct
	** their:
	*/

	/* Return values:
	** EXSJ_SAME: Same values, all's good fam
	** EXSJ_WEWON: Ours has precedence
	** EXSJ_THEYWON: Use the other server's value
	** EXSJ_MERGE: Pretty rare, used when both sides should use a new value
	*/
	aModej *our = (aModej *)ourx;
	aModej *their = (aModej *)theirx;

	// Since we just have integers here, the higher one wins
	if(our->p > their->p)
		return EXSJ_WEWON;
	else if(their->p > our->p)
		return EXSJ_THEYWON;
	else
		return EXSJ_SAME;
}

// Free all timers in this linked list (user quit)
void kickjoindelay_md_free(ModData *md) {
	kickTimer *kt, *kt_next;
	for(kt = (kickTimer *)md->ptr; kt; kt = kt_next) {
		kt_next = kt->next;
		safe_free(kt);
	}
	md->ptr = NULL;
}

// Periodically free old expired entries
EVENT(kickjoindelay_event) {
	Client *acptr;
	kickTimer *kt, *kt_next; // For iter8ion m9
	kickTimer *chanTimers; // To store moddata shit
	list_for_each_entry(acptr, &lclient_list, lclient_node) {
		if((chanTimers = moddata_local_client(acptr, kickjoinMDI).ptr)) {
			for(kt = chanTimers; kt; kt = kt_next) {
				kt_next = kt->next;
				if(TStime() - kt->lastkick > 20) {
					// Expired entry
					DelListItem(kt, chanTimers);
					safe_free(kt);
				}
			}
			// Update head y0
			moddata_local_client(acptr, kickjoinMDI).ptr = chanTimers;
		}
	}
}
