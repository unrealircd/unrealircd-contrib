/* GPLv3
 Copyright 2025 Valware
*/
/*** <<<MODULE MANAGER START>>>
module
{
	documentation "https://github.com/ValwareIRC/valware-unrealircd-mods/blob/main/haveibeenpwned/README.md";
	troubleshooting "In case of problems, documentation or e-mail me at v.a.pond@outlook.com";
	min-unrealircd-version "6.1.0";
	max-unrealircd-version "6.*";
	post-install-text {
		"The module is installed. Now all you need to do is add a loadmodule line:";
		"loadmodule \"third/haveibeenpwned\";";
		"And /REHASH the IRCd.";
		"The module does not need any other configuration.";
	}
}
*** <<<MODULE MANAGER END>>>
*/


#include "unrealircd.h"

ModuleHeader MOD_HEADER
= {
	"third/haveibeenpwned",	/* Name of module */
	"1.0", /* Version */
	"Checks haveibeenpwned for password leaks on OPER", /* Short description of module */
	"Valware", /* Author */
	"unrealircd-6",
};

typedef struct HaveIBeenPwnedData {
	Client *client;
	char *hash;
} HaveIBeenPwnedData;

ModDataInfo *haveibeenpwned_mdata = NULL;

void haveibeenpwned_mdata_free(ModData *m);

static char *HaveIBeenPwnedHash(Client *client)
{
	return moddata_client(client, haveibeenpwned_mdata).str;
}
int haveibeenpwned_pre_command(Client *client, MessageTag *mtags, const char *buf);
int haveibeenpwned_local_oper(Client *client, int add, const char *oper_block, const char *operclass);
void haveibeenpwned_check_complete(OutgoingWebRequest *request, OutgoingWebResponse *response);

MOD_TEST()
{
	MARK_AS_OFFICIAL_MODULE(modinfo);
	return MOD_SUCCESS;
}

MOD_INIT()
{
	ModDataInfo mreq;

	MARK_AS_OFFICIAL_MODULE(modinfo);

	RegisterApiCallbackWebResponse(modinfo->handle, "haveibeenpwned_check_complete", haveibeenpwned_check_complete);

	HookAdd(modinfo->handle, HOOKTYPE_PRE_COMMAND, 0, haveibeenpwned_pre_command);
	HookAdd(modinfo->handle, HOOKTYPE_LOCAL_OPER, 0, haveibeenpwned_local_oper);

	mreq.type = MODDATATYPE_CLIENT;
	mreq.name = "haveibeenpwned_hash";
	mreq.free = haveibeenpwned_mdata_free;
	mreq.serialize = NULL;
	mreq.unserialize = NULL;
	mreq.sync = 0;
	haveibeenpwned_mdata = ModDataAdd(modinfo->handle, mreq);

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

void haveibeenpwned_mdata_free(ModData *m)
{
	safe_free(m->str);
}

/* Hook for HOOKTYPE_PRE_COMMAND */
int haveibeenpwned_pre_command(Client *client, MessageTag *mtags, const char *buf)
{
	char *copy, *p, *opername, *password;
	char sha1bin[20];
	char sha1hex[41];

	if (!MyUser(client))
		return HOOK_CONTINUE;

	if (strncasecmp(buf, "OPER ", 5))
		return HOOK_CONTINUE;

	copy = strdup(buf);
	p = copy + 5; /* skip "OPER " */

	opername = strtoken(&p, NULL, " ");
	if (!opername)
	{
		safe_free(copy);
		return HOOK_CONTINUE;
	}

	password = strtoken(&p, NULL, " ");
	if (!password || !*password)
	{
		safe_free(copy);
		return HOOK_CONTINUE;
	}

	/* Compute SHA-1 of password */
	sha1hash_binary(sha1bin, password, strlen(password));
	binarytohex(sha1bin, 20, sha1hex);

	/* Convert to uppercase */
	for (int i = 0; i < 40; i++)
		sha1hex[i] = toupper(sha1hex[i]);
	sha1hex[40] = '\0';

	/* Store in moddata */
	safe_strdup(moddata_client(client, haveibeenpwned_mdata).str, sha1hex);

	safe_free(copy);
	return HOOK_CONTINUE;
}

/* Hook for HOOKTYPE_LOCAL_OPER */
int haveibeenpwned_local_oper(Client *client, int add, const char *oper_block, const char *operclass)
{
	char *hash;
	char url[256];
	OutgoingWebRequest *request;

	if (add != 1)
		return HOOK_CONTINUE;

	hash = HaveIBeenPwnedHash(client);
	if (!hash)
		return HOOK_CONTINUE;

	/* Prepare URL: https://api.pwnedpasswords.com/range/XXXXX */
	snprintf(url, sizeof(url), "https://api.pwnedpasswords.com/range/%.*s", 5, hash);

	request = safe_alloc(sizeof(OutgoingWebRequest));
	request->url = strdup(url);
	request->http_method = HTTP_METHOD_GET;
	request->apicallback = strdup("haveibeenpwned_check_complete");
	HaveIBeenPwnedData *data = safe_alloc(sizeof(HaveIBeenPwnedData));
	data->client = client;
	data->hash = strdup(hash);
	request->callback_data = data;
	request->max_redirects = 1;
	request->connect_timeout = 5;
	request->transfer_timeout = 10;

	url_start_async(request);

	/* Clear the hash from moddata */
	safe_free(moddata_client(client, haveibeenpwned_mdata).str);

	return HOOK_CONTINUE;
}

/* Callback for the web request */
void haveibeenpwned_check_complete(OutgoingWebRequest *request, OutgoingWebResponse *response)
{
	HaveIBeenPwnedData *data = (HaveIBeenPwnedData *)response->ptr;
	Client *client;
	char *fullhash, *suffix, *line;
	int found = 0;

	if (!data)
		return;

	client = data->client;
	fullhash = data->hash;

	if (!client || !MyUser(client) || !IsOper(client))
	{
		safe_free(data->hash);
		safe_free(data);
		return;
	}

	if (response->errorbuf || !response->memory)
	{
		/* Log error, but don't notify user */
		unreal_log(ULOG_INFO, "haveibeenpwned", "HAVEIBEENPWNED_API_ERROR", client,
				   "Error checking haveibeenpwned for $client: $error",
				   log_data_string("error", response->errorbuf ? response->errorbuf : "No response"));
		safe_free(fullhash);
		return;
	}

	suffix = fullhash + 5; /* The suffix after first 5 chars */

	/* Parse the response: lines like "SUFFIX:COUNT" */
	char *p = (char *)response->memory;
	int breach_count = 0;
	while ((line = strtoken(&p, NULL, "\r\n")))
	{
		char *colon = strchr(line, ':');
		if (colon)
		{
			*colon = '\0';
			if (!strcasecmp(line, suffix))
			{
				breach_count = atoi(colon + 1);
				found = 1;
				break;
			}
		}
	}

	if (found)
	{
		sendnotice(client, "*** WARNING: Your OPER password has been found in %d data breach(es)! "
				   "Please change your password immediately. "
				   "See https://haveibeenpwned.com/Passwords for more information.", breach_count);
		unreal_log(ULOG_WARNING, "haveibeenpwned", "HAVEIBEENPWNED_PASSWORD_LEAKED", client,
				   "$client.details used a password that has been leaked in a data breach");
	}

	safe_free(data->hash);
	safe_free(data);
}
