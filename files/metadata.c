/* Copyright (C) All Rights Reserved
** Written by k4be
** Website: https://github.com/pirc-pl/unrealircd-modules/
** License: GPLv3 https://www.gnu.org/licenses/gpl-3.0.html
*/

/*** <<<MODULE MANAGER START>>>
module
{
        documentation "https://github.com/pirc-pl/unrealircd-modules/blob/master/README.md#metadata";
        troubleshooting "In case of problems, contact k4be on irc.pirc.pl.";
        min-unrealircd-version "5.*";
        post-install-text {
                "The module is installed. Now all you need to do is add a loadmodule line:";
                "loadmodule \"third/metadata\";";
  				"And /REHASH the IRCd.";
				"The module may be additionaly configured to change the defaults.";
				"See documentation for help.";
				"Please note that the implemented feature is still \"Work In Progress\".";
        }
}
*** <<<MODULE MANAGER END>>>
*/

#include "unrealircd.h"

#define STR_HELPER(x) #x
#define STR(x) STR_HELPER(x)

#define MODE_SET 0 // get or set for perms
#define MODE_GET 1

#define MYCONF "metadata"

// our numerics
#define RPL_WHOISKEYVALUE	760
#define RPL_KEYVALUE		761
#define	RPL_METADATAEND		762
#define ERR_METADATALIMIT	764
#define ERR_TARGETINVALID	765
#define ERR_NOMATCHINGKEY	766
#define ERR_KEYINVALID		767
#define ERR_KEYNOTSET		768
#define ERR_KEYNOPERMISSION	769
#define RPL_METADATASUBOK	770
#define RPL_METADATAUNSUBOK	771
#define RPL_METADATASUBS	772
#define ERR_METADATATOOMANYSUBS	773
#define ERR_METADATASYNCLATER	774
#define ERR_METADATARATELIMIT	775
#define ERR_METADATAINVALIDSUBCOMMAND	776

#define SET_NUMERIC(x, str) { replies[x] = strdup(str); }
#define UNSET_NUMERIC(x) { safe_free(replies[x]); replies[x] = NULL; }

#ifdef PREFIX_AQ
#define HOP_OR_MORE (CHFL_HALFOP | CHFL_CHANOP | CHFL_CHANADMIN | CHFL_CHANOWNER)
#else
#define HOP_OR_MORE (CHFL_HALFOP | CHFL_CHANOP)
#endif

#define CHECKPARAMSCNT_OR_DIE(count, return) { if(parc < count+1 || BadPtr(parv[count])) { sendnumeric(client, ERR_NEEDMOREPARAMS, "METADATA"); return; } }

// target "*" is always the user issuing the command

#define PROCESS_TARGET_OR_DIE(target, user, channel, return) { \
	char *channame; \
	channame = strchr(target, '#'); \
	if(channame){ \
		channel = find_channel(channame, NULL); \
		if(!channel){ \
			sendnumeric(client, ERR_NOSUCHNICK, channame); \
			return; \
		} \
	} else { \
		if(strcmp(target, "*")){ \
			user = hash_find_nickatserver(target, NULL); \
			if(!user){ \
				sendnumeric(client, ERR_NOSUCHNICK, target); \
				return; \
			} \
		} else { \
			user = client; \
		} \
	} \
}

#define FOR_EACH_KEY() while(keyindex++, key = parv[keyindex], (!BadPtr(key) && keyindex < parc))
#define IsSendable(x)		(DBufLength(&x->local->sendQ) < 2048)
#define CHECKREGISTERED_OR_DIE(client, return)	{ if(!IsUser(client)){ sendnumeric(client, ERR_NOTREGISTERED); return; } }

struct metadata {
	char *name;
	char *value;
	struct metadata *next;
};

struct subscriptions {
	char *name;
	struct subscriptions *next;
};

struct moddata_user {
	struct metadata *metadata;
	struct subscriptions *subs;
	struct unsynced *us;
};

struct unsynced { // we're listing users (nicknames) that should be synced but were not
	char *name;
	char *key;
	struct unsynced *next;
};

void vsendto_one(Client *to, MessageTag *mtags, const char *pattern, va_list vl); // built-in: src/send.c
CMD_FUNC(cmd_metadata);
CMD_FUNC(cmd_metadata_remote);
CMD_FUNC(cmd_metadata_local);
EVENT(metadata_queue_evt);
char *metadata_cap_param(Client *client);
char *metadata_isupport_param(void);
int metadata_configtest(ConfigFile *cf, ConfigEntry *ce, int type, int *errs);
int metadata_configposttest(int *errs);
int metadata_configrun(ConfigFile *cf, ConfigEntry *ce, int type);
/* hooks */
int metadata_server_sync(Client *client);
int metadata_join(Client *client, Channel *channel, MessageTag *mtags, char *parv[]);
int metadata_user_registered(Client *client);

void metadata_user_free(ModData *md);
void metadata_channel_free(ModData *md);
void metadata_numeric(Client *to, int numeric, ...);
void setReplies(void);
void unsetReplies(void);
void free_metadata(struct metadata *metadata);
void free_subs(struct subscriptions *subs);
int is_subscribed(Client *user, char *key);
char *get_user_key_value(Client *user, char *key);
char *get_channel_key_value(Channel *channel, char *key);
void user_metadata_changed(Client *user, char *key, char *value, Client *changer);
void channel_metadata_changed(Channel *channel, char *key, char *value, Client *changer);
void metadata_free_list(struct metadata *metadata, char *whose, Client *client);
struct moddata_user *prepare_user_moddata(Client *user);
void metadata_set_channel(Channel *channel, char *key, char *value, Client *client);
void metadata_set_user(Client *user, char *key, char *value, Client *client);
void metadata_send_channel(Channel *channel, char *key, Client *client);
void metadata_send_user(Client *user, char *key, Client *client);
int metadata_subscribe(char *key, Client *client, int remove);
void metadata_clear_channel(Channel *channel, Client *client);
void metadata_clear_user(Client *user, Client *client);
void metadata_send_subscribtions(Client *client);
void metadata_send_all_for_channel(Channel *channel, Client *client);
void metadata_send_all_for_user(Client *user, Client *client);
void metadata_sync(char *what, Client *client);
int key_valid(char *key);
int check_perms(Client *user, Channel *channel, Client *client, char *key, int mode);
void send_change(Client *client, char *who, char *key, char *value, Client *changer);
int notify_or_queue(Client *client, char *who, char *key, char *value, Client *changer);

