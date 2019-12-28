/* Copyright (C) All Rights Reserved
** Written by Gottem <support@gottem.nl>
** Website: https://gottem.nl/unreal
** License: https://gottem.nl/unreal/license
*/

/*** <<<MODULE MANAGER START>>>
module {
	documentation "https://gottem.nl/unreal/man/clearlist";
	troubleshooting "In case of problems, check the FAQ at https://gottem.nl/unreal/halp or e-mail me at support@gottem.nl";
	min-unrealircd-version "5.*";
	//max-unrealircd-version "5.*";
	post-install-text {
		"The module is installed, now all you need to do is add a 'loadmodule' line to your config file:";
		"loadmodule \"third/clearlist\";";
		"Then /rehash the IRCd.";
		"For usage information, refer to the module's documentation found at: https://gottem.nl/unreal/man/clearlist";
	}
}
*** <<<MODULE MANAGER END>>>
*/

// One include for all cross-platform compatibility thangs
#include "unrealircd.h"

// Command string
#define MSG_CLEARLIST "CLEARLIST"

// Dem macros yo
#define CheckAPIError(apistr, apiobj) \
	do { \
		if(!(apiobj)) { \
			config_error("A critical error occurred on %s for %s: %s", (apistr), MOD_HEADER.name, ModuleGetErrorStr(modinfo->handle)); \
			return MOD_FAILED; \
		} \
	} while(0)

CMD_FUNC(clearlist); // Register command function

// Quality fowod declarations
static void dumpit(Client *client, char **p);

// Store some info about dem dere list types
typedef struct {
	int type;
	char flag;
} listType;

// Muh array
listType demListTypes[] = {
	{ MODE_BAN, 'b' }, // b& obv
	{ MODE_EXCEPT, 'e' }, // Ban exceptions y0
	{ MODE_INVEX, 'I' }, // Invite exception (invex) m8
	{ 0, 0 }, // Just in case, for unknown m0des
};

// Help string in case someone does just /CLEARLIST
static char *clearListhelp[] = {
	/* Special characters:
	** \002 = bold -- \x02
	** \037 = underlined -- \x1F
	*/
	"*** \002Help on /CLEARLIST\002 ***",
	"Allows you to easily clear out ban and exception lists (+b, +e, +I)",
	"Restricted to chanadmins, owners and IRC ops for obvious raisins",
	"Syntax:",
	"    \002/CLEARLIST\002 \037channel\037 \037flag\037 \037mask\037",
	"        Wildcards are accepted for \037flag\037 and \037mask\037",
	"Examples:",
	"    \002/CLEARLIST #bighek bI *!*@*\002",
	"        Clear all bans and invexes in #bighek",
	"    \002/CLEARLIST #bighek b *!uid*@*\002",
	"        Clear all bans on free IRCCloud accounts",
	"    \002/CLEARLIST #bighek * *\002",
	"        Clear +beI lists entirely",
	NULL
};

// Dat dere module header
ModuleHeader MOD_HEADER = {
	"third/clearlist",
	"2.0",
	"Adds CLEARLIST command to clear out banlists in bulk",
	"Gottem", // Author
	"unrealircd-5", // Modversion
};

// Initialisation routine (register hooks, commands and modes or create structs etc)
MOD_INIT() {
	CheckAPIError("CommandAdd(CLEARLIST)", CommandAdd(modinfo->handle, MSG_CLEARLIST, clearlist, 3, CMD_USER));

	MARK_AS_GLOBAL_MODULE(modinfo);
	return MOD_SUCCESS;
}

// Actually load the module here (also command overrides as they may not exist in MOD_INIT yet)
MOD_LOAD() {
	return MOD_SUCCESS; // We good
}

// Called on unload/rehash obv
MOD_UNLOAD() {
	return MOD_SUCCESS; // We good
}

// Dump a NULL-terminated array of strings to the user (taken from DarkFire IRCd)
static void dumpit(Client *client, char **p) {
	if(IsServer(client)) // Bail out early and silently if it's a server =]
		return;

	// Using sendto_one() instead of sendnumericfmt() because the latter strips indentation and stuff ;]
	for(; *p != NULL; p++)
		sendto_one(client, NULL, ":%s %03d %s :%s", me.name, RPL_TEXT, client->name, *p);

	// Let user take 8 seconds to read it
	client->local->since += 8;
}

