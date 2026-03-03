/*
  Licence: GPLv3 or later
  Copyright Ⓒ 2026 Valware
*/
/*** <<<MODULE MANAGER START>>>
module
{
		documentation "https://github.com/ValwareIRC/valware-unrealircd-mods/blob/main/member-roles/member-roles.conf.example";
		troubleshooting "In case of problems, documentation or e-mail me at v.a.pond@outlook.com";
		min-unrealircd-version "6.2.*";
		max-unrealircd-version "6.*";
		post-install-text {
				"The module is installed. Now all you need to do is add a loadmodule line:";
				"loadmodule \"third/member-roles\";";
				"And configure your custom member roles. See the example config and documentation for details.";
		}
}
*** <<<MODULE MANAGER END>>>
*/

#include "unrealircd.h"

/* Permission values: 0 = unset (inherit/default), 1 = yes, 2 = deny (explicit revoke) */
#define MRPERM_UNSET 0
#define MRPERM_YES   1
#define MRPERM_DENY  2

/* Permissions structure */
struct MemberRolePermissions
{
	int can_kick;
	int can_topic;
	int can_invite;
	int can_override_bans;  /* can talk even if banned */
	int is_voice;           /* bypass message restrictions (+m/+n/+c/+S/+N/+C/+T/+R/+M) */
	int is_unkickable;      /* cannot be kicked */
	int can_see_bans;       /* can view +b ban list */
	int can_see_invex;      /* can view +I invex list */
	int can_see_excepts;    /* can view +e except list */
	char *can_set;      /* modes this role can set (all channel modes) */
	char *can_unset;    /* modes this role can unset (all channel modes) */
};

/* Track which permissions were directly set (not inherited) */
struct MemberRoleDirectPerms
{
	unsigned can_kick:1;
	unsigned can_topic:1;
	unsigned can_invite:1;
	unsigned can_override_bans:1;
	unsigned is_voice:1;
	unsigned is_unkickable:1;
	unsigned can_see_bans:1;
	unsigned can_see_invex:1;
	unsigned can_see_excepts:1;
	unsigned can_set:1;
	unsigned can_unset:1;
};

/* Helper: parse a tri-state permission value */
static int parse_permission_value(const char *value)
{
	if (!strcmp(value, "yes"))
		return MRPERM_YES;
	else if (!strcmp(value, "deny"))
		return MRPERM_DENY;
	return MRPERM_UNSET;
}

/* Helper: merge a tri-state permission (deny wins over yes, yes wins over unset) */
static int merge_permission(int child, int parent)
{
	if (child == MRPERM_DENY || parent == MRPERM_DENY)
		return MRPERM_DENY;
	if (child == MRPERM_YES || parent == MRPERM_YES)
		return MRPERM_YES;
	return MRPERM_UNSET;
}

/* Helper: return string representation of permission value */
static const char *perm_to_str(int perm)
{
	switch (perm)
	{
		case MRPERM_YES: return "yes";
		case MRPERM_DENY: return "deny";
		default: return "no";
	}
}

/* Member role definition */
struct MemberRole
{
	struct MemberRole *prev, *next;
	char *name;
	char prefix;
	char sjoin_prefix;
	int rank;
	char mode;
	char *inherit;  /* role name to inherit from */
	struct MemberRolePermissions permissions;
	struct MemberRoleDirectPerms direct;  /* track which perms are directly set */
	Cmode *cmode;  /* pointer to registered cmode */
	unsigned is_builtin:1;  /* 1 if this is a built-in default role (q/a/o/h/v) */
};

static struct MemberRole *member_roles = NULL;
static struct MemberRolePermissions default_permissions;
static int have_default_permissions = 0;
static int using_builtin_defaults = 0;  /* 1 if only built-in defaults are loaded (no custom config) */
static int config_block_seen = 0;       /* 1 if a member-roles {} block was found in config (even if empty) */
static Module *module_handle = NULL;    /* stored module handle for dynamic mode registration */

/* Built-in default role definitions mirroring stock channel modes (q/a/o/h/v).
 * These are loaded when no member-roles {} config block is present,
 * replacing the need for chanowner.c, chanadmin.c, chanop.c, halfop.c, voice.c.
 */
struct BuiltinRoleDef {
	const char *name;
	char mode;
	char prefix;
	char sjoin_prefix;
	int rank;
	int can_kick;
	int can_topic;
	int can_invite;
	int can_override_bans;
	int is_voice;
	int is_unkickable;
	int can_see_bans;
	int can_see_invex;
	int can_see_excepts;
	const char *can_set;
	const char *can_unset;
};

static struct BuiltinRoleDef builtin_role_defs[] = {
	/*         name         mode pfx  sjoin rank  kick  topic inv   bans  voice unkick seeb  seei  seee  set   unset */
	{ "chanowner",  'q', '~', '*', 4000, MRPERM_YES, MRPERM_YES, MRPERM_YES, MRPERM_YES, MRPERM_YES, MRPERM_UNSET, MRPERM_YES, MRPERM_YES, MRPERM_YES, "*", "*" },
	{ "chanadmin",  'a', '&', '~', 3000, MRPERM_YES, MRPERM_YES, MRPERM_YES, MRPERM_YES, MRPERM_YES, MRPERM_UNSET, MRPERM_YES, MRPERM_YES, MRPERM_YES, "*", "*" },
	{ "chanop",     'o', '@', '@', 2000, MRPERM_YES, MRPERM_YES, MRPERM_YES, MRPERM_YES, MRPERM_YES, MRPERM_UNSET, MRPERM_YES, MRPERM_YES, MRPERM_YES, "*", "*" },
	{ "halfop",     'h', '%', '%', 1000, MRPERM_YES, MRPERM_YES, MRPERM_YES, MRPERM_YES, MRPERM_YES, MRPERM_UNSET, MRPERM_YES, MRPERM_YES, MRPERM_YES, "*", "*" },
	{ "voice",      'v', '+', '+',   -1, MRPERM_UNSET, MRPERM_UNSET, MRPERM_UNSET, MRPERM_UNSET, MRPERM_YES, MRPERM_UNSET, MRPERM_UNSET, MRPERM_UNSET, MRPERM_UNSET, NULL, NULL },
};
#define NUM_BUILTIN_ROLES (sizeof(builtin_role_defs) / sizeof(builtin_role_defs[0]))

#define OURCONF "member-roles"

/* Forward declarations */
int member_roles_configtest(ConfigFile *cf, ConfigEntry *ce, int type, int *errs);
int member_roles_configrun(ConfigFile *cf, ConfigEntry *ce, int type);
int member_role_is_ok(Client *client, Channel *channel, char mode, const char *param, int type, int what);
struct MemberRole *find_role_by_mode(char mode);
static struct MemberRole *find_role_by_name(const char *name);
struct MemberRole *find_highest_role(Client *client, Channel *channel);
void free_role(struct MemberRole *role);
void free_all_roles(void);
static int check_inheritance_loop(ConfigFile *cf, const char *role_name, const char *inherit_name, int depth);
static void resolve_role_inheritance(struct MemberRole *role, int depth);
int member_role_can_kick(Client *client, Client *victim, Channel *channel, const char *comment,
                         const char *client_member_modes, const char *victim_member_modes, const char **reject_reason);
int member_role_can_set_topic(Client *client, Channel *channel, const char *topic, const char **errmsg);
int member_role_pre_invite(Client *client, Client *target, Channel *channel, int *override);
int member_role_can_send_to_channel(Client *client, Channel *channel, Membership *lp, const char **msg, const char **errmsg, SendType sendtype, ClientContext *clictx);
int member_role_can_bypass_channel_message_restriction(Client *client, Channel *channel, BypassChannelMessageRestrictionType bypass_type);
int member_role_check_mode_access(Client *client, Channel *channel, MessageTag *mtags, const char *modebuf, const char *parabuf, time_t sendts, int samode);
const char *extban_automode_conv_param(BanContext *b, Extban *extban);
int extban_automode_is_ok(BanContext *b);
int automode_join(Client *client, Channel *channel, MessageTag *mtags);
CMD_FUNC(cmd_memberroles);
int member_roles_packet(Client *from, Client *to, Client *intended_to, char **msg, int *length);
int member_roles_reparsemode(Client *client, char **msg, int *length);
static struct MemberRolePermissions *get_effective_permissions(Client *client, Channel *channel);
static void ensure_builtin_defaults(void);
static int has_nonstock_roles(void);
static int register_member_modes(void);
int member_roles_server_synced(Client *client);
RPC_CALL_FUNC(rpc_member_roles_list);
RPC_CALL_FUNC(rpc_member_roles_get);

ModuleHeader MOD_HEADER =
{
	"third/member-roles",
	"1.0",
	"Custom channel member roles and permissions",
	"Valware",
	"unrealircd-6",
};