ModDataInfo *metadataUser;
ModDataInfo *metadataChannel;
long CAP_METADATA = 0L;
long CAP_METADATA_NOTIFY = 0L;
static char *replies[999];
struct settings {
	int max_user_metadata;
	int max_channel_metadata;
	int max_subscriptions;
	int enable_debug;
} metadata_settings;

ModuleHeader MOD_HEADER = {
	"third/metadata",   /* Name of module */
	"5.2", /* Version */
	"draft/metadata and draft/metadata-notify-2 cap", /* Short description of module */
	"k4be@PIRC",
	"unrealircd-5"
};

// config file stuff, based on Gottem's module

/*
metadata {
	max-user-metadata 10;
	max-channel-metadata 10;
	max-subscriptions 10;
	enable-debug 0;
};
*/

int metadata_configtest(ConfigFile *cf, ConfigEntry *ce, int type, int *errs) {
	ConfigEntry *cep; // For looping through our bl0cc
	int errors = 0; // Error count
	int i; // iter8or m8
	
	// Since we'll add a new top-level block to unrealircd.conf, need to filter on CONFIG_MAIN lmao
	if(type != CONFIG_MAIN)
		return 0; // Returning 0 means idgaf bout dis

	// Check for valid config entries first
	if(!ce || !ce->ce_varname)
		return 0;

	// If it isn't our bl0ck, idc
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

		if(!cep->ce_vardata || !strlen(cep->ce_vardata)) {
			config_error("%s:%i: %s::%s must be non-empty", cep->ce_fileptr->cf_filename, cep->ce_varlinenum, MYCONF, cep->ce_varname);
			errors++; // Increment err0r count fam
			continue;
		}
	
		if(!strcmp(cep->ce_varname, "max-user-metadata")) {
			// Should be an integer yo
			for(i = 0; cep->ce_vardata[i]; i++) {
				if(!isdigit(cep->ce_vardata[i])) {
					config_error("%s:%i: %s::%s must be an integer between 1 and 100", cep->ce_fileptr->cf_filename, cep->ce_varlinenum, MYCONF, cep->ce_varname);
					errors++; // Increment err0r count fam
					break;
				}
			}
			if(!errors && (atoi(cep->ce_vardata) < 1 || atoi(cep->ce_vardata) > 100)) {
				config_error("%s:%i: %s::%s must be an integer between 1 and 100", cep->ce_fileptr->cf_filename, cep->ce_varlinenum, MYCONF, cep->ce_varname);
				errors++; // Increment err0r count fam
			}
			continue;
		}

		if(!strcmp(cep->ce_varname, "max-channel-metadata")) {
			// Should be an integer yo
			for(i = 0; cep->ce_vardata[i]; i++) {
				if(!isdigit(cep->ce_vardata[i])) {
					config_error("%s:%i: %s::%s must be an integer between 0 and 100", cep->ce_fileptr->cf_filename, cep->ce_varlinenum, MYCONF, cep->ce_varname);
					errors++; // Increment err0r count fam
					break;
				}
			}
			if(!errors && (atoi(cep->ce_vardata) < 0 || atoi(cep->ce_vardata) > 100)) {
				config_error("%s:%i: %s::%s must be an integer between 0 and 100", cep->ce_fileptr->cf_filename, cep->ce_varlinenum, MYCONF, cep->ce_varname);
				errors++; // Increment err0r count fam
			}
			continue;
		}

		if(!strcmp(cep->ce_varname, "max-subscriptions")) {
			// Should be an integer yo
			for(i = 0; cep->ce_vardata[i]; i++) {
				if(!isdigit(cep->ce_vardata[i])) {
					config_error("%s:%i: %s::%s must be an integer between 1 and 100", cep->ce_fileptr->cf_filename, cep->ce_varlinenum, MYCONF, cep->ce_varname);
					errors++; // Increment err0r count fam
					break;
				}
			}
			if(!errors && (atoi(cep->ce_vardata) < 0 || atoi(cep->ce_vardata) > 100)) {
				config_error("%s:%i: %s::%s must be an integer between 1 and 100", cep->ce_fileptr->cf_filename, cep->ce_varlinenum, MYCONF, cep->ce_varname);
				errors++; // Increment err0r count fam
			}
			continue;
		}

		if(!strcmp(cep->ce_varname, "enable-debug")) {
			// Should be 0 or 1
			if(strlen(cep->ce_vardata) != 1 || (cep->ce_vardata[0] != '0' && cep->ce_vardata[0] != '1')) {
				config_error("%s:%i: %s::%s must be 0 or 1", cep->ce_fileptr->cf_filename, cep->ce_varlinenum, MYCONF, cep->ce_varname);
				errors++; // Increment err0r count fam
				break;
			}
			continue;
		}

		// Anything else is unknown to us =]
		config_warn("%s:%i: unknown item %s::%s", cep->ce_fileptr->cf_filename, cep->ce_varlinenum, MYCONF, cep->ce_varname); // So display just a warning
	}
	
	*errs = errors;
	return errors ? -1 : 1; // Returning 1 means "all good", -1 means we shat our panties
}

int metadata_configposttest(int *errs) {
	// null the settings to avoid keeping old value if none is set in config
	metadata_settings.max_user_metadata = 0;
	metadata_settings.max_channel_metadata = 0;
	metadata_settings.max_subscriptions = 0;
	return 1;
}

// "Run" the config (everything should be valid at this point)
int metadata_configrun(ConfigFile *cf, ConfigEntry *ce, int type) {
	ConfigEntry *cep; // For looping through our bl0cc

	// Since we'll add a new top-level block to unrealircd.conf, need to filter on CONFIG_MAIN lmao
	if(type != CONFIG_MAIN)
		return 0; // Returning 0 means idgaf bout dis

	// Check for valid config entries first
	if(!ce || !ce->ce_varname)
		return 0;

	// If it isn't our bl0cc, idc
	if(strcmp(ce->ce_varname, MYCONF))
		return 0;

	// Loop dat shyte fam
	for(cep = ce->ce_entries; cep; cep = cep->ce_next) {
		// Do we even have a valid name l0l?
		if(!cep->ce_varname)
			continue; // Next iteration imo tbh

		if(!strcmp(cep->ce_varname, "max-user-metadata")) {
			metadata_settings.max_user_metadata = atoi(cep->ce_vardata);
			continue;
		}

		if(!strcmp(cep->ce_varname, "max-channel-metadata")) {
			metadata_settings.max_channel_metadata = atoi(cep->ce_vardata);
			continue;
		}

		if(!strcmp(cep->ce_varname, "max-subscriptions")) {
			metadata_settings.max_subscriptions = atoi(cep->ce_vardata);
			continue;
		}

		if(!strcmp(cep->ce_varname, "enable-debug")) {
			metadata_settings.enable_debug = atoi(cep->ce_vardata);
			continue;
		}
	}
	return 1; // We good
}

