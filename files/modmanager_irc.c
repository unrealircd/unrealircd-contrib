/* Copyright (C) All Rights Reserved
** Written by Gottem <support@gottem.nl>
** Website: https://gottem.nl/unreal
** License: https://gottem.nl/unreal/license
*/

/*** <<<MODULE MANAGER START>>>
module {
	documentation "https://gottem.nl/unreal/man/modmanager_irc";
	troubleshooting "In case of problems, check the FAQ at https://gottem.nl/unreal/halp or e-mail me at support@gottem.nl";
	min-unrealircd-version "5.*";
	//max-unrealircd-version "5.*";
	post-install-text {
		"The module is installed, now all you need to do is add a 'loadmodule' line to your config file:";
		"loadmodule \"third/modmanager_irc\";";
		"Then /rehash the IRCd.";
		"For usage information, refer to the module's documentation found at: https://gottem.nl/unreal/man/modmanager_irc";
	}
}
*** <<<MODULE MANAGER END>>>
*/

// One include for all cross-platform compatibility thangs
#include "unrealircd.h"

#define MSG_MODMGR "MODMGR"
#define MYCONF "modmanager-irc"

#define EXLIBS_MAXLEN 192
#define DELAY_EVENT 3 // See 'waituntil' shit ;]

// Dem macros yo
CMD_FUNC(modmanager_irc); // Register command function
EVENT(modmanager_irc_closechild_event); // Also event

#define CheckAPIError(apistr, apiobj) \
	do { \
		if(!(apiobj)) { \
			config_error("A critical error occurred on %s for %s: %s", (apistr), MOD_HEADER.name, ModuleGetErrorStr(modinfo->handle)); \
			return MOD_FAILED; \
		} \
	} while(0)

#define closempipes(p) \
	do { \
		close((p)[0]); \
		close((p)[1]); \
	} while(0)

typedef struct {
	time_t waituntil; // Wait a couple seconds after forking to prevent not getting any output if the event runs too early ;];];]
	char *source; // Original command source (UID, not Client pointer because if the client exits then the Client object will soon be gone too)
	char *currentlibs; // Current EXLIBS value
	pid_t pid; // Child process ID
	int pipe_in, pipe_out, pipe_err; // The file descriptors for the proper/expected directions of every pipe
	FILE *stream_in, *stream_out, *stream_err; // Buffered file handles so we can just use fgets() and shit ;]
	int localclient; // Gotta st0re if the client is local to us, in order to relay only the appropriate notices
} ChildInfo;

// Quality fowod declarations
void freecfg(void);
int modmanager_irc_configtest(ConfigFile *cf, ConfigEntry *ce, int type, int *errs);
int modmanager_irc_configrun(ConfigFile *cf, ConfigEntry *ce, int type);
static void dumpit(Client *client, char **p);
void reset_exlibs(char *currentlibs);
void free_child(ChildInfo *child);

// Muh globals
static ChildInfo *runningchild = NULL; // Keep track of last child because we only run the module manager once (prevents conflicts etc)

// Store config options here
struct cfgstruct {
	char *default_exlibs;
	unsigned short int got_default_exlibs;
};
static struct cfgstruct muhcfg;