MOD_INIT()
{
	ExtbanInfo extban_req;
	RPCHandlerInfo r;
	
	MARK_AS_GLOBAL_MODULE(modinfo);
	module_handle = modinfo->handle;
	
	/* Register extban */
	memset(&extban_req, 0, sizeof(extban_req));
	extban_req.letter = 'M';
	extban_req.name = "automode";
	extban_req.is_ok = extban_automode_is_ok;
	extban_req.conv_param = extban_automode_conv_param;
	extban_req.is_banned = NULL; /* Not used for bans, only +I/+e */
	extban_req.options = EXTBOPT_INVEX; /* Available for +I and +e */
	if (!ExtbanAdd(modinfo->handle, extban_req))
	{
		config_error("[member-roles] Failed to register ~automode extban");
		return MOD_FAILED;
	}
	
	/* Add hooks for permission checking */
	HookAdd(modinfo->handle, HOOKTYPE_CONFIGRUN, 0, member_roles_configrun);
	HookAdd(modinfo->handle, HOOKTYPE_CAN_KICK, 0, member_role_can_kick);
	HookAdd(modinfo->handle, HOOKTYPE_CAN_SET_TOPIC, 0, member_role_can_set_topic);
	HookAdd(modinfo->handle, HOOKTYPE_PRE_INVITE, 0, member_role_pre_invite);
	HookAdd(modinfo->handle, HOOKTYPE_CAN_SEND_TO_CHANNEL, 0, member_role_can_send_to_channel);
	HookAdd(modinfo->handle, HOOKTYPE_CAN_BYPASS_CHANNEL_MESSAGE_RESTRICTION, 0, member_role_can_bypass_channel_message_restriction);
	HookAdd(modinfo->handle, HOOKTYPE_PRE_LOCAL_CHANMODE, -10000, member_role_check_mode_access);
	HookAdd(modinfo->handle, HOOKTYPE_LOCAL_JOIN, 0, automode_join);
	HookAdd(modinfo->handle, HOOKTYPE_REMOTE_JOIN, 0, automode_join);
	HookAdd(modinfo->handle, HOOKTYPE_PACKET, 0, member_roles_packet);
	HookAdd(modinfo->handle, HOOKTYPE_SERVER_SYNCED, 0, member_roles_server_synced);
	
	/* Add command for listing roles */
	CommandAdd(modinfo->handle, "MEMBERROLES", cmd_memberroles, 0, CMD_USER);
	
	/* Register RPC handlers */
	memset(&r, 0, sizeof(r));
	r.method = "member_roles.list";
	r.loglevel = ULOG_DEBUG;
	r.call = rpc_member_roles_list;
	if (!RPCHandlerAdd(modinfo->handle, &r))
	{
		config_error("[member-roles] Could not register RPC handler member_roles.list");
		return MOD_FAILED;
	}
	memset(&r, 0, sizeof(r));
	r.method = "member_roles.get";
	r.loglevel = ULOG_DEBUG;
	r.call = rpc_member_roles_get;
	if (!RPCHandlerAdd(modinfo->handle, &r))
	{
		config_error("[member-roles] Could not register RPC handler member_roles.get");
		return MOD_FAILED;
	}

	return MOD_SUCCESS;
}

MOD_LOAD()
{
	/* If no member-roles {} block was found in config, load built-in defaults.
	 * This is the fallback for users who load the module without any config.
	 */
	if (!config_block_seen)
	{
		ensure_builtin_defaults();
		if (register_member_modes() != MOD_SUCCESS)
			return MOD_FAILED;
	}
	else if (config_block_seen && member_roles == NULL)
	{
		/* Empty config block: check if any currently linked server needs compat.
		 * This handles the rehash case where servers are already linked.
		 */
		Client *acptr;
		list_for_each_entry(acptr, &global_server_list, client_node)
		{
			if (acptr->server && acptr->server->features.protocol < 6100)
			{
				unreal_log(ULOG_WARNING, "member-roles", "MEMBER_ROLES_COMPAT_OVERRIDE", acptr,
				    "[member-roles] Server $client has protocol < 6100. "
				    "Loading built-in default roles for compatibility despite empty config block.");
				ensure_builtin_defaults();
				if (register_member_modes() != MOD_SUCCESS)
					return MOD_FAILED;
				break;
			}
		}
	}

	/* Track whether we're running purely on built-in defaults */
	using_builtin_defaults = (member_roles != NULL) && !has_nonstock_roles();

	return MOD_SUCCESS;
}

/* Register all member_roles as channel prefix modes via CmodeAdd.
 * Skips roles that already have a cmode pointer (already registered).
 * Returns MOD_SUCCESS or MOD_FAILED.
 */
static int register_member_modes(void)
{
	CmodeInfo creq;
	struct MemberRole *role;

	for (role = member_roles; role; role = role->next)
	{
		if (role->cmode)
			continue; /* already registered */

		memset(&creq, 0, sizeof(creq));
		creq.paracount = 1;
		creq.is_ok = member_role_is_ok;
		creq.letter = role->mode;
		creq.prefix = role->prefix;
		creq.sjoin_prefix = role->sjoin_prefix;
		creq.rank = role->rank;
		creq.unset_with_param = 1;
		creq.type = CMODE_MEMBER;
		
		role->cmode = CmodeAdd(module_handle, creq, NULL);
		if (!role->cmode)
		{
			config_error("[member-roles] Failed to register mode +%c (prefix %c) for role '%s' - mode may already exist",
			            role->mode, role->prefix, role->name);
			return MOD_FAILED;
		}
	}
	
	return MOD_SUCCESS;
}

MOD_UNLOAD()
{
	free_all_roles();
	return MOD_SUCCESS;
}

/* Helper: find role by name during configtest */
static struct MemberRole *find_role_by_name(const char *name)
{
	struct MemberRole *role;
	for (role = member_roles; role; role = role->next)
	{
		if (!strcmp(role->name, name))
			return role;
	}
	return NULL;
}

/* Helper: check for inheritance loops and depth */
static int check_inheritance_loop(ConfigFile *cf, const char *role_name, const char *inherit_name, int depth)
{
	ConfigEntry *ce, *cep, *cepp, *ceppp;
	
	/* Check depth limit */
	if (depth > 8)
		return 1;
	
	/* If we've reached the starting role, we have a loop */
	if (!strcmp(role_name, inherit_name))
		return 1;
	
	/* Find the role being inherited from */
	for (ce = cf->items; ce; ce = ce->next)
	{
		if (!ce->name || strcmp(ce->name, OURCONF))
			continue;
			
		for (cep = ce->items; cep; cep = cep->next)
		{
			if (!strcmp(cep->name, inherit_name))
			{
				/* Found the inherited role, check if it has an inherit directive */
				for (cepp = cep->items; cepp; cepp = cepp->next)
				{
					if (!strcmp(cepp->name, "permissions"))
					{
						for (ceppp = cepp->items; ceppp; ceppp = ceppp->next)
						{
							if (!strcmp(ceppp->name, "inherit"))
							{
								/* Recursively check for loops */
								return check_inheritance_loop(cf, role_name, ceppp->value, depth + 1);
							}
						}
					}
				}
				/* No inherit directive found, no loop */
				return 0;
			}
		}
	}
	
	/* Role not found - will be caught by validation elsewhere */
	return 0;
}

/* Helper: resolve inheritance and merge permissions */
static void resolve_role_inheritance(struct MemberRole *role, int depth)
{
	struct MemberRole *parent;
	char *merged_can_set, *merged_can_unset;
	
	/* Prevent infinite recursion */
	if (depth > 8)
		return;
	
	/* If no inheritance, nothing to do */
	if (!role->inherit)
		return;
	
	/* Find parent role */
	parent = find_role_by_name(role->inherit);
	if (!parent)
		return; /* Will be caught by configtest */
	
	/* Recursively resolve parent's inheritance first */
	resolve_role_inheritance(parent, depth + 1);
	
	/* Merge boolean permissions (deny wins over yes, yes wins over unset) */
	role->permissions.can_kick = merge_permission(role->permissions.can_kick, parent->permissions.can_kick);
	role->permissions.can_topic = merge_permission(role->permissions.can_topic, parent->permissions.can_topic);
	role->permissions.can_invite = merge_permission(role->permissions.can_invite, parent->permissions.can_invite);
	role->permissions.can_override_bans = merge_permission(role->permissions.can_override_bans, parent->permissions.can_override_bans);
	role->permissions.is_voice = merge_permission(role->permissions.is_voice, parent->permissions.is_voice);
	role->permissions.is_unkickable = merge_permission(role->permissions.is_unkickable, parent->permissions.is_unkickable);
	role->permissions.can_see_bans = merge_permission(role->permissions.can_see_bans, parent->permissions.can_see_bans);
	role->permissions.can_see_invex = merge_permission(role->permissions.can_see_invex, parent->permissions.can_see_invex);
	role->permissions.can_see_excepts = merge_permission(role->permissions.can_see_excepts, parent->permissions.can_see_excepts);
	
	/* Merge can_set modes */
	if (parent->permissions.can_set)
	{
		if (role->permissions.can_set)
		{
			/* If either side is wildcard, result is wildcard */
			if (strchr(role->permissions.can_set, '*') || strchr(parent->permissions.can_set, '*'))
			{
				safe_free(role->permissions.can_set);
				safe_strdup(role->permissions.can_set, "*");
			}
			else
			{
				/* Merge unique characters */
				merged_can_set = safe_alloc(strlen(role->permissions.can_set) + strlen(parent->permissions.can_set) + 1);
				strcpy(merged_can_set, role->permissions.can_set);
				
				/* Add parent modes that aren't already present */
				const char *p;
				for (p = parent->permissions.can_set; *p; p++)
				{
					if (!strchr(merged_can_set, *p))
					{
						size_t len = strlen(merged_can_set);
						merged_can_set[len] = *p;
						merged_can_set[len + 1] = '\0';
					}
				}
				
				safe_free(role->permissions.can_set);
				role->permissions.can_set = merged_can_set;
			}
		}
		else
		{
			/* Just copy parent's can_set */
			safe_strdup(role->permissions.can_set, parent->permissions.can_set);
		}
	}
	
	/* Merge can_unset modes */
	if (parent->permissions.can_unset)
	{
		if (role->permissions.can_unset)
		{
			/* If either side is wildcard, result is wildcard */
			if (strchr(role->permissions.can_unset, '*') || strchr(parent->permissions.can_unset, '*'))
			{
				safe_free(role->permissions.can_unset);
				safe_strdup(role->permissions.can_unset, "*");
			}
			else
			{
				/* Merge unique characters */
				merged_can_unset = safe_alloc(strlen(role->permissions.can_unset) + strlen(parent->permissions.can_unset) + 1);
				strcpy(merged_can_unset, role->permissions.can_unset);
				
				/* Add parent modes that aren't already present */
				const char *p;
				for (p = parent->permissions.can_unset; *p; p++)
				{
					if (!strchr(merged_can_unset, *p))
					{
						size_t len = strlen(merged_can_unset);
						merged_can_unset[len] = *p;
						merged_can_unset[len + 1] = '\0';
					}
				}
				
				safe_free(role->permissions.can_unset);
				role->permissions.can_unset = merged_can_unset;
			}
		}
		else
		{
			/* Just copy parent's can_unset */
			safe_strdup(role->permissions.can_unset, parent->permissions.can_unset);
		}
	}
}

MOD_TEST()
{
	HookAdd(modinfo->handle, HOOKTYPE_CONFIGTEST, 0, member_roles_configtest);
	return MOD_SUCCESS;
}