void setReplies(void){
	SET_NUMERIC(RPL_WHOISKEYVALUE, "%s %s %s :%s");
	SET_NUMERIC(RPL_KEYVALUE, "%s %s %s :%s");
	SET_NUMERIC(RPL_METADATAEND, ":end of metadata");
	SET_NUMERIC(ERR_METADATALIMIT, "%s :metadata limit reached");
	SET_NUMERIC(ERR_TARGETINVALID, "%s :invalid metadata target");
	SET_NUMERIC(ERR_NOMATCHINGKEY, "%s %s :no matching key");
	SET_NUMERIC(ERR_KEYINVALID, ":%s");
	SET_NUMERIC(ERR_KEYNOTSET, "%s %s :key not set");
	SET_NUMERIC(ERR_KEYNOPERMISSION, "%s %s :permission denied");
	SET_NUMERIC(RPL_METADATASUBOK, ":%s");
	SET_NUMERIC(RPL_METADATAUNSUBOK, ":%s");
	SET_NUMERIC(RPL_METADATASUBS, ":%s");
	SET_NUMERIC(ERR_METADATATOOMANYSUBS, "%s");
	SET_NUMERIC(ERR_METADATASYNCLATER, "%s %s");
	SET_NUMERIC(ERR_METADATARATELIMIT, "%s %s %s :%s");
	SET_NUMERIC(ERR_METADATAINVALIDSUBCOMMAND, "%s :invalid metadata subcommand");
}

void unsetReplies(void){
	UNSET_NUMERIC(RPL_WHOISKEYVALUE);
	UNSET_NUMERIC(RPL_KEYVALUE);
	UNSET_NUMERIC(RPL_METADATAEND);
	UNSET_NUMERIC(ERR_METADATALIMIT);
	UNSET_NUMERIC(ERR_TARGETINVALID);
	UNSET_NUMERIC(ERR_NOMATCHINGKEY);
	UNSET_NUMERIC(ERR_KEYINVALID);
	UNSET_NUMERIC(ERR_KEYNOTSET);
	UNSET_NUMERIC(ERR_KEYNOPERMISSION);
	UNSET_NUMERIC(RPL_METADATASUBOK);
	UNSET_NUMERIC(RPL_METADATAUNSUBOK);
	UNSET_NUMERIC(RPL_METADATASUBS);
	UNSET_NUMERIC(ERR_METADATATOOMANYSUBS);
	UNSET_NUMERIC(ERR_METADATASYNCLATER);
	UNSET_NUMERIC(ERR_METADATARATELIMIT);
	UNSET_NUMERIC(ERR_METADATAINVALIDSUBCOMMAND);
}

void metadata_numeric(Client *to, int numeric, ...){
	va_list vl;
	char pattern[512];

	snprintf(pattern, sizeof(pattern), ":%s %.3d %s %s", me.name, numeric, to->name[0] ? to->name : "*", replies[numeric]);

	va_start(vl, numeric);
	vsendto_one(to, NULL, pattern, vl);
	va_end(vl);
}

// Configuration testing-related hewks go in testing phase obv
MOD_TEST(){
	// We have our own config block so we need to checkem config obv m9
	// Priorities don't really matter here
	HookAdd(modinfo->handle, HOOKTYPE_CONFIGTEST, 0, metadata_configtest);
	HookAdd(modinfo->handle, HOOKTYPE_CONFIGPOSTTEST, 0, metadata_configposttest);
	return MOD_SUCCESS;
}

MOD_INIT() {
	ClientCapabilityInfo cap;
	ClientCapability *c;
	ModDataInfo mreq;
	
	MARK_AS_GLOBAL_MODULE(modinfo);

	memset(&cap, 0, sizeof(cap));
	cap.name = "draft/metadata";
	cap.parameter = metadata_cap_param;
	c = ClientCapabilityAdd(modinfo->handle, &cap, &CAP_METADATA);
	
	memset(&cap, 0, sizeof(cap));
	cap.name = "draft/metadata-notify-2";
	c = ClientCapabilityAdd(modinfo->handle, &cap, &CAP_METADATA_NOTIFY);
	
	CommandAdd(modinfo->handle, "METADATA", cmd_metadata, MAXPARA, CMD_USER|CMD_SERVER|CMD_UNREGISTERED);
	
	memset(&mreq, 0 , sizeof(mreq));
	mreq.type = MODDATATYPE_CLIENT;
	mreq.name = "metadata_user",
	mreq.free = metadata_user_free;
	metadataUser = ModDataAdd(modinfo->handle, mreq);
	if(!metadataUser){
		config_error("[%s] Failed to request metadata_user moddata: %s", MOD_HEADER.name, ModuleGetErrorStr(modinfo->handle));
		return MOD_FAILED;
	}
	
	memset(&mreq, 0 , sizeof(mreq));
	mreq.type = MODDATATYPE_CHANNEL;
	mreq.name = "metadata_channel",
	mreq.free = metadata_channel_free;
	metadataChannel = ModDataAdd(modinfo->handle, mreq);
	if(!metadataChannel){
		config_error("[%s] Failed to request metadata_channel moddata: %s", MOD_HEADER.name, ModuleGetErrorStr(modinfo->handle));
		return MOD_FAILED;
	}
	
	HookAdd(modinfo->handle, HOOKTYPE_SERVER_SYNC, 0, metadata_server_sync);
	HookAdd(modinfo->handle, HOOKTYPE_LOCAL_JOIN, -2, metadata_join);
	HookAdd(modinfo->handle, HOOKTYPE_REMOTE_JOIN, -2, metadata_join);
	HookAdd(modinfo->handle, HOOKTYPE_LOCAL_CONNECT, 0, metadata_user_registered);

	HookAdd(modinfo->handle, HOOKTYPE_CONFIGRUN, 0, metadata_configrun);
	
	return MOD_SUCCESS;
}