CMD_FUNC(clearlist) {
	// Gets args: Client *client, MessageTag *recv_mtags, int parc, char *parv[]
	Ban *muhList, *ban, *bnext; // Pointer to em banlist, also pointers to ban entries
	listType *ltype; // Pointer to our listType struct
	char flag; // Store current flag/type for dat dere iteration fam
	char *types, *rawmask; // Pointers to store arguments
	char remflags[MAXMODEPARAMS + 2], *remstr[MAXMODEPARAMS + 1]; // Some buffers to store flags and masks to remove
	size_t remlen; // Keep track of all masks' lengths =]
	Channel *channel; // Pointer to channel m8
	int strcount, i, j; // Keep track of mask counts to do shit like -bbb, also "boolean" to actually delete the list entry
	int tainted, dountaint; // Keep track of whether the current entry is tainted (like ham@*)

	if(BadPtr(parv[1])) { // If first argument is a bad pointer, don't proceed
		dumpit(client, clearListhelp); // Return help string instead
		return;
	}

	if(BadPtr(parv[2]) || BadPtr(parv[3])) { // We need 3 args fam
		sendnumeric(client, ERR_NEEDMOREPARAMS, "CLEARLIST"); // Show "needs more parameters" error string
		return; // "Critical" error
	}

	if(parv[1][0] != '#') { // If first character is not a #, this is not a channel
		sendnotice(client, "Invalid channel name %s", parv[1]); // So gtfo
		return; // "Critical" error
	}

	types = parv[2]; // Store flags arg
	rawmask = parv[3]; // Store n!i@h mask arg
	if(!(channel = find_channel(parv[1], NULL))) { // Can we even find the channel?
		sendnotice(client, "Channel %s doesn't exist", parv[1]); // Lolnope
		return; // Actual critical error
	}

	// Gotta be IRC oper or have +a or +q to use this shit
	if(!IsOper(client) && !is_chanowner(client, channel) && !is_chanadmin(client, channel)) {
		sendnotice(client, "You're not allowed to do that (need +a, +q or IRC operator status)"); // Ain't gonna happen fam
		return; // Actual critical error
	}

	// If types contains a wildcard char, just iterate over all of the supported flags
	if(strchr(types, '*'))
		types = "beI"; // Which are +b, +e and +I

	// Loop over the global array of supported types
	for(ltype = demListTypes; ltype->type; ltype++) {
		flag = ltype->flag; // Get dat flag
		strcount = 0; // Set count to 0
		dountaint = 0; // Reset untaint flag for every bantype obv fam
		memset(remflags, 0, sizeof(remflags)); // Reset flags to remove entirely
		for(i = 0; i <= MAXMODEPARAMS; i++) remstr[i] = NULL; // Ditto for the masks
		remflags[0] = '-'; // Lil hack =]
		remlen = 0;

		if(!strchr(types, flag)) // Does the given argument match this type's flag?
			continue; // Next flag imo tbh

		// Check what flag this is m9
		switch(flag) {
			case 'b': // Banned lol
				muhList = channel->banlist; // Quality reference
				break;

			case 'e': // Ban exception
				muhList = channel->exlist; // Ayyyy
				break;

			case 'I': // Yay invex
				muhList = channel->invexlist; // Lmao
				break;

			default: // Just a safeguard imo tbh
				continue; // Next type
		}

		// Loop over all entries in the current list
		for(ban = muhList; ban; ban = bnext) {
			tainted = 0; // Not tainted yet
			bnext = ban->next; // Already get the next entry here, since we may delete the current one
			// Does the entry's mask match the given arg? (match_simple() does bighek with wildcards)
			// Also check for tainted entries like topkek@*
			if(match_simple(rawmask, ban->banstr) || (tainted = !match_simple("*!*@*", ban->banstr))) {
				if(tainted)
					dountaint = 1; // Only need one tainted hit cuz it's majikk =]

				remflags[strcount + 1] = flag; // Add this flag to our remflags array
				remstr[strcount] = ban->banstr;
				strcount++; // Increment strcount later cuz muh first char = index 0
				remlen += strlen(ban->banstr); // Increment em lol
			}

			// We'll use at least 5 masks together, until the banstring lengths exceed 200 chars or if we run out of entries
			if((strcount >= 5 && remlen >= 200) || !ban->next || strcount >= MAXMODEPARAMS || (strcount + 2) >= MAXPARA) {
				// Apparently using "* " for mask with active entries results in a weird MODE message
				if(!strchr(remflags, flag)) // So let's skip that shit
					continue;

				char *newparv[MAXPARA + 1]; // Gonna need new parv lol
				newparv[0] = client->name;
				newparv[1] = channel->chname;
				newparv[2] = remflags;
				for(i = 3, j = 0; i < MAXPARA && remstr[j]; i++, j++)
					newparv[i] = remstr[j];
				newparv[i] = NULL; // Some functions may depend on this ;]
				do_cmd(client, NULL, "MODE", i, newparv); // This shit takes care of removing it locally as well as br0adcasting it ;]

				// Reset that shit lol
				strcount = 0;
				memset(remflags, 0, sizeof(remflags));
				for(i = 0; i <= MAXMODEPARAMS; i++) remstr[i] = NULL;
				remflags[0] = '-'; // Lil hack =]
				remlen = 0;
			}
		}

		// In case of tainted masks, send a CLEARLIST command for it to all other local servers ;]
		// We can broadcast any mask for it as long as it doesn't match *!*@* really
		if(dountaint)
			sendto_server(client, 0, 0, NULL, ":%s CLEARLIST %s %c qwertyuiopasdfghjklzxcvbnm", client->name, channel->chname, flag);
	}
}