/* Config parser - test phase */
int member_roles_configtest(ConfigFile *cf, ConfigEntry *ce, int type, int *errs)
{
	int errors = 0;
	ConfigEntry *cep, *cepp;

	if (type != CONFIG_MAIN)
		return 0;

	if (!ce || !ce->name)
		return 0;

	if (strcmp(ce->name, OURCONF))
		return 0;

	/* Loop through each role definition */
	for (cep = ce->items; cep; cep = cep->next)
	{
		char *role_name = cep->name;
		int has_prefix = 0, has_rank = 0, has_mode = 0;
		
		if (!role_name)
		{
			config_error("%s:%i: %s block without name", cep->file->filename, cep->line_number, OURCONF);
			errors++;
			continue;
		}

		/* The "default" block only has permissions — no prefix/rank/mode */
		if (!strcmp(role_name, "default"))
		{
			for (cepp = cep->items; cepp; cepp = cepp->next)
			{
				if (!strcmp(cepp->name, "permissions"))
				{
					ConfigEntry *ceppp;
					for (ceppp = cepp->items; ceppp; ceppp = ceppp->next)
					{
						if (!strcmp(ceppp->name, "can_kick") ||
						    !strcmp(ceppp->name, "can_topic") ||
						    !strcmp(ceppp->name, "can_invite") ||
						    !strcmp(ceppp->name, "can_override_bans") ||
						    !strcmp(ceppp->name, "is_voice") ||
						    !strcmp(ceppp->name, "is_unkickable") ||
						    !strcmp(ceppp->name, "can_see_bans") ||
						    !strcmp(ceppp->name, "can_see_invex") ||
						    !strcmp(ceppp->name, "can_see_excepts"))
						{
							if (!ceppp->value || (strcmp(ceppp->value, "yes") && strcmp(ceppp->value, "no") && strcmp(ceppp->value, "deny")))
							{
								config_error("%s:%i: %s::default::permissions::%s must be 'yes', 'no', or 'deny'",
								            ceppp->file->filename, ceppp->line_number, OURCONF, ceppp->name);
								errors++;
							}
						}
						else if (!strcmp(ceppp->name, "can_set") || !strcmp(ceppp->name, "can_unset"))
						{
							if (!ceppp->value)
							{
								config_error("%s:%i: %s::default::permissions::%s requires a value",
								            ceppp->file->filename, ceppp->line_number, OURCONF, ceppp->name);
								errors++;
							}
						}
						else if (!strcmp(ceppp->name, "inherit"))
						{
							config_error("%s:%i: %s::default::permissions::inherit is not allowed in the default block",
							            ceppp->file->filename, ceppp->line_number, OURCONF);
							errors++;
						}
						else
						{
							config_warn("%s:%i: unknown %s::default::permissions item '%s'",
							           ceppp->file->filename, ceppp->line_number, OURCONF, ceppp->name);
						}
					}
				}
				else
				{
					config_warn("%s:%i: %s::default only supports 'permissions', not '%s'",
					           cepp->file->filename, cepp->line_number, OURCONF, cepp->name);
				}
			}
			continue; /* Skip the normal prefix/rank/mode checks */
		}

		/* Each role should have a block with prefix, rank, mode, permissions */
		for (cepp = cep->items; cepp; cepp = cepp->next)
		{
			if (!strcmp(cepp->name, "prefix"))
			{
				if (has_prefix)
				{
					config_error("%s:%i: duplicate %s::%s::prefix directive", 
					            cepp->file->filename, cepp->line_number, OURCONF, role_name);
					errors++;
					continue;
				}
				has_prefix = 1;
				
				if (!cepp->value || strlen(cepp->value) != 1)
				{
					config_error("%s:%i: %s::%s::prefix must be exactly one character", 
					            cepp->file->filename, cepp->line_number, OURCONF, role_name);
					errors++;
				}
			}
			else if (!strcmp(cepp->name, "rank"))
			{
				if (has_rank)
				{
					config_error("%s:%i: duplicate %s::%s::rank directive", 
					            cepp->file->filename, cepp->line_number, OURCONF, role_name);
					errors++;
					continue;
				}
				has_rank = 1;
				
				if (!cepp->value || atoi(cepp->value) <= 0)
				{
					config_error("%s:%i: %s::%s::rank must be a positive integer", 
					            cepp->file->filename, cepp->line_number, OURCONF, role_name);
					errors++;
				}
			}
			else if (!strcmp(cepp->name, "mode"))
			{
				if (has_mode)
				{
					config_error("%s:%i: duplicate %s::%s::mode directive", 
					            cepp->file->filename, cepp->line_number, OURCONF, role_name);
					errors++;
					continue;
				}
				has_mode = 1;
				
				if (!cepp->value || strlen(cepp->value) != 1)
				{
					config_error("%s:%i: %s::%s::mode must be exactly one character", 
					            cepp->file->filename, cepp->line_number, OURCONF, role_name);
					errors++;
				}
			}
			else if (!strcmp(cepp->name, "permissions"))
			{
				ConfigEntry *ceppp;
				/* Parse permissions block */
				for (ceppp = cepp->items; ceppp; ceppp = ceppp->next)
				{
					if (!strcmp(ceppp->name, "can_kick") ||
					    !strcmp(ceppp->name, "can_topic") ||
					    !strcmp(ceppp->name, "can_invite") ||
					    !strcmp(ceppp->name, "can_override_bans") ||
					    !strcmp(ceppp->name, "is_voice") ||
					    !strcmp(ceppp->name, "is_unkickable") ||
					    !strcmp(ceppp->name, "can_see_bans") ||
					    !strcmp(ceppp->name, "can_see_invex") ||
					    !strcmp(ceppp->name, "can_see_excepts"))
					{
						if (!ceppp->value || (strcmp(ceppp->value, "yes") && strcmp(ceppp->value, "no") && strcmp(ceppp->value, "deny")))
						{
							config_error("%s:%i: %s::%s::permissions::%s must be 'yes', 'no', or 'deny'",
							            ceppp->file->filename, ceppp->line_number, OURCONF, role_name, ceppp->name);
							errors++;
						}
					}
					else if (!strcmp(ceppp->name, "can_set") || !strcmp(ceppp->name, "can_unset"))
					{
						if (!ceppp->value)
						{
							config_error("%s:%i: %s::%s::permissions::%s requires a value",
							            ceppp->file->filename, ceppp->line_number, OURCONF, role_name, ceppp->name);
							errors++;
						}
					}
					else if (!strcmp(ceppp->name, "inherit"))
					{
						if (!ceppp->value)
						{
							config_error("%s:%i: %s::%s::permissions::inherit requires a role name",
							            ceppp->file->filename, ceppp->line_number, OURCONF, role_name);
							errors++;
						}
						else if (!strcmp(ceppp->value, role_name))
						{
							config_error("%s:%i: %s::%s::permissions::inherit cannot inherit from itself",
							            ceppp->file->filename, ceppp->line_number, OURCONF, role_name);
							errors++;
						}
						else if (check_inheritance_loop(cf, role_name, ceppp->value, 0))
						{
							config_error("%s:%i: %s::%s::permissions::inherit creates a circular dependency with '%s'",
							            ceppp->file->filename, ceppp->line_number, OURCONF, role_name, ceppp->value);
							errors++;
						}
					}
					else
					{
						config_warn("%s:%i: unknown %s::%s::permissions item '%s'",
						           ceppp->file->filename, ceppp->line_number, OURCONF, role_name, ceppp->name);
					}
				}
			}
			else
			{
				config_warn("%s:%i: unknown %s::%s item '%s'",
				           cepp->file->filename, cepp->line_number, OURCONF, role_name, cepp->name);
			}
		}

		if (!has_prefix)
		{
			config_error("%s:%i: %s::%s is missing required 'prefix' directive",
			            cep->file->filename, cep->line_number, OURCONF, role_name);
			errors++;
		}
		if (!has_rank)
		{
			config_error("%s:%i: %s::%s is missing required 'rank' directive",
			            cep->file->filename, cep->line_number, OURCONF, role_name);
			errors++;
		}
		if (!has_mode)
		{
			config_error("%s:%i: %s::%s is missing required 'mode' directive",
			            cep->file->filename, cep->line_number, OURCONF, role_name);
			errors++;
		}
	}

	/* Cross-role collision detection: check for duplicate mode letters, prefixes, and ranks */
	{
		ConfigEntry *cep2, *cepp2;
		for (cep = ce->items; cep; cep = cep->next)
		{
			char *name1 = cep->name;
			char mode1 = 0, prefix1 = 0;
			int rank1 = 0;
			
			if (!name1 || !strcmp(name1, "default"))
				continue;
			
			/* Extract mode, prefix, rank from this role */
			for (cepp = cep->items; cepp; cepp = cepp->next)
			{
				if (!strcmp(cepp->name, "mode") && cepp->value && strlen(cepp->value) == 1)
					mode1 = cepp->value[0];
				else if (!strcmp(cepp->name, "prefix") && cepp->value && strlen(cepp->value) == 1)
					prefix1 = cepp->value[0];
				else if (!strcmp(cepp->name, "rank") && cepp->value)
					rank1 = atoi(cepp->value);
			}
			
			/* Compare against all subsequent roles */
			for (cep2 = cep->next; cep2; cep2 = cep2->next)
			{
				char *name2 = cep2->name;
				char mode2 = 0, prefix2 = 0;
				int rank2 = 0;
				
				if (!name2 || !strcmp(name2, "default"))
					continue;
				
				for (cepp2 = cep2->items; cepp2; cepp2 = cepp2->next)
				{
					if (!strcmp(cepp2->name, "mode") && cepp2->value && strlen(cepp2->value) == 1)
						mode2 = cepp2->value[0];
					else if (!strcmp(cepp2->name, "prefix") && cepp2->value && strlen(cepp2->value) == 1)
						prefix2 = cepp2->value[0];
					else if (!strcmp(cepp2->name, "rank") && cepp2->value)
						rank2 = atoi(cepp2->value);
				}
				
				if (mode1 && mode2 && mode1 == mode2)
				{
					config_error("%s:%i: %s::%s and %s::%s both use mode letter '%c'",
					            cep2->file->filename, cep2->line_number, OURCONF, name1, OURCONF, name2, mode1);
					errors++;
				}
				if (prefix1 && prefix2 && prefix1 == prefix2)
				{
					config_error("%s:%i: %s::%s and %s::%s both use prefix character '%c'",
					            cep2->file->filename, cep2->line_number, OURCONF, name1, OURCONF, name2, prefix1);
					errors++;
				}
				if (rank1 > 0 && rank2 > 0 && rank1 == rank2)
				{
					config_warn("%s:%i: %s::%s and %s::%s both use rank %d (may cause undefined precedence)",
					           cep2->file->filename, cep2->line_number, OURCONF, name1, OURCONF, name2, rank1);
				}
			}
		}
	}

	*errs = errors;
	return errors ? -1 : 1;
}

