/*
License: GPLv3 or later
Name: third/queue
Author: Valware
*/


/*** <<<MODULE MANAGER START>>>
module
{
		documentation "https://github.com/ValwareIRC/valware-unrealircd-mods/blob/main/queue/README.md";
		troubleshooting "In case of problems, please file a bug report at https://github.com/ValwareIRC/valware-unrealircd-mods/issues/new?template=bug_report.md";
		min-unrealircd-version "6.*";
		max-unrealircd-version "6.*";
		post-install-text {
				"The module is installed. Now all you need to do is add a loadmodule line:";
				"loadmodule \"third/queue\";";
				"to your configuration, and then rehash your server.";
				"The module does not need any other configuration.";
		}
}
*** <<<MODULE MANAGER END>>>
*/

#include "unrealircd.h"

ModuleHeader MOD_HEADER
  = {
	"third/queue",
	"1.1",
	"Adds command /QUEUE",
	"Valware",
	"unrealircd-6",
};

int queue_local_quit(Client *client, MessageTag *mtags, const char *comment);
int queue_local_oper(Client *client, int add, const char *operblock, const char *operclass);

#define CMD_QUEUE "QUEUE"
#define CMD_NEXTQUEUE "NEXTQUEUE"
#define CMD_S2SQUEUE "S2SQUEUE"
#define QUEUECONF "queue"
#define CMD_QSYNC "QSYNC"
CMD_FUNC(QUEUE);
CMD_FUNC(NEXTQUEUE);
CMD_FUNC(S2SQUEUE);
CMD_FUNC(QSYNC);

RPC_CALL_FUNC(rpc_queue_list);

struct configstruct
{
	int max;
	long approx_session_length;
	char *automsg;
	
	unsigned short int got_max;
	unsigned short int got_ses_len;
	unsigned short int got_automsg;
};
static struct configstruct conf;

typedef struct ClientQueue
{
	char *id;
	char *sid;
	struct ClientQueue *next, *prev;
} ClientQueue;

static ClientQueue *queue_head = NULL, *queue_tail = NULL;

void setconf(void);
int queue_configtest(ConfigFile *cf, ConfigEntry *ce, int type, int *errs);
int queue_configrun(ConfigFile *cf, ConfigEntry *ce, int type);
int queue_serversync(Client *client);
ModDataInfo *queueMDI;
void queue_moddata_free(ModData *md);

/* Can't be arsed to track it with an int so we can just count them every time
 * Shouldn't be a problem unless you have like THOUSANDS of people in the queue
 * And hundreds more trying to join. Probably get another server lmao
*/
int count_clients_in_queue()
{
	int count = 0;
	ClientQueue *queue = queue_head;
	while (queue != NULL)
	{
		count++;
		queue = queue->next;
	}
	return count;
}

/* Get a user's position in the queue */
int get_position_in_queue(Client *client)
{
	int i = 1;
	ClientQueue *queue = queue_tail;
	while (queue != NULL)
	{
		if (!strcmp(queue->id,client->id))
			return i;
		queue = queue->prev;
		++i;
	}
	return 0; // not in queue
}

void notify_new_queue(const char *id)
{
	Client *client = find_user(id, NULL);
	unreal_log(ULOG_INFO, "queue","USER_JOINS_THE_QUEUE", client, "New user in the queue: $name", log_data_string("name", client->name));
}

/* Checks if the client is in the queue by id */
int is_client_in_queue_by_id(const char *id)
{
	ClientQueue *queue = queue_head;
	while (queue != NULL)
	{
		if (!strcmp(queue->id,id))
			return 1;
		queue = queue->next;
	}
	return 0;
}

/* Checks if the client is in the queue */
int is_client_in_queue(Client *client)
{
	if (!client)
		return 0;
	return is_client_in_queue_by_id(client->id);
}


