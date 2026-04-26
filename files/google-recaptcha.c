/*
  Licence: GPLv3
  Copyright Ⓒ 2024 Valerie Liu
*/

/*** <<<MODULE MANAGER START>>>
module
{
		documentation "https://github.com/ValwareIRC/valware-unrealircd-mods/blob/main/google-recaptcha/google-recaptcha.md";
		troubleshooting "In case of problems, please file a bug report at https://github.com/ValwareIRC/valware-unrealircd-mods/issues/new?template=bug_report.md";
		min-unrealircd-version "6.1.8";
		max-unrealircd-version "6.*";
		post-install-text {
				"The module is installed. Now all you need to do is add a loadmodule line:";
				"loadmodule \"third/google-recaptcha\";";
				"Don't forget to configure a 'recaptcha{}' block to point at your verification page (See docs)";
				"Once you're good to go, you can finally type: ./unrealircd rehash";
		}
}
*** <<<MODULE MANAGER END>>>
*/

#include "unrealircd.h"

ModuleHeader MOD_HEADER
={
	"third/google-recaptcha",
	"1.0",
	"Protect your UnrealIRCd network with Google reCAPTCHA",
	"Valware",
	"unrealircd-6",
};

#define RECAPTCHA_CONF "recaptcha"
#define RECAPTCHA_DB "../data/recaptcha.db"
#define GetRCCode(x)			moddata_client_get(x, "recaptcha_code")
#define SetRCCode(x, y)		do{ moddata_client(x, recaptcha_code).str = strdup(y); } while (0)
#define UnsetRCCode(x)		do{ moddata_client_set(x, "recaptcha_code", NULL); } while (0)

void set_config_defaults(void);
void free_config(void);
int recaptcha_configtest(ConfigFile *cf, ConfigEntry *ce, int type, int *errs);
int recaptcha_configposttest(int *errs); // You may not need this
int recaptcha_configrun(ConfigFile *cf, ConfigEntry *ce, int type);


struct recaptcha_conf{
	char *url;
	unsigned long ipcache;
	unsigned long timeout;

	unsigned short int got_url;
	unsigned short int got_ipcache;
	unsigned short int got_timeout;
};
static struct recaptcha_conf recaptcha_conf;



ModDataInfo *recaptcha_code;

void recaptcha_code_free(ModData *m);
const char *recaptcha_code_serialize(ModData *m);
void recaptcha_code_unserialize(const char *str, ModData *m);

/* Forward declarations */
RPC_CALL_FUNC(rpc_recaptcha_find);
RPC_CALL_FUNC(rpc_recaptcha_allow);


/* Hooks */
int recaptcha_pre_connect(Client *client);
int recaptcha_server_sync(Client *client);
int recaptcha_pre_local_handshake_timeout(Client *client, const char **comment);

json_t *match_rline(Client *client);
void generateRandomString(char *randomString, int length);
int remember_ip(const char *ip, time_t expiry);
bool is_ip_in_memory(const char *ip);


CMD_FUNC(CMD_RLINE);
CMD_FUNC(CMD_UNRLINE);
CMD_FUNC(CMD_RLINESYNC);
CMD_FUNC(CMD_REMOTE_ALLOW_RECAPTCHA);

EVENT(recaptcha_clearup_event);

static char *rc_help[] ={
	"*** RLINE/UNRLINE ***",
	"This command lets you add TKL-like 'R-Lines'.",
	"When a user is affected by an R-Line, they will be required to",
	"complete a Google ReCAPTCHA in order to connect to the server.",
	" ",
	"RLINE supports Extended Server Bans, see the following link for more info:",
	"https://www.unrealircd.org/docs/Extended_server_bans",
	" ",
	"Syntax:",
	"    RLINE <mask|nick> <duration> <reason>",
	"    UNRLINE <mask|nick>",
	" ",
	"RLINE Examples:",
	"    RLINE ~security-group:unknown-users New users are required to prove humanity",
	"    RLINE *@8.8.8.8 Only humans from google plz",
	"    RLINE ~asn:16276 VPS users from OVH are required to prove humanity",
	"    RLINE Valware You look like a bot, please prove your humanity",
	" ",
	"UNRLINE Examples:",
	"    UNRLINE *@8.8.8.8",
	"    UNRLINE ~asn:16276",
	"    UNRLINE Valware",
	" ",
	NULL
};
static void rline_help(Client *client)
{
	for(char **p = rc_help; *p != NULL; p++)
		sendto_one(client, NULL, ":%s %03d %s :%s", me.name, RPL_TEXT, client->name, *p);
}


