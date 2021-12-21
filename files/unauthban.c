
/*
 * Based on timedban.c. Created by k4be.
 * (C) Copyright 2009-2017 Bram Matthys (Syzop) and the UnrealIRCd team.
 * License: GPLv2
 *
 * This module adds an extended ban ~I:mask
 * Ban prefixed with ~I will match only users that are not identified
 * to services.
 *
 * Note that this extended ban is rather special in the sense that
 * it permits (crazy) triple-extbans to be set, such as:
 * +b ~I:~q:~c:#channel
 * (=mute a user that is also on #channel, unless he's idenitifed to services)
 *
 * The triple-extbans / double-stacking requires special routines that
 * are based on parts of the core and special recursion checks.
 * If you are looking for inspiration of coding your own extended ban
 * then look at another extended ban * module as this module is not a
 * good starting point ;)
   */

/*** <<<MODULE MANAGER START>>>
module
{
        documentation "https://github.com/pirc-pl/unrealircd-modules/blob/master/README.md";
        troubleshooting "In case of problems, contact k4be on irc.pirc.pl.";
        min-unrealircd-version "6.*";
        post-install-text {
                "The module is installed. Now all you need to do is add a loadmodule line:";
                "loadmodule \"third/unauthban\";";
  				"And /REHASH the IRCd.";
				"The module does not need any other configuration.";
				"Detailed documentation is available on https://github.com/pirc-pl/unrealircd-modules/blob/master/README.md";
        }
}
*** <<<MODULE MANAGER END>>>
*/
   
#include "unrealircd.h"

/* Maximum length of a ban */
#define MAX_LENGTH 128

ModuleHeader MOD_HEADER
  = {
	"third/unauthban",
	"6.0",
	"ExtBan ~I or ~unauth: bans that match only users that are not logged in",
	"k4be",
	"unrealircd-6",
    };

/* Forward declarations */
const char *unauthban_extban_conv_param(BanContext *b, Extban *extban);
int unauthban_extban_is_ok(BanContext *b);
int unauthban_is_banned(BanContext *b);

MOD_TEST()
{
	return MOD_SUCCESS;
}

