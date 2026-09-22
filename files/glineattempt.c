*/
/* Copyright (C) All Rights Reserved
** Written by Kelerion <kelerion@8010.co.uk>
** Licensed under the GNU General Public License v3.0
** License: https://gnu.org
*/
/*
 * glineattempt.c
 *
 * Logs clients rejected because they are G-Lined.
 */

#include "unrealircd.h"

ModuleHeader MOD_HEADER
  = {
        "third/glineattempt", /* name */
        "1.1.0", /* version */
        "Enable being able to see attempted gline connection requests", /* description */
        "Kelerion", /* author */
        "unrealircd-6",
    };

/*** <<<MODULE MANAGER START>>>
module
{
        documentation "https://www.unrealircd.org";
        troubleshooting "In case of problems, e-mail me at kelerion@8010.co.uk";
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
