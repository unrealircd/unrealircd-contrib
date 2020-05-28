/*
 * Stores user metadata (recognizing users by their accounts) and channel metadata
 * Created by k4be, based on channeldb.c
 * (C) Copyright 2019 Syzop, Gottem and the UnrealIRCd team
 * License: GPLv2
 */
 
 /*** <<<MODULE MANAGER START>>>
module
{
        documentation "https://github.com/pirc-pl/unrealircd-modules/blob/master/README.md#metadata-db";
        troubleshooting "In case of problems, contact k4be on irc.pirc.pl.";
        min-unrealircd-version "5.*";
        post-install-text {
                "The module is installed. Now all you need to do is add a loadmodule line:";
                "loadmodule \"third/metadata-db\";";
  				"And /REHASH the IRCd.";
  				"It'll take care of users on all servers in your network.";
  				"The \"third/metadata\" module is required for it to work.";
				"The metadata-db may be additionaly configured to change the defaults.";
				"See documentation for help.";
        }
}
*** <<<MODULE MANAGER END>>>
*/

#include "unrealircd.h"

ModuleHeader MOD_HEADER = {
	"third/metadata-db",   /* Name of module */
	"5.0.2", /* Version */
	"Metadata storage module", /* Short description of module */
	"k4be@PIRC",
	"unrealircd-5"
};

#define TRIGGER_LOGIN_ON_UID (UNREAL_VERSION_GENERATION == 5 && UNREAL_VERSION_MAJOR == 0 && UNREAL_VERSION_MINOR < 6)

#define METADATADB_VERSION 100
#define METADATADB_SAVE_EVERY 299

#define MAGIC_ENTRY_START	0x11111111
#define MAGIC_ENTRY_END	0x22222222

#define MYCONF "metadata-db"

#define WARN_WRITE_ERROR(fname) \
	do { \
		sendto_realops_and_log("[metadata-db] Error writing to temporary database file " \
		                       "'%s': %s (DATABASE NOT SAVED)", \
		                       fname, strerror(errno)); \
	} while(0)

#define W_SAFE(x) \
	do { \
		if (!(x)) { \
			WARN_WRITE_ERROR(tmpfname); \
			fclose(fd); \
			return 0; \
		} \
	} while(0)

#define IsMDErr(x, y, z) \
	do { \
		if (!(x)) { \
			config_error("A critical error occurred when registering ModData for %s: %s", MOD_HEADER.name, ModuleGetErrorStr((z)->handle)); \
			return MOD_FAILED; \
		} \
	} while(0)

#define FOREACH_CHANNEL_METADATA(channel,metadata) for(metadata=moddata_channel(channel, channelmd).ptr; metadata; metadata=metadata->next)
#define FOREACH_USER_METADATA(acptr,metadata) struct moddata_user *moddata = moddata_client(acptr, usermd).ptr; if(moddata) for(metadata=moddata->metadata; metadata; metadata=metadata->next)
#define FOREACH_STORED_METADATA(sm) for(sm = metadata_storage; sm; sm = sm->next)

struct metadata {
	char *name;
	char *value;
	struct metadata *next;
};

struct moddata_user {
	struct metadata *metadata;
	struct subscriptions *subs;
	struct unsynced *us;
};

struct metadata_storage {
	char *account;
	char *name;
	char *value;
	time_t last_seen;
	struct metadata_storage *next;
};

/* Forward declarations */
void metadatadb_moddata_free(ModData *md);
void setcfg(void);
void freecfg(void);
int metadatadb_configtest(ConfigFile *cf, ConfigEntry *ce, int type, int *errs);
int metadatadb_configrun(ConfigFile *cf, ConfigEntry *ce, int type);
EVENT(write_metadatadb_evt);

#ifndef HOOKTYPE_ACCOUNT_LOGIN
CMD_OVERRIDE_FUNC(cmd_svslogin);
CMD_OVERRIDE_FUNC(cmd_svsmode);
#endif

