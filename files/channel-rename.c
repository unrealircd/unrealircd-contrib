/* Copyright © 2025 Valware
 * License: GPLv3
 * Name: third/channel-rename
 */
/*** <<<MODULE MANAGER START>>>
module
{
        documentation "https://github.com/ValwareIRC/valware-unrealircd-mods/blob/main/channel-rename/README.md";
        troubleshooting "In case of problems, please file a bug report at https://github.com/ValwareIRC/valware-unrealircd-mods/issues/new?template=bug_report.md";
        min-unrealircd-version "6.1.0";
        max-unrealircd-version "6.*";
        post-install-text {
                "The module is installed. Now all you need to do is add a loadmodule line:";
                "loadmodule \"third/channel-rename\";";
                "The module needs no other configuration.";
                "Once you're good to go, you can finally type in your shell: ./unrealircd rehash";
        }
}
*** <<<MODULE MANAGER END>>>

*/

/* One include for all */
#include "unrealircd.h"

#define CONF_BLOCK_NAME "channel-rename"
#define CLIENT_FLAG_RENAME			0x1000000000 // Server cap
#define HasRenameServerCap(x) ((x)->flags & CLIENT_FLAG_RENAME)
#define SetRenameServerCap(x) ((x)->flags |= CLIENT_FLAG_RENAME)

/* Forward declarations */
void set_config(void);
int hookfunc_configtest(ConfigFile *cf, ConfigEntry *ce, int type, int *errs);
int hookfunc_configposttest(int *errs);
int hookfunc_configrun(ConfigFile *cf, ConfigEntry *ce, int type);
long CAP_CHANNEL_RENAME = 0L;
#define CAP_CHANNEL_RENAME_NAME "draft/channel-rename"
CMD_FUNC(channel_rename);
CMD_OVERRIDE_FUNC(cmd_protoctl);
RPC_CALL_FUNC(rpc_channel_rename);
ModDataInfo *channel_rename_md = NULL;
ModDataInfo *supports_rename_md = NULL;
const char *channel_rename_md_serialize(ModData *m);
void channel_rename_md_unserialize(const char *str, ModData *m);
void do_rename_channel(Channel *chan, const char *newname, Client *client);
const char *supports_rename_md_serialize(ModData *m);
void supports_rename_md_unserialize(const char *str, ModData *m);

/* In case of remote RENAME race conditions,
 * we need to close down our local channel since
 * we know it didn't exist when the command was issued
 */
static void close_channel(Channel *chan)
{
    Member *member;
    const char *parx[4];

    // Remove don't let channel be permanent
    parx[0] = NULL;
    parx[1] = chan->name;
    parx[2] = "-P";
    parx[3] = NULL;
    do_cmd(&me, NULL, "MODE", 4, parx);
    for (member = chan->members; member; member = member->next)
    {
        const char *parv[3];
        parv[0] = NULL;
        parv[1] = chan->name;
        parv[2] = NULL;
        do_cmd(member->client, NULL, "PART", 3, parv);
        sendnotice(member->client, "You were automatically parted from %s due to a renaming conflict.", chan->name);
    }
}


struct MyConfStruct
{
    long allowed_interval;
    bool got_allowed_interval;
};
static struct MyConfStruct MyConf;

ModuleHeader MOD_HEADER
= {
    "third/channel-rename", /* Name of module */
    "1.0.1", /* Version */
    "Adds draft/channel-rename functionality (IRCv3)", /* Short description of module */
    "Valware", /* Author */
    "unrealircd-6", /* Version of UnrealIRCd */
};