MOD_INIT()
{
	RPCHandlerInfo r;
	ModDataInfo mreq;

	MARK_AS_GLOBAL_MODULE(modinfo);
	
	set_config_defaults();
	HookAdd(modinfo->handle, HOOKTYPE_CONFIGRUN, 0, recaptcha_configrun);

	memset(&r, 0, sizeof(r));
	r.method = "recaptcha.find";
	r.loglevel = ULOG_DEBUG;
	r.call = rpc_recaptcha_find;
	if (!RPCHandlerAdd(modinfo->handle, &r))
	{
		config_error("[third/google-recaptcha] Could not register RPC handler");
		return MOD_FAILED;
	}

	memset(&r, 0, sizeof(r));
	r.method = "recaptcha.allow";
	r.loglevel = ULOG_DEBUG;
	r.call = rpc_recaptcha_allow;
	if (!RPCHandlerAdd(modinfo->handle, &r))
	{
		config_error("[third/google-recaptcha] Could not register RPC handler");
		return MOD_FAILED;
	}

	memset(&mreq, 0, sizeof(mreq));
	mreq.name = "recaptcha_code";
	mreq.free = recaptcha_code_free;
	mreq.serialize = recaptcha_code_serialize;
	mreq.unserialize = recaptcha_code_unserialize;
	mreq.type = MODDATATYPE_CLIENT;
	if (!(recaptcha_code = ModDataAdd(modinfo->handle, mreq)))
	{
		config_error("Could not add ModData for `recaptcha_code`. Please contact developer.");
		return MOD_FAILED;
	}

	CommandAdd(modinfo->handle, "RLINE", CMD_RLINE, 3, CMD_OPER);
	CommandAdd(modinfo->handle, "UNRLINE", CMD_UNRLINE, 1, CMD_OPER);
	CommandAdd(modinfo->handle, "RLINESYNC", CMD_RLINESYNC, 6, CMD_SERVER);
	CommandAdd(modinfo->handle, "RCSUCCESS", CMD_REMOTE_ALLOW_RECAPTCHA, 1, CMD_SERVER);
	
	EventAdd(modinfo->handle, "recaptcha_clearup_event", recaptcha_clearup_event, NULL, 10000, 0);

	HookAdd(modinfo->handle, HOOKTYPE_PRE_LOCAL_CONNECT, 0, recaptcha_pre_connect);
	HookAdd(modinfo->handle, HOOKTYPE_SERVER_SYNC, 0, recaptcha_server_sync);
	HookAdd(modinfo->handle, HOOKTYPE_PRE_LOCAL_HANDSHAKE_TIMEOUT, 0, recaptcha_pre_local_handshake_timeout);

	return MOD_SUCCESS;
}

MOD_TEST()
{
	memset(&recaptcha_conf, 0, sizeof(recaptcha_conf)); 
	HookAdd(modinfo->handle, HOOKTYPE_CONFIGTEST, 0, recaptcha_configtest);
	HookAdd(modinfo->handle, HOOKTYPE_CONFIGPOSTTEST, 0, recaptcha_configposttest);
	return MOD_SUCCESS;
}

MOD_LOAD()
{
	return MOD_SUCCESS;
}

MOD_UNLOAD()
{
	return MOD_SUCCESS;
}


RPC_CALL_FUNC(rpc_recaptcha_find)
{
	json_t *result;
	int i = 0;
	const char *token;

	REQUIRE_PARAM_STRING("token", token);

	result = json_object();

	Client *client2;

	list_for_each_entry(client2, &client_list, client_node)
	{
		if (GetRCCode(client2) != NULL && !strcmp(GetRCCode(client2),token))
		{
			i++;
			json_object_set_new(result, "success", json_string_unreal("Not found"));
		}
	}
	if (!i)
	{
		json_object_set_new(result, "failed", json_string_unreal("Not found"));
	}
	rpc_response(client, request, result);
	json_decref(result);
}