#if TRIGGER_LOGIN_ON_UID
CMD_OVERRIDE_FUNC(cmd_uid);
#endif

int account_login(Client *client, MessageTag *recv_mtags);
int user_quit(Client *client, MessageTag *mtags, char *comment);

int write_metadatadb(void);
int write_metadata_entry(FILE *fd, const char *tmpfname, struct metadata *metadata, char *name, time_t last_seen);
void store_metadata_for_user(Client *client, int remove);
int read_metadatadb(void);
void free_metadata_storage(void);
int how_many_metadata_channel(Channel *channel, ModDataInfo *channelmd);
int how_many_metadata_user(Client *client, ModDataInfo *usermd);
void store_metadata(char *account, struct metadata *metadata, time_t last_seen);
void send_out_metadata(char *name, char *key, char *value);
void set_channel_metadata(Channel *channel, struct metadata *metadata);
void set_user_metadata(char *account, struct metadata *metadata, time_t last_seen);

/* Global variables */
static uint32_t metadatadb_version = METADATADB_VERSION;
struct cfgstruct {
	char *database;
	int expire_after;
};
static struct cfgstruct cfg;
static struct metadata_storage *metadata_storage;

MOD_TEST(){
	memset(&cfg, 0, sizeof(cfg));
	HookAdd(modinfo->handle, HOOKTYPE_CONFIGTEST, 0, metadatadb_configtest);
	return MOD_SUCCESS;
}

MOD_INIT(){
	setcfg();

	if (!read_metadatadb()){
		char fname[512];
		snprintf(fname, sizeof(fname), "%s.corrupt", cfg.database);
		if (rename(cfg.database, fname) == 0)
			config_warn("[metadata-db] Existing database renamed to %s and starting a new one...", fname);
		else
			config_warn("[metadata-db] Failed to rename database from %s to %s: %s", cfg.database, fname, strerror(errno));
	}
	HookAdd(modinfo->handle, HOOKTYPE_CONFIGRUN, 0, metadatadb_configrun);
	return MOD_SUCCESS;
}

MOD_LOAD(){
	EventAdd(modinfo->handle, "metadatadb_write_metadatadb", write_metadatadb_evt, NULL, METADATADB_SAVE_EVERY*1000, 0);
	if (ModuleGetError(modinfo->handle) != MODERR_NOERROR){
		config_error("A critical error occurred when loading module %s: %s", MOD_HEADER.name, ModuleGetErrorStr(modinfo->handle));
		return MOD_FAILED;
	}
#ifndef HOOKTYPE_ACCOUNT_LOGIN // we have no ACCOUNT_LOGIN hook (added in 5.0.4), so we're on our own to handle that
	if(!CommandOverrideAddEx(modinfo->handle, "SVSLOGIN", 0, cmd_svslogin)){
		config_error("[%s] Crritical: Failed to request command override for SVSLOGIN: %s", MOD_HEADER.name, ModuleGetErrorStr(modinfo->handle));
	}
	if(!CommandOverrideAddEx(modinfo->handle, "SVSMODE", 0, cmd_svsmode)){
		config_error("[%s] Crritical: Failed to request command override for SVSMODE: %s", MOD_HEADER.name, ModuleGetErrorStr(modinfo->handle));
	}
	if(!CommandOverrideAddEx(modinfo->handle, "SVS2MODE", 0, cmd_svsmode)){
		config_error("[%s] Crritical: Failed to request command override for SVS2MODE: %s", MOD_HEADER.name, ModuleGetErrorStr(modinfo->handle));
	}
#else
	HookAdd(modinfo->handle, HOOKTYPE_ACCOUNT_LOGIN, 0, account_login);
#endif // HOOKTYPE_ACCOUNT_LOGIN

#if TRIGGER_LOGIN_ON_UID // 5.0.5 and 5.0.4 did not call ACCOUNT_LOGIN on UID
	if(!CommandOverrideAddEx(modinfo->handle, "UID", 0, cmd_uid)){
		config_error("[%s] Crritical: Failed to request command override for UID: %s", MOD_HEADER.name, ModuleGetErrorStr(modinfo->handle));
	}
#endif

	HookAdd(modinfo->handle, HOOKTYPE_REMOTE_QUIT, 0, user_quit);
	HookAdd(modinfo->handle, HOOKTYPE_LOCAL_QUIT, 0, user_quit);

	Client *acptr;

	list_for_each_entry(acptr, &client_list, client_node){ // process all users that are already connected
		if(!IsUser(acptr)) continue;
		account_login(acptr, NULL);
	}
	return MOD_SUCCESS;
}