// Module initialization
MOD_INIT()
{
    ModDataInfo mreq;
    ClientCapabilityInfo c;
    RPCHandlerInfo r;

    MARK_AS_GLOBAL_MODULE(modinfo);

    memset(&mreq, 0, sizeof(mreq));
    mreq.name = "channel-rename";
    mreq.type = MODDATATYPE_CHANNEL;
    mreq.serialize = channel_rename_md_serialize;
    mreq.unserialize = channel_rename_md_unserialize;
    mreq.sync = 1;
    mreq.self_write = 1;
    channel_rename_md = ModDataAdd(modinfo->handle, mreq);
    if (!channel_rename_md)
    {
        config_error("Unable to ModDataAdd() -- too many 3rd party modules loaded perhaps?");
        return MOD_FAILED;
    }

    memset(&mreq, 0, sizeof(mreq));
    mreq.name = "supports-rename";
    mreq.type = MODDATATYPE_CLIENT;
    mreq.serialize = supports_rename_md_serialize;
    mreq.unserialize = supports_rename_md_unserialize;
    mreq.sync = 1;
    supports_rename_md = ModDataAdd(modinfo->handle, mreq);
    if (!supports_rename_md)
    {
        config_error("Unable to ModDataAdd() -- too many 3rd part modules loaded perhaps?");
        return MOD_FAILED;
    }

    memset(&c, 0, sizeof(c));
    c.name = CAP_CHANNEL_RENAME_NAME;
    if (!ClientCapabilityAdd(modinfo->handle, &c, &CAP_CHANNEL_RENAME))
    {
        config_error("Unable to add capability %s", c.name);
        return MOD_FAILED;
    }

    set_config(); // Set defaults


    memset(&r, 0, sizeof(r));
    r.method = "channel.rename";
    r.loglevel = ULOG_DEBUG;
    r.call = rpc_channel_rename;

    RPCHandlerAdd(modinfo->handle, &r);
    
    if (!HookAdd(modinfo->handle, HOOKTYPE_CONFIGRUN, 0, hookfunc_configrun)) // Run through the config and set the values
    {
        config_error("Unable to add hook into HOOKTYPE_CONFIGRUN");
        return MOD_FAILED;
    }

    return MOD_SUCCESS;
}

// Module load
MOD_LOAD()
{
    if (!CommandAdd(modinfo->handle, "RENAME", channel_rename, 3, CMD_USER|CMD_SERVER))
    {
        config_error("Unable to add command RENAME");
        return MOD_FAILED;
    }

    if (!CommandOverrideAdd(modinfo->handle, "PROTOCTL", 0, cmd_protoctl))
    {
        config_error("Unable to override command PROTOCTL");
        return MOD_FAILED;
    }
    return MOD_SUCCESS;
}

// Module unload
MOD_UNLOAD()
{
    return MOD_SUCCESS;
}

// Module test
MOD_TEST()
{
    memset(&MyConf, 0, sizeof(MyConf)); // Clear it out

    HookAdd(modinfo->handle, HOOKTYPE_CONFIGTEST, 0, hookfunc_configtest); // Test the config
    //HookAdd(modinfo->handle, HOOKTYPE_CONFIGPOSTTEST, 0, hookfunc_configposttest); // Post-test the config
    return MOD_SUCCESS;
}

const char *channel_rename_md_serialize(ModData *m)
{
	static char buf[11];
	if (m->l == 0L)
		return NULL; /* not set */
	snprintf(buf, sizeof(buf), "%ld", m->l);
	return buf;
}

void channel_rename_md_unserialize(const char *str, ModData *m)
{
	m->l = atol(str);
}

const char *supports_rename_md_serialize(ModData *m)
{
	static char buf[2];
	if (m->i == 0)
		return NULL; /* not set */
	snprintf(buf, sizeof(buf), "%d", m->i);
	return buf;
}

void supports_rename_md_unserialize(const char *str, ModData *m)
{
	m->i = atoi(str);
}