// Help string in case someone does just /MODMGR
static char *modmanager_irc_help[] = {
	/* Special characters:
	** \002 = bold -- \x02
	** \037 = underlined -- \x1F
	*/
	"*** \002Help on /MODMGR\002 ***",
	"Allows you to control Unreal's module manager through regular IRC commands, instead of having to SSH to every server.",
	" ",
	"Syntax:",
	"    \002/MODMGR\002 <\037local|global|server name\037> \037install\037 \037module name\037 [\037EXLIBS compilation flags\037]",
	"    \002/MODMGR\002 <\037local|global|server name\037> \037uninstall\037 \037module name\037",
	"    \002/MODMGR\002 <\037local|global|server name\037> \037upgrade\037 <\037*|module name\037> [\037EXLIBS compilation flags\037]",
	"        The \037EXLIBS compilation flags\037 rarely need to be used; module authors will direct you to use them when necessary.",
	"        Keep in mind that a module upgrade involves recompiling the module all over again, thus you'll need to pass the required",
	"        EXLIBS flags every time. Alternatively, use the modmanager-irc::default-exlibs configuration directive to specify a default value.",
	"        Any value passed through IRC will \037override\037 the one from the config.",
	" ",
	"        For the \037upgrade\037 variation you can simply pass \037*\037 to upgrade all currently managed modules.",
	" ",
	"Examples:",
	"    \002/MODMGR local install chansno\002",
	"        Install my \037chansno\037 module only on the server you're currently connected to",
	"    \002/MODMGR global uninstall chansno\002",
	"        Uninstall my \037chansno\037 module from the entire network",
	"    \002/MODMGR global upgrade *\002",
	"        Upgrade all currently managed modules",
	"    \002/MODMGR global install wwwstats -lmysqlclient\002",
	"        Install k4be's \037wwwstats\037 module, which requires linking against the MySQL client library",
	"    \002/MODMGR global upgrade wwwstats -lmysqlclient\002",
	"        Upgrade that same module, which also requires the same compilation flags",
	NULL
};

// Dat dere module header
ModuleHeader MOD_HEADER = {
	"third/modmanager_irc", // Module name
	"1.0", // Version
	"Control Unreal's module manager through IRC", // Description
	"Gottem", // Author
	"unrealircd-5", // Modversion
};

// Configuration testing-related hewks go in testing phase obv
MOD_TEST() {
	memset(&muhcfg, 0, sizeof(muhcfg)); // Zero-initialise config

#ifdef _WIN32
	// Doing it like this to suppress any errors compilers may throw (like "hurr durr unreachable statement" for everything else that would otherwise be below here)
	// The module probably won't even compile on wind0ngs anyways th0 :DDDD
	config_error("[modmanager_irc] Unfortunately this module does not work on Windows");
	return MOD_FAILED;
#else
	// We have our own config block so we need to checkem config obv m9
	// Priorities don't really matter here
	HookAdd(modinfo->handle, HOOKTYPE_CONFIGTEST, 0, modmanager_irc_configtest);
	return MOD_SUCCESS;
#endif
}

// Initialisation routine (register hooks, commands and modes or create structs etc)
MOD_INIT() {
	CheckAPIError("CommandAdd(MODMGR)", CommandAdd(modinfo->handle, MSG_MODMGR, modmanager_irc, MAXPARA, CMD_USER));

	// Add an event to clean up the child when necessary, also outputs stdout/err to the originating user =]]]
	CheckAPIError("EventAdd(modmanager_irc_closechild_event)", EventAdd(modinfo->handle, "modmanager_irc_closechild_event", modmanager_irc_closechild_event, NULL, 5000, 0));

	MARK_AS_GLOBAL_MODULE(modinfo);

	HookAdd(modinfo->handle, HOOKTYPE_CONFIGRUN, 0, modmanager_irc_configrun);
	return MOD_SUCCESS;
}

// Actually load the module here (also command overrides as they may not exist in MOD_INIT yet)
MOD_LOAD() {
	return MOD_SUCCESS; // We good
}

// Called on unload/rehash obv
MOD_UNLOAD() {
	// Clean up any structs and other shit
	free_child(runningchild);
	runningchild = NULL;
	freecfg();
	return MOD_SUCCESS; // We good
}

// Gotta make sure we release any memory back to the OS on mod unload/reload (rehash)
void freecfg(void) {
	safe_free(muhcfg.default_exlibs);
}