void free_metadata_storage(void){
	struct metadata_storage *curr = metadata_storage, *prev = NULL;
	while(curr){
		prev = curr;
		curr = curr->next;
		safe_free(prev->account);
		safe_free(prev->name);
		safe_free(prev->value);
		safe_free(prev);
	}
}

MOD_UNLOAD(){
	write_metadatadb();
	freecfg();
	free_metadata_storage();
	return MOD_SUCCESS;
}

void setcfg(void){
	// Default: data/metadata.db
	safe_strdup(cfg.database, "metadata.db");
	convert_to_absolute_path(&cfg.database, PERMDATADIR);
	cfg.expire_after = 365; // a year
}

void freecfg(void){
	safe_free(cfg.database);
}

/*
metadata-db {
	database "metadata.db";
	expire-after 365; // days
}
*/

int metadatadb_configtest(ConfigFile *cf, ConfigEntry *ce, int type, int *errs){
	int errors = 0;
	ConfigEntry *cep;
	int i;

	if (type != CONFIG_MAIN)
		return 0;

	if (!ce || strcmp(ce->ce_varname, MYCONF))
		return 0;

	for (cep = ce->ce_entries; cep; cep = cep->ce_next)
	{
		if (!cep->ce_vardata) {
			config_error("%s:%i: blank %s::%s without value", cep->ce_fileptr->cf_filename, cep->ce_varlinenum, MYCONF, cep->ce_varname);
			errors++;
			continue;
		}
		if (!strcmp(cep->ce_varname, "database")) {
			convert_to_absolute_path(&cep->ce_vardata, PERMDATADIR);
			continue;
		}
		if (!strcmp(cep->ce_varname, "expire-after")) {
			// Should be an integer yo
			for(i = 0; cep->ce_vardata[i]; i++) {
				if(!isdigit(cep->ce_vardata[i])) {
					config_error("%s:%i: %s::%s must be an integer between 1 and 1000 (days)", cep->ce_fileptr->cf_filename, cep->ce_varlinenum, MYCONF, cep->ce_varname);
					errors++; // Increment err0r count fam
					break;
				}
			}
			if(!errors && (atoi(cep->ce_vardata) < 1 || atoi(cep->ce_vardata) > 1000)) {
				config_error("%s:%i: %s::%s must be an integer between 1 and 1000 (days)", cep->ce_fileptr->cf_filename, cep->ce_varlinenum, MYCONF, cep->ce_varname);
				errors++; // Increment err0r count fam
			}
			continue;
		}
		config_error("%s:%i: unknown directive metadata-db::%s", cep->ce_fileptr->cf_filename, cep->ce_varlinenum, cep->ce_varname);
		errors++;
	}

	*errs = errors;
	return errors ? -1 : 1;
}

int metadatadb_configrun(ConfigFile *cf, ConfigEntry *ce, int type){
	ConfigEntry *cep;

	// We are only interested in set::channeldb::database
	if (type != CONFIG_MAIN)
		return 0;

	if (!ce || strcmp(ce->ce_varname, "metadata-db"))
		return 0;

	for (cep = ce->ce_entries; cep; cep = cep->ce_next){
		if(!strcmp(cep->ce_varname, "database"))
			safe_strdup(cfg.database, cep->ce_vardata);
		if(!strcmp(cep->ce_varname, "expire-after"))
			cfg.expire_after = atoi(cep->ce_vardata);
	}
	return 1;
}

