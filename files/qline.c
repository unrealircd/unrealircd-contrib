/*
* Q-LINE MODULE: Provides the /QLINE and /UNQLINE commands, allowing O-lined users with the server-ban:gline privs to manually add Q-lines (global nick bans)
* at the server level, rather than relying on Services to do so via the /(UN)SQLINE server-only command or config file access.
*
* USAGE:
*
* Add a new Q-line entry: /QLINE <nickmask> :<Reason>
* Delete an active Q-line entry: /UNQLINE <nickmask>
* -----------------------------------------------------------------------------------------------------------------------------------------------
* MIT License
*
* Copyright (c) 2022 Avery 'Hexick' Q. [pseudonym]
*
* Permission is hereby granted, free of charge, to any person obtaining a copy
* of this software and associated documentation files (the "Software"), to deal
* in the Software without restriction, including without limitation the rights
* to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
* copies of the Software, and to permit persons to whom the Software is
* furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in all
* copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
* AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
* OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
* SOFTWARE.
*/

/*** <<<MODULE MANAGER START>>>
module
{
		documentation "https://github.com/Hexsl/hex-unrealircd-modules/blob/main/modules/qline/README.md";
		troubleshooting "I go by Hex on the UnrealIRCd network, and can also be emailed at me@hexick.com";
		min-unrealircd-version "6.*";
		max-unrealircd-version "6.*";
		post-install-text {
				"The module is installed. Now all you need to do is add a loadmodule line:";
				"loadmodule \"third/qline\";";
				"And then /rehash";
		}
}
*** <<<MODULE MANAGER END>>>
*/

#include "unrealircd.h"

CMD_FUNC(cmd_qline);
CMD_FUNC(cmd_unqline);

#define MSG_QLINE      "QLINE"        /* QLINE */
#define MSG_UNQLINE    "UNQLINE"      /* UNQLINE */

/* Module header */
ModuleHeader MOD_HEADER = {
	"third/qline",
	"1.0.0",
	"/QLINE and /UNQLINE commands to allow opers to manually add Q-lines (global nick bans).",
	"Hexick",
	"unrealircd-6",
};

/* Initialiation of the command module */
MOD_INIT() {
	CommandAdd(modinfo->handle, MSG_QLINE, cmd_qline, MAXPARA, CMD_USER);
	CommandAdd(modinfo->handle, MSG_UNQLINE, cmd_unqline, MAXPARA, CMD_USER);
	MARK_AS_GLOBAL_MODULE(modinfo);
	return MOD_SUCCESS;
}

/* This function is called when the module is loaded */
MOD_LOAD() {
	return MOD_SUCCESS;
}

/* Same as the above, except it's for when the module is unloaded */
MOD_UNLOAD() {
	return MOD_SUCCESS;
}

/* The actual structure of the QLINE command to be performed */
CMD_FUNC(cmd_qline) {
	char mo[32];
	const char* comment = (parc == 3) ? parv[2] : NULL;
	const char* tkllayer[10] = {
		me.name,        /*0  server.name */
		"+",            /*1  + = X-line add */
		"Q",            /*2  X-line type  */
		"*" ,           /*3  user */
		parv[1],        /*4  host */
		client->name,   /*5  Who set the ban */
		"0",            /*6  expire_at; never expire */
		NULL,           /*7  set_at */
		"no reason",    /*8  default reason */
		NULL			/*9 Extra NULL element to prevent OOB */
	};

	/* Verify privs */
	if (!ValidatePermissionsForPath("server-ban:gline", client, NULL, NULL, NULL)) {
		sendnumeric(client, ERR_NOPRIVILEGES);
		return;
	}

	/* Ensure the proper number of parameters */
	if (parc < 2)
		return;

	/* Do the thang */
	ircsnprintf(mo, sizeof(mo), "%lld", (long long)TStime());
	tkllayer[7] = mo;
	tkllayer[8] = comment ? comment : "no reason";
	cmd_tkl(&me, NULL, 10, tkllayer);
}

/* The actual structure of the UNQLINE command to be performed */
CMD_FUNC(cmd_unqline) {
	const char* tkllayer[7] = {
		me.name,           /*0  server.name */
		"-",               /*1  - = X-line removed */
		"Q",               /*2  X-line type */
		"*",               /*3  unused */
		parv[1],           /*4  host */
		client->name,      /*5  who removed the line */
		NULL			   /*6 Extra NULL element to prevent OOB */
	};

	/* Verify privs */
	if (!ValidatePermissionsForPath("server-ban:gline", client, NULL, NULL, NULL)) {
		sendnumeric(client, ERR_NOPRIVILEGES);
		return;
	}

	/* Ensure the proper number of parameters */
	if (parc < 2)
		return;

	/* Do the thang */
	cmd_tkl(&me, NULL, 7, tkllayer);
}