MOD_LOAD() {
	setReplies();
	ISupportAdd(modinfo->handle, "METADATA", metadata_isupport_param());
	// setting default values if not configured
	if(metadata_settings.max_user_metadata == 0) metadata_settings.max_user_metadata = 10;
	if(metadata_settings.max_channel_metadata == 0) metadata_settings.max_channel_metadata = 10;
	if(metadata_settings.max_subscriptions == 0) metadata_settings.max_subscriptions = 10;

	EventAdd(modinfo->handle, "metadata_queue", metadata_queue_evt, NULL, 1500, 0);

	return MOD_SUCCESS;
}

MOD_UNLOAD() {
	unsetReplies();
	return MOD_SUCCESS;
}

char *metadata_cap_param(Client *client){
	static char buf[20];
	ircsnprintf(buf, sizeof(buf), "maxsub=%d", metadata_settings.max_subscriptions);
	return buf;
}

char *metadata_isupport_param(void){
	static char buf[20];
	ircsnprintf(buf, sizeof(buf), "%d", metadata_settings.max_user_metadata);
	return buf;
}

void free_metadata(struct metadata *metadata){
	safe_free(metadata->name);
	safe_free(metadata->value);
	safe_free(metadata);
}

void free_subs(struct subscriptions *subs){
	safe_free(subs->name);
	safe_free(subs);
}

int is_subscribed(Client *user, char *key){
/*	if(MyUser(user) && (user->local->caps & CAP_METADATA_NOTIFY)) return 1; // metadata-notify cap means "subscribed to everything" */
	struct moddata_user *moddata = moddata_client(user, metadataUser).ptr;
	if(!moddata) return 0;
	struct subscriptions *subs;
	for(subs = moddata->subs; subs; subs = subs->next){
		if(!strcasecmp(subs->name, key))
			return 1;
	}
	return 0;
}

char *get_user_key_value(Client *user, char *key){
	struct moddata_user *moddata = moddata_client(user, metadataUser).ptr;
	struct metadata *metadata = NULL;
	if(!moddata)
		return NULL;
	for(metadata = moddata->metadata; metadata; metadata = metadata->next){
		if(!strcasecmp(key, metadata->name))
			return metadata->value;
	}
	return NULL;
}

char *get_channel_key_value(Channel *channel, char *key){
	struct metadata *metadata;
	for(metadata = moddata_channel(channel, metadataChannel).ptr; metadata; metadata = metadata->next){
		if(!strcasecmp(key, metadata->name))
			return metadata->value;
	}
	return NULL;
}

int notify_or_queue(Client *client, char *who, char *key, char *value, Client *changer){ // returns 1 if something remains to sync
	int trylater = 0;
	if(!who){
		sendto_snomask(SNO_JUNK, "notify_or_queue called with null who!");
		return 0;
	}
	if(!key){
		sendto_snomask(SNO_JUNK, "notify_or_queue called with null key!");
		return 0;
	}
	if(!client){
		sendto_snomask(SNO_JUNK, "notify_or_queue called with null client!");
		return 0;
	}
	struct unsynced *us, *prev_us;

	struct moddata_user *moddata = moddata_client(client, metadataUser).ptr;
	if(!moddata)
		moddata = prepare_user_moddata(client);

	if(IsSendable(client)){
		send_change(client, who, key, value, changer);
	} else { // store for the SYNC
		trylater = 1;
		prev_us = NULL;
		for(us = moddata->us, prev_us = NULL; us; us = us->next)
			prev_us = us; // find last list element
		us = safe_alloc(sizeof(struct unsynced));
		us->name = strdup(who);
		us->key = strdup(key);
		us->next = NULL;
		if(!prev_us){ // first one
			moddata->us = us;
		} else {
			prev_us->next = us;
		}
	}
	return trylater;
}

void send_change(Client *client, char *who, char *key, char *value, Client *changer){
	char *sender = NULL;
	if(!key){
		sendto_snomask(SNO_JUNK, "send_change called with null key!");
		return;
	}
	if(!who){
		sendto_snomask(SNO_JUNK, "send_change called with null who!");
		return;
	}
	if(!client){
		sendto_snomask(SNO_JUNK, "send_change called with null client!");
		return;
	}
	if(changer){
		if(IsServer(client)){
			sender = changer->id;
		} else {
			sender = changer->name;
		}
	}
	if(!sender)
		sender = me.name;
	if(changer && IsUser(changer) && MyUser(client)){
		if(!value){
			sendto_one(client, NULL, ":%s!%s@%s METADATA %s %s %s", sender, changer->user->username, GetHost(changer), who, key, "*");
		} else {
			sendto_one(client, NULL, ":%s!%s@%s METADATA %s %s %s :%s", sender, changer->user->username, GetHost(changer), who, key, "*", value);
		}
	} else { // sending S2S (sender is id) or receiving S2S (sender is servername)
		if(!value){
			sendto_one(client, NULL, ":%s METADATA %s %s %s", sender, who, key, "*");
		} else {
			sendto_one(client, NULL, ":%s METADATA %s %s %s :%s", sender, who, key, "*", value);
		}
	}
}

// used for broadcasting changes to subscribed users and linked servers
void user_metadata_changed(Client *user, char *key, char *value, Client *changer){
	Client *acptr;
	if(!user || !key) return; // sanity check
	list_for_each_entry(acptr, &lclient_list, lclient_node){ // notifications for local subscribers
		if(IsUser(acptr) && IsUser(user) && is_subscribed(acptr, key) && has_common_channels(user, acptr))
			notify_or_queue(acptr, user->name, key, value, changer);
	}

	list_for_each_entry(acptr, &server_list, special_node){ // notifications for linked servers
		if(acptr == &me) continue;
		send_change(acptr, user->name, key, value, changer);
	}
}

void channel_metadata_changed(Channel *channel, char *key, char *value, Client *changer){
	Client *acptr;
	if(!channel || !key) return; // sanity check
	list_for_each_entry(acptr, &lclient_list, lclient_node){ // notifications for local subscribers
		if(is_subscribed(acptr, key) && IsMember(acptr, channel))
			send_change(acptr, channel->chname, key, value, changer);
	}
	
	list_for_each_entry(acptr, &server_list, special_node){ // notifications for linked servers
		if(acptr == &me) continue;
		send_change(acptr, channel->chname, key, value, changer);
	}
}

