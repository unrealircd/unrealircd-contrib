
/*** <<<MODULE MANAGER START>>>
module
{
		documentation "https://github.com/ValwareIRC/valware-unrealircd-mods/blob/main/upgrade-notify/README.md";
		troubleshooting "In case of problems, documentation or e-mail me at v.a.pond@outlook.com";
		min-unrealircd-version "6.1.3";
		max-unrealircd-version "6.*";
		post-install-text {
				"The module is installed. Now all you need to do is add a loadmodule line:";
				"loadmodule \"third/upgrade-notify\";";
				"And /REHASH the IRCd.";
				"The module does not need any other configuration.";
		}
}
*** <<<MODULE MANAGER END>>>
*/

#include "unrealircd.h"

void my_download_complete(OutgoingWebRequest *request, OutgoingWebResponse *response);

#define TWICE_PER_DAY 43200000
EVENT(check_for_updates);

ModuleHeader MOD_HEADER
= {
	"third/upgrade-notify",	/* Name of module */
	"2.0", /* Version */
	"Sends out a message to opers when there is an upgrade available for UnrealIRCd", /* Short description of module */
	"Valware",
	"unrealircd-6",
};

MOD_INIT()
{
	RegisterApiCallbackWebResponse(modinfo->handle, "my_download_complete", my_download_complete);
	EventAdd(modinfo->handle, "check_for_updates", check_for_updates, NULL, TWICE_PER_DAY, 0); // once per day
	return MOD_SUCCESS;
}

/* Is first run when server is 100% ready */
MOD_LOAD()
{
	return MOD_SUCCESS;
}

/* Called when module is unloaded */
MOD_UNLOAD()
{
	return MOD_SUCCESS;
}


void my_download_complete(OutgoingWebRequest *request, OutgoingWebResponse *response)
{
	json_t *result;
	json_error_t jerr;
	char *version_string = "\0";
	int upgrade_available = 0;
	version_string = (char *)malloc(strlen(version) + 1);
	strcpy(version_string, version);
	char *tok = strtok(version_string, "-");
	char *current_version = "\0";

	while (tok != NULL)
	{
		// Check if the tok contains only digits and dots
		int is_numeric = 1;
		for (int i = 0; tok[i] != '\0'; i++) {
			if (!isdigit(tok[i]) && tok[i] != '.') {
				is_numeric = 0;
				break;
			}
		}

		// If the tok contains only digits and dots, it's the numbered string
		if (is_numeric) {
			current_version = tok;
			break; // Exit the loop once found
		}

		// Get the next tok
		tok = strtok(NULL, "-");
	}
	if (response->errorbuf || !response->memory)
	{
		unreal_log(ULOG_INFO, "mymod", "MYMOD_BAD_RESPONSE", NULL,
				   "Error while trying to check $url: $error",
				   log_data_string("url", request->url),
				   log_data_string("error", response->errorbuf ? response->errorbuf : "No data (body) returned"));
		return;
	}

	// result->memory contains all the data of the web response, in our case
	// we assume it is a JSON response, so we are going to parse it.
	// If you were expecting BINARY data then you can still use result->memory
	// but then have a look at the length in result->memory_len.
	result = json_loads(response->memory, JSON_REJECT_DUPLICATES, &jerr);
	if (!result)
	{
		unreal_log(ULOG_INFO, "upgrade", "API_BAD_RESPONSE", NULL,
				   "Error while trying to check $url: JSON parse error",
				   log_data_string("url", request->url));
		return;
	}
	const char *key;
	json_t *value;
	json_object_foreach(result, key, value)
	{
		if (!json_is_object(value))
			return;

		const char *key2;
		json_t *value2;
		json_object_foreach(value, key2, value2)
		{
			if (!strcmp(key2,"Stable"))
			{
				const char *key3;
				json_t *value3;
				json_object_foreach(value2, key3, value3)
				{
					if (!strcmp(key3, "version"))
					{
						const char *stable_version = json_string_value(value3);
						
						int result = strcmp(current_version, stable_version);
						if (result < 0)
						{
							unreal_log(ULOG_INFO, "upgrade", "UPGRADE_AVAILABLE", NULL, "There is an upgrade available for UnrealIRCd! Your version: UnrealIRCd %s - New version: UnrealIRCd %s", current_version, stable_version);
							unreal_log(ULOG_INFO, "upgrade", "UPGRADE_AVAILABLE", NULL, "Visit https://www.unrealircd.org/docs/Upgrading for information on upgrading.");
						}
						break;
					}
				}
			}
		}
	}

	json_decref(result);
	free(version_string);
}

EVENT(check_for_updates)
{
	OutgoingWebRequest *w = safe_alloc(sizeof(OutgoingWebRequest));
	safe_strdup(w->url, "https://www.unrealircd.org/downloads/list.json");
	w->http_method = HTTP_METHOD_GET;
	safe_strdup(w->apicallback, "my_download_complete");
	url_start_async(w);
}