RPC_CALL_FUNC(rpc_recaptcha_allow)
{
	json_t *result;
	int i = 0;
	const char *token;

	REQUIRE_PARAM_STRING("token", token);
	result = json_object();

	if (BadPtr(token) || !strlen(token))
	{
		json_object_set_new(result, "fail", json_string_unreal("Fail"));
		rpc_response(client, request, result);
		json_decref(result);
		return;
	}

	Client *client2;

	list_for_each_entry(client2, &client_list, client_node)
	{
		if (GetRCCode(client2) != NULL && !strcmp(GetRCCode(client2),token))
		{
			i++;
			if (is_handshake_finished(client2))
			{
				register_user(client2);
				UnsetRCCode(client2);
				remember_ip(client2->ip, TStime() + recaptcha_conf.ipcache);
			}
		}
	}
	if (!i)
	{
		sendto_server(NULL, 0, 0, NULL, ":%s RCSUCCESS %s", me.id, token);
	}
	json_object_set_new(result, "success", json_string_unreal("Success"));
	rpc_response(client, request, result);
	json_decref(result);
}


int recaptcha_pre_connect(Client *client)
{
	if (is_ip_in_memory(client->ip))
		return HOOK_CONTINUE;

	json_t *rline = match_rline(client);
	if (rline && GetRCCode(client) == NULL)
	{
		const char *reason = json_string_value(json_object_get(rline, "reason"));
		char randomString[16 + 1]; // +1 for the null-terminator
   		generateRandomString(randomString, 16);
		SetRCCode(client, randomString);

		sendnotice(client, "%s", reason);
		sendnotice(client, "%s?t=%s", recaptcha_conf.url, randomString);
		return HOOK_DENY; /* do not process register_user() */
	}

	return HOOK_CONTINUE; /* no action taken, proceed normally */
}

