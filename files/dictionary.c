/*
License: GPLv3 or later
Name: third/dictionary
Author: Valware
*/


/*** <<<MODULE MANAGER START>>>
module
{
		documentation "https://github.com/ValwareIRC/valware-unrealircd-mods/blob/main/dictionary/README.md";
		troubleshooting "In case of problems, please file a bug report at https://github.com/ValwareIRC/valware-unrealircd-mods/issues/new?template=bug_report.md";
		min-unrealircd-version "6.*";
		max-unrealircd-version "6.*";
		post-install-text {
				"The module is installed. Now all you need to do is add a loadmodule line:";
				"loadmodule \"third/dictionary\";";
				"to your configuration, and then rehash your server.";
				"The module does not need any other configuration.";
		}
}
*** <<<MODULE MANAGER END>>>
*/

#include "unrealircd.h"

#define DICTIONARY_URL "https://api.dictionaryapi.dev/api/v2/entries/en/"

// basically strncat but with a new char in memorayyy
char *construct_url(const char *base_url, const char *extra_params)
{
	size_t base_len = strlen(base_url) +1;
	size_t params_len = strlen(extra_params)+1;
	
	// Calculate the length of the resulting URL (+1 for the null terminator)
	size_t url_len = base_len+params_len+1;

	// Allocate memory for the URL
	char *url = (char *)safe_alloc(url_len);
	if (url != NULL)
	{
		// Copy the base URL into the constructed URL
		strncpy(url, base_url, base_len);
		url[base_len] = '\0'; // Null-terminate the base URL in the new string
		
		// Concatenate the extra parameters
		strncat(url, extra_params, params_len);
		url[url_len - 1] = '\0'; // Ensure null termination at the end
	}
	return url;
}

void query_api(char *endpoint, const char *callback, const char *uid)
{
	OutgoingWebRequest *w = safe_alloc(sizeof(OutgoingWebRequest));
	json_t *j;
	NameValuePrioList *headers = NULL;
	add_nvplist(&headers, 0, "Content-Type", "application/json; charset=utf-8");
	/* Do the web request */
	safe_strdup(w->url, endpoint);
	w->http_method = HTTP_METHOD_GET;
	w->headers = headers;
	w->max_redirects = 1;
	if (!BadPtr(uid))
		w->callback_data = strdup(uid);
	safe_strdup(w->apicallback, callback);
	url_start_async(w);
	safe_free(endpoint);
}

ModuleHeader MOD_HEADER
  = {
	"third/dictionary",
	"1.1",
	"Lets you and your visitors look up the definition of a word (English)",
	"Valware",
	"unrealircd-6",
};

/** We are adding a function here, but that's only an example trigger.
  * You can trigger this anywhere.
*/
CMD_FUNC(CMD_DICTIONARY);

void dictionary_download_complete(OutgoingWebRequest *request, OutgoingWebResponse *response);