CMD_FUNC(channel_rename)
{
    Channel *from, *to;

    if (BadPtr(parv[1]) || BadPtr(parv[2]))
    {
        sendnumeric(client, ERR_NEEDMOREPARAMS, "RENAME");
        return;
    }
    
    from = find_channel(parv[1]);
    to = find_channel(parv[2]);

    if (!from)
    {
        sendnumeric(client, ERR_NOSUCHCHANNEL, parv[1]);
        return;
    }

    // Skip some checks if they're not a local user
    if (MyUser(client))
    {
        if (!IsMember(client, from))
        {
            sendnumeric(client, ERR_NOTONCHANNEL, from->name);
            return;
        }

        if (!check_channel_access(client, from, "q") && (!IsOper(client) 
            || !ValidatePermissionsForPath("channel:rename", client, NULL, NULL, NULL))) // Must be channel owner to rename channel
        {
            sendnumeric(client, ERR_CHANOPRIVSNEEDED, from->name);
            sendnotice(client, "Only channel owners (+q) and IRCOps can rename channels. ");
            return;
        }

        // Channel name validation
        if (!valid_channelname(parv[2]))
        {
            sendto_one(client, NULL, "FAIL RENAME CANNOT_RENAME %s %s :Cannot rename channel: Invalid channel name.", parv[1], parv[2]);
            return;
        }

        if ((long)TStime() - moddata_channel(from, channel_rename_md).l < MyConf.allowed_interval)
        {
            sendto_one(client, NULL, "FAIL RENAME CANNOT_RENAME %s %s :Cannot rename channel: Channel has been renamed too recently.", parv[1], parv[2]);
            return;
        }
        ConfigItem_deny_channel *d;
        if ((d = find_channel_allowed(client, parv[2])))
        {
            if (d->warn)
            {
                unreal_log(ULOG_INFO, "rename", "RENAME_DENIED_FORBIDDEN_CHANNEL", client,
                            "Client $client.details tried to rename $channel_one to forbidden channel name $channel_two",
                            log_data_string("channel_one", from->name),
                            log_data_string("channel_two", parv[2]));
            }
            if (d->reason || d->redirect || d->class)
                sendto_one(client, NULL, "FAIL RENAME CANNOT_RENAME %s %s :Cannot rename channel: Forbidden channel name.", parv[1], parv[2]);
            
            return;
        }
    }
    if (to && strcasecmp(from->name, to->name))
    {
        if (!MyUser(client))
        {
            // Account for race-conditions
            // If we are here, this means the channel was created
            // on this side of the network before the remote
            // RENAME happened, so we have to destroy the new channel
            close_channel(to);
        }
        else {
            sendto_one(client, NULL, "FAIL RENAME CHANNEL_NAME_IN_USE %s %s :Cannot rename channel: That channel name is already in use.", from->name, to->name);
            return;
        }
    }

    history_destroy(from->name);
    // Rename it in the hash table
    del_from_channel_hash_table(from->name, from);
    strlcpy(from->name, parv[2], sizeof(from->name));
    add_to_channel_hash_table(parv[2], from);

    // Update last renamed timestamp
    moddata_channel(from, channel_rename_md).l = (long)TStime();

    // Do the channel renaming from client's perspective
    sendto_channel(from, client, NULL, NULL, CAP_CHANNEL_RENAME, SEND_LOCAL, recv_mtags, ":%s!%s@%s RENAME %s %s :%s", client->name, client->user->username, client->user->cloakedhost, parv[1], parv[2], (BadPtr(parv[3]) ? "No reason" : parv[3]));

    sendto_server(MyUser(client) ? NULL : client->direction, 0, 0, recv_mtags, ":%s RENAME %s %s :%s", client->id, parv[1], parv[2], (BadPtr(parv[3]) ? "No reason" : parv[3]));

    // Simulate a PART and JOIN for those who don't have a channel-rename cap, and only if the change was not entirely case-related
    if (strcasecmp(parv[1], parv[2]))
    {
        Member *m;
        for (m = from->members; m; m = m->next)
        {
            if (!MyUser(m->client)) // Only show to local users
                continue;

            if (HasCapability(m->client, "draft/channel-rename")) // We already showed it to those with this cap above
                continue;

            sendto_one(m->client, recv_mtags, ":%s!%s@%s PART %s :Channel renamed to %s: %s", m->client->name, m->client->user->username, m->client->user->cloakedhost, parv[1], parv[2], (BadPtr(parv[3]) ? "No reason" : parv[3]));
            sendto_one(m->client, recv_mtags, ":%s!%s@%s JOIN %s", m->client->name, m->client->user->username, m->client->user->cloakedhost, parv[2]);
            const char *parx[3];
            parx[0] = NULL;
            parx[1] = parv[2];
            parx[2] = NULL;
            if (!BadPtr(from->topic))
                do_cmd(m->client, NULL, "TOPIC", 2, parx);

            if (!HasCapability(m->client, "draft/no-implicit-names") && !HasCapability(m->client, "no-implicit-names")) // handle both for future
                do_cmd(m->client, NULL, "NAMES", 2, parx);

            sendto_one(m->client, NULL, ":%s NOTICE %s :This channel has been renamed from \"%s\" to \"%s\" by %s", me.name, from->name, parv[1], parv[2], client->name);
        }
    }


    // Loggit
    unreal_log(ULOG_INFO, "rename","CHANNEL_RENAME", client, "Channel $from has been renamed to $to ($client.name!$client.user.username@$client.user.cloakedhost): $reason",
                log_data_string("from", parv[1]),
                log_data_string("to", parv[2]),
                log_data_string("reason", (BadPtr(parv[3]) ? "No reason" : parv[3])));
}

