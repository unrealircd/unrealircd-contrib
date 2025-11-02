/* GPLv3 or later
   Valware © 2025
   Sends a server notice to a specific user
*/
/*** <<<MODULE MANAGER START>>>
module
{
        documentation "https://github.com/ValwareIRC/valware-unrealircd-mods/blob/main/rpc-servernotice/README.md";
        troubleshooting "In case of problems, check the documentation or e-mail me at v.a.pond@outlook.com";
        min-unrealircd-version "6.1.10";
        max-unrealircd-version "6.*";
        post-install-text
        {
                "The module is installed. Now all you need to do is add a loadmodule line:";
                "loadmodule \"third/rpc-servernotice\";";
                "Once you're good to go, you can finally type in your shell: ./unrealircd rehash";
        }
}
*** <<<MODULE MANAGER END>>>
*/

#include "unrealircd.h"

RPC_CALL_FUNC(rpc_servernotice);

ModuleHeader MOD_HEADER = {
	"third/rpc-servernotice",
	"1.0",
	"Send server notices to users via RPC",
	"Valware",
	"unrealircd-6"
};

MOD_INIT()
{
    RPCHandlerInfo r;

	memset(&r, 0, sizeof(r));
	r.method = "servernotice.send";
	r.loglevel = ULOG_DEBUG;
	r.call = rpc_servernotice;
	if (!RPCHandlerAdd(modinfo->handle, &r))
	{
		config_error("[third/rpc-servernotice] Could not register RPC handler 'servernotice.send'");
		return MOD_FAILED;
	}
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

RPC_CALL_FUNC(rpc_servernotice)
{
    json_t *result = json_object();  // initialize result
    Client *target;
    const char *nick, *msg;
    int as_privmsg;

    REQUIRE_PARAM_STRING("nick", nick);
    REQUIRE_PARAM_STRING("msg", msg);
    OPTIONAL_PARAM_BOOLEAN("as_privmsg", as_privmsg, 0);

    target = find_client(nick, NULL);
    if (!target)
    {
        rpc_error(client, request, JSON_RPC_ERROR_NOT_FOUND, "Nickname not found");
        json_decref(result);
        return;
    }

    char *msg_copy = strdup(msg);
    if (!msg_copy)
    {
        rpc_error(client, request, JSON_RPC_ERROR_INTERNAL_ERROR, "Memory allocation failed");
        json_decref(result);
        return;
    }
    
    char *saveptr = NULL;
    char *line = strtok_r(msg_copy, "\\n", &saveptr);

    while (line)
    {
        // skip empty lines
        if (*line != '\0')
        {
            if (!as_privmsg)
                sendnotice(target, "%s", line);
            else
            {
                const char *parx[4];
                MessageTag *mtags;
                new_message(&me, NULL, &mtags);
                sendto_one(target, mtags, ":%s PRIVMSG %s :%s", me.name, target->name, line);
                free_message_tags(mtags);
            }
        }
        line = strtok_r(NULL, "\\n", &saveptr);
    }

    free(msg_copy);

    rpc_response(client, request, result);
    json_decref(result);
}