/* Join the queue pal */
int join_queue(Client *client)
{
	if (is_client_in_queue(client))
		return 0;
	
	if (count_clients_in_queue() >= conf.max)
	{
		sendto_one(client, NULL, "FAIL QUEUE QUEUE_IS_FULL :Sorry, the queue is full. Please wait a while.");
		return 0;
	}

	ClientQueue *queuer = safe_alloc(sizeof(ClientQueue));
	if (!queuer)
		return 0; // could not queue them
	
	safe_strdup(queuer->id, client->id);
	safe_strdup(queuer->sid, client->uplink->id);
	queuer->next = queue_head;
	queuer->prev = NULL;

	if (queue_head != NULL)
		queue_head->prev = queuer;
	else
		queue_tail = queuer;

	queue_head = queuer;

	int count = count_clients_in_queue();

	Client *oper;
	notify_new_queue(client->id);

	moddata_local_variable(queueMDI).ptr = queue_head;
	return count;
}

int remove_queuer_by_id(const char *id)
{
	ClientQueue *current = queue_head, *to_update;
	int count = count_clients_in_queue();
	int i = 1;
	while (current != NULL)
	{
		if (!strcmp(id,current->id))
		{
			if (current->prev != NULL)
			{
				current->prev->next = current->next;
				to_update = current->prev;
				while (to_update != NULL)
				{
					--i;
					if (!strcmp(me.id,to_update->id))
					{
						Client *target = find_user(to_update->id, NULL);
						if (target)
							sendto_one(target, NULL, "NOTE QUEUE NEW_POSITION \2%d\2 :You are now number \2%d\2 in the queue.", count-i, count-i);
					}
					to_update = to_update->prev;
				}
			}
			else
				queue_head = current->next;

			if (current->next != NULL)
				current->next->prev = current->prev;
			else
				queue_tail = current->prev;
			
			safe_free(current);

			moddata_local_variable(queueMDI).ptr = queue_head;
			return 1;
		}
		current = current->next;
		++i;
	}
	return 0;
}

int remove_queuer(Client *client)
{
	if (!client)
		return 0;
	return remove_queuer_by_id(client->id);
}

/* Get the user at the top of the queue */
Client *get_next_in_queue()
{
	if (queue_tail == NULL)
		return NULL;

	Client *client = NULL;
	ClientQueue *current = queue_tail, *to_update;
	int count = count_clients_in_queue();

	while (!(client = find_user(current->id, NULL)))
	{
		client = find_user(current->prev->id, NULL);
		current = current->prev;
	}

	if (current->prev != NULL)
	{
		current->prev->next = NULL;
		queue_tail = current->prev;
		to_update = current->prev;
		int i = 1;
		while (to_update != NULL)
		{
			if (!strcmp(me.id,to_update->id))
			{
				Client *target = find_user(to_update->id, NULL);
				if (target)
					sendto_one(target, NULL, "NOTE QUEUE NEW_POSITION \2%d\2 :You are now number \2%d\2 in the queue.", count-i, count-i);
			}
			to_update = to_update->prev;
			++i;
		}
	}
	else
	{
		queue_head = NULL;
		queue_tail = NULL;
	}

	safe_free(current);

	moddata_local_variable(queueMDI).ptr = queue_head;
	return client;
}

void list_queue_notice(Client *client)
{
	int i = 1;
	ClientQueue *current = queue_tail;
	if (!current)
	{
		sendto_one(client, NULL, "NOTE QUEUE END_OF_QUEUE :The queue is empty.");
		return;
	}
	while (current != NULL)
	{
		Client *target = find_user(current->id, NULL);
		sendto_one(client, NULL, "NOTE QUEUE QUEUE_LIST %d %s :\2%d)\2 %s%s", i, (target) ? target->name : current->id, i, (target) ? target->name : current->id, (target) ? "" : " (de-sync)");
		current = current->prev;
		++i;
	}
	sendto_one(client, NULL, "NOTE QUEUE END_OF_QUEUE :End of queue list.");
}