EVENT(write_metadatadb_evt){
	Client *acptr;
	list_for_each_entry(acptr, &client_list, client_node){ // process all users that are already connected
		if(!IsUser(acptr)) continue;
		if(isdigit(*acptr->user->svid)) continue;
		store_metadata_for_user(acptr, 1);
	}
	write_metadatadb();
}

int how_many_metadata_channel(Channel *channel, ModDataInfo *channelmd){
	int count = 0;
	struct metadata *metadata;
	FOREACH_CHANNEL_METADATA(channel, metadata){
		count++;
	}
	return count;
}

int how_many_metadata_user(Client *client, ModDataInfo *usermd){
	int count = 0;
	struct metadata *metadata;
	FOREACH_USER_METADATA(client, metadata){
		count++;
	}
	return count;
}

int write_metadatadb(void){
	char tmpfname[512];
	FILE *fd;
	Channel *channel;
	struct metadata *metadata;
	struct metadata_storage *sm;
	int cnt = 0;
	
	ModDataInfo *usermd = findmoddata_byname("metadata_user", MODDATATYPE_CLIENT);
	ModDataInfo *channelmd = findmoddata_byname("metadata_channel", MODDATATYPE_CHANNEL);
	if(!usermd || !channelmd){
		sendto_realops("[metadata-db] Error obtaining moddata for metadata! Maybe you forgot to load the metadata module?");
		return 0;
	}

	// Write to a tempfile first, then rename it if everything succeeded
	snprintf(tmpfname, sizeof(tmpfname), "%s.tmp", cfg.database);
	fd = fopen(tmpfname, "wb");
	if (!fd){
		WARN_WRITE_ERROR(tmpfname);
		return 0;
	}

	W_SAFE(write_data(fd, &metadatadb_version, sizeof(metadatadb_version)));

	/* First, count +P channel metadata entries */
	for (channel = channels; channel; channel=channel->nextch)
		if(has_channel_mode(channel, 'P'))
			cnt += how_many_metadata_channel(channel, channelmd);

	
	/* ... and stored user entries, then write the count to the database */
	
	FOREACH_STORED_METADATA(sm)
		cnt++;
	
	W_SAFE(write_int64(fd, cnt));

	// now write the actual data

	for(channel = channels; channel; channel=channel->nextch){
		/* We only care about +P (persistent) channels */
		if(has_channel_mode(channel, 'P')){
			FOREACH_CHANNEL_METADATA(channel, metadata){
				if(!write_metadata_entry(fd, tmpfname, metadata, channel->chname, 0))
					return 0;
			}
		}
	}
	
	metadata = safe_alloc(sizeof(struct metadata)); // lazy
	FOREACH_STORED_METADATA(sm){
		metadata->name = sm->name;
		metadata->value = sm->value;
		if(!write_metadata_entry(fd, tmpfname, metadata, sm->account, sm->last_seen))
			return 0;
	}
	safe_free(metadata);
	
	// Everything seems to have gone well, attempt to close and rename the tempfile
	if (fclose(fd) != 0){
		WARN_WRITE_ERROR(tmpfname);
		return 0;
	}

#ifdef _WIN32
	/* The rename operation cannot be atomic on Windows as it will cause a "file exists" error */
	unlink(cfg.database);
#endif
	if (rename(tmpfname, cfg.database) < 0){
		sendto_realops_and_log("[metadata-db] Error renaming '%s' to '%s': %s (DATABASE NOT SAVED)", tmpfname, cfg.database, strerror(errno));
		return 0;
	}

	return 1;
}