// Set defaults for the configuration settings here (called in MOD_INIT)
void set_config(void)
{
    MyConf.allowed_interval = 60 * 15; // 15 minutes
}


// Configuration testing function (check for errors in the config block) (called in MOD_TEST)
int hookfunc_configtest(ConfigFile *cf, ConfigEntry *ce, int type, int *errs)
{
    // Keep track of errors, iterators, and the current config entry
    int errors = 0;
    int i;
    ConfigEntry *cep, *cep2;

    // Filter on CONFIG_MAIN only (top-level block)
    if(type != CONFIG_MAIN)
        return 0;

    // Validation check
    if(!ce || !ce->name)
        return 0;

    // Check if it's our block
    if(strcmp(ce->name, CONF_BLOCK_NAME))
        return 0;

    // Look inside the block
    for(cep = ce->items; cep; cep = cep->next)
    {
        if(!cep->value)
        {
            config_error("%s:%i: blank %s value", cep->file->filename, cep->line_number, CONF_BLOCK_NAME); // Rep0t error
            errors++;
            continue;
        }

        // Check for time
        if(!strcmp(cep->name, "allowed-interval"))
        {
            if(MyConf.got_allowed_interval)
            {
                config_error("%s:%i: duplicate %s::%s directive", cep->file->filename, cep->line_number, CONF_BLOCK_NAME, cep->name);
                errors++;
                continue;
            }

            MyConf.got_allowed_interval = 1;
            if(config_checkval(cep->value, CFG_TIME) <= 0)
            {
                config_error("%s:%i: %s::%s must be a time string like '7d10m' or simply '20'", cep->file->filename, cep->line_number, CONF_BLOCK_NAME, cep->name);
                errors++;
            }
            continue;
        }

        // Unknown directive, warn about it
        config_warn("%s:%i: unknown item %s::%s", cep->file->filename, cep->line_number, CONF_BLOCK_NAME, cep->name); // So display just a warning
    }

    // Return a bool int whether or not there were errors. If yes, -1, if no, 1
    *errs = errors;
    return errors ? -1 : 1;
}


// Run through the configuration and set the values (called in MOD_INIT, after MOD_TEST)
int hookfunc_configrun(ConfigFile *cf, ConfigEntry *ce, int type)
{
    ConfigEntry *cep, *cep2;

    if(type != CONFIG_MAIN)
        return 0;

    if(!ce || !ce->name)
        return 0;

    if(strcmp(ce->name, CONF_BLOCK_NAME))
        return 0;

    for(cep = ce->items; cep; cep = cep->next) 
    {
        if(!cep->name)
            continue;

        if(!strcmp(cep->name, "allowed-interval"))
        {
            MyConf.allowed_interval = config_checkval(cep->value, CFG_TIME);
            continue;
        }
    }

    return 1;
}