MOD_INIT()
{
	RPCHandlerInfo r;
	MARK_AS_GLOBAL_MODULE(modinfo);

	setconf();
	
	CommandAdd(modinfo->handle, CMD_QUEUE, QUEUE, 1, CMD_USER);
	CommandAdd(modinfo->handle, CMD_NEXTQUEUE, NEXTQUEUE, 1, CMD_OPER);
	CommandAdd(modinfo->handle, CMD_S2SQUEUE, S2SQUEUE, 2, CMD_SERVER);
	CommandAdd(modinfo->handle, CMD_QSYNC, QSYNC, 3, CMD_SERVER);
	HookAdd(modinfo->handle, HOOKTYPE_LOCAL_QUIT, 0, queue_local_quit);
	HookAdd(modinfo->handle, HOOKTYPE_LOCAL_OPER, 0, queue_local_oper);
	HookAdd(modinfo->handle, HOOKTYPE_CONFIGRUN, 0, queue_configrun);
	HookAdd(modinfo->handle, HOOKTYPE_SERVER_SYNCED, 0, queue_serversync);

	memset(&r, 0, sizeof(r));
	r.method = "queue.list";
	r.call = rpc_queue_list;
	if (!RPCHandlerAdd(modinfo->handle, &r))
	{
		config_error("[queue_list] Could not register RPC handler\"queue.list\"");
		return MOD_FAILED;
	}

	ModDataInfo mreq;
	memset(&mreq, 0, sizeof(mreq));
	mreq.type = MODDATATYPE_LOCAL_VARIABLE;
	mreq.name = "queue_list";
	mreq.free = queue_moddata_free;
	mreq.serialize = NULL;
	mreq.unserialize = NULL;
	mreq.sync = 0;
	queueMDI = ModDataAdd(modinfo->handle, mreq); // Add 'em yo
	if (!queueMDI)
		return MOD_FAILED;
	else
	{
		queue_head = moddata_local_variable(queueMDI).ptr;
		if (queue_head == NULL)
			queue_tail = NULL;
		ClientQueue *queue = queue_head;
		while (queue != NULL)
		{
			if (queue->next == NULL)
				queue_tail = queue;

			queue = queue->next;
		}
	}
	return MOD_SUCCESS;
}

MOD_LOAD()
{
	return MOD_SUCCESS;
}

MOD_TEST()
{
	memset(&conf, 0, sizeof(conf));

	HookAdd(modinfo->handle, HOOKTYPE_CONFIGTEST, 0, queue_configtest);
	return MOD_SUCCESS;
}


MOD_UNLOAD()
{
	safe_free(conf.automsg);
	return MOD_SUCCESS;
}

static char *queue_help[] = {
	"*** Help for /QUEUE ***",
	" ",
	"This command joins a queue to speak to a member of staff.",
	"You will be answered via private message when you reach the front of the queue.",
	"You will receive a NOTE from the server telling you when you matched up with a",
	"member of staff and who that member of staff is.",
	" ",
	"\2\037IMPORTANT\2\037: You should not reply to someone who are claiming to answer your",
	"query unless you have seen a NOTE from the server confirming their identity.",
	" ",
	"If in doubt, you should /WHOIS the person and verify yourself.",
	" ",
	"Syntax: /QUEUE [<option>]",
	"Examples:         \2[O]\2 = Oper-Only:",
	"/QUEUE            Joins a queue.",
	"/QUEUE POSITION   Checks your position in the queue.",
	"/QUEUE LEAVE      Leaves the queue.",
	"/QUEUE SIZE       View the size of the current queue.",
	"/QUEUE LIST       \2[O]\2 View a list of users in the queue in ascending order.",
	"/NEXTQUEUE        \2[O]\2 Begin a conversation with the next person in the queue.",

	NULL
};

static void sendhelp(Client *client)
{
	for(char **p = queue_help; *p != NULL; p++)
		sendto_one(client, NULL, ":%s %03d %s :%s", me.name, RPL_TEXT, client->name, *p);
}