void generateRandomString(char *randomString, int length)
{
	const char charset[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
	int charsetSize = sizeof(charset) - 1; // Exclude the null terminator

	// Seed the random number generator using time and process ID to ensure uniqueness
	unsigned int seed = (unsigned int)time(NULL) ^ getpid();
	srand(seed);

	// Generate the random string
	for (int i = 0; i < length; i++)
	{
		int randomIndex = rand() % charsetSize;
		randomString[i] = charset[randomIndex];
	}

	randomString[length] = '\0'; // Null-terminate the string
}

void recaptcha_code_unserialize(const char *str, ModData *m)
{
	safe_strdup(m->str, str);
}

const char *recaptcha_code_serialize(ModData *m)
{
	if (!m->str)
		return NULL;
	return m->str;
}

void recaptcha_code_free(ModData *m)
{
	safe_free(m->str);
}


// Function to load JSON from file
json_t *load_recaptcha_db()
{
	json_error_t error;
	json_t *root = json_load_file(RECAPTCHA_DB, 0, &error);

	if (!root)
	{
		return NULL;
	}

	return root;
}

// Function to write JSON to file
int save_recaptcha_db(json_t *root)
{
	if (json_dump_file(root, RECAPTCHA_DB, JSON_INDENT(4)) != 0)
	{
		return -1;
	}

	return 0;
}

// Function to add an object (entry) to the array
int add_rline(const char *mask, const char *reason, const char *set_by_user, time_t set_at, time_t expiry)
{
	json_t *root = load_recaptcha_db();
	if (!root)
	{
		root = json_object(); // Create new root if none exists
	}

	json_t *entries = json_object_get(root, "entries");
	if (!entries)
	{
		entries = json_array(); // Create a new array if it doesn't exist
		json_object_set_new(root, "entries", entries);
	}

	json_t *new_entry = json_object();
	json_object_set_new(new_entry, "mask", json_string(mask));
	json_object_set_new(new_entry, "reason", json_string(reason));
	json_object_set_new(new_entry, "set_by_user", json_string(set_by_user));
	json_object_set_new(new_entry, "set_at", json_integer(set_at));
	json_object_set_new(new_entry, "expiry", json_integer(expiry));

	json_array_append_new(entries, new_entry); // Add the new entry

	int result = save_recaptcha_db(root);
	json_decref(root);

	char *expiry_time = NULL;

	if ((long)expiry > 0)
	{
		long time_in_minutes = ((long)expiry-(long)TStime()) / 60;
		int len = snprintf(NULL, 0, "%ldm", time_in_minutes);
		expiry_time = safe_alloc(len + 1);
		if (expiry_time != NULL)
		{
			sprintf(expiry_time, "%ldm", time_in_minutes);
		}
	} else{
		expiry_time = safe_alloc(strlen("permanent") + 1);
		if (expiry_time != NULL)
		{
			strcpy(expiry_time, "permanent");
		}
	}

	unreal_log(ULOG_INFO, "tkl", "TKL_ADD", NULL, "R-Line added: $mask [reason: $reason] [by: $set_by] [expires: $expiry]",
				log_data_string("mask", mask),
				log_data_string("reason", reason),
				log_data_string("set_by", set_by_user),
				log_data_string("expiry", expiry_time));

	free(expiry_time);
	return result;
}

// Function to print all entries
void print_entries(Client *client)
{
	json_t *root = load_recaptcha_db();
	if (!root)
	{
		sendto_one(client, NULL, ":%s 219 %s R :End of /STATS report", me.name, client->name);
		return;
	}

	json_t *entries = json_object_get(root, "entries");
	if (!json_is_array(entries))
	{
		sendto_one(client, NULL, ":%s 219 %s R :End of /STATS report", me.name, client->name);
		json_decref(root);
		return;
	}

	size_t index;
	json_t *entry;
	json_array_foreach(entries, index, entry)
	{
		const char *mask = json_string_value(json_object_get(entry, "mask"));
		const char *reason = json_string_value(json_object_get(entry, "reason"));
		const char *set_by_user = json_string_value(json_object_get(entry, "set_by_user"));
		time_t set_at = (time_t)json_integer_value(json_object_get(entry, "set_at"));
		time_t expiry = (time_t)json_integer_value(json_object_get(entry, "expiry"));

		sendto_one(client, NULL, ":%s 223 %s R %s %ld %ld %s :%s", me.name, client->name, mask, expiry, set_at, set_by_user, reason);
	}
	sendto_one(client, NULL, ":%s 219 %s R :End of /STATS report", me.name, client->name);

	json_decref(root);
}

// Basic as fuck validation checking
bool valid_rline(const char *mask)
{
	// Ensure the mask is not NULL or empty
	if (!mask || strlen(mask) == 0)
	{
		return false;
	}

	// Check if the mask starts with *@ (hostmask)
	if (strncmp(mask, "*@", 2) == 0)
	{
		return true; // Valid hostmask
	}

	// Check if the mask starts with ~ (extended ban)
	if (mask[0] == '~')
	{
		// Find the colon in the mask
		const char *colon_pos = strchr(mask, ':');
		if (colon_pos && strlen(colon_pos + 1) > 0)
		{
			// Valid extended ban with a non-empty value after the colon
			return true;
		} else{
			// Invalid extended ban (missing colon or empty suffix)
			return false;
		}
	}

	// If none of the conditions matched, the mask is invalid
	return false;
}


// Function to find an entry by mask
json_t *find_rline_by_mask(const char *mask)
{
	json_t *root = load_recaptcha_db();
	if (!root) return NULL;

	json_t *entries = json_object_get(root, "entries");
	if (!json_is_array(entries))
	{
		json_decref(root);
		return NULL;
	}

	size_t index;
	json_t *entry;
	json_array_foreach(entries, index, entry)
	{
		const char *current_mask = json_string_value(json_object_get(entry, "mask"));
		if (strcmp(current_mask, mask) == 0)
		{
			json_incref(entry);  // Increment reference so we can return it
			json_decref(root);   // Decrease root since we don't need it anymore
			return entry;		// Return the matching entry
		}
	}

	json_decref(root);  // Clean up
	return NULL;  // No match found
}

json_t *match_rline(Client *client)
{
	json_t *root = load_recaptcha_db();
	if (!root) return NULL;

	json_t *entries = json_object_get(root, "entries");
	if (!json_is_array(entries)) 
	{
		json_decref(root);
		return NULL;
	}
	int found = 0;
	size_t index;
	json_t *entry, *ret = NULL;
	const char *reason = NULL;
	json_array_foreach(entries, index, entry)
	{
		GeoIPResult *geo;
		const char *mask = json_string_value(json_object_get(entry, "mask"));
		if (!strncmp(mask, "*@", 2) && (match_simple(mask+2, client->ip) || match_simple(mask+2, client->user->realhost)))
		{
			reason = json_string_value(json_object_get(entry, "reason"));
			found++;
			ret = entry;
		}

		// Check for "~account:" prefix
		else if (!strncmp(mask, "~account:", 9))
		{
			const char *account = mask + 9;
			if (IsLoggedIn(client) && !strcmp(client->user->account, account))
			{
				found++;
				ret = entry;
			}
		}

		// Check for "~geoip:" prefix
		else if (!strncmp(mask, "~country:", 8) && (geo = geoip_client(client)))
		{
			const char *geoip = mask + 8;
			if (!strcasecmp(geo->country_code, geoip))
			{
				found++;
				ret = entry;
			}
		}

		// Check for "~asn:" prefix
		else if (!strncmp(mask, "~asn:", 5) && (geo = geoip_client(client)))
		{
			unsigned int asn = strtoul(mask, NULL, 10);
			if (geo->asn == asn)
			{
				found++;
				ret = entry;
			}
		}

		// Check for "~realname:" prefix
		else if (!strncmp(mask, "~realname:", 10))
		{
			const char *gecos = mask + 10;
			if (match_simple(gecos, client->info))
			{
				found++;
				ret = entry;
			}
		}

		// Check for "~security-group:" prefix
		else if (!strncmp(mask, "~security-group:", 16))
		{
			const char *secgroup = mask + 16;
			if (user_allowed_by_security_group_name(client, secgroup))
			{
				found++;
				ret = entry;
			}
		}
	}
	if (ret)
		json_incref(ret);
	json_decref(root);  // Clean up
	return (ret) ? ret : NULL;
}

// Function to delete an entry by mask
int delete_rline_by_mask(const char *mask)
{
	json_t *root = load_recaptcha_db();
	if (!root) return -1;

	json_t *entries = json_object_get(root, "entries");
	if (!json_is_array(entries))
	{
		
		json_decref(root);
		return -1;
	}

	size_t index;
	json_t *entry;
	json_array_foreach(entries, index, entry)
	{
		const char *current_mask = json_string_value(json_object_get(entry, "mask"));
		if (strcmp(current_mask, mask) == 0)
		{
			json_array_remove(entries, index);  // Remove the entry at this index
			int result = save_recaptcha_db(root);
			json_decref(root);  // Clean up
			return result;
		}
	}

	json_decref(root);
	return -1;  // No match found
}

// Function to add IP address with expiry to the "memory" array
int remember_ip(const char *ip, time_t expiry)
{
	// Bail out early if we're not caching (require it every time)
	if (recaptcha_conf.ipcache == 0)
		return 1;

	json_t *root = load_recaptcha_db();
	if (!root)
	{
		root = json_object(); // Create new root if none exists
	}

	json_t *memory = json_object_get(root, "memory");
	if (!memory)
	{
		memory = json_object(); // Create a new memory object if it doesn't exist
		json_object_set_new(root, "memory", memory);
	}

	json_object_set_new(memory, ip, json_integer(expiry)); // Add IP with expiry

	int result = save_recaptcha_db(root);
	json_decref(root);

	return result;
}


// Function to check if an IP address exists in "memory" and is valid (not expired)
bool is_ip_in_memory(const char *ip)
{
	json_t *root = load_recaptcha_db();
	if (!root) return false;

	json_t *memory = json_object_get(root, "memory");
	if (!json_is_object(memory))
	{
		json_decref(root);
		return false;
	}

	json_t *expiry = json_object_get(memory, ip);
	if (!expiry)
	{
		json_decref(root);
		return false;
	}

	time_t expiry_time = (time_t)json_integer_value(expiry);
	time_t current_time = time(NULL);
	
	json_decref(root);

	return current_time < expiry_time; // Return true if the IP is still valid
}

// Function to remove expired IPs from the "memory" array in the db file
int clean_up_expired_ips()
{
	json_t *root = load_recaptcha_db();
	if (!root) return -1;

	json_t *memory = json_object_get(root, "memory");
	if (!json_is_object(memory))
	{
		json_decref(root);
		return 0;
	}

	const char *ip;
	json_t *expiry;
	json_t *to_remove = json_array(); // Collect expired IPs

	time_t current_time = time(NULL);
	json_object_foreach(memory, ip, expiry)
	{
		time_t expiry_time = (time_t)json_integer_value(expiry);
		if (current_time >= expiry_time)
		{
			json_array_append_new(to_remove, json_string(ip)); // Add expired IP to remove list
		}
	}

	size_t index;
	json_t *expired_ip;
	json_array_foreach(to_remove, index, expired_ip)
	{
		json_object_del(memory, json_string_value(expired_ip)); // Remove expired IPs
	}

	json_decref(to_remove);
	int result = save_recaptcha_db(root);
	json_decref(root);

	return result;
}

int clean_up_expired_rlines()
{
	json_t *root = load_recaptcha_db();
	if (!root)
	{
		return 0;
	}

	json_t *entries = json_object_get(root, "entries");
	if (!json_is_array(entries))
	{
		json_decref(root);
		return 0;
	}

	time_t current_time = time(NULL);  // Get the current time
	size_t index;
	json_t *entry;

	size_t entries_count = json_array_size(entries);
	for (size_t i = 0; i < entries_count; i++)
	{
		entry = json_array_get(entries, i);
		time_t expiry = (time_t)json_integer_value(json_object_get(entry, "expiry"));
		if (expiry == 0)
			continue;
		
		if (expiry < current_time)
		{
			unreal_log(ULOG_INFO, "tkl", "TKL_EXPIRE", NULL, "Expiring R-Line: $mask [reason: $reason] [set by: $set_by_user]",
									log_data_string("mask", json_string_value(json_object_get(entry, "mask"))),
									log_data_string("reason", json_string_value(json_object_get(entry, "reason"))),
									log_data_string("set_by_user", json_string_value(json_object_get(entry, "set_by_user"))));

			json_array_remove(entries, i);
			i--;
			entries_count--;
		}
	}

	// Save the updated JSON back to the file
	return save_recaptcha_db(root);
}


CMD_FUNC(CMD_RLINE)
{
	Client *lkup;
	const char *mask;
	char *hostip = NULL;

	if (BadPtr(parv[1]))
	{
		print_entries(client);
		return;
	}

	if (!strcasecmp(parv[1],"-help"))
	{
		rline_help(client);
		return;
	}

	if (!valid_rline(parv[1]))
	{
		if ((lkup = find_user(parv[1], NULL)))
		{
			size_t len = strlen(lkup->ip) + 3;
			hostip = safe_alloc(len);
			sprintf(hostip, "*@%s", lkup->ip);
			mask = hostip;
		}
		else{
			sendnotice(client, "Not a valid R-Line. Must be in the form of an *@ip.host, ~extended:ban, or nick.");
			return;
		}
	}
	else
		mask = parv[1];

	const char *reason;
	time_t time = TStime() + (!BadPtr(parv[2]) ? config_checkval(parv[2], CFG_TIME) : config_checkval("0", CFG_TIME));

	if (parc == 4)
		 reason = (!BadPtr(parv[3])) ? parv[3] : "No reason";
	else
		reason = "No reason";

	json_t *RLine = find_rline_by_mask(mask);
	if (RLine)
	{
		sendnotice(client, "[error] R-Line with that mask already exists: \"%s\"", mask);
		return;
	}
	add_rline(mask, reason, client->name, TStime(), time == TStime() ? 0 : time);
	sendto_server(NULL, 0, 0, NULL, ":%s RLINE %s %s :%s", client->id, parv[1], BadPtr(parv[2]) ? "0" : parv[2], reason);
	safe_free(hostip);
}

CMD_FUNC(CMD_UNRLINE)
{
	if (BadPtr(parv[1]))
		return;

	if (!strcasecmp(parv[1],"-help"))
	{
		rline_help(client);
		return;
	}

	json_t *result = find_rline_by_mask(parv[1]);
	Client *lkup;
	char *hostip = NULL;

	if (result)
	{
		unreal_log(ULOG_INFO, "tkl", "TKL_DEL", NULL, "R-Line removed: $mask [reason: $reason] [by: $set_by] [removed by: $client.details]",
					log_data_string("mask", json_string_value(json_object_get(result, "mask"))),
					log_data_string("reason", json_string_value(json_object_get(result, "reason"))),
					log_data_string("set_by", json_string_value(json_object_get(result, "set_by_user"))),
					log_data_client("client", client));
		delete_rline_by_mask(parv[1]);
		sendto_server(NULL, 0, 0, NULL, ":%s UNRLINE %s", client->id, parv[1]);
	}
	else if ((lkup = find_user(parv[1], NULL))) 
	{
		hostip = safe_alloc(strlen(lkup->ip)+3);
		sprintf(hostip, "*@%s", lkup->ip);
		
		result = find_rline_by_mask(hostip);
		if (!result)
			sendnotice(client, "[error] R-Line not found: \"%s\"", parv[1]);
		else
		{
			unreal_log(ULOG_INFO, "tkl", "TKL_DEL", NULL, "R-Line removed: $mask [reason: $reason] [by: $set_by] [removed by: $client.details]",
					log_data_string("mask", json_string_value(json_object_get(result, "mask"))),
					log_data_string("reason", json_string_value(json_object_get(result, "reason"))),
					log_data_string("set_by", json_string_value(json_object_get(result, "set_by_user"))),
					log_data_client("client", client));
			delete_rline_by_mask(hostip);
			sendto_server(NULL, 0, 0, NULL, ":%s UNRLINE %s", client->id, parv[1]);
		}
		safe_free(hostip);
	}
	else
		sendnotice(client, "[error] R-Line not found: \"%s\"", parv[1]);
}


EVENT(recaptcha_clearup_event)
{
	clean_up_expired_ips();
	clean_up_expired_rlines();
}


void set_config_defaults(void)
{
	recaptcha_conf.url = NULL;
	recaptcha_conf.ipcache = 604800; // 1 week
	recaptcha_conf.timeout = 60; // 1 minute
}

void free_config(void)
{
	safe_free(recaptcha_conf.url);
}

int recaptcha_configtest(ConfigFile *cf, ConfigEntry *ce, int type, int *errs)
{
	int errors = 0;
	int i;
	ConfigEntry *cep, *cep2; 

	if(type != CONFIG_MAIN)
		return 0;

	if(!ce || !ce->name)
		return 0;

	if(strcmp(ce->name, RECAPTCHA_CONF))
		return 0;

	for(cep = ce->items; cep; cep = cep->next)
	{
		if(!cep->name)
		{
			config_error("%s:%i: blank %s item", cep->file->filename, cep->line_number, RECAPTCHA_CONF); // Rep0t error
			errors++;
			continue;
		}

		if(!cep->value)
		{
			config_error("%s:%i: blank %s value", cep->file->filename, cep->line_number, RECAPTCHA_CONF); // Rep0t error
			errors++;
			continue;
		}

		if(!strcmp(cep->name, "url"))
		{
			if(recaptcha_conf.got_url)
			{
				config_error("%s:%i: duplicate %s::%s directive", cep->file->filename, cep->line_number, RECAPTCHA_CONF, cep->name);
				errors++;
				continue;
			}

			if(!strlen(cep->value))
			{
				config_error("%s:%i: %s::%s must be the URL of your Google reCAPTCHA Page installation", cep->file->filename, cep->line_number, RECAPTCHA_CONF, cep->name);
				errors++;
				continue;
			}
			recaptcha_conf.got_url = 1;
			continue;
		}
		if (!strcmp(cep->name, "ipcache"))
		{
			if (recaptcha_conf.got_ipcache)
			{
				config_error("%s:%i: duplicate %s::%s directive", cep->file->filename, cep->line_number, RECAPTCHA_CONF, cep->name);
				errors++;
				continue;
			}

			unsigned long value = config_checkval(cep->value, CFG_TIME);
			if (value <= 0 || value > 999*3600) // max 99 days
			{
				config_error("%s:%i: %s::%s must be a time value like 24h or 1w. Specify '0' to disable caching.", cep->file->filename, cep->line_number, RECAPTCHA_CONF, cep->name);
				errors++;
				continue;
			}
			recaptcha_conf.got_ipcache = 1;
			continue;
		}

		if (!strcmp(cep->name, "timeout"))
		{
			if (recaptcha_conf.got_timeout)
			{
				config_error("%s:%i: duplicate %s::%s directive", cep->file->filename, cep->line_number, RECAPTCHA_CONF, cep->name);
				errors++;
				continue;
			}

			unsigned long value = config_checkval(cep->value, CFG_TIME);
			if (value < 30 || value > 300) // maximum 5 minutes
			{
				config_error("%s:%i: %s::%s must be a time value between 30s and 5m", cep->file->filename, cep->line_number, RECAPTCHA_CONF, cep->name);
				errors++;
				continue;
			}
			recaptcha_conf.got_timeout = 1;
			continue;
		}

		// Anything else is unknown to us =]
		config_warn("%s:%i: unknown item %s::%s", cep->file->filename, cep->line_number, RECAPTCHA_CONF, cep->name); // So display just a warning
	}

	*errs = errors;
	return errors ? -1 : 1; 
}

int recaptcha_configposttest(int *errs)
{
	int errors = 0;

	// Extend this and the configtest function to check for more errors
	if(!recaptcha_conf.got_url)
	{
		config_error("[%s] %s::url is required but wasn't specified", MOD_HEADER.name, RECAPTCHA_CONF);
		errors++;
	}

	*errs = errors;
	return errors ? -1 : 1;
}

int recaptcha_configrun(ConfigFile *cf, ConfigEntry *ce, int type)
{
	ConfigEntry *cep, *cep2; 

	if(type != CONFIG_MAIN)
		return 0;

	if(!ce || !ce->name)
		return 0;

	if(strcmp(ce->name, RECAPTCHA_CONF))
		return 0;

	for(cep = ce->items; cep; cep = cep->next)
	{
		if(!cep->name)
			continue;

		else if(!strcmp(cep->name, "url"))
			safe_strdup(recaptcha_conf.url, cep->value);

		else if (!strcmp(cep->name, "ipcache"))
			recaptcha_conf.ipcache = config_checkval(cep->value, CFG_TIME);

		else if (!strcmp(cep->name, "timeout"))
			recaptcha_conf.timeout = config_checkval(cep->value, CFG_TIME);
		
	}

	return 1; // We good
}

/** parv[]
	1: L|M (Line or Memory)
	2: L:mask M:ip
	3: L:set_by nick M:expiry
	4: L:expiry
	5: L:set_at
	6: L:reason
 */
CMD_FUNC(CMD_RLINESYNC)
{
	if (!strcmp(parv[1],"R"))
	{
		if (!find_rline_by_mask(parv[2]))
			add_rline(parv[2], parv[6], parv[3], strtoul(parv[4], NULL, 10), strtoul(parv[5], NULL, 10));
	}
	else if (!strcmp(parv[1],"M"))
		if (!is_ip_in_memory(parv[2]))
			remember_ip(parv[2], strtoul(parv[3], NULL, 10));
}

int recaptcha_server_sync(Client *client)
{
	json_t *root = load_recaptcha_db();
	if (!root)
		return 0;
	
	json_t *entries = json_object_get(root, "entries");
	if (json_is_array(entries))
	{
		size_t index;
		json_t *entry;
		json_array_foreach(entries, index, entry)
		{
			const char *mask = json_string_value(json_object_get(entry, "mask"));
			const char *reason = json_string_value(json_object_get(entry, "reason"));
			const char *set_by_user = json_string_value(json_object_get(entry, "set_by_user"));
			time_t set_at = (time_t)json_integer_value(json_object_get(entry, "set_at"));
			time_t expiry = (time_t)json_integer_value(json_object_get(entry, "expiry"));

			sendto_one(client, NULL, ":%s RLINESYNC R %s %s %ld %ld :%s", me.name, mask, set_by_user, set_at, expiry, reason);
		}
	}
	
	json_t *memory = json_object_get(root, "memory");
	if (!json_is_object(memory))
	{
		json_decref(root);
		return 0;
	}

	const char *ip;
	json_t *expiry;
	json_object_foreach(memory, ip, expiry)
	{
		time_t expiry_time = (time_t)json_integer_value(expiry);
		sendto_one(client, NULL, ":%s RLINESYNC M %s %ld", me.name, ip, expiry_time);
	}
	json_decref(root);
	return 0;
}


int recaptcha_pre_local_handshake_timeout(Client *client, const char **comment)
{
	if (GetRCCode(client) && client->local->creationtime)
	{
		if ((unsigned long)TStime() - (unsigned long)client->local->creationtime < recaptcha_conf.timeout)
			return HOOK_ALLOW;
		*comment = "Google reCAPTCHA required to connect";
	}
	return HOOK_CONTINUE;
}

CMD_FUNC(CMD_REMOTE_ALLOW_RECAPTCHA)
{
	int i = 0;
	Client *client2;
	list_for_each_entry(client2, &client_list, client_node)
	{
		if (GetRCCode(client2) != NULL && !strcmp(GetRCCode(client2), parv[1]))
		{
			if (is_handshake_finished(client2))
			{
				register_user(client2);
				UnsetRCCode(client2);
				remember_ip(client2->ip, TStime() + recaptcha_conf.ipcache); // cache it so they don't have to do it again for a while
			}
		}
	}
	if (!i)
	{
		sendto_server(NULL, 0, 0, NULL, ":%s RCSUCCESS %s", client->id, parv[1]);
	}
}
