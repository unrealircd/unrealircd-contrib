/*
 * third/dumpcmds - Dump IRC commands to a file (data/cmds.txt)
 * (C) Copyright 2010-2019 Bram Matthys (Syzop)
 * License: GPLv2
 */
   
#include "unrealircd.h"

ModuleHeader MOD_HEADER
  = {
	"third/dumpcmds",
	"1.0",
	"Dump IRC commands to a file",
	"Syzop",
	"unrealircd-5",
    };

EVENT(do_dumpcmds);

MOD_INIT()
{
	EventAdd(modinfo->handle, "do_dumpcmds", do_dumpcmds, NULL, 2000, 1);
	
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

extern MODVAR RealCommand *CommandHash[256];

static char *command_flags(RealCommand *c)
{
	static char buf[512];

	*buf = '\0';
	if (c->flags & CMD_UNREGISTERED)
		strlcat(buf, "|UNREGISTERED", sizeof(buf));
	if (c->flags & CMD_USER)
		strlcat(buf, "|USER", sizeof(buf));
	if (c->flags & CMD_SERVER)
		strlcat(buf, "|SERVER", sizeof(buf));
	if (c->flags & CMD_OPER)
		strlcat(buf, "|OPER", sizeof(buf));

	return *buf ? buf + 1 : buf;
}

EVENT(do_dumpcmds)
{
	FILE *fd;
	int i;
	RealCommand *c;
	char fname[512];

	snprintf(fname, sizeof(fname), "%s/cmds.txt", PERMDATADIR);

	ircd_log(LOG_ERROR, "[dumpcmds] Dumping commands to %s...", fname);
	fd = fopen(fname, "w");
	
	if (!fd)
		return;
	
	for (i=0; i < 256; i++)
	{
		for (c = CommandHash[i]; c; c = c->next)
			fprintf(fd, "%s %s\n", c->cmd, command_flags(c));
	}
	fclose(fd);
}

/*** <<<MODULE MANAGER START>>>
module
{
	documentation "https://www.unrealircd.org/docs/dumpcmds%20module";

	// This is displayed in './unrealircd module info ..' and also if compilation of the module fails:
	troubleshooting "Contact syzop@unrealircd.org if this module fails to compile";

	// Minimum version necessary for this module to work:
	min-unrealircd-version "5.*";

	post-install-text {
		"The module is installed. Now all you need to do is add a loadmodule line:";
		"loadmodule \"third/dumpcmds\";";
		"And /REHASH the IRCd.";
		"The module does not need any other configuration.";
		"The list of available IRC commands will be dumped to data/dumpcmds.txt";
	}
}
*** <<<MODULE MANAGER END>>>
*/