CMD_FUNC(QUEUE)
{
	if (!BadPtr(parv[1]))
	{
		if (!strcasecmp(parv[1],"help"))
		{
			sendhelp(client);
		}
		else if (!strcasecmp(parv[1],"size"))
		{
			int count = count_clients_in_queue();
			sendto_one(client, NULL, "NOTE QUEUE QUEUE_SIZE %d :The size of the current queue is \2%d\2.", count, count);
		}
		else if (!strcasecmp(parv[1],"position"))
		{
			int pos = get_position_in_queue(client);
			if (!pos)
				sendto_one(client, NULL, "FAIL QUEUE NOT_IN_QUEUE :You are not in the queue.");
			else
				sendto_one(client, NULL, "NOTE QUEUE QUEUE_POSITION :You are number \2%d\2 in the queue.", pos);
		}
		else if (!strcasecmp(parv[1], "leave"))
		{
			if (!remove_queuer(client))
				sendto_one(client, NULL, "FAIL QUEUE NOT_IN_QUEUE :You are not in the queue.");
			else
			{
				sendto_server(NULL, 0, 0, NULL, "S2SQUEUE del %s", client->id);
				sendto_one(client, NULL, "NOTE QUEUE LEFT_THE_QUEUE :You have left the queue.");
			}
		}
		else if (!strcasecmp(parv[1], "list"))
		{
			if (!ValidatePermissionsForPath("queue", client, NULL, NULL, NULL))
			{
				sendnumeric(client, ERR_NOPRIVILEGES);
			}
			else
				list_queue_notice(client);
		}
	}
	else
	{
		int joined = join_queue(client);
		if (!joined)
		{
			sendto_one(client, NULL, "NOTE QUEUE UNABLE_TO_QUEUE 0 :There was an error when trying to join the queue. Maybe you are already joined?");
			return;
		}
		else
		{
			sendto_server(NULL, 0, 0, NULL, "S2SQUEUE add %s", client->id);
			sendto_one(client, NULL, "QUEUE SUCCESS JOINED_QUEUE :You have successfully joined the queue.");
			sendto_one(client, NULL, "NOTE QUEUE QUEUE_POSITION %d :You are currently number \2%d\2 in the queue. Approximate wait time: %ld minutes.", joined, joined, (conf.approx_session_length * joined) / 60);
		}
	}
	add_fake_lag(client, 5000);
}

CMD_FUNC(NEXTQUEUE)
{
	if (!ValidatePermissionsForPath("queue", client, NULL, NULL, NULL))
		return sendnumeric(client, ERR_NOPRIVILEGES);

	Client *target = get_next_in_queue();
	if (!target)
		return sendto_one(client, NULL, "FAIL QUEUE QUEUE_IS_EMPTY :There is nobody in the queue.");
	
	sendto_one(client, NULL, ":%s!%s@%s PRIVMSG %s :\2[AutoMsg]\2 You are now talking with %s",
				target->name, target->user->username, target->user->cloakedhost, client->name, target->name);
	
	if (MyUser(target))
	{
		sendto_one(target, NULL, ":%s!%s@%s PRIVMSG %s :\2[AutoMsg]\2 %s",
				client->name, client->user->username, client->user->cloakedhost, target->name, conf.automsg);
		sendto_one(target, NULL, "NOTE QUEUE ANSWERED_BY %s :You have been paired with %s.", client->name, client->name);
	}
	else
	{
		sendto_one(target, NULL, ":%s PRIVMSG %s :\2[AutoMsg]\2 %s",
				client->id, target->name, conf.automsg);
		sendto_one(target, NULL, "SREPLY %s N QUEUE ANSWERED_BY %s :You have been paired with %s.", target->name, client->name, client->name);
	}
	sendto_server(NULL, 0, 0, NULL, "QSYNC * DEL %s", target->id);
}

int queue_local_quit(Client *client, MessageTag *mtags, const char *comment)
{
	remove_queuer(client);
	return 0;
}

