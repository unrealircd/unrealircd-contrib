/*
 * third/hidebans - Hide channel bans from non-ops
 * (C) Copyright 2012-2021 Bram Matthys (Syzop)
 * License: GPLv2
 */
   
#include "unrealircd.h"

ModuleHeader MOD_HEADER
  = {
	"third/hidebans",
	"1.1",
	"Hide channel bans from non-ops",
	"Bram Matthys (Syzop)",
	"unrealircd-6"
    };

/* Forward declarations */
int hidebans_packet(Client *from, Client *to, Client *intended_to, char **msg, int *length);
int hidebans_reparsemode(char **msg, int *length);

MOD_INIT()
{
	HookAdd(modinfo->handle, HOOKTYPE_PACKET, 0, hidebans_packet);
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

int hidebans_isskochanop(char *channel_in, Client *client)
{
	char channelname[CHANNELLEN+1];
	char *p;
	Channel *channel;
	
	strlcpy(channelname, channel_in, sizeof(channelname));
	p = strchr(channelname, ' ');
	if (p)
		*p = '\0';
	
	channel = find_channel(channelname);
	if (!channel)
		return 0; /* channel does not exist. err on the safe side.. */
	
	if (check_channel_access(client, channel, "hoaq"))
		return 1;
	
	return 0; /* not in the channel or not a chanop */
}

int hidebans_packet(Client *from, Client *to, Client *intended_to, char **msg, int *length)
{
	char *p, *buf = *msg;
	
	/* We are only interested in outgoing data. Also ircops get to see everything as-is */
	if (IsMe(to) || !MyUser(to) || IsOper(to) || !buf || !length || !*length)
		return 0;
	
	buf[*length] = '\0'; /* safety */

	if (*buf == '@')
	{
		/* Skip over message tags */
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
	
	if (!strncmp(p, "367 ", 4) || !strncmp(p, "348 ", 4) || !strncmp(p, "346 ", 4)) /* +b, +e, +I */
	{
		/* +beI list results */
		if (hidebans_isskochanop(p+4, to))
			return 0; /* show as-is if chanop */
		
		/* otherwise, drop it.. */
		*msg = NULL;
		*length = 0;
		return 0;
	} else
	if (!strncmp(p, "MODE ", 5))
	{
		/* MODE */
		if (p[5] != '#')
			return 0; /* user mode change, not channel mode */
		
		if (hidebans_isskochanop(p+5, to))
			return 0; /* show as-is if chanop */
		
		/* Now it gets interesting... we have to re-parse and re-write the entire MODE line. */
		return hidebans_reparsemode(msg, length);
	}
	
	return 0;
}

int hidebans_reparsemode(char **msg, int *length)
{
	char modebuf[1024], parabuf[1024]; /* (warning: shadow) */
	char omodebuf[1024], oparabuf[1024];
	static char obuf[1024];
	char *p, *o, *header_end = NULL;
	ParseMode pm;
	int n;
	int add = -1;
	int modes_processed = 0;
	
	*modebuf = *parabuf = *obuf = *omodebuf = *oparabuf = '\0';

	/* :main MODE #test .... .....
	 *      ^    ^     ^
	 *      1    2     3
	 */
	if (**msg == '@')
	{
		p = strchr(*msg, ' ');
		if (!p)
			return 0;
		p = strchr(p+1, ' ');
	} else {
		p = strchr(*msg, ' ');
	}
	if (!p)
		return 0; /* parse error 1 (impossible) */
	
	p = strchr(p + 1, ' ');
	if (!p)
		return 0; /* parse error 2 (impossible too) */
	
	p = strchr(p + 1, ' ');
	if (!p)
		return 0; /* parse error 3 (shouldn't happen) */
	p++;
	
	header_end = p;
	
	/* p now points to modebuf */
	for (o = modebuf; (*p && (*p != ' ')); p++)
		*o++ = *p;
	*o = '\0';
	
	if (!*p)
		return 0; /* paramless mode. always fine. no further processing required. */
	
	strlcpy(parabuf, p, sizeof(parabuf));
	stripcrlf(parabuf);
	
	/* Re-write the header */
	if (header_end - *msg > sizeof(obuf)-2)
		abort(); /* impossible */
	strlcpy(obuf, *msg, header_end - *msg + 1);

	/* Yay. Can use my new parse_chanmode() function.. */
	for (n = parse_chanmode(&pm, modebuf, parabuf); n; n = parse_chanmode(&pm, NULL, NULL))
	{
		if ((pm.modechar == 'b') || (pm.modechar == 'e') || (pm.modechar == 'I'))
			continue; /* skip +beI */

		/* Add the '+' or '-', IF needed */
		if ((pm.what == MODE_ADD) && (add != 1))
		{
			add = 1;
			strlcat(omodebuf, "+", sizeof(omodebuf));
		} else
		if ((pm.what == MODE_DEL) && (add != 0))
		{
			add = 0;
			strlcat(omodebuf, "-", sizeof(omodebuf));
		}
		
		/* Add the mode character */
		if (strlen(omodebuf) < sizeof(omodebuf)-2)
		{
			p = omodebuf + strlen(omodebuf);
			*p++ = pm.modechar;
			*p = '\0';
		}
		
		if (pm.param)
		{
			/* Parameter mode */
			strlcat(oparabuf, " ", sizeof(oparabuf));
			strlcat(oparabuf, pm.param, sizeof(oparabuf));
		}
		
		modes_processed++;
	}
	
	if (modes_processed == 0)
	{
		/* All modes were hidden. Don't send the MODE line at all. */
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

/*** <<<MODULE MANAGER START>>>
module
{
	documentation "https://www.unrealircd.org/docs/";

	// This is displayed in './unrealircd module info ..' and also if compilation of the module fails:
	troubleshooting "Contact syzop@unrealircd.org if this module fails to compile";

	// Minimum version necessary for this module to work:
	min-unrealircd-version "6.*";

	post-install-text {
		"The module is installed. Now all you need to do is add a loadmodule line:";
		"loadmodule \"third/hidebans\";";
		"And /REHASH the IRCd.";
		"The module does not need any other configuration.";
	}
}
*** <<<MODULE MANAGER END>>>
*/
