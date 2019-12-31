
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
        min-unrealircd-version "5.*";
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

/* Maximum time (in minutes) for a ban */
#define TIMEDBAN_MAX_TIME	9999

/* Maximum length of a ban */
#define MAX_LENGTH 128

/* Split timeout event in <this> amount of iterations */
#define TIMEDBAN_TIMER_ITERATION_SPLIT 4

/* Call timeout event every <this> seconds.
 * NOTE: until all channels are processed it takes
 *       TIMEDBAN_TIMER_ITERATION_SPLIT * TIMEDBAN_TIMER.
 */
#define TIMEDBAN_TIMER	2

/* We allow a ban to (potentially) expire slightly before the deadline.
 * For example with TIMEDBAN_TIMER_ITERATION_SPLIT=4 and TIMEDBAN_TIMER=2
 * a 1 minute ban would expire at 56-63 seconds, rather than 60-67 seconds.
 * This is usually preferred.
 */
#define TIMEDBAN_TIMER_DELTA ((TIMEDBAN_TIMER_ITERATION_SPLIT*TIMEDBAN_TIMER)/2)

ModuleHeader MOD_HEADER
  = {
	"third/unauthban",
	"5.0",
	"ExtBan ~I: bans that match only users that are not logged in",
	"k4be@PIRC",
	"unrealircd-5",
    };

/* Forward declarations */
char *unauthban_extban_conv_param(char *para_in);
int unauthban_extban_is_ok(Client *client, Channel* channel, char *para_in, int checkt, int what, int what2);
int unauthban_is_banned(Client *client, Channel *channel, char *ban, int chktype, char **msg, char **errmsg);
char *unauthban_chanmsg(Client *, Client *, Channel *, char *, int);

MOD_TEST()
{
	return MOD_SUCCESS;
}