/* Config parser - run phase */
int member_roles_configrun(ConfigFile *cf, ConfigEntry *ce, int type)
{
	ConfigEntry *cep, *cepp, *ceppp;

	if (type != CONFIG_MAIN)
		return 0;

	if (!ce || !ce->name)
		return 0;

	if (strcmp(ce->name, OURCONF))
		return 0;

	config_block_seen = 1;

	/* Parse each role definition and add to linked list */
	for (cep = ce->items; cep; cep = cep->next)
	{
		/* Handle the "default" block separately */
		if (!strcmp(cep->name, "default"))
		{
			have_default_permissions = 1;
			memset(&default_permissions, 0, sizeof(default_permissions));
			for (cepp = cep->items; cepp; cepp = cepp->next)
			{
				if (!strcmp(cepp->name, "permissions"))
				{
					for (ceppp = cepp->items; ceppp; ceppp = ceppp->next)
					{
						if (!strcmp(ceppp->name, "can_kick"))
							default_permissions.can_kick = parse_permission_value(ceppp->value);
						else if (!strcmp(ceppp->name, "can_topic"))
							default_permissions.can_topic = parse_permission_value(ceppp->value);
						else if (!strcmp(ceppp->name, "can_invite"))
							default_permissions.can_invite = parse_permission_value(ceppp->value);
						else if (!strcmp(ceppp->name, "can_override_bans"))
							default_permissions.can_override_bans = parse_permission_value(ceppp->value);
						else if (!strcmp(ceppp->name, "is_voice"))
							default_permissions.is_voice = parse_permission_value(ceppp->value);
						else if (!strcmp(ceppp->name, "is_unkickable"))
							default_permissions.is_unkickable = parse_permission_value(ceppp->value);
						else if (!strcmp(ceppp->name, "can_see_bans"))
							default_permissions.can_see_bans = parse_permission_value(ceppp->value);
						else if (!strcmp(ceppp->name, "can_see_invex"))
							default_permissions.can_see_invex = parse_permission_value(ceppp->value);
						else if (!strcmp(ceppp->name, "can_see_excepts"))
							default_permissions.can_see_excepts = parse_permission_value(ceppp->value);
						else if (!strcmp(ceppp->name, "can_set"))
							safe_strdup(default_permissions.can_set, ceppp->value);
						else if (!strcmp(ceppp->name, "can_unset"))
							safe_strdup(default_permissions.can_unset, ceppp->value);
					}
				}
			}
			continue;
		}

		{
		struct MemberRole *role = safe_alloc(sizeof(struct MemberRole));
		
		safe_strdup(role->name, cep->name);
		
		/* Set defaults */
		role->permissions.can_kick = 0;
		role->permissions.can_topic = 0;
		role->permissions.can_invite = 0;
		role->permissions.can_override_bans = 0;
		role->permissions.is_voice = 0;
		role->permissions.is_unkickable = 0;
		role->permissions.can_see_bans = 0;
		role->permissions.can_see_invex = 0;
		role->permissions.can_see_excepts = 0;
		role->permissions.can_set = NULL;
		role->permissions.can_unset = NULL;
		
		/* Parse role properties */
		for (cepp = cep->items; cepp; cepp = cepp->next)
		{
			if (!strcmp(cepp->name, "prefix"))
			{
				role->prefix = cepp->value[0];
				role->sjoin_prefix = cepp->value[0];
			}
			else if (!strcmp(cepp->name, "rank"))
			{
				role->rank = atoi(cepp->value);
			}
			else if (!strcmp(cepp->name, "mode"))
			{
				role->mode = cepp->value[0];
			}
			else if (!strcmp(cepp->name, "permissions"))
			{
				for (ceppp = cepp->items; ceppp; ceppp = ceppp->next)
				{
					if (!strcmp(ceppp->name, "can_kick"))
					{
						role->permissions.can_kick = parse_permission_value(ceppp->value);
						role->direct.can_kick = 1;
					}
					else if (!strcmp(ceppp->name, "can_topic"))
					{
						role->permissions.can_topic = parse_permission_value(ceppp->value);
						role->direct.can_topic = 1;
					}
					else if (!strcmp(ceppp->name, "can_invite"))
					{
						role->permissions.can_invite = parse_permission_value(ceppp->value);
						role->direct.can_invite = 1;
					}
					else if (!strcmp(ceppp->name, "can_override_bans"))
					{
						role->permissions.can_override_bans = parse_permission_value(ceppp->value);
						role->direct.can_override_bans = 1;
					}
					else if (!strcmp(ceppp->name, "is_voice"))
					{
						role->permissions.is_voice = parse_permission_value(ceppp->value);
						role->direct.is_voice = 1;
					}
					else if (!strcmp(ceppp->name, "is_unkickable"))
					{
						role->permissions.is_unkickable = parse_permission_value(ceppp->value);
						role->direct.is_unkickable = 1;
					}
					else if (!strcmp(ceppp->name, "can_see_bans"))
					{
						role->permissions.can_see_bans = parse_permission_value(ceppp->value);
						role->direct.can_see_bans = 1;
					}
					else if (!strcmp(ceppp->name, "can_see_invex"))
					{
						role->permissions.can_see_invex = parse_permission_value(ceppp->value);
						role->direct.can_see_invex = 1;
					}
					else if (!strcmp(ceppp->name, "can_see_excepts"))
					{
						role->permissions.can_see_excepts = parse_permission_value(ceppp->value);
						role->direct.can_see_excepts = 1;
					}
					else if (!strcmp(ceppp->name, "can_set"))
					{
						safe_strdup(role->permissions.can_set, ceppp->value);
						role->direct.can_set = 1;
					}
					else if (!strcmp(ceppp->name, "can_unset"))
					{
						safe_strdup(role->permissions.can_unset, ceppp->value);
						role->direct.can_unset = 1;
					}
					else if (!strcmp(ceppp->name, "inherit"))
						safe_strdup(role->inherit, ceppp->value);
				}
			}
		}
		
		/* Add to linked list */
		AddListItem(role, member_roles);
		} /* end of non-default role block */
	}

	/* After all roles are loaded, resolve inheritance */
	{
		struct MemberRole *role;
		for (role = member_roles; role; role = role->next)
		{
			resolve_role_inheritance(role, 0);
		}
	}

	/* Add built-in defaults for any stock modes not overridden by config.
	 * Skipped if block is empty (no roles) to honor "empty = no modes".
	 */
	if (member_roles != NULL)
		ensure_builtin_defaults();

	/* Register modes now (during CONFIGRUN, before MOD_LOAD) */
	if (register_member_modes() != MOD_SUCCESS)
		return -1;

	return 1;
}

/* Helper: find role by mode character */
struct MemberRole *find_role_by_mode(char mode)
{
	struct MemberRole *role;
	for (role = member_roles; role; role = role->next)
	{
		if (role->mode == mode)
			return role;
	}
	return NULL;
}

/* Helper: find highest ranking role user has in channel */
struct MemberRole *find_highest_role(Client *client, Channel *channel)
{
	struct MemberRole *role, *highest = NULL;
	const char *modes;
	const char *p;
	
	if (!IsMember(client, channel))
		return NULL;
	
	modes = get_channel_access(client, channel);
	if (!modes || !*modes)
		return NULL;
	
	for (p = modes; *p; p++)
	{
		role = find_role_by_mode(*p);
		if (role && (!highest || role->rank > highest->rank))
			highest = role;
	}
	
	return highest;
}

void free_role(struct MemberRole *role)
{
	if (!role)
		return;
	safe_free(role->name);
	safe_free(role->inherit);
	safe_free(role->permissions.can_set);
	safe_free(role->permissions.can_unset);
	safe_free(role);
}

void free_all_roles(void)
{
	struct MemberRole *role, *next;
	for (role = member_roles; role; role = next)
	{
		next = role->next;
		free_role(role);
	}
	member_roles = NULL;
	safe_free(default_permissions.can_set);
	safe_free(default_permissions.can_unset);
	have_default_permissions = 0;
	using_builtin_defaults = 0;
	config_block_seen = 0;
	memset(&default_permissions, 0, sizeof(default_permissions));
}

/* Create built-in default roles for any stock mode letters (q/a/o/h/v)
 * not already defined by user configuration.
 * This replaces the need for chanowner.c, chanadmin.c, chanop.c, halfop.c, voice.c.
 */
static void ensure_builtin_defaults(void)
{
	int i;
	for (i = 0; i < (int)NUM_BUILTIN_ROLES; i++)
	{
		struct BuiltinRoleDef *def = &builtin_role_defs[i];
		struct MemberRole *role;
		
		/* Skip if a role with this mode letter already exists (user-configured override) */
		if (find_role_by_mode(def->mode))
			continue;
		
		role = safe_alloc(sizeof(struct MemberRole));
		safe_strdup(role->name, def->name);
		role->mode = def->mode;
		role->prefix = def->prefix;
		role->sjoin_prefix = def->sjoin_prefix;
		role->rank = def->rank;
		role->is_builtin = 1;
		role->permissions.can_kick = def->can_kick;
		role->permissions.can_topic = def->can_topic;
		role->permissions.can_invite = def->can_invite;
		role->permissions.can_override_bans = def->can_override_bans;
		role->permissions.is_voice = def->is_voice;
		role->permissions.is_unkickable = def->is_unkickable;
		role->permissions.can_see_bans = def->can_see_bans;
		role->permissions.can_see_invex = def->can_see_invex;
		role->permissions.can_see_excepts = def->can_see_excepts;
		if (def->can_set)
			safe_strdup(role->permissions.can_set, def->can_set);
		if (def->can_unset)
			safe_strdup(role->permissions.can_unset, def->can_unset);
		
		AddListItem(role, member_roles);
	}
}

/* Check if we have custom (non-stock) roles that would be incompatible with older servers.
 * Returns 1 if any role uses a mode letter outside the stock set {q, a, o, h, v}.
 */
