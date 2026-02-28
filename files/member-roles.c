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

/* Permissions structure */
struct MemberRolePermissions
{
	unsigned can_kick:1;
	unsigned can_topic:1;
	unsigned can_invite:1;
	unsigned can_override_bans:1;  /* can talk even if banned */
	unsigned is_voice:1;           /* bypass message restrictions (+m/+n/+c/+S/+N/+C/+T/+R/+M) */
	unsigned is_unkickable:1;      /* cannot be kicked */
	unsigned can_see_bans:1;       /* can view +b ban list */
	unsigned can_see_invex:1;      /* can view +I invex list */
	unsigned can_see_excepts:1;    /* can view +e except list */
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
};

static struct MemberRole *member_roles = NULL;
static struct MemberRolePermissions default_permissions;
static int have_default_permissions = 0;

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
int member_role_check_mode_access(Client *client, Channel *channel, MessageTag *mtags, const char *modebuf, const char *parabuf, time_t sendts, int samode);
int member_role_list_mode_request(Client *client, Channel *channel, const char *req);
const char *extban_automode_conv_param(BanContext *b, Extban *extban);
int extban_automode_is_ok(BanContext *b);
int automode_join(Client *client, Channel *channel, MessageTag *mtags);
CMD_FUNC(cmd_memberroles);
int member_roles_packet(Client *from, Client *to, Client *intended_to, char **msg, int *length);
int member_roles_reparsemode(Client *client, char **msg, int *length);
static struct MemberRolePermissions *get_effective_permissions(Client *client, Channel *channel);

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
	CmodeInfo creq;
	ExtbanInfo extban_req;
	
	MARK_AS_GLOBAL_MODULE(modinfo);
	
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
	
	/* Add command for listing roles */
	CommandAdd(modinfo->handle, "MEMBERROLES", cmd_memberroles, 0, CMD_USER);
	
	return MOD_SUCCESS;
}

