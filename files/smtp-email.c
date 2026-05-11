/* 
	LICENSE: GPLv3-or-later
	Copyright Ⓒ 2024 Valerie Liu
	Sends emails about chosen logs to chosen recipients
 	This module requires cURL in order to work
*/

/*** <<<MODULE MANAGER START>>>
module
{
		documentation "https://github.com/ValwareIRC/valware-unrealircd-mods/blob/main/email/README.md";
		troubleshooting "In case of problems, please file a bug report at https://github.com/ValwareIRC/valware-unrealircd-mods/issues/new?template=bug_report.md";
		min-unrealircd-version "6.1.8.1";
		max-unrealircd-version "6.*";
		post-install-text {
				"The module is installed. Please add the following line to your unrealircd.conf";
				"This module requires that cURL be installed on your system. Make sure to enable it in the ./Config script";
				"loadmodule \"third/smtp-email\";";
				"And see the documentation for how to configure";
				"Once you're done, you can /REHASH";
		}
}
*** <<<MODULE MANAGER END>>>
*/

#include "unrealircd.h"

ModuleHeader MOD_HEADER
= {
	"third/smtp-email",	/* Name of module */
	"1.1", /* Version */
	"Send emails about chosen logs using SMTP", /* Short description of module */
	"Valware",
	"unrealircd-6",
};

struct upload_status
{
	int lines_read;
	char *payload;  // Pointer to dynamically generated email content
};

struct EmailConf
{
	char *username;
	char *password;
	char *port;
	char *host;
	MultiLine *recipients;
	MultiLine *events;

	unsigned long int got_username;
	unsigned long int got_password;
	unsigned long int got_port;
	unsigned long int got_host;
};
static struct EmailConf ec;

void set_config_defaults(void)
{
	ec.username = NULL;
	ec.password = NULL;
	ec.port = NULL;
	ec.host = NULL;
	ec.recipients = NULL;
	ec.events = NULL;
}

void free_config(void)
{
	safe_free(ec.username);
	safe_free(ec.password);
	safe_free(ec.port);
	safe_free(ec.host);
	freemultiline(ec.recipients);
	freemultiline(ec.events);
	memset(&ec, 0, sizeof(ec));
}

int email_configtest(ConfigFile *cf, ConfigEntry *ce, int type, int *errs);
int email_configposttest(int *errs);
int email_configrun(ConfigFile *cf, ConfigEntry *ce, int type);
int log_hook_email(LogLevel loglevel, const char *subsystem, const char *event_id, MultiLine *msg, json_t *json, const char *json_serialized, const char *timebuf);

int should_log_to_email(const char *subsystem)
{
	MultiLine *ml;
	for (ml = ec.events; ml && ml->line; ml = ml->next)
	{
		if (!strcasecmp(subsystem, ml->line))
			return 1;
	}
	return 0;
}

size_t payload_source(void *ptr, size_t size, size_t nmemb, void *userp)
{
	struct upload_status *upload_ctx = (struct upload_status *)userp;
	const char *data = upload_ctx->payload + upload_ctx->lines_read;

	if (size == 0 || nmemb == 0 || (size * nmemb) < 1)
		return 0;

	size_t len = strlen(data);
	if (len > size * nmemb)
		len = size * nmemb; // Adjust length to prevent overflow
	
	if (len > 0)
	{
		memcpy(ptr, data, len);  // Copy the email content to buffer
		upload_ctx->lines_read += len;  // Update the position
		return len;  // Return the length of the copied data
	}

	return 0;  // No more data to send
}

MOD_LOAD()
{
	return MOD_SUCCESS;
}

MOD_UNLOAD()
{
	free_config();
	return MOD_SUCCESS;
}

MOD_INIT()
{
	memset(&ec, 0, sizeof(ec));
	HookAdd(modinfo->handle, HOOKTYPE_LOG, 0, log_hook_email);
	HookAdd(modinfo->handle, HOOKTYPE_CONFIGRUN, 0, email_configrun);
	return MOD_SUCCESS;
}

MOD_TEST()
{
	HookAdd(modinfo->handle, HOOKTYPE_CONFIGTEST, 0, email_configtest);
	HookAdd(modinfo->handle, HOOKTYPE_CONFIGPOSTTEST, 0, email_configposttest);
	return MOD_SUCCESS;
}