MOD_INIT()
{
	ExtbanInfo extban;
	memset(&extban, 0, sizeof(ExtbanInfo));
	extban.letter = 'I';
	extban.name = "unauth";
	extban.options |= EXTBOPT_ACTMODIFIER; /* not really, but ours shouldn't be stacked from group 1 */
	extban.options |= EXTBOPT_CHSVSMODE; /* so "SVSMODE -nick" will unset affected ~t extbans */
	extban.options |= EXTBOPT_INVEX; /* also permit timed invite-only exceptions (+I) */
	extban.conv_param = unauthban_extban_conv_param;
	extban.is_ok = unauthban_extban_is_ok;
	extban.is_banned = unauthban_is_banned;
	extban.is_banned_events = BANCHK_ALL;

	if (!ExtbanAdd(modinfo->handle, extban))
	{
		config_error("unauthban: unable to register 'I' extban type!!");
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

/** Generic helper for our conv_param extban function.
 * Mostly copied from clean_ban_mask()
 * FIXME: Figure out why we have this one at all and not use conv_param? ;)
 */
const char *generic_clean_ban_mask(BanContext *b, Extban *extban)
{
	char *cp, *x;
	char *user;
	char *host;
	static char maskbuf[512];
	char *mask;

	/* Work on a copy */
	strlcpy(maskbuf, b->banstr, sizeof(maskbuf));
	mask = maskbuf;

	cp = strchr(mask, ' ');
	if (cp)
		*cp = '\0';

	/* Strip any ':' at beginning since that would cause a desync */
	for (; (*mask && (*mask == ':')); mask++);
	if (!*mask)
		return NULL;

	/* Forbid ASCII <= 32 in all bans */
	for (x = mask; *x; x++)
		if (*x <= ' ')
			return NULL;

	/* Extended ban? */
	if (is_extended_ban(mask))
	{
		const char *nextbanstr;
		Extban *extban = findmod_by_bantype(mask, &nextbanstr);
		if (!extban)
			return NULL; /* reject unknown extban */
		if (extban->conv_param)
		{
			const char *ret;
			static char retbuf[512];
			BanContext *b = safe_alloc(sizeof(BanContext));
			b->banstr = nextbanstr;
			ret = extban->conv_param(b, extban);
			ret = prefix_with_extban(ret, b, extban, retbuf, sizeof(retbuf));
			safe_free(b);
			return ret;
		}
		/* else, do some basic sanity checks and cut it off at 80 bytes */
		if ((mask[1] != ':') || (mask[2] == '\0'))
		    return NULL; /* require a ":<char>" after extban type */
		if (strlen(mask) > 80)
			mask[80] = '\0';
		return mask;
	}

	if ((*mask == '~') && !strchr(mask, '@'))
		return NULL; /* not an extended ban and not a ~user@host ban either. */

	if ((user = strchr((cp = mask), '!')))
		*user++ = '\0';
	if ((host = strrchr(user ? user : cp, '@')))
	{
		*host++ = '\0';

		if (!user)
			return make_nick_user_host(NULL, trim_str(cp,USERLEN), 
				trim_str(host,HOSTLEN));
	}
	else if (!user && strchr(cp, '.'))
		return make_nick_user_host(NULL, NULL, trim_str(cp,HOSTLEN));
	return make_nick_user_host(trim_str(cp,NICKLEN), trim_str(user,USERLEN), 
		trim_str(host,HOSTLEN));
}

/** Convert ban to an acceptable format (or return NULL to fully reject it) */
const char *unauthban_extban_conv_param(BanContext *b, Extban *extban)
{
	static char retbuf[MAX_LENGTH+1];
	char para[MAX_LENGTH+1];
	const char *newmask; /**< Cleaned matching method, such as 'n!u@h' */
	static int unauthban_extban_conv_param_recursion = 0;
	
	if (unauthban_extban_conv_param_recursion)
		return NULL; /* reject: recursion detected! */

	strlcpy(para, b->banstr, sizeof(para)); /* work on a copy (and truncate it) */
	
	/* ~I:n!u@h   for direct matching
	 * ~I:~x:.... when calling another bantype
	 */

	unauthban_extban_conv_param_recursion++;
	b->banstr = para;
	newmask = generic_clean_ban_mask(b, extban);
	unauthban_extban_conv_param_recursion--;
	if (!newmask || (strlen(newmask) <= 1))
		return NULL;

	snprintf(retbuf, sizeof(retbuf), "%s", newmask);
	return retbuf;
}

int unauthban_extban_syntax(Client *client, char *reason)
{
	if (MyUser(client))
	{
		sendnotice(client, "Error when setting unauth ban: %s", reason);
		sendnotice(client, " Syntax: +b ~I:mask");
		sendnotice(client, "Example: +b ~I:nick!user@host");
		sendnotice(client, "Valid masks are: nick!user@host or another extban type such as ~c, ~S, .. but not ~a");
	}
	return 0; /* FAIL: ban rejected */
}

/** Generic helper for sub-bans, used by our "is this ban ok?" function */
int generic_ban_is_ok(BanContext *b)
{
	if ((b->banstr[0] == '~') && MyUser(b->client))
	{
		Extban *extban;
		const char *nextbanstr;

		/* This portion is copied from clean_ban_mask() */
		if (is_extended_ban(b->banstr) && MyUser(b->client))
		{
			if (RESTRICT_EXTENDEDBANS && !ValidatePermissionsForPath("immune:restrict-extendedbans",b->client,NULL,NULL,NULL))
			{
				if (!strcmp(RESTRICT_EXTENDEDBANS, "*"))
				{
					if (b->is_ok_check == EXBCHK_ACCESS_ERR)
						sendnotice(b->client, "Setting/removing of extended bans has been disabled");
					return 0; /* REJECT */
				}
				if (strchr(RESTRICT_EXTENDEDBANS, b->banstr[1]))
				{
					if (b->is_ok_check == EXBCHK_ACCESS_ERR)
						sendnotice(b->client, "Setting/removing of extended bantypes '%s' has been disabled", RESTRICT_EXTENDEDBANS);
					return 0; /* REJECT */
				}
			}
			/* And next is inspired by cmd_mode */
			extban = findmod_by_bantype(b->banstr, &nextbanstr);
			if (extban && extban->is_ok)
			{
				b->banstr = nextbanstr;
				if ((b->is_ok_check == EXBCHK_ACCESS) || (b->is_ok_check == EXBCHK_ACCESS_ERR))
				{
					if (!extban->is_ok(b) &&
					    !ValidatePermissionsForPath("channel:override:mode:extban",b->client,NULL,b->channel,NULL))
					{
						return 0; /* REJECT */
					}
				} else
				if (b->is_ok_check == EXBCHK_PARAM)
				{
					if (!extban->is_ok(b))
					{
						return 0; /* REJECT */
					}
				}
			}
		}
	}
	
	/* ACCEPT:
	 * - not an extban; OR
	 * - extban with NULL is_ok; OR
	 * - non-existing extban character (handled by conv_param?)
	 */
	return 1;
}

/** Validate ban ("is this ban ok?") */
int unauthban_extban_is_ok(BanContext *b)
{
	char para[MAX_LENGTH+1];
	char tmpmask[MAX_LENGTH+1];
	char *newmask; /**< Cleaned matching method, such as 'n!u@h' */
	static int unauthban_extban_is_ok_recursion = 0;
	int res;

	/* Always permit deletion */
	if (b->what == MODE_DEL)
		return 1;

	if (unauthban_extban_is_ok_recursion)
		return 0; /* Recursion detected (~I:~I:....) */

	if (b->is_ok_check != EXBCHK_PARAM)
		return 1;

	strlcpy(para, b->banstr, sizeof(para)); /* work on a copy (and truncate it) */
	
	/* ~I:n!u@h   for direct matching
	 * ~I:~x:.... when calling another bantype
	 */

	if(strlen(para) >= 3 && para[0] == '~'){
		//check for other bantypes that won't be compatible
		if(!strncmp(para+1, "a:", 2) || !strncmp(para+1, "t:", 2) || !strncmp(para+1, "account:", 8) || !strncmp(para+1, "time:", 5))
			return unauthban_extban_syntax(b->client, "Invalid nested ban type"); // ~t will work with us, but only in front
	}

	strlcpy(tmpmask, para, sizeof(tmpmask));
	unauthban_extban_is_ok_recursion++;
	//res = extban_is_ok_nuh_extban(client, channel, tmpmask, checkt, what, what2);
	res = generic_ban_is_ok(b);
	unauthban_extban_is_ok_recursion--;
	if (res == 0)
	{
		/* This could be anything ranging from:
		 * invalid n!u@h syntax, unknown (sub)extbantype,
		 * disabled extban type in conf, too much recursion, etc.
		 */
		return unauthban_extban_syntax(b->client, "Invalid matcher");
	}

	return 1; /* OK */
}

/** Check if the user is currently banned */
int unauthban_is_banned(BanContext *b)
{
	if(IsLoggedIn(b->client))
		return 0; // this is the magic
	return ban_check_mask(b);
}