void metadata_free_list(struct metadata *metadata, char *whose, Client *client){
	struct metadata *prev_metadata = metadata;
	char *name;
	while(metadata){
		name = metadata->name;
		safe_free(metadata->value);
		metadata = metadata->next;
		safe_free(prev_metadata);
		prev_metadata = metadata;
		if(client && whose && *whose){ // send out the data being removed, unless we're unloading the module
			metadata_numeric(client, RPL_KEYVALUE, whose, name, "*", "");
			if(*whose == '#'){
				channel_metadata_changed(find_channel(whose, NULL), name, NULL, client);
			} else {
				user_metadata_changed(hash_find_nickatserver(whose, NULL), name, NULL, client);
			}
		}
		safe_free(name);
	}
}

void metadata_channel_free(ModData *md){
	if(!md->ptr) return; // was not set
	struct metadata *metadata = md->ptr;
	metadata_free_list(metadata, NULL, NULL);
}

void metadata_user_free(ModData *md){
	struct moddata_user *moddata = md->ptr;
	if(!moddata) return; // was not set
	struct subscriptions *sub = moddata->subs;
	struct subscriptions *prev_sub = sub;
	struct unsynced *us = moddata->us;
	struct unsynced *prev_us;
	while(sub){
		safe_free(sub->name);
		sub = sub->next;
		safe_free(prev_sub);
		prev_sub = sub;
	}
	struct metadata *metadata = moddata->metadata;
	metadata_free_list(metadata, NULL, NULL);
	while(us){
		safe_free(us->name);
		safe_free(us->key);
		prev_us = us;
		us = us->next;
		safe_free(prev_us);
	}
	safe_free(moddata);
}

struct moddata_user *prepare_user_moddata(Client *user){
	moddata_client(user, metadataUser).ptr = safe_alloc(sizeof(struct moddata_user));
	struct moddata_user *ptr = moddata_client(user, metadataUser).ptr;
	ptr->metadata = NULL;
	ptr->subs = NULL;
	return ptr;
}

void metadata_set_user(Client *user, char *key, char *value, Client *client){
	int changed = 0;
	Client *target;
	char *target_name;
	int removed = 0;
	int set = 0;
	int count = 0;

	if(user){
		target = user;
		target_name = user->name;
	} else {
		target = client;
		target_name = "*";
	}
		
	struct moddata_user *moddata = moddata_client(target, metadataUser).ptr;
	if(!moddata){ // first call for this user
		moddata = prepare_user_moddata(target);
	}
	struct metadata *currMetadata = moddata->metadata;
	struct metadata *prevMetadata = NULL;
	if(BadPtr(value) || strlen(value) == 0){ // unset
		value = NULL; // just to make sure
		removed = 0;
		while(currMetadata){
			if(!strcasecmp(key, currMetadata->name)){
				removed = 1;
				changed = 1;
				if(prevMetadata){
					prevMetadata->next = currMetadata->next;
				} else {
					moddata->metadata = currMetadata->next; // removing the first one
				}
				free_metadata(currMetadata);
				break;
			}
			prevMetadata = currMetadata;
			currMetadata = currMetadata->next;
		}
		if(!removed){
			if(client) metadata_numeric(client, ERR_KEYNOTSET, target_name, key); // not set so can't remove
			return;
		}
	} else { // set
		while(currMetadata){
			if(!strcasecmp(key, currMetadata->name)){
				set = 1;
				if(strcmp(value, currMetadata->value)){
					safe_free(currMetadata->value);
					currMetadata->value = strdup(value);
					changed = 1;
				}
			}
			prevMetadata = currMetadata;
			currMetadata = currMetadata->next;
			count++;
		}
		if(!set){
			if(!client || count < metadata_settings.max_user_metadata){ // add new entry for user
				currMetadata = safe_alloc(sizeof(struct metadata));
				if(prevMetadata){
					prevMetadata->next = currMetadata;
				} else {
					moddata->metadata = currMetadata; // setting the first entry
				}
				currMetadata->next = NULL;
				currMetadata->name = strdup(key);
				currMetadata->value = strdup(value);
				changed = 1;
			} else { // no more allowed
				if(client)
					metadata_numeric(client, ERR_METADATALIMIT, target_name);
				return;
			}
		}
	}
	if(!IsServer(client) && MyConnect(client))
		metadata_numeric(client, RPL_KEYVALUE, target_name, key, "*", value?value:""); // all OK
	if(changed && (client == &me || IsUser(client) || IsServer(client)))
		user_metadata_changed(target, key, value, client);
}

void metadata_set_channel(Channel *channel, char *key, char *value, Client *client){
	int changed = 0;
	int set = 0;
	int count = 0;
	struct metadata *currMetadata;
	struct metadata *prevMetadata = NULL;

	if(BadPtr(value) || strlen(value) == 0){ // unset
		value = NULL; // just to make sure
		int removed = 0;
		for(currMetadata = moddata_channel(channel, metadataChannel).ptr; currMetadata; currMetadata = currMetadata->next){
			if(!strcasecmp(key, currMetadata->name)){
				removed = 1;
				changed = 1;
				if(!prevMetadata){ // it's the first one
					moddata_channel(channel, metadataChannel).ptr = currMetadata->next;
				} else {
					prevMetadata->next = currMetadata->next;
				}
				free_metadata(currMetadata);
				break;
			}
			prevMetadata = currMetadata;
		}
		if(!removed){
			if(client)
				metadata_numeric(client, ERR_KEYNOTSET, channel->chname, key); // not set so can't remove
			return;
		}
	} else { // set
		for(currMetadata = moddata_channel(channel, metadataChannel).ptr; currMetadata; currMetadata = currMetadata->next){
			if(!strcasecmp(key, currMetadata->name)){
				set = 1;
				if(strcmp(value, currMetadata->value)){ // is the new value the same as the old one?
					safe_free(currMetadata->value);
					currMetadata->value = strdup(value);
					changed = 1;
				}
				break;
			}
			prevMetadata = currMetadata;
			count++;
		}
		if(!set){
			if(!client || count < metadata_settings.max_channel_metadata){ // add new entry for user
				currMetadata = safe_alloc(sizeof(struct metadata));
				if(prevMetadata){
					prevMetadata->next = currMetadata;
				} else {
					moddata_channel(channel, metadataChannel).ptr = currMetadata; // adding the first entry
				}
				currMetadata->next = NULL;
				currMetadata->name = strdup(key);
				currMetadata->value = strdup(value);
				changed = 1;
			} else { // no more allowed
				if(client)
					metadata_numeric(client, ERR_METADATALIMIT, channel->chname);
				return;
			}
		}
	}
	if(IsUser(client) && MyUser(client))
		metadata_numeric(client, RPL_KEYVALUE, channel->chname, key, "*", value?value:""); // all OK
	if(changed && (IsUser(client) || IsServer(client)))
		channel_metadata_changed(channel, key, value, client);
}

