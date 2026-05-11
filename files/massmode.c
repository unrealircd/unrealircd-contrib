/*
  Licence: GPLv3 or later
  Copyright Ⓒ 2026 Valware
*/
/*** <<<MODULE MANAGER START>>>
module
{
		documentation "https://github.com/ValwareIRC/valware-unrealircd-mods/blob/main/massmode/README.md";
		troubleshooting "In case of problems, documentation or e-mail me at v.a.pond@outlook.com";
		min-unrealircd-version "6.0.7";
		max-unrealircd-version "6.*";
		post-install-text {
				"The module is installed. Now all you need to do is add a loadmodule line:";
				"loadmodule \"third/massmode\";";
				"And rehash the server.";
		}
}
*** <<<MODULE MANAGER END>>>
*/

#include "unrealircd.h"

/* Module header */
ModuleHeader MOD_HEADER = {
	"third/massmode",
	"1.0",
	"Allows operators to mass set modes on all channels or users",
	"Valware",
	"unrealircd-6",
};

CMD_FUNC(cmd_massmode)
{
    if (BadPtr(parv[1]))
    {
        sendnumeric(client, ERR_NEEDMOREPARAMS, "MASSMODE", "Not enough parameters");
        return;
    }
    if (!strcasecmp(clictx->cmd->cmd, "MASSMODE"))
    {
        if (!ValidatePermissionsForPath("mass:channelmode",client,NULL,NULL,NULL))
        {
            sendnumeric(client, ERR_NOPRIVILEGES, "You do not have permission to mass change channel modes");
            return;
        }
        Channel *chan;
        for (chan = channels; chan; chan = chan->nextch)
        {
            
            const char *parx[parc+2];
            parx[0] = NULL;
            parx[1] = chan->name;
            int i = 2;
            int j = 1;
            while (!BadPtr(parv[j]))
                parx[i++] = parv[j++];
            
            parx[i] = NULL;
            do_cmd(&me, NULL, "MODE", MAXPARA, parx);
        }
    } else if (!strcasecmp(clictx->cmd->cmd, "MASSUMODE"))
    {
        if (!ValidatePermissionsForPath("mass:usermode",client,NULL,NULL,NULL))
        {
            sendnumeric(client, ERR_NOPRIVILEGES, "You do not have permission to mass change user modes");
            return;
        }
        Client *cptr;
        list_for_each_entry(cptr, &client_list, client_node)
        {
            const char *parx[parc+2];
            parx[0] = NULL;
            parx[1] = cptr->name;
            int i = 2;
            int j = 1;
            while (!BadPtr(parv[j]))
                parx[i++] = parv[j++];
            
            parx[i] = NULL;
            do_cmd(&me, NULL, "SVS2MODE", MAXPARA, parx);
        }
    }
}

MOD_INIT()
{
	MARK_AS_GLOBAL_MODULE(modinfo);
    CommandAdd(modinfo->handle, "MASSMODE", cmd_massmode, MAXPARA, CMD_OPER);
    CommandAdd(modinfo->handle, "MASSUMODE", cmd_massmode, MAXPARA, CMD_OPER);
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