MOD_INIT()
{
ExtbanInfo extban;
	memset(&extban, 0, sizeof(ExtbanInfo));
	extban.flag = 'I';
	extban.options |= EXTBOPT_ACTMODIFIER; /* not really, but ours shouldn't be stacked from group 1 */
	extban.options |= EXTBOPT_CHSVSMODE; /* so "SVSMODE -nick" will unset affected ~t extbans */
	extban.options |= EXTBOPT_INVEX; /* also permit timed invite-only exceptions (+I) */
	extban.conv_param = unauthban_extban_conv_param;
	extban.is_ok = unauthban_extban_is_ok;
	extban.is_banned = unauthban_is_banned;

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
 */
char *generic_clean_ban_mask(char *mask)
{
	char *cp, *x;
	char *user;
	char *host;
	Extban *p;
	static char maskbuf[512];

	/* Work on a copy */
	strlcpy(maskbuf, mask, sizeof(maskbuf));
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
		p = findmod_by_bantype(mask[1]);
		if (!p)
			return NULL; /* reject unknown extban */
		if (p->conv_param)
			return p->conv_param(mask);
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
char *unauthban_extban_conv_param(char *para_in)
{
	static char retbuf[MAX_LENGTH+1];
	char para[MAX_LENGTH+1];
	char tmpmask[MAX_LENGTH+1];
	char *newmask; /**< Cleaned matching method, such as 'n!u@h' */
	static int unauthban_extban_conv_param_recursion = 0;
	
	if (unauthban_extban_conv_param_recursion)
		return NULL; /* reject: recursion detected! */

	strlcpy(para, para_in+3, sizeof(para)); /* work on a copy (and truncate it) */
	
	/* ~I:n!u@h   for direct matching
	 * ~I:~x:.... when calling another bantype
	 */

	strlcpy(tmpmask, para, sizeof(tmpmask));
	unauthban_extban_conv_param_recursion++;
	//newmask = extban_conv_param_nuh_or_extban(tmpmask);
	newmask = generic_clean_ban_mask(tmpmask);
	unauthban_extban_conv_param_recursion--;
	if (!newmask || (strlen(newmask) <= 1))
		return NULL;

	snprintf(retbuf, sizeof(retbuf), "~I:%s", newmask);
	return retbuf;
}

int unauthban_extban_syntax(Client *client, int checkt, char *reason)
{
	if (MyUser(client) && (checkt == EXBCHK_PARAM))
	{
		sendnotice(client, "Error when setting unauth ban: %s", reason);
		sendnotice(client, " Syntax: +b ~I:mask");
		sendnotice(client, "Example: +b ~I:nick!user@host");
		sendnotice(client, "Valid masks are: nick!user@host or another extban type such as ~c, ~S, .. but not ~a");
	}
	return 0; /* FAIL: ban rejected */
}

/** Generic helper for sub-bans, used by our "is this ban ok?" function */
int generic_ban_is_ok(Client *client, Channel *channel, char *mask, int checkt, int what, int what2)
{
	if ((mask[0] == '~') && MyUser(client))
	{
		Extban *p;

		/* This portion is copied from clean_ban_mask() */
		if (is_extended_ban(mask) && MyUser(client))
		{
			if (RESTRICT_EXTENDEDBANS && !ValidatePermissionsForPath("immune:restrict-extendedbans",client,NULL,NULL,NULL))
			{
				if (!strcmp(RESTRICT_EXTENDEDBANS, "*"))
				{
					if (checkt == EXBCHK_ACCESS_ERR)
						sendnotice(client, "Setting/removing of extended bans has been disabled");
					return 0; /* REJECT */
				}
				if (strchr(RESTRICT_EXTENDEDBANS, mask[1]))
				{
					if (checkt == EXBCHK_ACCESS_ERR)
						sendnotice(client, "Setting/removing of extended bantypes '%s' has been disabled", RESTRICT_EXTENDEDBANS);
					return 0; /* REJECT */
				}
			}
			/* And next is inspired by cmd_mode */
			p = findmod_by_bantype(mask[1]);
			if (checkt == EXBCHK_ACCESS)
			{
				if (p && p->is_ok && !p->is_ok(client, channel, mask, EXBCHK_ACCESS, what, what2) &&
				    !ValidatePermissionsForPath("channel:override:mode:extban",client,NULL,channel,NULL))
				{
					return 0; /* REJECT */
				}
			} else
			if (checkt == EXBCHK_ACCESS_ERR)
			{
				if (p && p->is_ok && !p->is_ok(client, channel, mask, EXBCHK_ACCESS, what, what2) &&
				    !ValidatePermissionsForPath("channel:override:mode:extban",client,NULL,channel,NULL))
				{
					p->is_ok(client, channel, mask, EXBCHK_ACCESS_ERR, what, what2);
					return 0; /* REJECT */
				}
			} else
			if (checkt == EXBCHK_PARAM)
			{
				if (p && p->is_ok && !p->is_ok(client, channel, mask, EXBCHK_PARAM, what, what2))
				{
					return 0; /* REJECT */
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
int unauthban_extban_is_ok(Client *client, Channel* channel, char *para_in, int checkt, int what, int what2)
{
	char para[MAX_LENGTH+1];
	char tmpmask[MAX_LENGTH+1];
	char *newmask; /**< Cleaned matching method, such as 'n!u@h' */
	static int unauthban_extban_is_ok_recursion = 0;
	int res;

	/* Always permit deletion */
	if (what == MODE_DEL)
		return 1;

	if (unauthban_extban_is_ok_recursion)
		return 0; /* Recursion detected (~t:1:~t:....) */

	strlcpy(para, para_in+3, sizeof(para)); /* work on a copy (and truncate it) */
	
	/* ~I:n!u@h   for direct matching
	 * ~I:~x:.... when calling another bantype
	 */

	if(strlen(para) >= 2 && para[0] == '~'){
		//check for other bantypes that won't be compatible
		switch(para[1]){
			case 't': case 'a': return unauthban_extban_syntax(client, checkt, "Invalid nested ban type");; // ~t will work with us, but only in front
			default: break;
		}
	}

	strlcpy(tmpmask, para, sizeof(tmpmask));
	unauthban_extban_is_ok_recursion++;
	//res = extban_is_ok_nuh_extban(client, channel, tmpmask, checkt, what, what2);
	res = generic_ban_is_ok(client, channel, tmpmask, checkt, what, what2);
	unauthban_extban_is_ok_recursion--;
	if (res == 0)
	{
		/* This could be anything ranging from:
		 * invalid n!u@h syntax, unknown (sub)extbantype,
		 * disabled extban type in conf, too much recursion, etc.
		 */
		return unauthban_extban_syntax(client, checkt, "Invalid matcher");
	}

	return 1; /* OK */
}

/** Check if the user is currently banned */
int unauthban_is_banned(Client *client, Channel *channel, char *ban, int chktype, char **msg, char **errmsg)
{
	if (strncmp(ban, "~I:", 3))
		return 0; /* not for us */
	if(IsLoggedIn(client)) return 0; // this is the magic
	ban += 3; // skip extban prefix
	return ban_check_mask(client, channel, ban, chktype, msg, errmsg, 0);
}