int metadata_subscribe(char *key, Client *client, int remove){
	struct moddata_user *moddata = moddata_client(client, metadataUser).ptr;
	struct subscriptions *curr_subs;
	struct subscriptions *prev_subs = NULL;
	int found = 0;
	int count = 0;
	int trylater = 0;
	char *value;
	unsigned int hashnum;
	Channel *channel;
	Client *acptr;
	if(!client) return 0;
	
	if(!moddata) // first call for this user
		moddata = prepare_user_moddata(client);
	for(curr_subs = moddata->subs; curr_subs; curr_subs = curr_subs->next){
		count++;
		if(!strcasecmp(key, curr_subs->name)){
			found = 1;
			if(remove){ // remove element from list
				if(!prev_subs){ // removing the first entry
					moddata->subs = curr_subs->next;
				} else {
					prev_subs->next = curr_subs->next;
				}
				free_subs(curr_subs);
			}
			break;
		}
		prev_subs = curr_subs;
	}
	if(!remove && !found){
		if(count < metadata_settings.max_subscriptions){
			curr_subs = safe_alloc(sizeof(struct subscriptions));
			if(prev_subs){
				prev_subs->next = curr_subs;
			} else {
				moddata->subs = curr_subs; // first one
			}
			curr_subs->next = NULL;
			curr_subs->name = strdup(key);
		} else { // no more allowed
			metadata_numeric(client, ERR_METADATATOOMANYSUBS, key);
			return 0;
		}
	}
	if(!remove){
		metadata_numeric(client, RPL_METADATASUBOK, key);
		if(!IsUser(client)) return 0; // unregistered user is not getting any keys yet
		// we have to send out all subscribed data now
		trylater = 0;
		list_for_each_entry(acptr, &client_list, client_node){
			value = NULL;
			if(IsUser(client) && IsUser(acptr) && has_common_channels(acptr, client))
				value = get_user_key_value(acptr, key);
			if(value)
				trylater |= notify_or_queue(client, acptr->name, key, value, NULL);
		}
		for(hashnum = 0; hashnum < CHAN_HASH_TABLE_SIZE; hashnum++){
			for(channel = hash_get_chan_bucket(hashnum); channel; channel = channel->hnextch){
				if(IsMember(client, channel)){
					value = get_channel_key_value(channel, key);
					if(value)
						trylater |= notify_or_queue(client, channel->chname, key, value, NULL);
				}
			}
		}
		if(trylater)
			return 1;
	} else {
		metadata_numeric(client, RPL_METADATAUNSUBOK, key);	
	}
	return 0;
}

void metadata_send_channel(Channel *channel, char *key, Client *client){
	struct metadata *metadata;
	int found = 0;
	for(metadata = moddata_channel(channel, metadataChannel).ptr; metadata; metadata = metadata->next){
		if(!strcasecmp(key, metadata->name)){
			found = 1;
			metadata_numeric(client, RPL_KEYVALUE, channel->chname, key, "*", metadata->value);
			break;
		}
	}
	if(!found)
		metadata_numeric(client, ERR_NOMATCHINGKEY, channel->chname, key);
}

void metadata_send_user(Client *user, char *key, Client *client){
	if(!user) user = client;
	struct moddata_user *moddata = moddata_client(user, metadataUser).ptr;
	struct metadata *metadata = NULL;
	if(moddata){
		metadata = moddata->metadata;
	}
	int found = 0;
	for( ; metadata; metadata = metadata->next){
		if(!strcasecmp(key, metadata->name)){
			found = 1;
			metadata_numeric(client, RPL_KEYVALUE, user->name, key, "*", metadata->value);
			break;
		}
	}
	if(!found)
		metadata_numeric(client, ERR_NOMATCHINGKEY, user->name, key);
}

void metadata_clear_channel(Channel *channel, Client *client){
	struct metadata *metadata = moddata_channel(channel, metadataChannel).ptr;
	metadata_free_list(metadata, channel->chname, client);
	moddata_channel(channel, metadataChannel).ptr = NULL;
}

void metadata_clear_user(Client *user, Client *client){
	if(!user) user = client;
	struct moddata_user *moddata = moddata_client(user, metadataUser).ptr;
	struct metadata *metadata = NULL;
	if(!moddata) return; // nothing to delete
	metadata = moddata->metadata;
	metadata_free_list(metadata, user->name, client);
	moddata->metadata = NULL;
}

void metadata_send_subscribtions(Client *client){
	struct subscriptions *subs;
	struct moddata_user *moddata = moddata_client(client, metadataUser).ptr;
	if(!moddata) return;
	for(subs = moddata->subs; subs; subs = subs->next)
		metadata_numeric(client, RPL_METADATASUBS, subs->name);
}

void metadata_send_all_for_channel(Channel *channel, Client *client){
	struct metadata *metadata;
	for(metadata = moddata_channel(channel, metadataChannel).ptr; metadata; metadata = metadata->next)
		metadata_numeric(client, RPL_KEYVALUE, channel->chname, metadata->name, "*", metadata->value);
}

void metadata_send_all_for_user(Client *user, Client *client){
	struct metadata *metadata;
	if(!user) user = client;
	struct moddata_user *moddata = moddata_client(user, metadataUser).ptr;
	if(!moddata) return;
	for(metadata = moddata->metadata; metadata; metadata = metadata->next)
		metadata_numeric(client, RPL_KEYVALUE, user->name, metadata->name, "*", metadata->value);
}

int key_valid(char *key){
	for(;*key;key++){
		if(*key >= 'a' && *key <= 'z') continue;
		if(*key >= 'A' && *key <= 'Z') continue;
		if(*key >= '0' && *key <= '9') continue;
		if(*key == '_' || *key == '.' || *key == ':' || *key == '-') continue;
		return 0;
	}
	return 1;
}