MOD_INIT()
{
	CommandAdd(modinfo->handle, "DICT", CMD_DICTIONARY, 0, CMD_USER);
	RegisterApiCallbackWebResponse(modinfo->handle, "dictionary_download_complete", dictionary_download_complete);
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

CMD_FUNC(CMD_DICTIONARY)
{
	if (parc < 1 || BadPtr(parv[1]))
	{
		sendnumeric(client, ERR_NEEDMOREPARAMS, "DICT");
		return;
	}
	char *lookup = construct_url(DICTIONARY_URL, parv[1]);
	if (!lookup)
	{
		sendto_one(client, NULL, ":%s 292 %s :Couldn't construct URL", me.name, client->name);
		return;
	}
	query_api(lookup, "dictionary_download_complete", client->id);
	sendto_one(client, NULL, ":%s 290 %s :-", me.name, client->name);
	sendto_one(client, NULL, ":%s 292 %s :Looking up \"%s\"...", me.name, client->name, parv[1]);
	// because legit users will take more time to read and so should be unaffected,
	// rather than someone spamming the api command
	add_fake_lag(client, 10000);
}

void dictionary_download_complete(OutgoingWebRequest *request, OutgoingWebResponse *response)
{
	json_t *result;
	json_error_t jerr;
	Client *client = find_user((char *)response->ptr, NULL);

	if (response->errorbuf || !response->memory)
	{
		sendto_one(client, NULL, ":%s 292 %s :Sorry, I could not find that word in our dictionary.", me.name, client->name);
		return;
	}

	result = json_loads(response->memory, JSON_REJECT_DUPLICATES, &jerr);
	if (!result)
	{
		unreal_log(ULOG_INFO, "dictionary", "DICTIONARY_BAD_RESPONSE", NULL,
				   "Error while trying to check $url: JSON parse error",
				   log_data_string("url", request->url));
		return;
	}

	if (!client)
	{
		json_decref(result);
		return;
	}
	size_t i, j, k, l;
	json_t *data, *meanings, *meaning, *definitions, *definition;
	const char *word;
	const char *key;
	json_t *value;

	for (i = 0; i < json_array_size(result); i++)
	{
		data = json_array_get(result, i);
		if (!json_is_object(data))
		{
			unreal_log(ULOG_INFO, "dictionary", "DICTIONARY_BAD_RESPONSE", NULL,
					   "Array item %zu is not an object", i);
			sendto_one(client, NULL, ":%s 292 %s :Sorry, something went wrong. Please try again later.", me.name, client->name);
			json_decref(result);
			return;
		}

		sendto_one(client, NULL, ":%s 290 %s :-", me.name, client->name);
		sendto_one(client, NULL, ":%s 292 %s :\037(%zu/%zu)\037:", me.name, client->name, i + 1, json_array_size(result));
		json_object_foreach(data, key, value)
		{
			if (!strcasecmp(key, "word"))
			{
				word = json_string_value(value);
			}
			else if (!strcasecmp(key, "meanings"))
			{
				for (j = 0; j < json_array_size(value); j++)
				{
					meaning = json_array_get(value, j);
					json_object_foreach(meaning, key, value)
					{
						if (!strcasecmp(key, "partOfSpeech"))
						{
							sendto_one(client, NULL, ":%s 292 %s :Part of Speech: %s", me.name, client->name, json_string_value(value));
						}
						else if (!strcasecmp(key, "definitions"))  // Fixed condition
						{
							for (k = 0; k < json_array_size(value); k++)
							{
								definition = json_array_get(value, k);
								json_object_foreach(definition, key, value)
								{
									if (!strcasecmp(key, "definition"))
									{
										sendto_one(client, NULL, ":%s 292 %s :Definition: %s", me.name, client->name, json_string_value(value));
									}
									else if (!strcasecmp(key, "example"))
									{
										sendto_one(client, NULL, ":%s 292 %s :Example: %s", me.name, client->name, json_string_value(value));
									}
									else if (!strcasecmp(key, "synonyms"))
									{
										sendto_one(client, NULL, ":%s 292 %s :Synonyms:", me.name, client->name);
										for (l = 0; l < json_array_size(value); l++)
										{
											sendto_one(client, NULL, ":%s 292 %s :- %s", me.name, client->name, json_string_value(json_array_get(value, l)));
										}
									}
									else if (!strcasecmp(key, "antonyms"))
									{
										sendto_one(client, NULL, ":%s 292 %s :Antonyms:", me.name, client->name);
										for (l = 0; l < json_array_size(value); l++)
										{
											sendto_one(client, NULL, ":%s 292 %s :- %s", me.name, client->name, json_string_value(json_array_get(value, l)));
										}
									}
								}
							}
						}
					}
				}
			}
		}
	}
	sendto_one(client, NULL, ":%s 290 %s :End of /DICT", me.name, client->name);
	sendto_one(client, NULL, ":%s 290 %s :-", me.name, client->name);
	json_decref(result);
}