int write_metadata_entry(FILE *fd, const char *tmpfname, struct metadata *metadata, char *name, time_t last_seen){
	W_SAFE(write_int32(fd, MAGIC_ENTRY_START));
	/* Owner name */
	W_SAFE(write_str(fd, name));
	/* Last seen time (0 for channels) */
	W_SAFE(write_data(fd, &last_seen, sizeof(last_seen)));
	/* Metadata key name */
	W_SAFE(write_str(fd, metadata->name));
	/* Metadata key value */
	W_SAFE(write_str(fd, metadata->value));
	W_SAFE(write_int32(fd, MAGIC_ENTRY_END));
	return 1;
}

#define FreeMetadataEntry() \
 	do { \
		/* Some of these might be NULL */ \
		safe_free(name); \
		safe_free(metadata.name); \
		safe_free(metadata.value); \
	} while(0)

#define R_SAFE(x) \
	do { \
		if (!(x)) { \
			config_warn("[metadata-db] Read error from database file '%s' (possible corruption): %s", cfg.database, strerror(errno)); \
			fclose(fd); \
			FreeMetadataEntry(); \
			return 0; \
		} \
	} while(0)

void store_metadata(char *account, struct metadata *metadata, time_t last_seen){
	if(TStime() - (cfg.expire_after * 60 * 60 * 24) > last_seen){
		sendto_snomask(SNO_JUNK, "[metadata-db] Expiring metadata key %s for account %s, last seen %ld, current time %ld", metadata->name, account, (long int)last_seen, (long int)TStime());
		return; // dropping the outdated entry
	}
	struct metadata_storage *prev = NULL, *curr;
	for(curr = metadata_storage; curr; curr = curr->next){
		if(!strcmp(curr->account, account) && !strcasecmp(curr->name, metadata->name)){ // we already know this metadata - the user has changed it in the meantime
			safe_free(curr->value);
			curr->value = strdup(metadata->value);
			return; // we're done with this one
		}
		prev = curr;
	}
	curr = safe_alloc(sizeof(struct metadata_storage)); // create a new list entry
	curr->account = strdup(account);
	curr->name = strdup(metadata->name);
	curr->value = strdup(metadata->value);
	curr->last_seen = last_seen;
	if(!prev){ // adding first entry
		metadata_storage = curr;
	} else {
		prev->next = curr;
	}
}

void send_out_metadata(char *name, char *key, char *value){
//	do_cmd(Client *client, MessageTag *mtags, char *cmd, int parc, char *parv[])
	char *parv[] = {
		NULL,
		name,
		key,
		"*",
		value
	};
	do_cmd(&me, NULL, "METADATA", 5, parv);
}

void set_channel_metadata(Channel *channel, struct metadata *metadata){
	send_out_metadata(channel->chname, metadata->name, metadata->value);
}

void set_user_metadata(char *account, struct metadata *metadata, time_t last_seen){
	// first, find whether there is an user logged into this account
	Client *acptr;
	int found = 0;
	list_for_each_entry(acptr, &client_list, client_node){
		if(!IsUser(acptr)) continue;
		if(!isdigit(*acptr->user->svid)){
			if(!strcmp(acptr->user->svid, account)){
				found = 1;
				send_out_metadata(acptr->name, metadata->name, metadata->value); // there may be more than one user with a single account, so no "break"
			}
		}
	}

	if(found)
		last_seen = TStime();
	store_metadata(account, metadata, last_seen);
}