int check_perms(Client *user, Channel *channel, Client *client, char *key, int mode){ // either user or channel should be NULL
	Membership *lp;
	if((user == client) || (!user && !channel)) // specified target is "*" or own nick
		return 1;
	if(IsOper(client) && mode == MODE_GET)
		return 1; // allow ircops to view everything
	if(channel){
		if((lp = find_membership_link(client->user->channel, channel)) && ((lp->flags & HOP_OR_MORE) || (mode == MODE_GET))) // allow setting channel metadata if we're halfop or more, and getting when we're just on this channel
			return 1;
	} else if(user){
		if(mode == MODE_SET){
			if(user == client) return 1;
		} else if(mode==MODE_GET){
			if(has_common_channels(user, client)) return 1;
		}
		
	}
	if(key) metadata_numeric(client, ERR_KEYNOPERMISSION, user?user->name:channel->chname, key);
	return 0;
}

CMD_FUNC(cmd_metadata_local){ // METADATA <Target> <Subcommand> [<Param 1> ... [<Param n>]]
	Channel *channel = NULL;
	Client *user = NULL;
	
	char buf[1024] = "";
	int i;
	int trylater;
	
	if(metadata_settings.enable_debug){
		for(i=1; i<parc; i++){
			if(!BadPtr(parv[i])) strncat(buf, parv[i], 1023);
			strncat(buf, " ", 1023);
		}
		sendto_snomask(SNO_JUNK, "Received METADATA, sender %s, params: %s", client->name, buf);
	}
	
	CHECKPARAMSCNT_OR_DIE(2, return);
	char *target = parv[1];
	char *cmd = parv[2];
	char *key;
	int keyindex = 3-1;
	char *value = NULL;
	char *channame;
	
	if(!strcasecmp(cmd, "GET")){
		CHECKREGISTERED_OR_DIE(client, return);
		CHECKPARAMSCNT_OR_DIE(3, return);
		PROCESS_TARGET_OR_DIE(target, user, channel, return);
		FOR_EACH_KEY(){
			if(check_perms(user, channel, client, key, MODE_GET)){
				if(!key_valid(key)){
					metadata_numeric(client, ERR_KEYINVALID, key);
					continue;
				}
				if(channel){
					metadata_send_channel(channel, key, client);
				} else {
					metadata_send_user(user, key, client);
				}
			}
		}
	} else if(!strcasecmp(cmd, "LIST")){ // we're just not sending anything if there are no permissions
		CHECKREGISTERED_OR_DIE(client, return);
		PROCESS_TARGET_OR_DIE(target, user, channel, return);
		if(check_perms(user, channel, client, NULL, MODE_GET)){
			if(channel){
				metadata_send_all_for_channel(channel, client);
			} else {
				metadata_send_all_for_user(user, client);
			}
		}
		metadata_numeric(client, RPL_METADATAEND);
	} else if(!strcasecmp(cmd, "SET")){
		CHECKPARAMSCNT_OR_DIE(3, return);
		PROCESS_TARGET_OR_DIE(target, user, channel, return);
		key = parv[3];
		if(!check_perms(user, channel, client, key, MODE_SET)) return;
		if(parc > 3 && !BadPtr(parv[4])) value = parv[4];

		if(!key_valid(key)){
			metadata_numeric(client, ERR_KEYINVALID, key);
			return;
		}

		if(channel){
			metadata_set_channel(channel, key, value, client);
		} else {
			metadata_set_user(user, key, value, client);
		}
	} else if(!strcasecmp(cmd, "CLEAR")){
		CHECKREGISTERED_OR_DIE(client, return);
		PROCESS_TARGET_OR_DIE(target, user, channel, return);
		if(check_perms(user, channel, client, "*", MODE_SET)){
			if(channel){
				metadata_clear_channel(channel, client);
			} else {
				metadata_clear_user(user, client);
			}
		}
		metadata_numeric(client, RPL_METADATAEND);
	} else if(!strcasecmp(cmd, "SUB")){
		PROCESS_TARGET_OR_DIE(target, user, channel, return);
		CHECKPARAMSCNT_OR_DIE(3, return);
		trylater = 0;
		FOR_EACH_KEY(){
			if(key_valid(key)){
				if(metadata_subscribe(key, client, 0))
					trylater = 1;
			} else {
				metadata_numeric(client, ERR_KEYINVALID, key);
				continue;
			}
		}
/*		if(trylater)
			metadata_numeric(client, ERR_METADATASYNCLATER, "*", "5"); // tell client to sync after 5 seconds
*/
		metadata_numeric(client, RPL_METADATAEND);
	} else if(!strcasecmp(cmd, "UNSUB")){
		CHECKREGISTERED_OR_DIE(client, return);
		CHECKPARAMSCNT_OR_DIE(3, return);
		int subok = 0;
		FOR_EACH_KEY(){
			if(key_valid(key)){
				metadata_subscribe(key, client, 1);
			} else {
				metadata_numeric(client, ERR_KEYINVALID, key);
				continue;
			}
		}
		metadata_numeric(client, RPL_METADATAEND);
	} else if(!strcasecmp(cmd, "SUBS")){
		CHECKREGISTERED_OR_DIE(client, return);
		metadata_send_subscribtions(client);
		metadata_numeric(client, RPL_METADATAEND);
	} else if(!strcasecmp(cmd, "SYNC")){ // the SYNC command is now ignored, as we're using events to send out the queue
		CHECKREGISTERED_OR_DIE(client, return);
		PROCESS_TARGET_OR_DIE(target, user, channel, return);
/*		if(channel){
			metadata_sync(channel->chname, client);
		} else {
			metadata_sync(user->name, client);
		}*/
	} else if(!strcasecmp(cmd, "RESYNC")){ // custom command to resend the data to remote servers without splitting
		if(!IsOper(client)){
			sendnumeric(client, ERR_NOPRIVILEGES);
			return;
		}
		list_for_each_entry(user, &server_list, special_node){ // notifications for linked servers
			if(user == &me) continue;
			metadata_server_sync(user);
		}
	} else {
		metadata_numeric(client, ERR_METADATAINVALIDSUBCOMMAND, cmd);
	}
}

