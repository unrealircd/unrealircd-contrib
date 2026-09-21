/*
 * glineattempt.c
 *
 * Logs clients rejected because they are G-Lined.
 */

/*** <<<MODULE MANAGER START>>>
module
{
        min-unrealircd-version "6.*";
        post-install-text {
                "The module is installed. Now all you need to do is add a loadmodule line:";
                "loadmodule \"third/glineattempt\";";
                "And /REHASH the IRCd.";
                "The module does not need any other configuration.";
        }
}
*** <<<MODULE MANAGER END>>>
*/

#include "unrealircd.h"

#define GLINEATTEMPT_VERSION "1.1"

ModuleHeader MOD_HEADER
  = {
    "third/glineattempt",
    GLINEATTEMPT_VERSION,
    "Log failed GLINE connection attempts",
    "xxxchat",
    "unrealircd-6",
  };

int glineattempt_banned_client(Client *client, const char *bantype,
                               const char *reason, int global)
{
    if (!MyConnect(client))
        return 0;

    if (!global || strcmp(bantype, "G-Lined"))
        return 0;

    unreal_log(ULOG_INFO, "glineattempt", "GLINE_CONNECTION_REJECTED",
               client,
               "GLINE rejected connection from $client.details - reason: $reason",
               log_data_string("ip", client->ip),
               log_data_string("reason", reason));

    return 0;
}

MOD_TEST()
{
    return MOD_SUCCESS;
}

MOD_INIT()
{
    HookAdd(modinfo->handle, HOOKTYPE_BANNED_CLIENT, 0,
             glineattempt_banned_client);

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