int read_metadatadb(void){
	FILE *fd;
	uint32_t version;
	int added = 0;
	int i;
	uint64_t count = 0;
	uint32_t magic;

	// new data will be stored in these vars
	Channel *channel;
	struct metadata metadata;
	char *name;
	time_t last_seen;

	fd = fopen(cfg.database, "rb");
	if (!fd){
		if (errno == ENOENT){
			/* Database does not exist. Could be first boot */
			config_warn("[metadata-db] No database present at '%s', will start a new one", cfg.database);
			return 1;
		} else {
			config_warn("[metadata-db] Unable to open the database file '%s' for reading: %s", cfg.database, strerror(errno));
			return 0;
		}
	}
	
	R_SAFE(read_data(fd, &version, sizeof(version)));
	if (version > metadatadb_version){
		config_warn("[metadata-db] Database '%s' has a wrong version: expected it to be <= %u but got %u instead", cfg.database, metadatadb_version, version);
		fclose(fd);
		return 0;
	}

	R_SAFE(read_data(fd, &count, sizeof(count)));

	for (i=1; i <= count; i++){
		name = NULL;
		metadata.name = NULL;
		metadata.value = NULL;
		last_seen = 0;

		R_SAFE(read_data(fd, &magic, sizeof(magic)));
		if (magic != MAGIC_ENTRY_START)		{
			config_error("[metadata-db] Corrupt database (%s) - metadata magic start is 0x%x. Further reading aborted.", cfg.database, magic);
			break;
		}
		R_SAFE(read_str(fd, &name));
		R_SAFE(read_data(fd, &last_seen, sizeof(last_seen)));
		R_SAFE(read_str(fd, &metadata.name));
		R_SAFE(read_str(fd, &metadata.value));
		R_SAFE(read_data(fd, &magic, sizeof(magic)));

		/* If we got this far, we can initialize the data with the above */
		if(*name == '#'){ // a channel
			channel = get_channel(&me, name, CREATE);
			set_channel_metadata(channel, &metadata);
		} else {
			set_user_metadata(name, &metadata, last_seen);
		}
		FreeMetadataEntry();
		added++;

		if (magic != MAGIC_ENTRY_END){
			config_error("[metadata-db] Corrupt database (%s) - metadata magic end is 0x%x. Further reading aborted.", cfg.database, magic);
			break;
		}
	}

	fclose(fd);

	if (added)
		sendto_realops_and_log("[metadata-db] Read %d metadata entries", added);
	return 1;
}
#undef FreeMetadataEntry
#undef R_SAFE

#ifndef HOOKTYPE_ACCOUNT_LOGIN
CMD_OVERRIDE_FUNC(cmd_svslogin){ // that's based on modules/sasl.c
	Client *target;

	CallCommandOverride(ovr, client, recv_mtags, parc, parv);
	if (!SASL_SERVER || MyUser(client) || (parc < 3) || !parv[3])
		return;
	target = find_client(parv[2], NULL);
	if(!target)
		return;
	sendto_snomask(SNO_JUNK, "[metadata-db] Acting on SVSLOGIN for %s", target->name);
	account_login(target, recv_mtags);
}

CMD_OVERRIDE_FUNC(cmd_svsmode){ // and that's based on modules/svsmode.c
	int i;
	char *m;
	Client *target;
	long setflags = 0;

	CallCommandOverride(ovr, client, recv_mtags, parc, parv);

	if (!IsULine(client))
		return;


	if (parc < 3 || parv[1][0] == '#')
		return;
	if (!(target = find_person(parv[1], NULL)))
		return;
	for (m = parv[2]; *m; m++)
		switch (*m){
			/* we may not get these, but they shouldnt be in default */
			case ' ':
			case '\n':
			case '\r':
			case '\t':
				break;
			case 'd':
				if (parv[3]){ /*  else setting deaf */
					sendto_snomask(SNO_JUNK, "[metadata-db] Acting on SVSMODE for %s", target->name);
					account_login(target, recv_mtags);
				}
				break;
		} /*switch*/
}
#endif //HOOKTYPE_ACCOUNT_LOGIN

#if TRIGGER_LOGIN_ON_UID
CMD_OVERRIDE_FUNC(cmd_uid){
	char *sstamp;
	char *nick;
	Client *acptr;

	CallCommandOverride(ovr, client, recv_mtags, parc, parv);
	if (parc < 13 || !IsServer(client))
		return;
	sstamp = parv[7];
	nick = parv[1];
	
	acptr = find_person(nick, NULL);
	if(!acptr)
		return;
	
	if(IsUser(acptr) && *sstamp != '*'){
		sendto_snomask(SNO_JUNK, "[metadata-db] Acting on UID for %s", acptr->name);
		account_login(acptr, recv_mtags);
	}
}
#endif // TRIGGER_LOGIN_ON_UID