int modmanager_irc_configtest(ConfigFile *cf, ConfigEntry *ce, int type, int *errs) {
	int errors = 0; // Error count
	ConfigEntry *cep; // To store the current variable/value pair etc
	size_t len;

	// Since we'll add a top-level block to unrealircd.conf, need to filter on CONFIG_MAIN lmao
	if(type != CONFIG_MAIN)
		return 0; // Returning 0 means idgaf bout dis

	// Check for valid config entries first
	if(!ce || !ce->ce_varname)
		return 0;

	// If it isn't our block, idc
	if(strcmp(ce->ce_varname, MYCONF))
		return 0;

	// Loop dat shyte fam
	for(cep = ce->ce_entries; cep; cep = cep->ce_next) {
		// Do we even have a valid name l0l?
		// This should already be checked by Unreal's core functions but there's no harm in having it here too =]
		if(!cep->ce_varname) {
			config_error("%s:%i: blank %s item", cep->ce_fileptr->cf_filename, cep->ce_varlinenum, MYCONF); // Rep0t error
			errors++; // Increment err0r count fam
			continue; // Next iteration imo tbh
		}

		if(!cep->ce_vardata) {
			config_error("%s:%i: blank %s value", cep->ce_fileptr->cf_filename, cep->ce_varlinenum, MYCONF); // Rep0t error
			errors++;
			continue;
		}

		if(!strcmp(cep->ce_varname, "default-exlibs")) {
			if(muhcfg.got_default_exlibs) {
				config_error("%s:%i: duplicate %s::%s directive", cep->ce_fileptr->cf_filename, cep->ce_varlinenum, MYCONF, cep->ce_varname);
				errors++;
				continue;
			}

			muhcfg.got_default_exlibs = 1;
			len = strlen(cep->ce_vardata);
			if(!len) {
				config_error("%s:%i: %s::%s must be non-empty fam", cep->ce_fileptr->cf_filename, cep->ce_varlinenum, MYCONF, cep->ce_varname);
				errors++;
			}
			else if(len > EXLIBS_MAXLEN) {
				config_error("%s:%i: the length of %s::%s must be equal to or less than %d characters", cep->ce_fileptr->cf_filename, cep->ce_varlinenum, MYCONF, cep->ce_varname, EXLIBS_MAXLEN);
				errors++;
			}
			continue;
		}

		// Anything else is unknown to us =]
		config_warn("%s:%i: unknown item %s::%s", cep->ce_fileptr->cf_filename, cep->ce_varlinenum, MYCONF, cep->ce_varname); // So display just a warning
	}

	*errs = errors;
	return errors ? -1 : 1; // Returning 1 means "all good", -1 means we shat our panties
}

// "Run" the config (everything should be valid at this point)
int modmanager_irc_configrun(ConfigFile *cf, ConfigEntry *ce, int type) {
	ConfigEntry *cep; // To store the current variable/value pair etc

	// Since we'll add a top-level block to unrealircd.conf, need to filter on CONFIG_MAIN lmao
	if(type != CONFIG_MAIN)
		return 0; // Returning 0 means idgaf bout dis

	// Check for valid config entries first
	if(!ce || !ce->ce_varname)
		return 0;

	// If it isn't modmanager_irc, idc
	if(strcmp(ce->ce_varname, MYCONF))
		return 0;

	// Loop dat shyte fam
	for(cep = ce->ce_entries; cep; cep = cep->ce_next) {
		// Do we even have a valid name l0l?
		if(!cep->ce_varname)
			continue; // Next iteration imo tbh

		if(!strcmp(cep->ce_varname, "default-exlibs")) {
			safe_strdup(muhcfg.default_exlibs, cep->ce_vardata);
			continue;
		}
	}

	return 1; // We good
}

// Dump a NULL-terminated array of strings to the user (taken from DarkFire IRCd)
static void dumpit(Client *client, char **p) {
	if(IsServer(client))
		return;

	// Using sendto_one() instead of sendnumericfmt() because the latter strips indentation and stuff ;]
	for(; *p != NULL; p++)
		sendto_one(client, NULL, ":%s %03d %s :%s", me.name, RPL_TEXT, client->name, *p);

	// Let user take 8 seconds to read it
	client->local->since += 8;
}