RPC_CALL_FUNC(rpc_channel_rename)
{
    Channel *from, *to;
    const char *from_name, *to_name, *reason;

    REQUIRE_PARAM_STRING("from_name", from_name);
    REQUIRE_PARAM_STRING("to_name", to_name);
    OPTIONAL_PARAM_STRING("reason", reason);

    
    from = find_channel(from_name);
    to = find_channel(to_name);

    if (!from)
    {
        rpc_error_fmt(client, request, JSON_RPC_ERROR_INVALID_PARAMS, "Could not find specified channel");
        return;
    }

    // Channel name validation
    if (!valid_channelname(to_name))
    {
        rpc_error_fmt(client, request, JSON_RPC_ERROR_INVALID_PARAMS, "Requested invalid channel name for renaming");
        return;
    }

    if (to && strcasecmp(from->name, to->name))
    {
        rpc_error_fmt(client, request, JSON_RPC_ERROR_INVALID_PARAMS, "Channel name already in use.");
        return;
    }

    history_destroy(from->name);
    // Rename it in the hash table
    del_from_channel_hash_table(from->name, from);
    strlcpy(from->name, to_name, sizeof(from->name));
    add_to_channel_hash_table(to_name, from);


    // Update last renamed timestamp
    moddata_channel(from, channel_rename_md).l = (long)TStime();

    // Do the channel renaming from client's perspective
    sendto_channel(from, client, NULL, NULL, CAP_CHANNEL_RENAME, SEND_LOCAL, NULL, ":%s RENAME %s %s :%s", me.name, from_name, to_name, reason ? reason : "No reason");

    sendto_server(MyUser(client) ? NULL : client->direction, 0, 0, NULL, ":%s RENAME %s %s :%s", client->id, from_name, to_name, reason);

    // Simulate a PART and JOIN for those who don't have a channel-rename cap, and only if the change was not entirely case-related
    if (strcasecmp(from_name, to_name))
    {
        Member *m;
        for (m = from->members; m; m = m->next)
        {
            if (!MyUser(m->client)) // Only show to local users
                continue;

            if (m->client && HasCapability(m->client, "draft/channel-rename")) // We already showed it to those with this cap above
                continue;

            sendto_one(m->client, NULL, ":%s!%s@%s PART %s :Channel renamed to %s: %s", m->client->name, m->client->user->username, m->client->user->cloakedhost, from_name, to_name, reason ? reason : "No reason");
            sendto_one(m->client, NULL, ":%s!%s@%s JOIN %s", m->client->name, m->client->user->username, m->client->user->cloakedhost, to_name);
            const char *parx[3];
            parx[0] = NULL;
            parx[1] = to_name;
            parx[2] = NULL;
            if (!BadPtr(from->topic))
                do_cmd(m->client, NULL, "TOPIC", 2, parx);

            if (!HasCapability(m->client, "draft/no-implicit-names") && !HasCapability(m->client, "no-implicit-names")) // handle both for future
                do_cmd(m->client, NULL, "NAMES", 2, parx);

            sendto_one(m->client, NULL, ":%s NOTICE %s :This channel has been renamed from \"%s\" to \"%s\" by an admin", me.name, from->name, from_name, to_name);
        }
    }

    // Loggit
    unreal_log(ULOG_INFO, "rename","CHANNEL_RENAME", NULL, "Channel $from has been renamed to $to via RPC: $reason",
                log_data_string("from", from_name),
                log_data_string("to", to_name),
                log_data_string("reason", reason));
}

/* Override PROTOCTL here and run original function.
 * We are only looking for a "RENAME" parameter so
 * we don't need to be too strict since it doesn't
 * have any value
 */
CMD_OVERRIDE_FUNC(cmd_protoctl)
{
    if (MyConnect(client))
        for (int j = 1; parv[j]; j++)
            if (!strcmp(parv[j], "RENAME"))
                SetRenameServerCap(client);
    
    CALL_NEXT_COMMAND_OVERRIDE(); // Call the next command override function
}