int queue_local_oper(Client *client, int add, const char *operblock, const char *operclass)
{
	// either de-oper or they're not queue-lined lol
	if (!add || !ValidatePermissionsForPath("queue", client, NULL, NULL, NULL))
		return 0;

	int count = count_clients_in_queue();
	if (!count)
		return 0;

	sendto_one(client, NULL, "NOTE QUEUE PEOPLE_WAITING %d :There are \2%d\2 people waiting in the queue.", count, count);
	return 0;
}

/* Set config defaults:
	queue {
		max 50;
		automsg "Thank you for waiting. How can I help you today?";
	}
*/
void setconf(void)
{
	conf.max = 50;
	conf.approx_session_length = 900;
	safe_strdup(conf.automsg, "Thank you for waiting. How can I help you today?");
}

int queue_configtest(ConfigFile *cf, ConfigEntry *ce, int type, int *errs)
{
	int errors = 0;
	int i;
	ConfigEntry *cep;

	if(type != CONFIG_MAIN)
		return 0;

	if(!ce || !ce->name)
		return 0;

	if(strcmp(ce->name, "queue"))
		return 0;

	for(cep = ce->items; cep; cep = cep->next)
	{
		if(!cep->name)
		{
			config_error("%s:%i: blank %s item", cep->file->filename, cep->line_number, QUEUECONF);
			errors++;
			continue;
		}

		if(!cep->value)
		{
			config_error("%s:%i: blank %s value", cep->file->filename, cep->line_number, QUEUECONF);
			errors++;
			continue;
		}

		if(!strcmp(cep->name, "automsg"))
		{
			if(conf.got_automsg)
			{
				config_error("%s:%i: duplicate %s::%s directive", cep->file->filename, cep->line_number, QUEUECONF, cep->name);
				errors++;
				continue;
			}

			conf.got_automsg = 1;

			if(!strlen(cep->value))
			{
				config_error("%s:%i: %s::%s cannot be empty", cep->file->filename, cep->line_number, QUEUECONF, cep->name);
				errors++;
			}
			continue;
		}


		if(!strcmp(cep->name, "session-length"))
		{
			if(conf.got_ses_len)
			{
				config_error("%s:%i: duplicate %s::%s directive", cep->file->filename, cep->line_number, QUEUECONF, cep->name);
				errors++;
				continue;
			}

			conf.got_ses_len = 1;
			if(config_checkval(cep->value, CFG_TIME) <= 0)
			{
				config_error("%s:%i: %s::%s must be a time string like 1h20m (1 hour 20 minutes)", cep->file->filename, cep->line_number, QUEUECONF, cep->name);
				errors++;
			}
			continue;
		}

		if(!strcmp(cep->name, "max-in-queue"))
		{
			if(conf.got_max)
			{
				config_error("%s:%i: duplicate %s::%s directive", cep->file->filename, cep->line_number, QUEUECONF, cep->name);
				errors++;
				continue;
			}

			conf.got_max = 1;
			for(i = 0; cep->value[i]; i++)
			{
				if(!isdigit(cep->value[i]) || (atoi(cep->value) > 1000 || atoi(cep->value) < 3))
				{
					config_error("%s:%i: %s::%s must be a number between 3 and 1000 ", cep->file->filename, cep->line_number, QUEUECONF, cep->name);
					errors++;
					break;
				}
			}
			continue;
		}
		config_warn("%s:%i: unknown item %s::%s", cep->file->filename, cep->line_number, QUEUECONF, cep->name);
	}

	*errs = errors;
	return errors ? -1 : 1;
}
int queue_configrun(ConfigFile *cf, ConfigEntry *ce, int type)
{
	ConfigEntry *cep;

	if(type != CONFIG_MAIN)
		return 0; 

	if(!ce || !ce->name)
		return 0;

	if(strcmp(ce->name, "queue"))
		return 0;

	for(cep = ce->items; cep; cep = cep->next)
	{
		if(!cep->name)
			continue;

		if(!strcmp(cep->name, "automsg"))
		{
			safe_strdup(conf.automsg, cep->value);
			continue;
		}

		if(!strcmp(cep->name, "session-length"))
		{
			conf.approx_session_length = config_checkval(cep->value, CFG_TIME);
			continue;
		}

		if(!strcmp(cep->name, "max-in-queue"))
		{
			conf.max = atoi(cep->value);
			continue;
		}
	}

	return 1; // We good
}