void reset_exlibs(char *currentlibs) {
	if(currentlibs) {
		setenv("EXLIBS", currentlibs, 1); // Attempt to reset it anyways lol
		safe_free(currentlibs);
	}
	else
		unsetenv("EXLIBS");
}

void free_child(ChildInfo *child) {
	if(!child)
		return;

	// Cleanly close pipes imo tbh (actually file streams but fclose() handles the underlying file descriptor/pipe too)
	if(child->stream_in)
		fclose(child->stream_in);
	if(child->stream_out)
		fclose(child->stream_out);
	if(child->stream_err)
		fclose(child->stream_err);

	if(child->pid) {
		kill(child->pid, SIGKILL); // Force termination ;]
		waitpid(child->pid, NULL, WNOHANG | WUNTRACED); // Attempt proper cleanup
	}

	reset_exlibs(child->currentlibs);
	safe_free(child->source);
	safe_free(child);
}

CMD_FUNC(modmanager_irc) {
	/* Gets args: Client *client, MessageTag *recv_mtags, int parc, char *parv[]
	**
	** client: Pointer to user executing command
	** recv_mtags: Received/incoming message tags (IRCv3 stuff)
	** parc: Amount of arguments (also includes the command in the count)
	** parv: Contains the actual args, first one starts at parv[1]
	**
	** So "MODMGR test" would result in parc = 2 and parv[1] = "test"
	** Also, parv[0] seems to always be NULL, so better not rely on it fam
	**
	** This function returns void, so simply return to stop processing
	*/
	Client *srv; // Pointer to server we need to forward to
	char *target; // Local/global/server name
	char *cmd; // Install/uninstall/upgrade
	char *modname; // Actual module name, or '*'
	char *p; // For checkin em valid characters y0
	char exlibs[EXLIBS_MAXLEN + 1]; // Gotta parse all remaining parv[] arg00ments into one char array (can't store a whole lot cuz we need to be able to communicate it fully to other serburs ;])
	char shellcmd[BUFSIZE + 1]; // Should fit exlibs entirely, as well as the rest of the command ;]
	char *currentlibs; // In case Unreal's environment already has EXLIBS set somehow, return it to its original value afterwards
	int i, offset;
	int local, global, ignore_exlibs;
	int bail; // For if we fail to set EXLIBS
	size_t len_shellcmd, len_exlibs, len;
	int fd_in[2], fd_out[2], fd_err[2]; // Array lengths of 2 due to pipes having both read and write streams ;]
	FILE *stream_in, *stream_out, *stream_err; // Buffered IO handles for ez parsing of data
	pid_t pid; // Child process ID
	int flags_out, flags_err; // fnctl() flags to make shit non-blocking

	if(!IsUser(client)) // Shouldn't be possible anyways but whatevs =]
		return;

	if(!IsOper(client) || !ValidatePermissionsForPath("modmanager-irc", client, NULL, NULL, NULL)) {
		sendnumeric(client, ERR_NOPRIVILEGES); // Check ur privilege fam
		return;
	}

	if(parc < 2 || BadPtr(parv[1])) { // When doing just /MODMGR
		dumpit(client, modmanager_irc_help); // Return help string instead
		return;
	}

	if(parc < 4 || BadPtr(parv[3])) { // Need at least 3 arguments lol
		sendnumeric(client, ERR_NEEDMOREPARAMS, MSG_MODMGR);
		return;
	}

	if(runningchild) {
		sendnotice(client, "[modmanager_irc] ERROR: This server is still running a command against the module manager (PID: %d)", runningchild->pid);
		return;
	}

	target = parv[1];
	cmd = parv[2];
	modname = parv[3];
	exlibs[0] = '\0';
	len_exlibs = 0;
	srv = NULL;
	local = (!strcmp(target, "local"));
	global = (!strcmp(target, "global"));

	if(!local && !global) {
		srv = find_server(target, NULL);
		if(!srv) {
			sendnotice(client, "[modmanager_irc] ERROR: Unable to find server (%s)", target);
			return;
		}
		if(srv == &me) {
			srv = NULL;
			target = "local";
		}
		// Don't forward to other servers, first we check for errors ;]
	}

	if(strcmp(cmd, "install") && strcmp(cmd, "uninstall") && strcmp(cmd, "upgrade")) {
		sendnotice(client, "[modmanager_irc] ERROR: Invalid module manager command (%s)", cmd);
		return;
	}

	// Yes, even *sdfjhgjksdfg will be treated as simply *
	if(*modname == '*') {
		if(strcmp(cmd, "upgrade")) {
			sendnotice(client, "[modmanager_irc] ERROR: Module name can only be '*' for the 'upgrade' command");
			return;
		}
		modname = NULL;
	}
	else {
		if(!strncmp(modname, "third/", 6)) // Always remove leading third/ because we prepend it ourselves anyways (to support just passing module names)
			modname += 6;

		if(!*modname) { // Could happen if someone *only* passes "third/" :>
			sendnotice(client, "[modmanager_irc] ERROR: No module name given");
			return;
		}
		p = modname;
		while(*p) {
			// I could use isalpha() but it's locale-dependent so we *might* accept e.g. Arabic shit as valid too, and let's not do that =]
			if(*p < 45 || // Absolute lower boundary: -
				*p > 122 || // Absolute upper boundary: z
				*p == 47 || // The / character is inbetween the dot and zero
				(*p >= 58 && *p <= 64) || // The characters :;<=>?@ are inbetween 9 and A lol
				(*p >= 91 && *p <= 94) || // The characters [\]^ are inbetween Z and underscore y0
				*p == 96 // The ` character (backtick) comes after em underscore but before a
			) {
				sendnotice(client, "[modmanager_irc] ERROR: Module name can only consist of letters/numbers/dots/underscores/hyphens (%s)", modname);
				return;
			}
			p++;
		}
	}

	// Got EXLIBS from IRC
	// Don't check modmanager-irc::default-exlibs value yet, so every server can use different values ;]
	ignore_exlibs = (!strcmp(cmd, "uninstall")); // Don't even bother parsing it for uninstall commands [[=[==[[==[
	offset = 1; // Don't count space for first arg kek
	if(!ignore_exlibs && parc >= 5 && !BadPtr(parv[4])) {
		for(i = 4; i < parc && !BadPtr(parv[i]); i++) {
			len = strlen(parv[i]);
			if(len_exlibs + len + offset > sizeof(exlibs)) { // Account for space and nullbyet, otherwise we'd be truncating and that's n0 good amigo
				sendnotice(client, "[modmanager_irc] ERROR: Length of EXLIBS flags is too long (may not exceed %ld characters)", sizeof(exlibs) - 1);
				return;
			}

			ircsnprintf(exlibs + len_exlibs, sizeof(exlibs) - len_exlibs, "%s%s", (len_exlibs > 0 ? " ": ""), parv[i]);
			if(len_exlibs) // Account for space if not the first iterations ;]
				len_exlibs++;
			len_exlibs += len;
			offset = 2;
		}
	}

	// Send notice about command usage imo tbh famalam
	if(MyUser(client)) {
		if(len_exlibs)
			sendto_snomask_global(SNO_EYES, "[modmanager_irc] %s (%s@%s) used the %s command [args: %s %s %s %s]",
				client->name, client->user->username, client->user->realhost, MSG_MODMGR, target, cmd, (modname ? modname : "*"), exlibs);
		else
			sendto_snomask_global(SNO_EYES, "[modmanager_irc] %s (%s@%s) used the %s command [args: %s %s %s]",
				client->name, client->user->username, client->user->realhost, MSG_MODMGR, target, cmd, (modname ? modname : "*"));
	}

	// Forward the command to a specific server if we need to (in which case we always use the "local" option)
	// Also prepend EXLIBS arg with a colon so the receiving server only has to re-parse parv[i] once ;];];];]
	if(srv) {
		sendto_one(srv, NULL, ":%s %s local %s %s :%s", client->name, MSG_MODMGR, cmd, (modname ? modname : "*"), (len_exlibs ? exlibs : ""));
		return;
	}

	// Forward to all other servers in case of "global" obv (we do this before the actual shellcmd executes so we can get errors from all servers y0) ;]
	if(global)
		sendto_server(client, 0, 0, NULL, ":%s %s global %s %s :%s", client->name, MSG_MODMGR, cmd, (modname ? modname : "*"), (len_exlibs ? exlibs : ""));

	// Now get config value if necessary ;]
	if(!ignore_exlibs && !len_exlibs && muhcfg.default_exlibs) {
		strlcpy(exlibs, muhcfg.default_exlibs, sizeof(exlibs));
		len_exlibs = strlen(exlibs);
	}

	ircsnprintf(shellcmd, sizeof(shellcmd), "%s/unrealircd module %s", SCRIPTDIR, cmd);
	len_shellcmd = strlen(shellcmd);
	if(modname) {
		len = strlen(modname);

		if(len_shellcmd + len + 8 > sizeof(shellcmd)) { // Account for space, "third/" and nullbyet
			sendnotice(client, "[modmanager_irc] ERROR: Maximum shell command length exceeded (got %ld characters but should be less than or equal to %ld)",
				len_shellcmd + len + 7, sizeof(shellcmd) - 1);
			return;
		}

		// No need to wrap the module name in escaped quotes, cuz we don't allow any chars that need quotation in the module name to begin with ;]
		ircsnprintf(shellcmd + len_shellcmd, sizeof(shellcmd) - len_shellcmd, " third/%s", modname);
		len_shellcmd += len + 7; // Account for "third/" and space too
	}

	bail = 0;
	currentlibs = NULL;
	safe_strdup(currentlibs, getenv("EXLIBS")); // Always store currentlibs though (despite ignore_exlibs etc), makes it much easier to reset that shit =]]]

	if(len_exlibs) {
		if(setenv("EXLIBS", exlibs, 1)) // Returns non-zero on error ;]
			bail = 1;
	}
	else {
		if(setenv("EXLIBS", "", 1)) // Set to empty string instead of unsetting it
			bail = 1;
	}

	if(bail) {
		sendnotice(client, "[modmanager_irc] ERROR: Unable to set EXLIBS environment variable (%s)", strerror(errno));
		reset_exlibs(currentlibs);
		return;
	}

	if(pipe(fd_in) < 0) {
		sendnotice(client, "[modmanager_irc] ERROR: Unable to set up pipes for STDIN (%s)", strerror(errno));
		reset_exlibs(currentlibs);
		return;
	}

	if(pipe(fd_out) < 0) {
		sendnotice(client, "[modmanager_irc] ERROR: Unable to set up pipes for STDOUT (%s)", strerror(errno));
		closempipes(fd_in);
		reset_exlibs(currentlibs);
		return;
	}

	if(pipe(fd_err) < 0) {
		sendnotice(client, "[modmanager_irc] ERROR: Unable to set up pipes for STDERR (%s)", strerror(errno));
		closempipes(fd_out);
		closempipes(fd_in);
		reset_exlibs(currentlibs);
		return;
	}

	// Make sure the IRCd doesn't stall when waiting for messages
	flags_out = fcntl(fd_out[0], F_GETFL, 0);
	flags_err = fcntl(fd_err[0], F_GETFL, 0);
	if(fcntl(fd_out[0], F_SETFL, flags_out | O_NONBLOCK) < 0) {
		sendnotice(client, "[modmanager_irc] ERROR: Unable to set non-blocking mode for STDOUT pipe (%s)", strerror(errno));
		closempipes(fd_err);
		closempipes(fd_out);
		closempipes(fd_in);
		reset_exlibs(currentlibs);
		return;
	}
	if(fcntl(fd_err[0], F_SETFL, flags_err | O_NONBLOCK) < 0) {
		sendnotice(client, "[modmanager_irc] ERROR: Unable to set non-blocking mode for STDERR pipe (%s)", strerror(errno));
		closempipes(fd_err);
		closempipes(fd_out);
		closempipes(fd_in);
		reset_exlibs(currentlibs);
		return;
	}

	if(!(stream_in = fdopen(fd_in[1], "a"))) {  // Append without reading cuz we got stdout for dat ;]
		sendnotice(client, "[modmanager_irc] ERROR: Unable to open buffered file handle for STDIN (%s)", strerror(errno));
		closempipes(fd_err);
		closempipes(fd_out);
		closempipes(fd_in);
		reset_exlibs(currentlibs);
		return;
	}
	if(!(stream_out = fdopen(fd_out[0], "r"))) {
		// You don't actually need to do both fclose() and regular close(), but otherwise you'd get a bunch of close(fd_xxx[y]) lines instead of just closempipes(), so fuck that =]
		sendnotice(client, "[modmanager_irc] ERROR: Unable to open buffered file handle for STDOUT (%s)", strerror(errno));
		fclose(stream_in);
		closempipes(fd_err);
		closempipes(fd_out);
		closempipes(fd_in);
		reset_exlibs(currentlibs);
		return;
	}
	if(!(stream_err = fdopen(fd_err[0], "r"))) {
		sendnotice(client, "[modmanager_irc] ERROR: Unable to open buffered file handle for STDERR (%s)", strerror(errno));
		fclose(stream_out);
		fclose(stream_in);
		closempipes(fd_err);
		closempipes(fd_out);
		closempipes(fd_in);
		reset_exlibs(currentlibs);
		return;
	}

	pid = fork();
	if(pid < 0) {
		sendnotice(client, "[modmanager_irc] ERROR: Unable to fork child process (%s)", strerror(errno));
		fclose(stream_err);
		fclose(stream_out);
		fclose(stream_in);
		closempipes(fd_err);
		closempipes(fd_out);
		closempipes(fd_in);
		reset_exlibs(currentlibs);
		return;
	}

	// Parent stuff goes here
	if(pid > 0) {
		runningchild = safe_alloc(sizeof(ChildInfo));
		runningchild->waituntil = TStime() + DELAY_EVENT;
		safe_strdup(runningchild->source, client->id); // To return output to the user running the command
		runningchild->pid = pid; // To cleanly terminate the child process from the parent
		runningchild->stream_in = stream_in;
		runningchild->stream_out = stream_out;
		runningchild->stream_err = stream_err;
		runningchild->currentlibs = currentlibs;
		runningchild->localclient = (MyUser(client));
		return;
	}

	// The rest here is for the child =]
	// Close the file descriptors used by the parent
	close(fd_in[1]);
	close(fd_out[0]);
	close(fd_err[0]);

	// Now clone the new pipes to the default std* file descriptors
	// Also we don't need stdin at the moment, but it's prolly better if all 3 descriptors get replaced for consistency ;];];]];
	close(0); // Close stdin in general
	if(dup(fd_in[0]) < 0) { // Clone new stdin reading stream to original stdin
		// Shit can't really fail at this point, but otherwise gcc throws "unused return value" warnings (also we can't reuse the parent's sockets so sendnotice() is b0rked anyways) =]
	}

	// Then stdout
	close(1);
	if(dup(fd_out[1]) < 0) {}

	// Also stderr
	close(2);
	if(dup(fd_err[1]) < 0) {}

	execl("/bin/sh", "sh", "-c", shellcmd, NULL); // Replaces process image, so return values from the unrealircd wrapper get passed through =]]]]]
}