int log_hook_email(LogLevel loglevel, const char *subsystem, const char *event_id, MultiLine *msg, json_t *json, const char *json_serialized, const char *timebuf)
{
	if (!should_log_to_email(event_id))
		return 0;

	if (!ec.username || !ec.password || !ec.host || !ec.port || !msg->line)
	{
		unreal_log(ULOG_INFO, "debug", "MISSING_SHIT", NULL, "There was shit missing");
		return 0;
	}

	CURL *curl;
	CURLcode res;

	// Initialize curl session
	curl = curl_easy_init();
	if (!curl)
		return 0;

	// Example: Dynamically build the email content
	char email_body[4096];  // Adjust size as needed

	// Build the email body with log details
	snprintf(email_body, sizeof(email_body),
		"To: %s Staff\r\n"
		"From: %s\r\n"
		"Subject: UnrealIRCd: %s\r\n"
		"Content-Type: text/html; charset=UTF-8\r\n"
		"\r\n"
		"<html><body>"
		"Dear Oper,<br><br>This email is to let you know a log that you subscribe to has been generated. Please see the following information:<br><br>"
		"<b>Server</b>: %s<br>"
		"<b>Subsystem</b>: %s<br>"
		"<b>Event ID</b>: %s<br>"
		"<b>Message</b>: %s<br>"
		"<b>Sever Time</b>: %s<br>"
		"<br><b>JSON:</b><br>"
		"<code>%s</code>"
		"</body></html>",
		strlen(me.info) ? me.info : "Server",
		ec.username,
		msg->line,
		strlen(me.name) ? me.name : "Server",
		subsystem,
		event_id,
		msg->line,
		timebuf,
		json_serialized
	);

	// Pass the dynamic email body to the payload source
	struct upload_status upload_ctx = { .lines_read = 0, .payload = email_body };

	char *host = safe_alloc(strlen(ec.host)+strlen(ec.port)+10);
	sprintf(host, "smtps://%s:%s", ec.host, ec.port);
	char *from_email = safe_alloc(strlen(ec.username)+3);
	sprintf(from_email, "<%s>", ec.username);

	// Set SMTP server and email details
	curl_easy_setopt(curl, CURLOPT_URL, host);
	curl_easy_setopt(curl, CURLOPT_USERNAME, ec.username);  // Your email
	curl_easy_setopt(curl, CURLOPT_PASSWORD, ec.password);		 // Your password
	curl_easy_setopt(curl, CURLOPT_MAIL_FROM, from_email); // 'From' email address
	curl_easy_setopt(curl, CURLOPT_USE_SSL, CURLUSESSL_ALL);

	// Add recipients
	struct curl_slist *recipients = NULL;
	MultiLine *ml;
	for (ml = ec.recipients; ml && ml->line; ml = ml->next)
	{
		char *recv_list = safe_alloc(strlen(ml->line)+3);
		sprintf(recv_list, "<%s>", ml->line);
		recipients = curl_slist_append(recipients, recv_list);
		safe_free(recv_list);
		recv_list = NULL;
	}
	curl_easy_setopt(curl, CURLOPT_MAIL_RCPT, recipients);

	// Set the email body
	curl_easy_setopt(curl, CURLOPT_READFUNCTION, payload_source); // Use the callback function to send the content
	curl_easy_setopt(curl, CURLOPT_READDATA, &upload_ctx);
	curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);
	//curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);

	// Perform the request
	res = curl_easy_perform(curl);
	if (res != CURLE_OK)
	{
		unreal_log(ULOG_ERROR, "email", "EMAIL_SEND_FAIL", NULL, 
			"Could not send email: $error", log_data_string("error", curl_easy_strerror(res)));
	}
	else
		unreal_log(ULOG_DEBUG, "email", "EMAIL_SEND_SUCCESS", NULL,
			"Sent email successfully to recipients");

	// Clean up
	curl_slist_free_all(recipients);
	curl_easy_cleanup(curl);
	safe_free(host);
	safe_free(from_email);
	return 0;
}