static int has_nonstock_roles(void)
{
	struct MemberRole *role;
	for (role = member_roles; role; role = role->next)
	{
		if (role->mode != 'q' && role->mode != 'a' && role->mode != 'o'
		    && role->mode != 'h' && role->mode != 'v')
			return 1;
	}
	return 0;
}

/* HOOKTYPE_SERVER_SYNCED: Check linked server protocol compatibility.
 * If a server has protocol < 6100 and we have non-stock custom roles,
 * log an error since the remote server won't understand our custom modes.
 */
int member_roles_server_synced(Client *client)
{
	int protocol_version;
	
	if (!client || !client->server)
		return 0;
	
	protocol_version = client->server->features.protocol;
	
	/* If we have no modes at all (empty config block) and an incompatible server links,
	 * force-load built-in defaults so the remote server's q/a/o/h/v modes are understood.
	 */
	if (protocol_version < 6100 && member_roles == NULL && config_block_seen)
	{
		unreal_log(ULOG_WARNING, "member-roles", "MEMBER_ROLES_COMPAT_OVERRIDE", client,
		    "[member-roles] Server $client has protocol version $protocol_version (< 6100). "
		    "Loading built-in default roles (q/a/o/h/v) for compatibility despite empty config block.",
		    log_data_integer("protocol_version", protocol_version));
		
		ensure_builtin_defaults();
		using_builtin_defaults = 1;
		
		if (register_member_modes() != MOD_SUCCESS)
		{
			unreal_log(ULOG_ERROR, "member-roles", "MEMBER_ROLES_COMPAT_FAILED", client,
			    "[member-roles] Failed to register built-in default modes for compatibility.");
		}
		else
		{
			extcmodes_check_for_changes();
			isupport_check_for_changes();
		}
		return 0;
	}
	
	if (protocol_version < 6100 && has_nonstock_roles())
	{
		unreal_log(ULOG_ERROR, "member-roles", "MEMBER_ROLES_INCOMPATIBLE_SERVER", client,
		    "[member-roles] Server $client has protocol version $protocol_version (< 6100) and is incompatible "
		    "with custom member roles. Custom mode letters unknown to the remote server will not "
		    "function correctly. Either upgrade the remote server or remove custom member-roles "
		    "configuration.",
		    log_data_integer("protocol_version", protocol_version));
	}
	
	return 0;
}

/* Helper: merge a mode string (can_set/can_unset) from source into dest.
 * dest is a static buffer of size bufsize. Adds unique characters from source.
 */
static void merge_mode_string(char *dest, size_t bufsize, const char *source)
{
	const char *p;
	size_t len;
	
	if (!source || !*source)
		return;
	
	/* Wildcard overrides everything */
	if (strchr(source, '*') || strchr(dest, '*'))
	{
		dest[0] = '*';
		dest[1] = '\0';
		return;
	}
	
	len = strlen(dest);
	for (p = source; *p && len < bufsize - 1; p++)
	{
		if (!strchr(dest, *p))
		{
			dest[len] = *p;
			dest[len + 1] = '\0';
			len++;
		}
	}
}

/* Helper: get the effective permissions for a user in a channel.
 * Aggregates permissions from ALL custom roles the user holds (not just the highest).
 * Boolean permissions use merge_permission (deny > yes > unset).
 * can_set/can_unset strings are merged (union of all allowed modes).
 * Returns a pointer to a static struct, or NULL if no permissions apply.
 */
static struct MemberRolePermissions *get_effective_permissions(Client *client, Channel *channel)
{
	static struct MemberRolePermissions merged;
	static char merged_can_set[256];
	static char merged_can_unset[256];
	struct MemberRole *role;
	const char *modes;
	const char *p;
	int found_any = 0;
	
	if (!IsMember(client, channel))
	{
		if (have_default_permissions)
			return &default_permissions;
		return NULL;
	}
	
	modes = get_channel_access(client, channel);
	if (!modes || !*modes)
	{
		if (have_default_permissions)
			return &default_permissions;
		return NULL;
	}
	
	/* Initialize merged struct */
	memset(&merged, 0, sizeof(merged));
	merged_can_set[0] = '\0';
	merged_can_unset[0] = '\0';
	
	/* Iterate all modes the user has and merge permissions from matching roles */
	for (p = modes; *p; p++)
	{
		role = find_role_by_mode(*p);
		if (!role)
			continue;
		
		found_any = 1;
		
		/* Merge boolean permissions */
		merged.can_kick = merge_permission(merged.can_kick, role->permissions.can_kick);
		merged.can_topic = merge_permission(merged.can_topic, role->permissions.can_topic);
		merged.can_invite = merge_permission(merged.can_invite, role->permissions.can_invite);
		merged.can_override_bans = merge_permission(merged.can_override_bans, role->permissions.can_override_bans);
		merged.is_voice = merge_permission(merged.is_voice, role->permissions.is_voice);
		merged.is_unkickable = merge_permission(merged.is_unkickable, role->permissions.is_unkickable);
		merged.can_see_bans = merge_permission(merged.can_see_bans, role->permissions.can_see_bans);
		merged.can_see_invex = merge_permission(merged.can_see_invex, role->permissions.can_see_invex);
		merged.can_see_excepts = merge_permission(merged.can_see_excepts, role->permissions.can_see_excepts);
		
		/* Merge mode strings */
		if (role->permissions.can_set)
			merge_mode_string(merged_can_set, sizeof(merged_can_set), role->permissions.can_set);
		if (role->permissions.can_unset)
			merge_mode_string(merged_can_unset, sizeof(merged_can_unset), role->permissions.can_unset);
	}
	
	if (!found_any)
	{
		if (have_default_permissions)
			return &default_permissions;
		return NULL;
	}
	
	/* Point the struct's string pointers to the static buffers (or NULL if empty) */
	merged.can_set = merged_can_set[0] ? merged_can_set : NULL;
	merged.can_unset = merged_can_unset[0] ? merged_can_unset : NULL;
	
	return &merged;
}

int member_role_is_ok(Client *client, Channel *channel, char mode, const char *param, int type, int what)
{
	Client *target;
	struct MemberRole *client_role, *mode_role;
	struct MemberRolePermissions *perms;
	
	if ((type == EXCHK_ACCESS) || (type == EXCHK_ACCESS_ERR))
	{
		target = find_user(param, NULL);

		if ((what == MODE_DEL) && (target == client))
		{
			/* User may always remove their own modes */
			return EX_ALLOW;
		}
		
		/* Find the role being set/unset */
		mode_role = find_role_by_mode(mode);
		if (!mode_role)
			return EX_DENY;
		
		/* Get aggregated permissions and highest role for rank check */
		perms = get_effective_permissions(client, channel);
		client_role = find_highest_role(client, channel);
		
		/* Check if client has permission to set/unset this mode */
		if (perms)
		{
			const char *allowed_modes = (what == MODE_ADD) ? perms->can_set : perms->can_unset;
			if (allowed_modes && (strchr(allowed_modes, mode) || strchr(allowed_modes, '*')))
			{
				/* Check if they have sufficient rank */
				int client_rank = client_role ? client_role->rank : 0;
				if (client_rank >= mode_role->rank)
					return EX_ALLOW;
			}
		}
		
		/* Custom member roles require explicit permission - no fallback to chanop */
		if (type == EXCHK_ACCESS_ERR)
		{
			sendnumeric(client, ERR_CHANOPRIVSNEEDED, channel->name);
			unreal_log(ULOG_INFO, "member-roles", "MEMBER_ROLE_MODE_DENIED", client,
			    "[member-roles] $client.details denied setting member mode +$mode_char on $channel (insufficient role)",
			    log_data_char("mode_char", mode),
			    log_data_string("channel", channel->name));
		}
		return EX_DENY;
	}

	/* fallthrough */
	return EX_DENY;
}

int member_role_can_kick(Client *client, Client *victim, Channel *channel, const char *comment,
                         const char *client_member_modes, const char *victim_member_modes, const char **reject_reason)
{
	struct MemberRolePermissions *client_perms, *victim_perms;
	static char errmsg[NICKLEN+256];
	
	/* Check if victim has is_unkickable permission */
	victim_perms = get_effective_permissions(victim, channel);
	if (victim_perms && victim_perms->is_unkickable == MRPERM_YES && !IsULine(client))
	{
		ircsnprintf(errmsg, sizeof(errmsg), ":%s %d %s %s :%s",
		            me.name, ERR_CANNOTDOCOMMAND, client->name, "KICK",
		            "user has a role that makes them unkickable");
		*reject_reason = errmsg;
		
		sendnotice(victim,
		    "*** %s tried to kick you from channel %s (%s)",
		    client->name, channel->name, comment);
		
		unreal_log(ULOG_INFO, "member-roles", "MEMBER_ROLE_KICK_DENIED", client,
		    "[member-roles] $client.details tried to kick unkickable user $victim from $channel",
		    log_data_string("victim", victim->name),
		    log_data_string("channel", channel->name));
		
		return EX_ALWAYS_DENY;
	}
	
	/* Check if client has can_kick permission */
	client_perms = get_effective_permissions(client, channel);
	if (client_perms && client_perms->can_kick == MRPERM_YES)
		return EX_ALLOW;
	
	return EX_ALLOW; /* Let other modules/default logic handle it */
}

int member_role_can_set_topic(Client *client, Channel *channel, const char *topic, const char **errmsg)
{
	struct MemberRolePermissions *perms;
	
	perms = get_effective_permissions(client, channel);
	if (perms && perms->can_topic == MRPERM_YES)
		return EX_ALLOW;
	
	return EX_ALLOW; /* Let other modules/default logic handle it */
}

int member_role_pre_invite(Client *client, Client *target, Channel *channel, int *override)
{
	struct MemberRolePermissions *perms;
	
	perms = get_effective_permissions(client, channel);
	if (perms && perms->can_invite == MRPERM_YES)
		return HOOK_ALLOW;
	
	return HOOK_CONTINUE; /* Let other modules/default logic handle it */
}