// format of S2S is same as the event: ":origin METADATA <client/channel> <key name> *[ :<key value>]"
CMD_FUNC(cmd_metadata_remote){ // handling data from linked server
	Channel *channel = NULL;
	Client *user = NULL;
	char *target= parv[1];
	char *key;
	char *value;
	char *channame = strchr(target, '#');
	
	char buf[1024] = "";
	int i;
	if(metadata_settings.enable_debug){
		for(i=1; i<parc; i++){
			if(!BadPtr(parv[i])) strncat(buf, parv[i], 1023);
			strncat(buf, " ", 1023);
		}
		sendto_snomask(SNO_JUNK, "Received remote METADATA, sender: %s, params: %s", client->name, buf);
	}
	
	if(parc < 5 || BadPtr(parv[4])){
		if(parc == 4 && !BadPtr(parv[3])){
			value = NULL;
		} else {
			sendto_snomask(SNO_JUNK, "METADATA not enough args from %s", client->name);
			return;
		}
	} else {
		value = parv[4];
	}
	target = parv[1];
	key = parv[2];
	channame = strchr(target, '#');

	if(!*target || !strcmp(target, "*") || !key_valid(key)){
		sendto_snomask(SNO_JUNK, "Bad metadata target %s or key %s from %s", target, key, client->name);
		return;
	}
	PROCESS_TARGET_OR_DIE(target, user, channel, return);

	if(channel){
		metadata_set_channel(channel, key, value, client);
	} else {
		metadata_set_user(user, key, value, client);
	}
}

CMD_FUNC(cmd_metadata){
	if(client != &me && MyConnect(client) && !IsServer(client))
		cmd_metadata_local(client, recv_mtags, parc, parv);
	else
		cmd_metadata_remote(client, recv_mtags, parc, parv);
}

int metadata_server_sync(Client *client){ // we send all our data to the server that was just linked
	Client *acptr;
	struct moddata_user *moddata;
	struct metadata *metadata;
	unsigned int  hashnum;
	Channel *channel;
	
	list_for_each_entry(acptr, &client_list, client_node){ // send out users (all on our side of the link)
		moddata = moddata_client(acptr, metadataUser).ptr;
		if(!moddata) continue;
		for(metadata = moddata->metadata; metadata; metadata = metadata->next)
			send_change(client, acptr->name, metadata->name, metadata->value, &me);
	}

	for(hashnum = 0; hashnum < CHAN_HASH_TABLE_SIZE; hashnum++){ // send out channels
		for(channel = hash_get_chan_bucket(hashnum); channel; channel = channel->hnextch){
			for(metadata = moddata_channel(channel, metadataChannel).ptr; metadata; metadata = metadata->next)
				send_change(client, channel->chname, metadata->name, metadata->value, &me);
		}
	}
	return 0;
}

int metadata_join(Client *client, Channel *channel, MessageTag *mtags, char *parv[]){
	Client *acptr;
	Member *cm;
	char *value;
	struct unsynced *prev_us;
	struct unsynced *us;
	Membership *lp;
	struct subscriptions *subs;
	struct metadata *metadata;

	struct moddata_user *moddata = moddata_client(client, metadataUser).ptr;
	if(!moddata) return 0; // the user is both not subscribed to anything and has no own data
	for(metadata = moddata->metadata; metadata; metadata = metadata->next){ // if joining user has metadata, let's notify all subscribers
		list_for_each_entry(acptr, &lclient_list, lclient_node){
			if(IsMember(acptr, channel) && is_subscribed(acptr, metadata->name))
				notify_or_queue(acptr, client->name, metadata->name, metadata->value, NULL);
		}
	}
	for(subs = moddata->subs; subs; subs = subs->next){
		value = get_channel_key_value(channel, subs->name); // notify joining user about channel metadata
		if(value)
			notify_or_queue(client, channel->chname, subs->name, value, NULL);
		for(cm = channel->members; cm; cm = cm->next){ // notify joining user about other channel members' metadata
			acptr = cm->client;
			if(acptr == client) continue; // ignore own data
			value = get_user_key_value(acptr, subs->name);
			if(value)
				notify_or_queue(client, acptr->name, subs->name, value, NULL);
		}
	}
	return 0;
}

void metadata_sync(char *what, Client *client){ // the argument can be either channel or user, but we're ignoring it anyway and syncing everything
	int trylater = 0;
	Client *acptr;
	Channel *channel = NULL;

	struct moddata_user *my_moddata = moddata_client(client, metadataUser).ptr;
	if(!my_moddata)
		return; // nothing queued
	struct unsynced *us = my_moddata->us;
	struct unsynced *prev_us;
	
	while(us){
		if(!IsSendable(client)){
			trylater = 1;
			break;
		}
		acptr = hash_find_nickatserver(us->name, NULL);
		if(acptr && has_common_channels(acptr, client)){ // if not, the user has vanished since or one of us parted the channel
			struct moddata_user *moddata = moddata_client(acptr, metadataUser).ptr;
			if(moddata){
				struct metadata *metadata = moddata->metadata;
				while(metadata){
					if(!strcasecmp(us->key, metadata->name)){ // has it
						char *value = get_user_key_value(acptr, us->key);
						if(value)
							send_change(client, us->name, us->key, value, NULL);
					}
					metadata = metadata->next;
				}
			}
		}
		// now remove the processed entry
		prev_us = us;
		us = us->next;
		safe_free(prev_us->name);
		safe_free(prev_us);
		my_moddata->us = us; // we're always removing the first list item
	}

/*	if(trylater){
		metadata_numeric(client, ERR_METADATASYNCLATER, what, "5"); // tell client to sync after 5 seconds
	}
*/
}

int metadata_user_registered(Client *client){	//	if we have any metadata set at this point, let's broadcast it to other servers and users
	struct metadata *metadata;
	struct moddata_user *moddata = moddata_client(client, metadataUser).ptr;
	if(!moddata) return HOOK_CONTINUE;
	for(metadata = moddata->metadata; metadata; metadata = metadata->next)
		user_metadata_changed(client, metadata->name, metadata->value, client);
	return HOOK_CONTINUE;
}

EVENT(metadata_queue_evt){ // let's check every 1.5 seconds whether we have something to send
	Client *acptr;
	list_for_each_entry(acptr, &lclient_list, lclient_node){ // notifications for local subscribers
		if(!IsUser(acptr)) continue;
		metadata_sync("*", acptr);
	}
}