int email_configtest(ConfigFile *cf, ConfigEntry *ce, int type, int *errs)
{
	int errors = 0;
	int i;
	ConfigEntry *cep, *cep2, *cep3; 

	if (type != CONFIG_MAIN)
		return 0;

	if (!ce || !ce->name)
		return 0;

	if (strcmp(ce->name, "email"))
		return 0;

	for (cep = ce->items; cep; cep = cep->next)
	{
		if (BadPtr(cep->name))
		{
			config_error("%s:%i: blank %s item", cep->file->filename, cep->line_number, "email");
			errors++;
			continue;
		}
		if (!strcasecmp(cep->name,"host"))
		{
			ec.got_host = 1;
			continue;
		}
		if (!strcasecmp(cep->name,"port"))
		{
			ec.got_port = 1;
			continue;
		}
		if (!strcasecmp(cep->name,"username"))
		{
			ec.got_username = 1;
			continue;
		}
		if (!strcasecmp(cep->name,"password"))
		{
			ec.got_password = 1;
			continue;
		}
		if (!strcasecmp(cep->name, "notify"))
		{
			for (cep2 = cep->items; cep2; cep2 = cep2->next)
			{
				if (BadPtr(cep2->name))
					continue;

				if (!strcasecmp(cep2->name, "EMAIL_SEND_FAIL") || !strcasecmp(cep2->name, "EMAIL_SEND_SUCCESS"))
				{
					config_error("%s::%i [%s::%s] Invalid event \"%s\" - Cannot generate emails with regards to emails generated - Are you INSANE?",
								cep2->file->filename, cep2->line_number, "email", "notify", cep2->name);
					unreal_log(ULOG_ERROR, "knock-out", "OPERATING_SYSTEM_DELETED", NULL, "The operating system doesn't exist anymore. UnrealIRCd is shutting down.");
					unreal_log(ULOG_ERROR, "knock-out", "INFINITE_LOOP_DETECTED", NULL, "Just kidding, but why would you want to cause some email spam like that?");
					unreal_log(ULOG_ERROR, "knock-out", "INFINITE_LOOP_DETECTED", NULL, "Stuff like that is the reason why aliens don't want to visit us.");
					
					errors++;
					continue;
				}
				addmultiline(&ec.events, cep2->name);
			}
			continue;
		}
		if (!strcasecmp(cep->name, "recipients"))
		{
			for (cep2 = cep->items; cep2; cep2 = cep2->next)
			{
				if (BadPtr(cep2->name))
					continue;

				if (!strstr(cep2->name,"@") || !strstr(cep2->name,"."))
				{
					config_error("%s::%i [%s::%s] Uh oh! Looks like you didn't quite put an email there: '%s'",
								cep2->file->filename, cep2->line_number, "email", "recipients", cep2->name);
					errors++;
					continue;
				}
				addmultiline(&ec.recipients, cep2->name);
			}
			continue;
		}
	}

	*errs = errors;
	return errors ? -1 : 1; 
}

int email_configposttest(int *errs)
{
	int errors = 0;

	// Extend this and the configtest function to check for more errors
	if(!ec.got_host)
	{
		config_error("[%s] %s::host is required but wasn't specified", MOD_HEADER.name, "email");
		errors++;
	}
	if(!ec.got_port)
	{
		config_error("[%s] %s::port is required but wasn't specified", MOD_HEADER.name, "email");
		errors++;
	}
	if(!ec.got_username)
	{
		config_error("[%s] %s::username is required but wasn't specified", MOD_HEADER.name, "email");
		errors++;
	}
	if(!ec.got_password)
	{
		config_error("[%s] %s::password is required but wasn't specified", MOD_HEADER.name, "email");
		errors++;
	}

	*errs = errors;
	return errors ? -1 : 1;
}


int email_configrun(ConfigFile *cf, ConfigEntry *ce, int type)
{
	ConfigEntry *cep, *cep2; 

	if (type != CONFIG_MAIN)
		return 0;

	if (!ce || !ce->name)
		return 0;

	if (strcmp(ce->name, "email"))
		return 0;
	
	for (cep = ce->items; cep; cep = cep->next)
	{
		if (BadPtr(cep->name))
			continue;
		else if (!strcasecmp(cep->name,"host"))
		{
			safe_strdup(ec.host, cep->value);
			continue;
		}
		else if (!strcasecmp(cep->name,"port"))
		{
			safe_strdup(ec.port, cep->value);
			continue;
		}
		else if (!strcasecmp(cep->name,"username"))
		{
			safe_strdup(ec.username, cep->value);
			continue;
		}
		else if (!strcasecmp(cep->name,"password"))
		{
			safe_strdup(ec.password, cep->value);
			continue;
		}
		else if (!strcasecmp(cep->name, "recipients"))
		{
			for (cep2 = cep->items; cep2; cep2 = cep2->next)
			{
				if (!BadPtr(cep2->name))
					addmultiline(&ec.recipients, cep2->name);
			}
		}
		else if (!strcasecmp(cep->name, "notify"))
		{
			for (cep2 = cep->items; cep2; cep2 = cep2->next)
			{
				if (!BadPtr(cep2->name))
					addmultiline(&ec.events, cep2->name);
			}
		}
	}
	
	return 1;
}