MOD_LOAD()
{
	CmodeInfo creq;
	struct MemberRole *role;
    /* Register all configured member roles as prefix modes */
	for (role = member_roles; role; role = role->next)
	{
		memset(&creq, 0, sizeof(creq));
		creq.paracount = 1;
		creq.is_ok = member_role_is_ok;
		creq.letter = role->mode;
		creq.prefix = role->prefix;
		creq.sjoin_prefix = role->sjoin_prefix;
		creq.rank = role->rank;
		creq.unset_with_param = 1;
		creq.type = CMODE_MEMBER;
		
		role->cmode = CmodeAdd(modinfo->handle, creq, NULL);
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
	
	/* Merge boolean permissions (OR logic) */
	role->permissions.can_kick |= parent->permissions.can_kick;
	role->permissions.can_topic |= parent->permissions.can_topic;
	role->permissions.can_invite |= parent->permissions.can_invite;
	role->permissions.can_override_bans |= parent->permissions.can_override_bans;
	role->permissions.is_voice |= parent->permissions.is_voice;
	role->permissions.is_unkickable |= parent->permissions.is_unkickable;
	role->permissions.can_see_bans |= parent->permissions.can_see_bans;
	role->permissions.can_see_invex |= parent->permissions.can_see_invex;
	role->permissions.can_see_excepts |= parent->permissions.can_see_excepts;
	
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
							if (!ceppp->value || (strcmp(ceppp->value, "yes") && strcmp(ceppp->value, "no")))
							{
								config_error("%s:%i: %s::default::permissions::%s must be 'yes' or 'no'",
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
						if (!ceppp->value || (strcmp(ceppp->value, "yes") && strcmp(ceppp->value, "no")))
						{
							config_error("%s:%i: %s::%s::permissions::%s must be 'yes' or 'no'",
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
							default_permissions.can_kick = !strcmp(ceppp->value, "yes");
						else if (!strcmp(ceppp->name, "can_topic"))
							default_permissions.can_topic = !strcmp(ceppp->value, "yes");
						else if (!strcmp(ceppp->name, "can_invite"))
							default_permissions.can_invite = !strcmp(ceppp->value, "yes");
						else if (!strcmp(ceppp->name, "can_override_bans"))
							default_permissions.can_override_bans = !strcmp(ceppp->value, "yes");
						else if (!strcmp(ceppp->name, "is_voice"))
							default_permissions.is_voice = !strcmp(ceppp->value, "yes");
						else if (!strcmp(ceppp->name, "is_unkickable"))
							default_permissions.is_unkickable = !strcmp(ceppp->value, "yes");
						else if (!strcmp(ceppp->name, "can_see_bans"))
							default_permissions.can_see_bans = !strcmp(ceppp->value, "yes");
						else if (!strcmp(ceppp->name, "can_see_invex"))
							default_permissions.can_see_invex = !strcmp(ceppp->value, "yes");
						else if (!strcmp(ceppp->name, "can_see_excepts"))
							default_permissions.can_see_excepts = !strcmp(ceppp->value, "yes");
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
		role->sjoin_prefix = '!';
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
						role->permissions.can_kick = !strcmp(ceppp->value, "yes");
						role->direct.can_kick = 1;
					}
					else if (!strcmp(ceppp->name, "can_topic"))
					{
						role->permissions.can_topic = !strcmp(ceppp->value, "yes");
						role->direct.can_topic = 1;
					}
					else if (!strcmp(ceppp->name, "can_invite"))
					{
						role->permissions.can_invite = !strcmp(ceppp->value, "yes");
						role->direct.can_invite = 1;
					}
					else if (!strcmp(ceppp->name, "can_override_bans"))
					{
						role->permissions.can_override_bans = !strcmp(ceppp->value, "yes");
						role->direct.can_override_bans = 1;
					}
					else if (!strcmp(ceppp->name, "is_voice"))
					{
						role->permissions.is_voice = !strcmp(ceppp->value, "yes");
						role->direct.is_voice = 1;
					}
					else if (!strcmp(ceppp->name, "is_unkickable"))
					{
						role->permissions.is_unkickable = !strcmp(ceppp->value, "yes");
						role->direct.is_unkickable = 1;
					}
					else if (!strcmp(ceppp->name, "can_see_bans"))
					{
						role->permissions.can_see_bans = !strcmp(ceppp->value, "yes");
						role->direct.can_see_bans = 1;
					}
					else if (!strcmp(ceppp->name, "can_see_invex"))
					{
						role->permissions.can_see_invex = !strcmp(ceppp->value, "yes");
						role->direct.can_see_invex = 1;
					}
					else if (!strcmp(ceppp->name, "can_see_excepts"))
					{
						role->permissions.can_see_excepts = !strcmp(ceppp->value, "yes");
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
	struct MemberRole *role;
	for (role = member_roles; role; role = role->next)
	{
		resolve_role_inheritance(role, 0);
	}

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
	memset(&default_permissions, 0, sizeof(default_permissions));
}

/* Helper: get the effective permissions for a user in a channel.
 * Returns the user's highest custom role's permissions, or the
 * default permissions if configured, or NULL if neither applies.
 */
static struct MemberRolePermissions *get_effective_permissions(Client *client, Channel *channel)
{
	struct MemberRole *role = find_highest_role(client, channel);
	if (role)
		return &role->permissions;
	if (have_default_permissions && IsMember(client, channel))
		return &default_permissions;
	return NULL;
}

int member_role_is_ok(Client *client, Channel *channel, char mode, const char *param, int type, int what)
{
	Client *target;
	struct MemberRole *client_role, *mode_role;
	
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
		
		/* Find client's highest role */
		client_role = find_highest_role(client, channel);
		
		/* Check if client has permission to set/unset this mode */
		if (client_role)
		{
			const char *allowed_modes = (what == MODE_ADD) ? client_role->permissions.can_set : client_role->permissions.can_unset;
			if (allowed_modes && (strchr(allowed_modes, mode) || strchr(allowed_modes, '*')))
			{
				/* Check if they have sufficient rank */
				if (client_role->rank >= mode_role->rank)
					return EX_ALLOW;
			}
		}
		
		/* Custom member roles require explicit permission - no fallback to chanop */
		if (type == EXCHK_ACCESS_ERR)
			sendnumeric(client, ERR_CHANOPRIVSNEEDED, channel->name);
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
	if (victim_perms && victim_perms->is_unkickable && !IsULine(client))
	{
		ircsnprintf(errmsg, sizeof(errmsg), ":%s %d %s %s :%s",
		            me.name, ERR_CANNOTDOCOMMAND, client->name, "KICK",
		            "user has a role that makes them unkickable");
		*reject_reason = errmsg;
		
		sendnotice(victim,
		    "*** %s tried to kick you from channel %s (%s)",
		    client->name, channel->name, comment);
		
		return EX_ALWAYS_DENY;
	}
	
	/* Check if client has can_kick permission */
	client_perms = get_effective_permissions(client, channel);
	if (client_perms && client_perms->can_kick)
		return EX_ALLOW;
	
	return EX_ALLOW; /* Let other modules/default logic handle it */
}

int member_role_can_set_topic(Client *client, Channel *channel, const char *topic, const char **errmsg)
{
	struct MemberRolePermissions *perms;
	
	perms = get_effective_permissions(client, channel);
	if (perms && perms->can_topic)
		return EX_ALLOW;
	
	return EX_ALLOW; /* Let other modules/default logic handle it */
}

int member_role_pre_invite(Client *client, Client *target, Channel *channel, int *override)
{
	struct MemberRolePermissions *perms;
	
	perms = get_effective_permissions(client, channel);
	if (perms && perms->can_invite)
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
	if (perms->can_override_bans)
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
	if (perms->is_voice)
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
			if (cm->type == CMODE_MEMBER)
				sendnumeric(client, ERR_CHANOPRIVSNEEDED, channel->name);
			else
				sendnumeric(client, ERR_CHANOPRIVSNEEDED, channel->name);
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
	
	if (!IsOper(client))
	{
		sendnumeric(client, ERR_NOPRIVILEGES);
		return;
	}
	
	sendnotice(client, "*** Member Roles:");
	
	/* Show default permissions if configured */
	if (have_default_permissions)
	{
		sendnotice(client, "Role: default (users with no channel mode)");
		sendnotice(client, "  Permissions:");
		sendnotice(client, "    can_kick: %s", default_permissions.can_kick ? "yes" : "no");
		sendnotice(client, "    can_topic: %s", default_permissions.can_topic ? "yes" : "no");
		sendnotice(client, "    can_invite: %s", default_permissions.can_invite ? "yes" : "no");
		sendnotice(client, "    can_override_bans: %s", default_permissions.can_override_bans ? "yes" : "no");
		sendnotice(client, "    is_voice: %s", default_permissions.is_voice ? "yes" : "no");
		sendnotice(client, "    is_unkickable: %s", default_permissions.is_unkickable ? "yes" : "no");
		sendnotice(client, "    can_see_bans: %s", default_permissions.can_see_bans ? "yes" : "no");
		sendnotice(client, "    can_see_invex: %s", default_permissions.can_see_invex ? "yes" : "no");
		sendnotice(client, "    can_see_excepts: %s", default_permissions.can_see_excepts ? "yes" : "no");
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
		sendnotice(client, "Role: %s", role->name);
		sendnotice(client, "  Mode: +%c  Prefix: %c  Rank: %d", role->mode, role->prefix, role->rank);
		
		if (role->inherit)
			sendnotice(client, "  Inherits from: %s", role->inherit);
		
		sendnotice(client, "  Permissions:");
		
		/* Helper macro for boolean permissions */
		#define SHOW_BOOL_PERM(perm, name, direct_flag) \
			do { \
				if (role->permissions.perm) { \
					if (role->inherit && !role->direct.direct_flag) { \
						sendnotice(client, "    %s: yes (inherited from %s)", name, role->inherit); \
					} else { \
						sendnotice(client, "    %s: yes", name); \
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
		case 'b': return perms->can_see_bans;
		case 'e': return perms->can_see_excepts;
		case 'I': return perms->can_see_invex;
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
				can_see = perms && perms->can_see_bans;
			else if (pm.modechar == 'e')
				can_see = perms && perms->can_see_excepts;
			else /* 'I' */
				can_see = perms && perms->can_see_invex;

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