void store_metadata_for_user(Client *client, int remove){ // client must be logged in
	struct metadata *metadata;
	struct metadata_storage *sm, *prev_sm, *next_sm;
	ModDataInfo *usermd;
	int found;
	
	usermd = findmoddata_byname("metadata_user", MODDATATYPE_CLIENT);
	if(!usermd){
		sendto_realops("[metadata-db] Error obtaining moddata for metadata! Maybe you forgot to load the metadata module?");
		return;
	}
	
	FOREACH_USER_METADATA(client, metadata){
		found = 0;
		prev_sm = NULL;
		FOREACH_STORED_METADATA(sm){
			prev_sm = sm;
			if(strcmp(sm->account, client->user->svid)) // other user
				continue;
			if(strcmp(sm->name, metadata->name)) // key name not matching
				continue;
			// now let's replace the stored metadata, as the user's one has priority
			found = 1;
			if(strcmp(sm->value, metadata->value)){
				sendto_snomask(SNO_JUNK, "[metadata-db] %s replacing key %s", client->name, metadata->name);
				safe_free(sm->value);
				sm->value = strdup(metadata->value);
			}
			sm->last_seen = TStime();
		}
		if(found)
			continue;
		// then save user's metadata to the storage (the user set something before logging in)
		sm = safe_alloc(sizeof(struct metadata_storage));
		sm->account = strdup(client->user->svid);
		sm->name = strdup(metadata->name);
		sm->value = strdup(metadata->value);
		sm->last_seen = TStime();
		sendto_snomask(SNO_JUNK, "[metadata-db] %s saving key %s", client->name, metadata->name);
		if(!prev_sm){
			metadata_storage = sm;
		} else {
			prev_sm->next = sm;
		}
	}
	
	// remove metadata that the user no longer wants
	if(!remove) return;
	sm = metadata_storage;
	prev_sm = NULL;
	while(sm){
		next_sm = sm->next;
		if(!strcmp(sm->account, client->user->svid)){ // other user
			found = 0;
			FOREACH_USER_METADATA(client, metadata){
				if(!strcmp(sm->name, metadata->name)){
					found = 1;
					break;
				}
			}
			if(!found){ // drop from the list
				sendto_snomask(SNO_JUNK, "[metadata-db] %s dropping key %s", client->name, sm->name);
				if(prev_sm){
					prev_sm->next = sm->next;
				} else {
					metadata_storage = sm->next;
				}
				safe_free(sm->account);
				safe_free(sm->name);
				safe_free(sm->value);
				safe_free(sm);
			} else {
				prev_sm = sm;
			}
		} else {
			prev_sm = sm;
		}
		sm = next_sm;
	}
}

int account_login(Client *client, MessageTag *recv_mtags){
	ModDataInfo *usermd;
	struct metadata_storage *sm;

	usermd = findmoddata_byname("metadata_user", MODDATATYPE_CLIENT);
	if(!usermd){
		sendto_realops("[metadata-db] Error obtaining moddata for metadata! Maybe you forgot to load the metadata module?");
		return 0;
	}
	if(isdigit(*client->user->svid)){ // just logged out, ignoring
		return 0;
	}

	store_metadata_for_user(client, 0);

	// now let's set all stored metadata for the user
	FOREACH_STORED_METADATA(sm){
		if(strcmp(sm->account, client->user->svid)) // other user
			continue;
		send_out_metadata(client->name, sm->name, sm->value);
		sm->last_seen = TStime();
	}
	return 0;
}

int user_quit(Client *client, MessageTag *mtags, char *comment){ // store metadata for quitting users
	if(!IsUser(client)) return 0;
	sendto_snomask(SNO_JUNK, "[metadata-db] %s quits, svid is %s", client->name, client->user->svid);
	if(isdigit(*client->user->svid)) return 0;
	store_metadata_for_user(client, 1);
	return 0;
}

