/* Copyright (C) All Rights Reserved
** Written by Gottem <support@gottem.nl>
** Website: https://gottem.nl/unreal
** License: https://gottem.nl/unreal/license
*/

/*** <<<MODULE MANAGER START>>>
module {
	documentation "https://gottem.nl/unreal/man/anti_amsg";
	troubleshooting "In case of problems, check the FAQ at https://gottem.nl/unreal/halp or e-mail me at support@gottem.nl";
	min-unrealircd-version "6.*";
	//max-unrealircd-version "6.*";
	post-install-text {
		"The module is installed, now all you need to do is add a 'loadmodule' line to your config file:";
		"loadmodule \"third/anti_amsg\";";
		"Then /rehash the IRCd.";
		"For usage information, refer to the module's documentation found at: https://gottem.nl/unreal/man/anti_amsg";
	}
}
*** <<<MODULE MANAGER END>>>
*/

// One include for all cross-platform compatibility thangs
#include "unrealircd.h"

// Command to override
#define OVR_PRIVMSG "PRIVMSG"

// Muh macros
#define CheckAPIError(apistr, apiobj) \
	do { \
		if(!(apiobj)) { \
			config_error("A critical error occurred on %s for %s: %s", (apistr), MOD_HEADER.name, ModuleGetErrorStr(modinfo->handle)); \
			return MOD_FAILED; \
		} \
	} while(0)

// Big hecks go here
typedef struct t_amsgInfo amsgInfo;
struct t_amsgInfo {
	char *target;
	char *body;
	long tiem;
};

// Quality fowod declarations
void anti_amsg_free(ModData *md);
CMD_OVERRIDE_FUNC(anti_amsg_override);

// Muh globals
ModDataInfo *amsgMDI; // To store every user's last message with their client pointer ;3

// Dat dere module header
ModuleHeader MOD_HEADER = {
	"third/anti_amsg", // Module name
	"2.1.0", // Version
	"Drop messages originating from /amsg", // Description
	"Gottem", // Author
	"unrealircd-6", // Modversion
};

// Initialisation routine (register hooks, commands and modes or create structs etc)
MOD_INIT() {
	ModDataInfo mreq; // Request that shit
	memset(&mreq, 0, sizeof(mreq));
	mreq.type = MODDATATYPE_LOCAL_CLIENT;
	mreq.name = "amsg_lastmessage"; // Name it
	mreq.free = anti_amsg_free; // Function to free 'em
	amsgMDI = ModDataAdd(modinfo->handle, mreq);
	CheckAPIError("ModDataAdd(amsg_lastmessage)", amsgMDI);

	MARK_AS_GLOBAL_MODULE(modinfo);

	return MOD_SUCCESS;
}

// Actually load the module here (also command overrides as they may not exist in MOD_INIT yet)
MOD_LOAD() {
	CheckAPIError("CommandOverrideAdd(PRIVMSG)", CommandOverrideAdd(modinfo->handle, OVR_PRIVMSG, 0, anti_amsg_override));
	return MOD_SUCCESS; // We good
}

// Called on unload/rehash obv
MOD_UNLOAD() {
	return MOD_SUCCESS; // We good
}

void anti_amsg_free(ModData *md) {
	if(md->ptr) {
		amsgInfo *amsg = md->ptr;
		safe_free(amsg->target);
		safe_free(amsg->body);
		safe_free(amsg);
		md->ptr = NULL;
	}
}

// Now for the actual override
CMD_OVERRIDE_FUNC(anti_amsg_override) {
	// Gets args: CommandOverride *ovr, Client *client, MessageTag *recv_mtags, int parc, char *parv[]
	char *last, *target, *body; // User's last message, accompanying target and stripped body (like colours and shit)
	long ltime, tstiem;// Timestamps to go with it
	amsgInfo *amsg; // st0re message inf0
	size_t targetlen, bodylen; // Lengths to alloc8 the struct vars with in a bit
	int bail = 0; // In case we need to droppem but we still need to free some shit before returning

	// Inb4duplicate notices (also allow U-Lines obiously) =]
	if(parc < 3 || !client || !MyUser(client) || IsULine(client)) {
		CallCommandOverride(ovr, client, recv_mtags, parc, parv); // Run original function yo
		return;
	}

	// Check for PRIVMSG #chan1,#chan2
	if(!BadPtr(parv[1]) && strchr(parv[1], ',')) {
		sendnotice(client, "*** Multi-target messaging is not allowed");
		return; // Stop processing yo
	}

	// Also duplicate messages at the exact same time
	if(!BadPtr(parv[2])) {
		// Some shitty ass scripts may use different colours/markup across chans, so fuck that
		if(!(body = (char *)StripControlCodes(parv[2]))) {
			CallCommandOverride(ovr, client, recv_mtags, parc, parv);
			return;
		}

		amsg = moddata_local_client(client, amsgMDI).ptr; // Get client data
		targetlen = sizeof(char) * (strlen(parv[1]) + 1);
		bodylen = sizeof(char) * (strlen(body) + 1);
		tstiem = TStime();
		target = last = NULL;

		// If we have client data (i.e. this is not the first message)
		if(amsg) {
			target = amsg->target;
			last = amsg->body;
			ltime = amsg->tiem;

			// If 3 seconds have passed since the last message, allow it anyways (in the event of people manually re-sending that shit afterwards)
			if(tstiem - ltime > 3)
				last = NULL;
		}

		// Only bail if the current target differs from the last one and the message is the same
		if(target && last && ltime && body && strcmp(parv[1], target) && !strcmp(body, last) && ltime <= tstiem) {
			// Not doing sendnotice() here because we send to *client* but the nick in the notice is actually the intended *target*, which means they'll get a notice in the proper window ;];;]];
			sendto_one(client, NULL, ":%s NOTICE %s :Multi-target messaging is not allowed (%s)", me.name, parv[1], parv[1]);
			bail = 1;
		}

		if(amsg) {
			safe_free(amsg->target);
			safe_free(amsg->body);
		}
		else
			amsg = safe_alloc(sizeof(amsgInfo));

		// Alloc8 em
		amsg->target = safe_alloc(targetlen);
		amsg->body = safe_alloc(bodylen);
		amsg->tiem = tstiem;

		// Copy that shit
		strncpy(amsg->target, parv[1], targetlen);
		strncpy(amsg->body, body, bodylen);

		moddata_local_client(client, amsgMDI).ptr = amsg; // Set client data
	}

	if(!bail)
		CallCommandOverride(ovr, client, recv_mtags, parc, parv);
}