EVENT(modmanager_irc_closechild_event) {
	int status;
	Client *client; // Original command source
	char stdbuf[256]; // Not using BUFSIZE cuz that's too long for sendnotice/realops anyways lol
	int deadclient;
	int has_stderr;
	int skip_stdout;
	int remove;

	// Wait a couple seconds after forking to prevent not getting any output if the event runs too early ;];];]
	// This doesn't cause the file streams/descriptors to be closed by the child, since we as the parent still have them open we can still read from that shit =]]]
	if(!runningchild || !runningchild->pid || TStime() < runningchild->waituntil)
		return;

	// Make sure the original user still exists before trying to notice em
	client = hash_find_id(runningchild->source, NULL); // Skip find_client() shit and use the ID hash directly so it saves some time =]
	deadclient = (!client || IsDead(client));

	// Reads shit line by line y0
	// Shit gets a lil funky here cuz we'll output errors from *all* servers back to the originating oper, but stdout only gets sent once (from the local server) --
	// UNLESS stdout contains the lines indicating the module (de)installation was successful
	// But if that oper /quit from IRC then all notices are sent to every server's local opers
	has_stderr = 0;
	while(fgets(stdbuf, sizeof(stdbuf), runningchild->stream_err)) {
		// fgets() also stores the terminating newline in the buffer, but Unreal's send* functions strip that shit anyways [[==[=[[=[==[=[
		has_stderr = 1;

		// Skip a few errors that don't indicate any real troubleshooting information =]
		if(!strncmp(stdbuf, "collect2:", 9) || !strncmp(stdbuf, "make[", 5) || !strncmp(stdbuf, "make:", 5))
			continue;

		if(!strncmp(stdbuf, "All actions were successful", 27)) {
			// Need to do this because sometimes some additional information (non-errors) is sent to stdout by the module manager
			has_stderr = 0;
			continue; // Don't output this line either kek
		}

		if(deadclient)
			sendto_realops("[modmanager_irc, relayed] %s", stdbuf);
		else
			sendnotice(client, "[modmanager_irc] %s", stdbuf);
	}

	// No need to even bother checking for module post-install shit =]
	if(!has_stderr) {
		skip_stdout = 1;
		while(fgets(stdbuf, sizeof(stdbuf), runningchild->stream_out)) {
			if(skip_stdout) {
				// Only interested in "Module [']third/xxx compiled successfully " and all the post-install shit that comes after it : D
				if(!strncmp(stdbuf, "Module third/", 13) || !strncmp(stdbuf, "Module 'third/", 14))
					skip_stdout = 0;
				else
					continue;
			}
			if(deadclient)
				sendto_realops("[modmanager_irc, relayed] %s", stdbuf);
			else if(runningchild->localclient)
				sendnotice(client, "[modmanager_irc] %s", stdbuf);
			else if(!strncmp(stdbuf, "Post-installation information", 29)) // Skip post-install text for remote oper
				skip_stdout = 1;
			else
				sendnotice(client, "[modmanager_irc] %s", stdbuf);
		}
	}

	// Check if child exited or was signalled to stop
	remove = 0;
	waitpid(runningchild->pid, &status, WNOHANG | WUNTRACED); // Don't block the rest of the IRCd ;]

	// Not checking WIFSTOPPED(status) cuz that might indicate someone's trying to debug/trace the child process (which is ok ;])
	if(WIFSIGNALED(status)) {
		// SIGTERM etc
		remove = 1;
		if(deadclient)
			sendto_realops("[modmanager_irc, relayed] Child process terminated unexpectedly: received signal %d", WTERMSIG(status));
		else
			sendnotice(client, "[modmanager_irc] Child process terminated unexpectedly: received signal %d", WTERMSIG(status));
	}

	else if(WIFEXITED(status))
		remove = 1; // Clean exit l0l

	if(remove) {
		runningchild->pid = 0; // No need to kill it again from free_child() :>
		free_child(runningchild);
		runningchild = NULL;
	}
}