void queue_moddata_free(ModData *md)
{
	if(md->ptr)
	{
		safe_free(md->ptr);
		md->ptr = NULL;
	}
}

CMD_FUNC(S2SQUEUE)
{
	if (parc < 2 || BadPtr(parv[1]) || BadPtr(parv[2]))
		return;
	
	int add = (!strcmp(parv[1],"add")) ? 1 : 0;
	Client *target = find_user(parv[2], NULL);
	if (!target)
		return;

	if (add)
		join_queue(target);
	else
		remove_queuer(target);

	sendto_server(client, 0, 0, NULL, ":%s S2SQUEUE %s %s", client->name, parv[1], parv[2]);
}

/** Server syncing - Maybe a bit noisy
	So the way this works is by asking other servers about users we have in the list who
	belong to them. so if they were answered when they were desync'd they can be safely removed.
	So if we still have Bob from another server, we just ask the server if they have Bob in their
	list, and if they don't they tell us to delete it. This is because during a de-sync only someone
	on the user's server or attached server can answer them.
*/
CMD_FUNC(QSYNC)
{
	if (BadPtr(parv[2]) || BadPtr(parv[3]))
	{
		return;
	}
	Client *server = find_server(parv[1], NULL);
	int is_request = (!strcmp(parv[2],"REQ")) ? 1 : 0; 
	int is_delete = (!strcmp(parv[2],"DEL")) ? 1 : 0;
	int is_add = (!strcmp(parv[2],"ADD")) ? 1 : 0;
	if (is_request && server && IsMe(server))
	{
		if (!is_client_in_queue_by_id(parv[3]))
		{
			sendto_server(NULL, 0, 0, NULL, "QSYNC * DEL %s", parv[3]);
		}
	}

	else if (is_delete)
	{
		remove_queuer_by_id(parv[3]);
	}
	else if (is_add)
	{
		if (!is_client_in_queue_by_id(parv[3]))
		{
			/** We don't check for max queue because this is from a server not a user, so we need to stay in sync */
			ClientQueue *queuer = safe_alloc(sizeof(ClientQueue));
			safe_strdup(queuer->id, parv[3]);
			safe_strdup(queuer->sid, client->id);
			queuer->next = queue_head;
			queuer->prev = NULL;

			if (queue_head != NULL)
				queue_head->prev = queuer;
			else
				queue_tail = queuer;

			queue_head = queuer;
			notify_new_queue(parv[3]);
		}
	}

	sendto_server(client, 0, 0, NULL, ":%s QSYNC %s %s %s", client->name, parv[1], parv[2], parv[3]);
}

int queue_serversync(Client *client)
{
	ClientQueue *queue = queue_head;
	while (queue != NULL)
	{
		sendto_server(NULL, 0, 0, NULL, ":%s QSYNC %s %s %s", me.id, queue->sid, (strcmp(queue->sid,me.id)) ? "REQ" : "ADD", queue->id);
		queue = queue->next;
	}
	return 0;
}


RPC_CALL_FUNC(rpc_queue_list)
{
	json_t *result, *list, *item;
	Client *queuer;
	ClientQueue *queue = queue_tail;
	int details;
	result = json_object();
	list = json_array();

	OPTIONAL_PARAM_INTEGER("object_detail_level", details, 4);
	json_object_set_new(result, "list", list);

	while (queue != NULL)
	{
		queuer = find_user(queue->id, NULL);
		if (!IsUser(queuer))
			continue;

		item = json_object();
		json_expand_client(item, NULL, queuer, details);
		json_array_append_new(list, item);
		queue = queue->prev;
	}

	rpc_response(client, request, result);
	json_decref(result);
}