int member_role_can_send_to_channel(Client *client, Channel *channel, Membership *lp, const char **msg, const char **errmsg, SendType sendtype, ClientContext *clictx)
{
	struct MemberRolePermissions *perms;
	const char *ban_msg = NULL;
	
	if (!MyUser(client))
		return HOOK_CONTINUE;
	
	if (!lp) /* not in channel */
		return HOOK_CONTINUE;
	
	perms = get_effective_permissions(client, channel);
	if (!perms)
		return HOOK_CONTINUE;
	
	/* can_override_bans: Allow speaking even if banned
	 * The ban check in message.c only checks "vhoaq" for exemption.
	 * If our role has can_override_bans, we check if they're banned
	 * and allow them to speak anyway by not setting the error.
	 */
	if (perms->can_override_bans == MRPERM_YES)
	{
		/* Check if they would be banned */
		if (is_banned(client, channel, BANCHK_MSG, msg, &ban_msg))
		{
			/* They are banned, but we allow them to speak due to role permission */
			/* Clear any error message and return success */
			*errmsg = NULL;
			return HOOK_CONTINUE;
		}
	}
	
	return HOOK_CONTINUE;
}

int member_role_can_bypass_channel_message_restriction(Client *client, Channel *channel, BypassChannelMessageRestrictionType bypass_type)
{
	struct MemberRolePermissions *perms;
	
	perms = get_effective_permissions(client, channel);
	if (!perms)
		return HOOK_CONTINUE;
	
	/* Check is_voice permission - it bypasses ALL message restrictions */
	if (perms->is_voice == MRPERM_YES)
		return HOOK_ALLOW;
	
	return HOOK_CONTINUE;
}

/* Check if a user has permission to use a specific channel mode via can_set/can_unset */
int member_role_check_mode_access(Client *client, Channel *channel, MessageTag *mtags, const char *modebuf, const char *parabuf, time_t sendts, int samode)
{
	struct MemberRolePermissions *perms;
	const char *m;
	char what = '+';
	Cmode *cm;
	
	/* Only check for local users */
	if (!MyUser(client))
		return 0;
	
	/* Don't interfere with SAMODE, U-Lines, or servers */
	if (samode || IsULine(client) || IsServer(client))
		return 0;
	
	/* Get effective permissions (custom role or default) */
	perms = get_effective_permissions(client, channel);
	if (!perms)
		return 0; /* No permissions defined, let default permission system handle it */
	
	/* Parse the mode string */
	for (m = modebuf; *m; m++)
	{
		if (*m == '+')
		{
			what = '+';
			continue;
		}
		if (*m == '-')
		{
			what = '-';
			continue;
		}
		
		/* Find the mode handler */
		cm = find_channel_mode_handler(*m);
		if (!cm)
			continue; /* Unknown mode, will be rejected elsewhere */
		
		/* Check permissions for this mode */
		const char *allowed_modes = (what == '+') ? perms->can_set : perms->can_unset;
		
		/* If no permission string set, continue (let default system handle it) */
		if (!allowed_modes)
			continue;
		
		/* Check if this mode is in the allowed list or wildcard */
		if (!strchr(allowed_modes, *m) && !strchr(allowed_modes, '*'))
		{
			/* Not allowed - send error and deny */
			sendnumeric(client, ERR_CHANOPRIVSNEEDED, channel->name);
			unreal_log(ULOG_INFO, "member-roles", "MEMBER_ROLE_CHANMODE_DENIED", client,
			    "[member-roles] $client.details denied setting channel mode $mode_dir$mode_char on $channel",
			    log_data_char("mode_char", *m),
			    log_data_string("mode_dir", (what == '+') ? "+" : "-"),
			    log_data_string("channel", channel->name));
			return HOOK_DENY;
		}
		
		/* If wildcard "*", need to check rank for member modes */
		if (strchr(allowed_modes, '*') && cm->type == CMODE_MEMBER)
		{
			struct MemberRole *client_role = find_highest_role(client, channel);
			struct MemberRole *target_role = find_role_by_mode(*m);
			int client_rank = client_role ? client_role->rank : 0;
			if (target_role && client_rank < target_role->rank)
			{
				sendnumeric(client, ERR_CHANOPRIVSNEEDED, channel->name);
				unreal_log(ULOG_INFO, "member-roles", "MEMBER_ROLE_CHANMODE_DENIED", client,
				    "[member-roles] $client.details denied setting channel mode $mode_dir$mode_char on $channel (rank too low)",
				    log_data_char("mode_char", *m),
				    log_data_string("mode_dir", (what == '+') ? "+" : "-"),
				    log_data_string("channel", channel->name));
				return HOOK_DENY;
			}
		}
	}
	
	return 0; /* Allow */
}

CMD_FUNC(cmd_memberroles)
{
	struct MemberRole *role;
	struct MemberRole *parent;
	int is_oper = IsOper(client);
	
	sendnotice(client, "*** Member Roles%s:", using_builtin_defaults ? " (built-in defaults)" : "");
	
	/* Non-opers get a summary view (name, mode, prefix, rank only) */
	if (!is_oper)
	{
		if (!member_roles && !have_default_permissions)
		{
			sendnotice(client, "No member roles configured.");
			return;
		}
		
		if (have_default_permissions)
			sendnotice(client, "Role: default (users with no channel mode)");
		
		for (role = member_roles; role; role = role->next)
		{
			sendnotice(client, "Role: %s  Mode: +%c  Prefix: %c  Rank: %d%s",
			          role->name, role->mode, role->prefix, role->rank,
			          role->is_builtin ? " (built-in)" : "");
		}
		
		sendnotice(client, "*** End of member roles list (use as oper for full details)");
		return;
	}
	
	/* Opers get full details */
	
	/* Show default permissions if configured */
	if (have_default_permissions)
	{
		sendnotice(client, "Role: default (users with no channel mode)");
		sendnotice(client, "  Permissions:");
		sendnotice(client, "    can_kick: %s", perm_to_str(default_permissions.can_kick));
		sendnotice(client, "    can_topic: %s", perm_to_str(default_permissions.can_topic));
		sendnotice(client, "    can_invite: %s", perm_to_str(default_permissions.can_invite));
		sendnotice(client, "    can_override_bans: %s", perm_to_str(default_permissions.can_override_bans));
		sendnotice(client, "    is_voice: %s", perm_to_str(default_permissions.is_voice));
		sendnotice(client, "    is_unkickable: %s", perm_to_str(default_permissions.is_unkickable));
		sendnotice(client, "    can_see_bans: %s", perm_to_str(default_permissions.can_see_bans));
		sendnotice(client, "    can_see_invex: %s", perm_to_str(default_permissions.can_see_invex));
		sendnotice(client, "    can_see_excepts: %s", perm_to_str(default_permissions.can_see_excepts));
		sendnotice(client, "    can_set: %s", default_permissions.can_set ? default_permissions.can_set : "(none)");
		sendnotice(client, "    can_unset: %s", default_permissions.can_unset ? default_permissions.can_unset : "(none)");
	}
	
	if (!member_roles && !have_default_permissions)
	{
		sendnotice(client, "No member roles configured.");
		return;
	}
	
	for (role = member_roles; role; role = role->next)
	{
		sendnotice(client, "Role: %s%s", role->name, role->is_builtin ? " (built-in)" : "");
		sendnotice(client, "  Mode: +%c  Prefix: %c  SJOIN prefix: %c  Rank: %d", role->mode, role->prefix, role->sjoin_prefix, role->rank);
		
		if (role->inherit)
			sendnotice(client, "  Inherits from: %s", role->inherit);
		
		sendnotice(client, "  Permissions:");
		
		/* Helper macro for boolean permissions */
		#define SHOW_BOOL_PERM(perm, name, direct_flag) \
			do { \
				if (role->permissions.perm != MRPERM_UNSET) { \
					if (role->inherit && !role->direct.direct_flag) { \
						sendnotice(client, "    %s: %s (inherited from %s)", name, perm_to_str(role->permissions.perm), role->inherit); \
					} else { \
						sendnotice(client, "    %s: %s", name, perm_to_str(role->permissions.perm)); \
					} \
				} else { \
					sendnotice(client, "    %s: no", name); \
				} \
			} while(0)
		
		SHOW_BOOL_PERM(can_kick, "can_kick", can_kick);
		SHOW_BOOL_PERM(can_topic, "can_topic", can_topic);
		SHOW_BOOL_PERM(can_invite, "can_invite", can_invite);
		SHOW_BOOL_PERM(can_override_bans, "can_override_bans", can_override_bans);
		SHOW_BOOL_PERM(is_voice, "is_voice", is_voice);
		SHOW_BOOL_PERM(is_unkickable, "is_unkickable", is_unkickable);
		SHOW_BOOL_PERM(can_see_bans, "can_see_bans", can_see_bans);
		SHOW_BOOL_PERM(can_see_invex, "can_see_invex", can_see_invex);
		SHOW_BOOL_PERM(can_see_excepts, "can_see_excepts", can_see_excepts);
		
		#undef SHOW_BOOL_PERM
		
		/* Handle can_set and can_unset */
		if (role->permissions.can_set)
		{
			if (role->inherit && !role->direct.can_set)
			{
				sendnotice(client, "    can_set: %s (inherited from %s)", 
				          role->permissions.can_set, role->inherit);
			}
			else if (role->inherit)
			{
				parent = find_role_by_name(role->inherit);
				if (parent && parent->permissions.can_set)
					sendnotice(client, "    can_set: %s (merged with %s)", 
					          role->permissions.can_set, role->inherit);
				else
					sendnotice(client, "    can_set: %s", role->permissions.can_set);
			}
			else
			{
				sendnotice(client, "    can_set: %s", role->permissions.can_set);
			}
		}
		else
		{
			sendnotice(client, "    can_set: (none)");
		}
		
		if (role->permissions.can_unset)
		{
			if (role->inherit && !role->direct.can_unset)
			{
				sendnotice(client, "    can_unset: %s (inherited from %s)", 
				          role->permissions.can_unset, role->inherit);
			}
			else if (role->inherit)
			{
				parent = find_role_by_name(role->inherit);
				if (parent && parent->permissions.can_unset)
					sendnotice(client, "    can_unset: %s (merged with %s)", 
					          role->permissions.can_unset, role->inherit);
				else
					sendnotice(client, "    can_unset: %s", role->permissions.can_unset);
			}
			else
			{
				sendnotice(client, "    can_unset: %s", role->permissions.can_unset);
			}
		}
		else
		{
			sendnotice(client, "    can_unset: (none)");
		}
	}
	
	sendnotice(client, "*** End of member roles list");
}

