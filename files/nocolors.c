/*
  Licence: GPLv3 or later
  Copyright Ⓒ 2026 Valware
*/
/*** <<<MODULE MANAGER START>>>
module
{
		documentation "https://github.com/ValwareIRC/valware-unrealircd-mods/blob/main/nocolors/README.md";
		troubleshooting "In case of problems, documentation or e-mail me at v.a.pond@outlook.com";
		min-unrealircd-version "6.*";
		max-unrealircd-version "6.*";
		post-install-text {
				"The module is installed. Now all you need to do is add a loadmodule line:";
				"loadmodule \"third/nocolors\";";
				"And rehash the server. Block colors with usermode +C: /umode2 +C";
		}
}
*** <<<MODULE MANAGER END>>>
*/

#include "unrealircd.h"

#define IsNoColors(cptr)    (cptr->umodes & UMODE_NOCOLORS)
long UMODE_NOCOLORS = 0L;

int striplmao(Client *from, Client *to, Client *intended_to, char **msg, int *length)
{
	char *p;
	static char buf[MAXLINELENGTH];
	
	if ((IsMe(to) || !MyUser(to) || !buf || !length || !*length)
			&& !IsNoColors(to))
		return 0;

	strcpy(buf, StripControlCodes(*msg));
	*length = strlen(buf);
	*msg = buf;

	return 0;
}

/* Module header */
ModuleHeader MOD_HEADER = {
	"third/nocolors",
	"1.0",
	"Usermode +C - Strip colors and formatting from incoming messages",
	"Valware",
	"unrealircd-6",
};

MOD_INIT()
{
	MARK_AS_GLOBAL_MODULE(modinfo);
    HookAdd(modinfo->handle, HOOKTYPE_PACKET, 0, striplmao);
	UmodeAdd(modinfo->handle, 'C', UMODE_GLOBAL, 0, NULL, &UMODE_NOCOLORS);
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