/* Extban ~automode - automatically set modes on join via +I or +e
 * Syntax: ~automode:mode:matcher
 * Examples:
 *   +I ~automode:o:*!*@trusted.host
 *   +e ~automode:v:~account:TrustedUser
 *   +I ~automode:W:~certfp:1234567890abcdef
 */

const char *extban_automode_conv_param(BanContext *b, Extban *extban)
{
	static char retbuf[512];
	char *mode_part, *matcher_part;
	char buf[512];
	Cmode *cm;
	char *p;
	
	strlcpy(buf, b->banstr, sizeof(buf));
	
	/* Split into modes:matcher */
	mode_part = buf;
	matcher_part = strchr(buf, ':');
	
	if (!matcher_part || matcher_part == buf)
		return NULL; /* Invalid: no matcher part or empty mode */
	
	*matcher_part = '\0';
	matcher_part++;
	
	/* Mode part should have at least one character */
	if (!*mode_part)
		return NULL;
	
	/* Validate each mode character exists and is a prefix mode */
	for (p = mode_part; *p; p++)
	{
		cm = find_channel_mode_handler(*p);
		if (!cm || cm->type != CMODE_MEMBER)
			return NULL;
	}
	
	/* Matcher must not be empty */
	if (!*matcher_part)
		return NULL;
	
	/* Return the formatted extban */
	snprintf(retbuf, sizeof(retbuf), "%s:%s", mode_part, matcher_part);
	return retbuf;
}

int extban_automode_is_ok(BanContext *b)
{
	char buf[512];
	char *mode_part, *matcher_part;
	BanContext b2;
	Cmode *cm;
	char *p;
	
	/* Only allow in +I (invex) and +e (except), not in bans */
	if (b->ban_type == EXBTYPE_BAN)
	{
		if (b->is_ok_check == EXBCHK_PARAM)
			sendnotice(b->client, "ERROR: ~automode extban can only be used with +I or +e, not with bans");
		return 0;
	}
	
	strlcpy(buf, b->banstr, sizeof(buf));
	
	/* Split into modes:matcher */
	mode_part = buf;
	matcher_part = strchr(buf, ':');
	
	if (!matcher_part || matcher_part == buf)
	{
		if (b->is_ok_check == EXBCHK_PARAM)
			sendnotice(b->client, "ERROR: ~automode syntax is ~automode:modes:matcher (e.g., ~automode:ov:*!*@host)");
		return 0;
	}
	
	*matcher_part = '\0';
	matcher_part++;
	
	/* Mode part should have at least one character */
	if (!*mode_part)
	{
		if (b->is_ok_check == EXBCHK_PARAM)
			sendnotice(b->client, "ERROR: ~automode modes cannot be empty");
		return 0;
	}
	
	/* Validate each mode character and check permissions */
	for (p = mode_part; *p; p++)
	{
		/* Validate the mode exists and is a prefix mode */
		cm = find_channel_mode_handler(*p);
		if (!cm || cm->type != CMODE_MEMBER)
		{
			if (b->is_ok_check == EXBCHK_PARAM)
				sendnotice(b->client, "ERROR: ~automode mode '%c' is not a valid prefixmode", *p);
			return 0;
		}
		
		/* Check if the client has permission to set this mode on others */
		if (cm->is_ok && b->is_ok_check == EXBCHK_PARAM)
		{
			int ret = cm->is_ok(b->client, b->channel, *p, b->client->name, EXCHK_ACCESS, MODE_ADD);
			if (ret != EX_ALLOW)
			{
				sendnotice(b->client, "ERROR: You don't have permission to set mode +%c on others", *p);
				return 0;
			}
		}
	}
	
	/* Matcher must not be empty */
	if (!*matcher_part)
	{
		if (b->is_ok_check == EXBCHK_PARAM)
			sendnotice(b->client, "ERROR: ~automode matcher cannot be empty");
		return 0;
	}
	
	/* Validate the matcher part using extban_is_ok_nuh_extban */
	memcpy(&b2, b, sizeof(BanContext));
	b2.banstr = matcher_part;
	if (!extban_is_ok_nuh_extban(&b2))
	{
		if (b->is_ok_check == EXBCHK_PARAM)
			sendnotice(b->client, "ERROR: ~automode has invalid matcher '%s'", matcher_part);
		return 0;
	}
	
	return 1;
}

/* Apply automode on join */
int automode_join(Client *client, Channel *channel, MessageTag *mtags)
{
	Ban *ban;
	BanContext *b;
	char modes_buf[256];
	char params_buf[512];
	char nick_buf[NICKLEN + 1];
	int modes_len;
	int list_type;
	
	if (!MyUser(client))
		return 0; /* Only process for local users */
	
	/* Build combined mode string from all matching automodes */
	modes_buf[0] = '\0';
	modes_len = 0;
	
	b = safe_alloc(sizeof(BanContext));
	b->client = client;
	b->channel = channel;
	b->ban_check_types = BANCHK_JOIN;
	
	/* Check both invex (+I) and except (+e) lists */
	for (list_type = 0; list_type < 2; list_type++)
	{
		if (list_type == 0)
		{
			b->ban_type = EXBTYPE_INVEX;
			ban = channel->invexlist;
		}
		else
		{
			b->ban_type = EXBTYPE_EXCEPT;
			ban = channel->exlist;
		}
		
		for (; ban; ban = ban->next)
		{
			/* Check if this is an automode extban */
			if (strncmp(ban->banstr, "~automode:", 10) == 0 ||
			    strncmp(ban->banstr, "~M:", 3) == 0)
			{
				const char *params = ban->banstr;
				char *mode_part, *matcher_part;
				char extban_buf[512];
				char *p;
				
				/* Skip the extban prefix */
				if (strncmp(params, "~automode:", 10) == 0)
					params += 10;
				else if (strncmp(params, "~M:", 3) == 0)
					params += 3;
				
				strlcpy(extban_buf, params, sizeof(extban_buf));
				
				/* Parse modes:matcher */
				mode_part = extban_buf;
				matcher_part = strchr(extban_buf, ':');
				
				if (!matcher_part || !*mode_part)
					continue;
				
				*matcher_part = '\0';
				matcher_part++;
				
				/* Check if the user matches */
				b->banstr = matcher_part;
				if (ban_check_mask(b))
				{
					/* User matches! Add modes to the buffer */
					for (p = mode_part; *p && modes_len < sizeof(modes_buf) - 1; p++)
					{
						/* Avoid duplicates */
						if (!strchr(modes_buf, *p))
						{
							modes_buf[modes_len++] = *p;
							modes_buf[modes_len] = '\0';
						}
					}
				}
			}
		}
	}
	
	safe_free(b);
	
	/* Filter out modes the user already has (e.g. from services or other modules) */
	if (modes_len > 0)
	{
		const char *existing_modes = get_channel_access(client, channel);
		if (existing_modes && *existing_modes)
		{
			char filtered[256];
			int flen = 0;
			int i;
			for (i = 0; i < modes_len; i++)
			{
				if (!strchr(existing_modes, modes_buf[i]))
				{
					filtered[flen++] = modes_buf[i];
				}
			}
			filtered[flen] = '\0';
			strlcpy(modes_buf, filtered, sizeof(modes_buf));
			modes_len = flen;
		}
	}
	
	/* Now set all modes at once if any were collected */
	if (modes_len > 0)
	{
		int i;
		strlcpy(nick_buf, client->name, sizeof(nick_buf));
		params_buf[0] = '\0';
		
		/* Build parameter string: "nick nick nick..." for each mode */
		for (i = 0; i < modes_len; i++)
		{
			if (i > 0)
				strlcat(params_buf, " ", sizeof(params_buf));
			strlcat(params_buf, nick_buf, sizeof(params_buf));
		}
		
		/* Set all modes at once */
		set_channel_mode(channel, NULL, modes_buf, params_buf);
	}
	
	return 0;
}

/* Helper: serialize a role's permissions into a JSON object */
static json_t *permissions_to_json(struct MemberRolePermissions *perms)
{
	json_t *obj = json_object();
	
	json_object_set_new(obj, "can_kick", json_string(perm_to_str(perms->can_kick)));
	json_object_set_new(obj, "can_topic", json_string(perm_to_str(perms->can_topic)));
	json_object_set_new(obj, "can_invite", json_string(perm_to_str(perms->can_invite)));
	json_object_set_new(obj, "can_override_bans", json_string(perm_to_str(perms->can_override_bans)));
	json_object_set_new(obj, "is_voice", json_string(perm_to_str(perms->is_voice)));
	json_object_set_new(obj, "is_unkickable", json_string(perm_to_str(perms->is_unkickable)));
	json_object_set_new(obj, "can_see_bans", json_string(perm_to_str(perms->can_see_bans)));
	json_object_set_new(obj, "can_see_invex", json_string(perm_to_str(perms->can_see_invex)));
	json_object_set_new(obj, "can_see_excepts", json_string(perm_to_str(perms->can_see_excepts)));
	json_object_set_new(obj, "can_set", perms->can_set ? json_string(perms->can_set) : json_null());
	json_object_set_new(obj, "can_unset", perms->can_unset ? json_string(perms->can_unset) : json_null());
	
	return obj;
}

/* Helper: serialize a single role into a JSON object */
static json_t *role_to_json(struct MemberRole *role)
{
	char mode_str[2] = { role->mode, '\0' };
	char prefix_str[2] = { role->prefix, '\0' };
	char sjoin_str[2] = { role->sjoin_prefix, '\0' };
	json_t *obj = json_object();
	
	json_object_set_new(obj, "name", json_string(role->name));
	json_object_set_new(obj, "mode", json_string(mode_str));
	json_object_set_new(obj, "prefix", json_string(prefix_str));
	json_object_set_new(obj, "sjoin_prefix", json_string(sjoin_str));
	json_object_set_new(obj, "rank", json_integer(role->rank));
	json_object_set_new(obj, "is_builtin", json_boolean(role->is_builtin));
	
	if (role->inherit)
		json_object_set_new(obj, "inherits_from", json_string(role->inherit));
	else
		json_object_set_new(obj, "inherits_from", json_null());
	
	json_object_set_new(obj, "permissions", permissions_to_json(&role->permissions));
	
	return obj;
}

/* RPC: member_roles.list - List all configured member roles */
RPC_CALL_FUNC(rpc_member_roles_list)
{
	json_t *result, *list, *item;
	struct MemberRole *role;
	
	result = json_object();
	list = json_array();
	
	/* Include default role if configured */
	if (have_default_permissions)
	{
		item = json_object();
		json_object_set_new(item, "name", json_string("default"));
		json_object_set_new(item, "mode", json_null());
		json_object_set_new(item, "prefix", json_null());
		json_object_set_new(item, "rank", json_null());
		json_object_set_new(item, "inherits_from", json_null());
		json_object_set_new(item, "permissions", permissions_to_json(&default_permissions));
		json_array_append_new(list, item);
	}
	
	for (role = member_roles; role; role = role->next)
	{
		json_array_append_new(list, role_to_json(role));
	}
	
	json_object_set_new(result, "list", list);
	rpc_response(client, request, result);
	json_decref(result);
}

/* RPC: member_roles.get - Get details of a specific member role by name */
RPC_CALL_FUNC(rpc_member_roles_get)
{
	json_t *result;
	const char *role_name;
	struct MemberRole *role;
	
	REQUIRE_PARAM_STRING("name", role_name);
	
	/* Handle "default" specially */
	if (!strcmp(role_name, "default"))
	{
		if (!have_default_permissions)
		{
			rpc_error(client, request, JSON_RPC_ERROR_NOT_FOUND, "No default role configured");
			return;
		}
		result = json_object();
		json_object_set_new(result, "name", json_string("default"));
		json_object_set_new(result, "mode", json_null());
		json_object_set_new(result, "prefix", json_null());
		json_object_set_new(result, "rank", json_null());
		json_object_set_new(result, "inherits_from", json_null());
		json_object_set_new(result, "permissions", permissions_to_json(&default_permissions));
		rpc_response(client, request, result);
		json_decref(result);
		return;
	}
	
	role = find_role_by_name(role_name);
	if (!role)
	{
		rpc_error(client, request, JSON_RPC_ERROR_NOT_FOUND, "Role not found");
		return;
	}
	
	result = role_to_json(role);
	rpc_response(client, request, result);
	json_decref(result);
}

/* Helper: check if a user with only custom member roles can see a given list type.
 * Returns 1 if they can see it, 0 if they cannot.
 * Users with built-in hoaq access always see lists via normal IRCd logic,
 * so this only matters for users who ONLY have custom roles.
 */
static int member_roles_can_see_list(Client *client, Channel *channel, char list_type)
{
	struct MemberRolePermissions *perms;

	perms = get_effective_permissions(client, channel);
	if (!perms)
		return 1; /* No permissions defined — don't interfere, let normal IRCd logic apply */

	switch (list_type)
	{
		case 'b': return (perms->can_see_bans == MRPERM_YES);
		case 'e': return (perms->can_see_excepts == MRPERM_YES);
		case 'I': return (perms->can_see_invex == MRPERM_YES);
		default:  return 0;
	}
}

/* HOOKTYPE_PACKET handler: intercept outgoing +b/+e/+I list numerics and MODE lines
 * to hide them from users whose custom role lacks the relevant can_see_* permission.
 * Modeled after third/hidebans by Syzop.
 */
int member_roles_packet(Client *from, Client *to, Client *intended_to, char **msg, int *length)
{
	char *p, *buf = *msg;
	char *orig_p;

	/* Only interested in outgoing data to local non-oper users */
	if (IsMe(to) || !MyUser(to) || IsOper(to) || !buf || !length || !*length)
		return 0;

	buf[*length] = '\0'; /* safety */

	/* Skip message tags if present */
	if (*buf == '@')
	{
		p = strchr(buf, ' ');
		if (!p)
			return 0;
		p = strchr(p + 1, ' ');
	} else {
		p = strchr(buf, ' ');
	}
	if (!p)
		return 0; /* too short */
	p++;

	orig_p = p;

	/* Numerics 367 (+b list), 348 (+e list), 346 (+I list) */
	if (!strncmp(p, "367 ", 4) || !strncmp(p, "348 ", 4) || !strncmp(p, "346 ", 4))
	{
		char channelname[CHANNELLEN+1];
		char *chan_start;
		Channel *channel;
		char list_type;

		/* Determine which list type */
		if (!strncmp(orig_p, "367 ", 4))
			list_type = 'b';
		else if (!strncmp(orig_p, "348 ", 4))
			list_type = 'e';
		else
			list_type = 'I';

		/* Numeric format: "367 nick #channel banmask ..."
		 * Skip past "367 " then past the nick to find the channel */
		chan_start = strchr(p + 4, ' ');
		if (!chan_start)
			return 0;
		chan_start++; /* skip the space */

		strlcpy(channelname, chan_start, sizeof(channelname));
		{
			char *sp = strchr(channelname, ' ');
			if (sp)
				*sp = '\0';
		}

		channel = find_channel(channelname);
		if (!channel)
			return 0;

		if (member_roles_can_see_list(to, channel, list_type))
			return 0; /* show as-is */

		/* Drop the line */
		*msg = NULL;
		*length = 0;
		return 0;
	}
	else if (!strncmp(p, "MODE ", 5))
	{
		/* MODE line */
		if (p[5] != '#')
			return 0; /* user mode change, not channel mode */

		/* If user has no effective permissions, don't interfere with MODE display */
		{
			char channelname[CHANNELLEN+1];
			char *sp;
			Channel *channel;

			strlcpy(channelname, p + 5, sizeof(channelname));
			sp = strchr(channelname, ' ');
			if (sp)
				*sp = '\0';

			channel = find_channel(channelname);
			if (!channel)
				return 0;

			if (!get_effective_permissions(to, channel))
				return 0; /* no permissions defined — let normal IRCd logic apply */
		}

		/* Re-parse and filter out hidden +b/+e/+I from the MODE line */
		return member_roles_reparsemode(to, msg, length);
	}

	return 0;
}

/* Re-parse a MODE line and strip out +b/+e/+I changes the user cannot see.
 * Closely follows the approach from third/hidebans.
 */
int member_roles_reparsemode(Client *client, char **msg, int *length)
{
	char modebuf[1024], parabuf[1024];
	char omodebuf[1024], oparabuf[1024];
	static char obuf[1024];
	char *p, *o, *header_end = NULL;
	char channelname[CHANNELLEN+1];
	Channel *channel;
	ParseMode pm;
	int n;
	int add = -1;
	int modes_processed = 0;
	struct MemberRolePermissions *perms;

	*modebuf = *parabuf = *obuf = *omodebuf = *oparabuf = '\0';

	/* Parse: ":source MODE #channel +modes params\r\n"
	 * Possibly with message tags: "@tags :source MODE #channel ..."
	 *                                     ^1     ^2   ^3
	 */
	if (**msg == '@')
	{
		p = strchr(*msg, ' ');
		if (!p)
			return 0;
		p = strchr(p + 1, ' ');
	} else {
		p = strchr(*msg, ' ');
	}
	if (!p)
		return 0; /* parse error 1 */

	/* p is at " MODE" — skip to after MODE */
	p = strchr(p + 1, ' ');
	if (!p)
		return 0; /* parse error 2 */

	/* p is at " #channel" — extract channel name */
	strlcpy(channelname, p + 1, sizeof(channelname));
	o = strchr(channelname, ' ');
	if (o)
		*o = '\0';

	channel = find_channel(channelname);
	if (!channel)
		return 0;

	/* Get the user's effective permissions */
	perms = get_effective_permissions(client, channel);

	/* Skip past channel name to the mode string */
	p = strchr(p + 1, ' ');
	if (!p)
		return 0; /* parse error 3 */
	p++;

	header_end = p;

	/* Extract modebuf */
	for (o = modebuf; (*p && (*p != ' ')); p++)
		*o++ = *p;
	*o = '\0';

	if (!*p)
		return 0; /* paramless mode — always fine, no +b/+e/+I possible */

	strlcpy(parabuf, p, sizeof(parabuf));
	stripcrlf(parabuf);

	/* Re-write the header (everything up to the mode string) */
	if (header_end - *msg > (int)(sizeof(obuf) - 2))
		abort(); /* impossible */
	strlcpy(obuf, *msg, header_end - *msg + 1);

	/* Iterate through each mode change */
	for (n = parse_chanmode(&pm, modebuf, parabuf); n; n = parse_chanmode(&pm, NULL, NULL))
	{
		/* Check if this is a +b/+e/+I that should be hidden */
		if (pm.modechar == 'b' || pm.modechar == 'e' || pm.modechar == 'I')
		{
			int can_see;

			if (pm.modechar == 'b')
				can_see = perms && (perms->can_see_bans == MRPERM_YES);
			else if (pm.modechar == 'e')
				can_see = perms && (perms->can_see_excepts == MRPERM_YES);
			else /* 'I' */
				can_see = perms && (perms->can_see_invex == MRPERM_YES);

			if (!can_see)
				continue; /* hide this mode change */
		}

		/* Add the '+' or '-', IF needed */
		if ((pm.what == MODE_ADD) && (add != 1))
		{
			add = 1;
			strlcat(omodebuf, "+", sizeof(omodebuf));
		}
		else if ((pm.what == MODE_DEL) && (add != 0))
		{
			add = 0;
			strlcat(omodebuf, "-", sizeof(omodebuf));
		}

		/* Add the mode character */
		if (strlen(omodebuf) < sizeof(omodebuf) - 2)
		{
			p = omodebuf + strlen(omodebuf);
			*p++ = pm.modechar;
			*p = '\0';
		}

		if (pm.param)
		{
			strlcat(oparabuf, " ", sizeof(oparabuf));
			strlcat(oparabuf, pm.param, sizeof(oparabuf));
		}

		modes_processed++;
	}

	if (modes_processed == 0)
	{
		/* All modes were hidden — don't send the MODE line at all */
		*msg = NULL;
		*length = 0;
		return 0;
	}

	/* Send the (potentially) modified line */
	strlcat(obuf, omodebuf, sizeof(obuf));
	strlcat(obuf, oparabuf, sizeof(obuf));
	strlcat(obuf, "\r\n", sizeof(obuf));
	*msg = obuf;
	*length = strlen(obuf);
	return 0;
}
