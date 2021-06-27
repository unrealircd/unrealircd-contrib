## anti_amsg
Some clients implement a command like ``/amsg <message>``, which sends that message to all active tabs (both channels and private queries).  
This can get really spammy, so the module will allow the first message to go through and block the others for a short period of time.  
It will also strip colours and control codes, in case some people have edgy scripts for different markup across tabs.  
The offender will get an in-channel notice if the target is a channel and in the server console if otherwise.
****
## anticaps
Some people may often type in caps which is annoying af really.  
So this module allows you to block these messages (or convert to all-lowercase) by means of an ``anticaps { }`` configuration block.  
It works for both channel and user private messages. ;] It disregards colour codes and control characters as well as spaces while counting caps.  
U-Lines and opers are once again exempt.  

**Config block:**
```
anticaps {
    capslimit 10; // Default: 50
    minlength 10; // Default: 30
    lowercase_it 1; // Default: 0
}
```
Above example would set a limit of 10% capital letters, with a minimum message length of 10.  
In other words, if the message is less than 10 characters it won't even bother checking.  
You can also set it to 0 to effectively disable it. The entire block is optional.  
Set ``lowercase_it`` to ``1`` to lowercase the entire (plaintext) message instead of downright bl0cking it. ;]
___
## auditorium
The good ol' channel mode ``+u`` from Unreal 3.x. =]  
Since a bunch of stuff for this mode was hardcoded in U3's core sauce, I've made a few adjustments to maximise its usability.  
I've never used ``+u`` myself back then, so I'm not 100% sure if the features match (it was basically guesswork based on the dirty U3 sauce).  

_Once the channel mode is set, the following restrictions apply:_

**Everyone with** ``+o/+a/+q``**:**  
* Can see the full user list and all channel events (joins, quits, etc)
* Will see messages from anyone, even those without a list access mode

**People without** ``+o/+a/+q``**:**
* Can only see users with any of those 3 modes
* Will also only see messages from such users

**IRCOpers only:**
* If they have the proper override (which afaik is ``override:see:who:onchannel``) they can also see everyone, but not their messages if they don't have the proper list modes

The channel mode can only be set by people with ``+a`` or ``+q`` (and opers with ``OperOverride`` obv).  
When the mode is unset or someone gets chanops, they won't immediately see the other users "joining".  
Instead, people have to do ``/names #chan`` (this is how it worked in U3 too).  
OR if you have ``+D`` active as well, that module will take care of the rest. ;]
___
## autojoin_byport
Similar to `set::auto-join`, this forces users to join one/multiple channels on connect, based on the p0rt they're using.

**Config block:**
```
autojoin_byport {
    6667 "#plain";
    6697 "#ssl,#ssl2";
}
```
Should be pretty clear how that works. ;]  
Currently it doesn't check for duplicates or max channels, so bnice.  
It does check for valid channel names/lengths and port ranges. =]
___
## autovhost
Apply vhosts at **connect time** based on users' raw nick formats or IP addresses.  
The vhosts are entered in `unrealircd.conf` and they must be at least 5 chars in length.  
Furthermore, you can only enter the host part (so **not** `ident@host`).

There are some dynamic variables you can specify in the config, which will be replaced with a certain value on user connect:
* `$nick`: nickname 0bv m8
* `$ident`: ident 0bv m9

If a resulting vhost would be illegal (unsupported characters etc) then opers with the `EYES` snomask will get notification about this.

**Usage:**  
Add a config block that looks like this:
```
autovhost {
    *!*@some.host $nick.big.heks;
    *!*@some.host $ident.big.heks;
    Mibbit*!*@*mibbit.com mibbit.is.gay;
    192.168.* premium.lan;
}
```
So everyone connecting from a typical LAN IP will get the vhost `premium.lan`, for example.  
Keep in mind it replaces `virthost` and **not** `cloakedhost`, so when you do `/umode -x` this custom vhost is lost (why would you do `-x` anyways? ;]).

Furthermore, services like for example Anope's `HostServ` will override this vhost as they generally set it **after** we changed it.
___
## bancheck_access
This one was mostly written for keks. It prevents chanops/hops from banning anyone who has `+o`, `+a` and/or `+q`.  
The module will check every banmask against people who have any of those access modes, using both their vhost and cloaked host.  
Opers can ban anyone regardless, but they can also still be affected by other bans if they don't have a high enough access mode.  
This is done to maintain fairness in the channel, but opers may sometimes need to set wide-ass and/or insane channel bans.  
Similar to `fixhop`, this mod will simply strip the affected masks and let the IRCd handle further error processing.

**Note:**  
Afaik Unreal is unable to "ask" your `NickServ` flavour about access lists from within a module.  
So if you have Anope and did `/cs access #chan add <nick> 10` it will auto-op that person when they authenticate, but Unreal knows fuck all about this list.  
As such, it simply checks if they have the proper access mode **right now**.

Also, because chan(h)ops could theoretically use `/cs mode #chan +b <mask>` to bypass the module, U-Lines are also restricted. ;]

**Config block:**  
The module will display a notice to the user in case certain masks are stripped, provided you set the following config value:
```
set {
    // Rest of set { } block goes hur 0fc
    bancheck_access_notif 1;
}
```
**Also, keep in mind** that this only prevents **setting** of bans, they can still do like `/mode #chan -b *!*@hostname.of.chanop`.
___
## banfix_voice
So apparently someone with `+v` can still talk if they're banned (`+b`) or mutebanned (`+b ~q:`).  
This seems counterintuitive to me and probably a bunch of others, so the module rectifies that.  
Keep in mind that people with at least `+h` or IRC operator status (as well as U-Lines) can still talk through it.

Also, when you're not even voiced but have `+b` on you and you `/notice #chan`, you don't see a "you are banned" message.  
So I fixed that too. =]

Just load it and g0. =]

___
## block_masshighlight
This shit can halp you prevent highlight spam on your entire network. =]  
It keeps track of a user's messages on a per-channel basis and checks if they highlight one person too many times or too many different persons at once.  
Opers and U-Lines are exempt (as per usual), but also those with list modes `+a` and `+q`. ;3  
When someone hits the threshold, opers with the snomask `EYES` will get a server notice through the module (enable that shit with `/mode <nick> +s +e` etc ;]).

In some cases you might wanna exclude a certain channel from these checks, in which case you can use channel mode `+B`.  
This can be useful for quiz/game channels.

Couple o' thangs to keep in mind: 
* The module doesn't count duplicate nicks on the same line as separate highlights
* The module doesn't exclude **your own nick**, because bots tend to just run through `/names` and won't exclude themselves

**Config block:**  
_The module doesn't necessarily require any configuration, it uses the following block as defaults_
```
block_masshighlight {
	maxnicks 5;
	delimiters "	 ,.-_/\:;";
	action gline;
	duration 7d;
	reason "No mass highlighting allowed";
	snotice 1;
	banident 1;
	multiline 0;
	allow_authed 0;
	//allow_accessmode o;
	percent 1;
	show_opers_origmsg 1;
}
```
* **maxnicks**:  Maximum amount of highlighted nicks (going over this number results in `action` setting in) -- **works in conjunction with `percent`**
* **delimiters**: List of characters to split a sentence by (don't forget the surrounding quotes lol) -- any char not in the default list may prevent highlights anyways
* **action**: Action to take, must be one of: `drop` (drop silently [for the offender]), `notice` (drop, but **do** show notice to them), `gline`, `zline`, `shun`, `tempshun`, `kill`, `viruschan`
* **duration**: How long to `gline`, `zline` or `shun` for, is a "timestring" like `7d`, `1h5m20s`, etc
* **reason**: Reason to show to the user, must be at least 4 chars long
* **snotice**: Whether to send snomask notices when users cross the highlight threshold, must be `0` or `1` (this only affects messages from the module indicating what action it'll take)
* **banident**: When set to `1` it will ban `ident@iphost`, otherwise `*@iphost` (useful for shared ZNCs etc)
* **multiline**: When set to `1` it will keep counting highlights until it encounters a line **without** one
* **allow_authed**: When set to `1` it will let logged-in users bypass this shit
* **allow_accessmode**: Must be **one** of `vhoaq` (or omitted entirely for no exceptions \[the default\]), exempts everyone with at minimum the specified mode from highlight checks (e.g. `a` includes people with `+q`)
* **percent**: Threshold for the amount of characters belonging to highlights, not counting `delimiters` (e.g. `hi nick` would be 67%) -- **works in conjunction with `maxnicks`**
* **show_opers_origmsg**: Display the message that was dropped to opers with the `SNO_EYES` snomask set (**this is entirely separate from the `snotice` option**)

If you omit a directive which is required for a certain `action`, you'll get a warning and it will proceed to use the default.  
It should be pretty clear what directives are required in what cases imo tbh famalam. ;];]
___
## block_no_tls
Allows privileged opers to temporarily block new, non-TLS (aka SSL) **user** connections network-wide. ;3  
There's a new operpriv, which if set, allows an oper of that class to both block and unblock non-TLS connections.  
Keep in mind that rehashing the IRCd also resets the blocking flag for that server. ;]  
Opers with snomask `SNO_KILLS` (`/umode +s +k` etc) will see notices about disallowed connections.

**Config block:**
```
operclass netadmin-blocknotls {
	parent netadmin;
	permissions {
		blocknotls;
	}
}
```

**Syntax:**  
`BLOCKNOTLS`  
`UNBLOCKNOTLS`

Both commands do not take any arguments and are broadcasted to other servers. =]
___
## block_notlsident
Another request by `Celine`. =] Blocks certain idents from connecting if they're using a plain port (so without SSL/TLS lol).  
As it supports wildcards too, it doesn't take into account the maximum ident length (`USERLEN`, which is 10 by default). ;]

**Config block:**
```
block_notlsident {
	ident "*ayy*";
	ident "someident";
	// More go here
}
```
___
## bot-tag
This one adds an `inspircd.org/bot` message tag to each message sent by a bot (marked with +B flag).  
This is compatible with more than one existing server software, and can be used bots to avoid replying to other bots.  
In my opinion, a metadata key is the superior solution, but so far message tags are much more universally supported.
___
## chansno
My port of [AngryWolf's module for Unreal 3.x](http://www.angrywolf.org/modules.php).  
Allows you to assign different channels for different types of server notifications. It works like the snomask system, but instead of receiving notifications in your private server console thingy, you will get the messages in channels. ;]  
Simply load the module and get to `unrealircd.conf`, see below for halp.

**Muy importante:** there is some different functionality for Unreal versions `5.0.1` and lower:
* `tkl-del` doesn't exist **at all** (there is a bug that is fixed only in 5.0.2 and onwards ;])

**Config block:**
```
chansno {
	msgtype privmsg;
	
	channel "#huehue" {
		server-connects;
		squits;
		oper-ups;
	}

	channel "#kek" {
    		mode-changes;
		topics;
		joins;
		parts;
		kicks;
	}
}
```
The first directive, `msgtype`, can either be `privmsg` or `notice`.  
The first one dumps a plain message as if it were ein user, the second is a channel notice.

And here's a list of types you can use fam:
* `mode-changes` => Mode changes inside channels
* `topics` => Topic changes lol
* `joins` => Join messages obv
* `parts` => Part messages 0bv
* `kicks` => Kick messages OBV
* `nick-changes` => Nickname changes m8
* `connects` => Local client connections
* `disconnects` => Local client disconnections
* `server-connects` => Local server connections
* `squits` => Local server disconnections
* `unknown-users` => Disconnections of unknown (local) clients
* `channel-creations` => Channel creations (previously non-existent channels)
* `channel-destructions` => Channel destructions (the last user parted from a channel), notify local users only (to prevent duplicates)
* `oper-ups` => Successful oper-ups, displays opernick and opercla$$
* `spamfilter-hits` => Notify whenever someone matches a spamfilter
* `tkl-add` => Notify whenever a TKL is added (G-Line, K-Line, Z-Line, E-Line, etc)
* `tkl-del` => Or deleted =]

You may have noticed the term `local` a lot above. This means the underlying hook only triggers for clients connected directly to that particular server.  
So people on `server A` doing a chanmode change won't cause `server B` to pick up on it. In this case `A` will simply broadcast it so everyone gets notified. =]  
The exception on this are channel destructions; every server will see the event so they will only broadcast it to their local users. I added dis because fuck dupes. ;3
___
## chgswhois
This module provides command /CHGSWHOIS and /DELSWHOIS which does what it says on the tin

This module changes a users "special whois" line to the string you specify


Syntax: /CHGSWHOIS username is a bastardó

Syntax: /DELSWHOIS username

This module needs to be loaded on all servers on the network
___
## clearlist
This module is quite similar to `rmtkl` in that you can clear "ban" lists based on masks with wildcards.  
So you could use a mask of `*!*@*` or even just `*` to clear errythang, it's pretty straightforward imo tbh. =]

Couple o' thangs to keep in mind:
* The module will run Unreal's `TKL_DEL` hook, so other modules are aware of the changes it makes.
* Sometimes (for unknown reasons) you might get corrupt masks in your ban lists (e.g. `butthams@*` instead of `butthams!*@*`), running any `/clearlist` command will clear those as well.  
This was added because not even with raw server2server commands can you remove those masks.

**Syntax:**
`CLEARLIST <channel> <types> <mask>`

`Types` list:
* `b` => Ban
* `e` => Ban exception
* `I` => Invite exception

**Examples:**
* `CLEARLIST #bighek b *`
* `CLEARLIST #ayylmao Ie guest*!*@*`
* `CLEARLIST #hambutt * *`
___
## clones
Yay for `AngryWolf`. =]  
Adds a command `/CLONES` to list all users having the same IP address matching the given options.  
Clones are listed by a nickname or by a minimal number of concurrent sessions connecting from the local or the given server.

I made one change, which is instead of having `/helpop clones` (which requires you to add a block to all servers' `help.conf` files) you can now view a built-in halp thingay. =]

**Syntax:**
`CLONES <min-num-of-sessions|nickname> [server]`

**Some examples:**
* `CLONES 2` => Lists local clones having `2` or more sessions
* `CLONES Loser` => Lists local clones of `Loser`
* `CLONES 3 hub.test.com` => Lists all clones with at least 3 sessions, which are connecting from `hub.test.com`
* `CLONES` => View built-in halp
___
## commandsno
Guess who originally wrote this one?  
Adds a snomask `+C` for opers so they can see what commands people use. ;3  
Specify what commands may be monitored in the config, then set the snomask with `/mode <nick> +s +C` (or extend `set::snomask-on-oper`).

For obvious reasons `PRIVMSG` and `NOTICE` can't be monitored, deal w/ it.

**Config block:**
I've kept the "location" the same as the original one but I did rename the directive. It's simply a comma-delimited list of commands. ;]
```
set {
	// Rest of the set block goes here obv, like kline-address, network-name, etc
	commandsno "version,admin,module";
}
```
___
## debug
Allows privileged opers to easily view internal (configuration) data. I know you can do some of it with a `/STATS` command, but this shit displays different info than that. ;]

**Usage:**
Currently it allows opers with the operpriv `debug` to view configured oper and operclass information. By default it asks the server to which you're connected, but since it may differ across servers (local O-Lines etc), you can also ask another server directly. =]

**Config block:**
```
operclass netadmin-debug {
	parent netadmin;
	permissions {
    		debug;
	}
}
```

**Syntax:**
`DBG <datatype> [server]`
I went with `DBG` and not `DEBUG` cuz a bunch of clients probably nick the `/debug` command for themselves, get at me. ;];]

**Examples:**
* `DBG opers` => Get a list of known opers from the server you're on
* `DBG operclasses ayy.check.em` => Asks the leaf `ayy.check.em` about the operclasses it knows
___
## denyban
In the event of a netsplit, if someone tries to be funny by setting `+b *!*@*` on the main channel, nobody will be able to join until opers come in and fix it.  
This module allows you to deny such bans. =] Optionally, specify if opers are allowed to override it and if offenders should get a notice.  
I even went a bit further and also applied these restrictions to `SAMODE`. ;]

Also, because chanops could theoretically use `/cs mode #chan +b <mask>` to bypass the module, U-Lines are also restricted. ;]

**Config block:**
```
denyban {
	allowopers 0;
	denynotice 1;
	reason "ayy ur faget lol (stripped $num masks)";
	mask BAN_ALL;
	mask *!*@*malvager.net;
}
```

The mask `*!*@*` is kinda special, so you'll have to specify `BAN_ALL` to prevent people from doing `MODE #chan +b *!*@*` etc.  
For anything else it will do a wildcard match, as is usual.  
The directives `allowopers` and `denynotice` should be pretty clear. They default to `1` and `0`, respectively.

For the `reason` directive you can use `$num`, which will be replaced with the actual amount of stripped masks. ;]

I went with `denyban` over `deny ban` (notice the space) to avoid any potential future conflict with Unreal core stuff.

**Also, keep in mind** that this only prevents **setting** of bans, they can still do like `/mode #chan -b *!*@*malvager.net`.
___
## extjwt
This one provides an `EXTJWT` command to generate tokens for authenticating to external web services.  
It is currently based on the "Work In Progress" (that means the spec can change and then the module and clients will need updating).  
Specification available here: [EXTJWT specification](https://github.com/ircv3/ircv3-specifications/blob/f3facfbe5f2686f2ab2a3322cadd31dacd3f5177/extensions/extjwt.md).

To use the `vfy` claim (`verify-url` option) you must provide your own verification facility, separate for each service (and for the default JWT when applicable).  
See the specification for details.

The module looks for a config block:
```C
extjwt {
	method "HS256"; // must be one of: NONE (not recommended), HS256, HS384, HS512, ES256, ES384, ES512, RS256, RS384, RS512
	expire-after 30; // seconds
	secret "somepassword"; // do not set when using ES*, RS* or NONE method, required for HS* method
//	key "somefile.pem"; // path to the private key PEM file placed in conf/ - do not set when using HS* or NONE method, required for ES* and RS* method
	service "test" { // optional service block
		method "ES512"; // supported: HS256, HS384, HS512, ES256, ES384, ES512, RS256, RS384, RS512
//		secret "anotherpassword"; // required for HS methods
		key "someotherfile.pem"; // required for ES and RS methods
		expire-after 60; // seconds, will be inherited from default if not given
		verify-url "https://example.com/verify/?t=%s"; // optional, won't be inherited, must be http or https, must contain %s
	};
	service "test2" {
		method "HS384";
		secret "yetanotherpassword";
	};
};
```

### How to create ES and RS keys
These methods allow you to pass your public key to someone verifying your tokens, without giving them chance to generate their own signed tokens (they would need to have the private key for that). Below is a quick reference for generating the key pairs.

To generate RS256, RS384 or RS512 private key (to add to your IRCd):
```
openssl genrsa -out privkey.pem 4096
```

To generate matching public key (to use for token verification):
```
openssl rsa -in privkey.pem -pubout > pubkey.pem
```


To generate ES256, ES384 or ES512 private key (to add to your IRCd):
```
openssl ecparam -genkey -name secp521r1 -noout -out privkey.pem
```

To generate matching public key (to use for token verification):
```
openssl ec -in privkey.pem -pubout -out pubkey.pem
```

Of course, substitute your preferred file names for `pubkey.pem` and `privkey.pem`.
___
## extwarn
Enables additional error checking of the IRCd configuration.  
I originally wrote it to (temporarily?) work around a [bug](https://bugs.unrealircd.org/view.php?id=4950) (although it may be by design) where you don't get a warning about using non-existent operclasses during rehash/init.  
So it currently only checks for that, but I may extend it to include other things later. =]

The module only throws **warnings** (hence the name lol), so it allows the rehash etc to continue normally. This way it won't break any functionality. ;];]

To make sure the entire config is available to the module, it delays its report by 10 seconds after a rehash/bootup.
___
## fantasy
Fantasy commands may sound familiar if you're using Anope IRC services or have fucked with InspIRCd at some point.  
They're basically aliases for certain commands and are visible to other users, since they're generally called like `!cmd dicks`.  
Unreal provides aliases too but afaik that only works for `/cmd dicks`, which isn't visible to others.  
You can also change the command prefix to like `?cmd` or `\cmd` or whatever you want (one character max), just not `/cmd`. ;]  
It also supports a few special inline variables, more on that bel0w.

Furthermore, these aliases are limited to **channel messages**. There's not much you can do in private messages anyways, besides maybe `ACTION` shit.

**Keep in mind messages similar to `!!cmd` (like, more than one leading `cmdchar`) are ignored, as well as messages starting with any other character**

**Special variables:**
I'll put these before the config block so you'll have that info before you get to it. ;] You can use all the special variants for `$1` through `$9`.
* `$chan` => Will obv be replaced with the channel you dumped the command in
* `$1` => Will be replaced with the user's first argument
* `$1-` => Becomes the first argument plus everything after it (greedy)
* `$1i` => If the first arg is a nick, replace it with their ident/username
* `$1h` => If the first arg is a nick, replace it with their hostname

**You cannot use multiple greedy vars however**, raisins should be obvious.

**Config block:**  
_Creating aliases might be a bit complex, so I'll just dump an example config right here and explain in-line with comments. =]_
```
fantasy {
	// Change command prefix to \, so it becomes \dovoice etc
	//cmdchar \;

	// "!dovoice urmom" results in "/mode $chan +v urmom"
	dovoice         "MODE $chan +v $1";
	unvoice         "MODE $chan -v $1";

	// "!fgt urmom" is turned into "/kick $chan urmom :ayyyyy ur faget"
	fgt             "KICK $chan $1 :ayyyyy ur faget";

	// "!bitchnigga urmom dickface" results in a separate "/kick $chan $nick :nigga b0iiii" for both urmom and dickface
	bitchnigga      "KICK $chan $1- :nigga b0iiii";

	// "!ayylmao urmom u goddam fuckstick" becomes "/kick urmom :u goddam fuckstick"
	ayylmao         "KICK $chan $1 :$2-";

	// "!invex urmom" is majikked into "/mode $chan +I *!*@urmom.tld"
	invex           "MODE $chan +I *!*@$1h";
	uninvex         "MODE $chan -I *!*@$1h";

	// "!safe urmom" will become "/kick $chan urmom :$chan is a safe space lol m8 urmom"
	safe            "KICK $chan $1 :$chan is a safe space lol m8 $1";

	// It is also possible to have the same alias issue multiple commands ;]
	safe            "MODE $chan +b *!*@$1h";

	// You can also go through ChanServ, provided you have access with it
	n               "PRIVMSG ChanServ :KICK $chan $1 change your nick and rejoin lol";

	// Set a channel to registered-only and muted
	floodprot       "MODE $chan +Rm";
}
```
As you may have noticed there are some colons in there. These are required, because otherwise there's no telling where the target arg stops and the message begins.  
Also, in case you do `!devoice` (so without ne args) it will try and voice the user who issued it. Permissions for using the resulting command are eventually (and automajikally) checked by the internal `do_cmd()` call I have in hur. ;3
___
## findchmodes

This one allows IRCoperators to check which channels use certain channel mode.  
You can use it to check, for example, who has the Channel History enabled.

Usage example:

`/findchmodes +H`
___
## fixhop
In my opinion the `+h` access mode (channel halfops yo) is a little b0rked (cuz le RFC compliancy prob).  
So I made a qt3.14 module that implements some tweaks which you can enable at will in da config. ;3

Also, because hops could theoretically use `/cs mode #chan +b <mask>` to bypass the module, U-Lines are also restricted. ;]

**Usage:**  
Just compile that shit, load it and add the tweaks you want to a top-level config block (disable one by simply removing/leaving out the directive):
```
fixhop {
	allow_invite;
	disallow_widemasks;
	widemask_notif;
	disallow_chmodes "ft";
	chmode_notif;
}
```
* `allow_invite` => Apparently when a chan is `+i` halfops can't invite people, this overrides that
* `disallow_widemasks` => Hops can normally set a ban/exemption/invex for shit like `*!*@*`, this disallows that by stripping those masks
* `widemask_notif` => Enable an in-channel notice directed only at the hopped user
* `disallow_chmodes` => Will strip the given channel modes in either direction, ne arguments should be taken into account
* `chmode_notif` => Enable in-channel n0tice for dis
___
## gecos_replace
Requested by `TimeRider`, this module allows you to e.g. replace a Mibbit user's real name/gecos (`http://www.mibbit.com`) with something else.

I don't foresee many people needing this, so the module functionality is pretty basic at the moment.  
For example: the match is a literal match, you **can't** use wildcards (`*something*`). It **is** case-insensitive though. =]

**Config block:**
```
gecos-replace {
	match "http://www.mibbit.com";
	replace "";
	}

gecos-replace {
	match "ayy";
	replace "lmao";
}
```
In case of the Mibbit example the entire gecos is just their URL, so because we're replacing it with nothing (effectively erasing the text) it will instead become the user's on-connect nick.  
Some Unreal core code (and perhaps the client?) most likely depends on it being non-NULL and having a length of 1 or more. I think the gecos is also specified as required in em RFC. =]
___
## geoip-base
This one provides data for other "geoip" modules (currently there is only one available, "geoip-whois").

This module needs to be loaded on only single server on the network. (You may keep it active on a second one for redundancy, it won't break anything.)

The module looks for a config block:
```
geoip {
	ipv4-blocks-file "GeoLite2-Country-Blocks-IPv4.csv";
	ipv6-blocks-file "GeoLite2-Country-Blocks-IPv6.csv";
	countries-file "GeoLite2-Country-Locations-en.csv";
}
```
If one of blocks files is missing, the address type is ignored by the module.  
If more files can't be loaded, the module fails.

If this config block is not given, it defaults to looking for three files in conf:
GeoLite2-Country-Blocks-IPv4.csv, GeoLite2-Country-Locations-en.csv, GeoLite2-Country-Blocks-IPv6.csv.
These can be downloaded from [here](https://dev.maxmind.com/geoip/geoip2/geolite2/#Downloads) (get GeoLite2 Country in CSV format).  
We are notified that the download method may change soon.
___
## geoip-chanban

This one allow banning users from certain countries on a channel.  
Exceptions and invite exceptions are also possible.

`/mode #channel +b ~C:FR` - will prevent all users from France from joining.

`/mode #channel +iI ~C:RO` - only users from Romania will be able to join.

`/mode #channel +be *4*!*@* ~C:PL` - only users from Poland are allowed to have a number "4" in their nick.

Load this module on every server, together with geoip-base (on two servers for redundancy) or geoip-transfer (on remaining ones).
___
## geoip-connect-notice

This one sends geoip information to a far connect (+F) snomask.

Do not load this module on more than one server on the network.  
Correctly configured `geoip-base` or `geoip-transfer` is required.
___
## geoip-transfer

This one transfers data that come from the geoip-base module loaded on other server, so you don't have to use the resource-intensive geoip-base everywhere.  
It may be needed by the "geoip-chanban".
___
## geoip-whois

This one appends swhois info to all users, unless they are not listed in the geoip data.

This module needs to be loaded on only single server on the network (it'll serve the whole network), and requires the "geoip-base" module loaded on same server.

The module looks for a config block:
```
geoip-whois {
	display-name; // Poland
	display-code; // PL
	//display-continent; // Europe
	info-string "connected from "; // remember the trailing space!
}
```

Display option left out means that this info won't be displayed. (Keep at least one enabled.)  
No info-string text will cause the module to default to "connected from ".
___
## getlegitusers
Originally written by `darko`, this module simply counts how many legitimate clients you have on your network.  
People who are on more than one channel are seen as legitimate, while they who are in no channels are marked "unknown".  
It also accounts for services boats.

Just run `/getlegitusers` and check your server console. =]
___
## helpop-lite
"Lite" version of my helpop module.

Provides:

- Usermode 'h' (HelpOp)
- Channelmode 'g' (HelpOp-only chan)
- Special whois (swhois) line saying the user "is available for help"
- Command /HELPOPS (same syntax as GLOBOPS and LOCOPS, and sends to all with usermode 'h')
___
## helpop
This module provides the following:
  
- Usermode h (HelpOp)
- Channel mode g (Only HelpOp can join)
- Command: /helpoper   - this sends a server notice to other users who have usermode +h if the user also have +h
  (Syntax: /helpoper your message to send)
- Command: /report     - this sends server notice from a user with no usermode +h to users who have usermode +h
  (Syntax: /report your message to send)
- SWHOIS line marking +h "is available for help"
 
 
Shout out to Gottem for his dank templates  
Shout out to Syzop for his epic documentation skillz

Parts of code taken from parts of unrealircd5.0.9 source code

This module have no configurable option

This module is a re-make of the old helpop module (which was replaced by the then /helpop ?cmds) which
was removed in UnrealIRCd v4 but I liked it so I maked it again

This module needs to be installed on every server on your network
___
## joinmute
Originally written by `Dvlpr`, this module prevents people who just joined a channel from speaking for a set amount of seconds.  
It works with a new channel mode: `+J <seconds>`, for which you need to be chanop or higher to set. Furthermore, opers, U-Lines and anyone with any access mode (one or more of `+vhoaq`) will be able to talk right away.  
U-Lines are exempt since some services may join a channel, do something and part. =]
___
## kickjoindelay
Adds a chanmode `+j <delay>` to prevent people from rejoining too fast after a kick.  
You can set a delay of between 1 and 20 seconds, as anything higher might be a bit much lol.  
You gotta have at least `+o` to set this shit. Opers and U-Lines are exempt, as well as servers (cuz just in case lol).
___
## listrestrict
Allows you to impose certain restrictions on `/LIST` usage, such as requiring clients to have been online for a certain period of time.  
Simply load the module and add a new block to your `unrealircd.conf`, for which see bel0w.  
Opers, servers and U-Lines are exempt for obvious reasons. ;]

Even though Unreal now has a `set::restrict-commands` block you can use to delay `/LIST` usage, it doesn't provide the honeypot functionality, so I've kept this module as-is.  
`listrestrict` adds overrides with a higher priority though, so even if you configure both then `listrestrict` will run first.

**Config block:**
```
listrestrict {
	connect-delay 60; // How long a client must have been online for
	need-auth 1; // Besides above, also require authentication w/ services
	auth-is-enough 1; // Don't even check connect-delay if user is identified OR exempt from having to authenticate entirely
	fake-channels 1; // Send a fake channel list if connect-delay and need-auth checks fail
	ban-time 7d; // For channels with an *-Line action (accepted but ignored for non-Lines), use 0 for permanent bans (defaults to 1 day)

	fake-channel {
		// Only the name is required
		name "#honeypot";
		//topic "ayy lmao"; // Defaults to "DO NOT JOIN"
		//users 50; // Defaults to 2 users, must be >= 1
		ban-action gline; // G-Line won't kick in if connect-delay and need-auth checks are satisfied, or if the user has a 'fake-channels' exception
	}

	fake-channel {
		name "#fakelol";
		topic "top kek";
		users 10;
		ban-action kill;
	}

	exceptions {
		all "user@*";
		connect "someone@some.isp"; // Only require auth
		auth "*@123.123.123.*"; // Only require connect-delay
		fake-channels "ayy@lmao"; // Don't send a fake channel list, just prevent sending of the legit one

		// You can also specify multiple types for the same mask
		// This user would only need to wait <connect-delay> seconds and won't get a fake channel list at all
		auth "need@moar";
		fake-channels "need@moar";
	}
}
```
Omitting a directive entirely will make it default to **off** unless otherwise mentioned above.  
If `connect-delay` is specified, the minimum required value is `10` as anything below seems pretty damn useless to me. =]  
The `exceptions` block should be pretty self explanatory. ;]

For the `ban-action`, refer to [Unreal's wiki for a list of possible actions](https://www.unrealircd.org/docs/Actions). ;]  
Keep in mind that all `soft-*` actions cannot be used, you must use `auth-is-enough` instead.  
You also can not use `block` and `warn` because those are pretty useless here. ;];;];]
___
## message_commonchans
Adds a umode `+c` so you can prevent fucktards who don't share a channel with you from messaging you privately.  
Simply load it and do `/mode <your_nick> +c`. ;3 Opers and U-Lines are exempt from this (i.e. they can always send private messages), the reason why should be obvious imo tbh fam.  
Additionally, since U-Lines such as `NickServ` are usually not present in any channels, they can always receive messages too (rip `/ns identify` otherwise ;]).
___
## metadata-db

This one stores metadata for registered users (based on their account names) coming from the metadata module, and restores it for them at logon.  
User data will expire after specified time (in days). You probably want to set this to a value similar to your services account expiration.  
Metadata is also stored for +P channels.

Of course, the `metadata` module is required to be loaded for it to work.

This module needs to be loaded on only single server on the network. (You may keep it active on a second one for redundancy, it won't break anything.)

The module looks for a config block:
```C
metadata-db {	
	database "metadata.db";
	expire-after 365; // days
};
```
If the config is not specified, the above defaults are used.
___
## metadata

This one implements the METADATA command, allowing users to set their avatars, message colouring, status texts etc.  
It is currently based on the "Work In Progress" (that means the spec can change and then the module and clients will need updating) specification available here: [metadata specification](https://gist.github.com/k4bek4be/92c2937cefd49990fbebd001faf2b237).  
It is known to work with the [PIRC web client](https://github.com/pirc-pl/pirc-gateway) and [IRCcloud](https://www.irccloud.com/).

For compatibility, the module uses two CAPs: `draft/metadata` and `draft/metadata-notify-2`, and also an ISUPPORT tag of `METADATA`.

The module looks for a config block:
```C
metadata {
	max-user-metadata 10;	// maximum metadata count for a single user
	max-channel-metadata 10;	// maximum metadata count for a single channel
	max-subscriptions 10;	// maximum number of metadata keys an user can subscribe to
	enable-debug 0;	// set to 1 for ircops to receive all METADATA commands (floody)
};
```
If the config is not specified, the above defaults are used.

Short usage explanation (for "avatar" metadata key name):

- Set the avatar URL for your nick: `/metadata * set avatar :https://example.com/example.png`
- Remove your avatar: `/metadata * set avatar`
- Subscribe to avatars of users (so server will send them for you): `/metadata * sub avatar`
- The notification sent by the server: `:irc.example.com METADATA someone avatar * :https://example.com/example.png`

Please keep these * signs intact.
___
## modmanager_irc
**Doesn't work on Windows ;]**

Building upon the existing functionality of Unreal's module manager, this module allows control of that shit through IRC commands instead of having to SSH to every server. ;];]  
As a bonus, it also supports passing custom compilation flags by means of the `EXLIBS` environment variable. This is very rarely needed; module authors will tell you to use it when necessary.

Also, keep in mind that a module upgrade involves recompiling the module all over again, thus you'll need to pass the required `EXLIBS` flags every time (or see **Config blocks** section for a way around that).

**Important note:** while this module works perfectly fine on any Unreal 5.x version, there currently is a minor problem with the module manager which I expect to be fixed in 5.0.2.  
This problem is that even when module compilation fails, Unreal sometimes may report the test phase as being successful and ends up outputting the errors twice.

**Config blocks:**  
First off, you'll need to grant opers the new `modmanager-irc` permission:
```
operclass netadmin-modmanager {
	parent netadmin;
	permissions {
		modmanager-irc;
	}
}
```

For now there's one extra option you can slam in your configuration file, which is of course entirely optional:
```
modmanager-irc {
	default-exlibs "-lldap";
}
```
If any oper omits the `EXLIBS` flags then the module will use whatever you specify here.  
Anything specified on IRC **will override what's in the config** instead of adding them together, to prevent any conflicts.

You may also wanna make sure that the `class { }` block that your opers use has a large enough `sendq` value, due to the possibility that **a large amount of text may be sent at once**.  
The `sendq`/`recvq` values are from the perspective of the **server**, so `sendq` is for messages **to users**.
```
class opers {
	// Rest of the block here
	sendq 1M; // Unreal has this in its example config so you might be good already, this should be plenty big =]
}
```
**Syntax:**  
`MODMGR <local|global|server name> install <module name> [EXLIBS compilation flags]`  
`MODMGR <local|global|server name> uninstall <module name>`  
`MODMGR <local|global|server name> upgrade <*|module name> [EXLIBS compilation flags]`

All these arguments should be pretty clear. =]  
For the `module name` you can omit both the leading `third/` and trailing `.c`, because both are assumed anyways. ;].

**Because we need to remain able to forward the command to other servers, there's a limit of 192 characters for any `EXLIBS` value (conf or IRC)**.  
Many clients cut off messages around 230 characters anyways, so anything much higher and shit will be incomplete regardless. ;];];]

The module emits one global message indicating usage of this command, which can be seen if you have the `SNO_EYES` snomask set (`/umode +s +e`).  
All other messages like compilation errors are usually only shown to the oper running the command (exceptions apply, see directly below).

Now, a few other things to keep in mind as well:
* Only one command can run at a time, otherwise shit might get funky. ;]
* Because the module manager is called externally, it would normally block the entire IRCd until it's completely finished.  
  To work around this I'm using an event that runs every 5 seconds to see if we need to get output from the modmanager.  
  This means that any output could be delayed for up to 8 seconds; there's also a 3 second delay after running the modmanager to prevent some race conditions.  
  Meaning you could get the output from the second "tick".
* A side effect of using this event is that the oper who originally ran the command might have `/quit` from IRC.  
  In this case, any messages originally intended for them will now be sent to all opers **local to the server emitting those messages**.  
  Like after a `global install` command, compilation errors from all servers would normally have been relayed to only the originating oper.  
  But if they're gone then `server A` will send them to all opers locally connected to it, `server B` for its own local opers, etc.  
  Any such messages are prefixed with `relayed` (for lack of a better term elemao).
* As the originating oper, it might be a bit confusing why you're getting certain messages from a certain server (or not getting them) in case of `global` commands. So I'll explain em right here:
    * From the server you're connected to you'll receive: module not installed error, compilation errors, compilation success message and any post-install text from the author indicating the next steps
    * From other servers: module not installed error, compilation errors and compilation success message

**Examples:**  
`MODMGR` => Display built-in halp stuff =]]]  
`MODMGR local install chansno` => Install my `chansno` module only on the server you're currently connected to  
`MODMGR global uninstall chansno` => Uninstall my `chansno` module from the entire network  
`MODMGR global upgrade *` => Upgrade all currently managed modules  
`MODMGR global install wwwstats -lmysqlclient` => Install k4be's `wwwstats` module, which requires linking against the MySQL client library  
`MODMGR global upgrade wwwstats -lmysqlclient` => Upgrade that same module, which also requires the same compilation flags

And prest0. =]
___
## monitor
This one implements the IRCv3 [MONITOR command](https://ircv3.net/specs/core/monitor-3.2).  
It's independent from the built-in WATCH.
___
## nicksuffix
Requested by `Celine`, this module prevents people from doing `/nick <anything here>`.  
Rather, it suffixes the "original" nick (the one on connect) with what the user specifies.  
There's also a special "nick" to revert back to the original one. ;]  
The module also comes with a new config block, which is **required**.

**Config block:**
```
nicksuffix {
	separator "|";
	restore "me";
}
```
The `separator` can be one of the following characters: `-_[]{}\|`  
The other directive is so when people do `/nick me` they'll revert back to what they had on connect.  
It can be anything you want, as long as Unreal sees it as a valid nick.

Also, keep in mind that the specified `separator` char **cannot** be used for "base" nicknames, so connecting to IRC with a nick such as `foo|bar` is not possible.
___
## noghosts
Originally proposed by `PeGaSuS`, this module makes IRC opers part `+O` channels once they oper down.  
I've kept the "ghosts" thing for lack of a better term, deal w/ it. =]

**Config block:**
```
noghosts {
	message "g h o s t b u s t e r s";
	flags "O";
	channels {
		"#opers"; // Don't forget the "" to make sure Unreal doesn't interpret it as a comment ;]
		"#moar";
	}
}
```
All configurables are optional; the default message is simply `Opered down` and if no `channels` are specified then it'll check all `+O` ones the opered-down oper is in.  
I may extend `flags` to contain more flags at some point, which would also affect the `message` (prolly). =]
___
## noinvite
Adds a new umode `+N` (as in **N**oinvite ;]) to prevent all invites.  
The only ones that can still invite you are 0pers and U-Lines obv.  
Simply l0ad the module and do like `/umode +N` for profits.
___
## nopmchannel
A request by `westor`, this module is p much the opposite of my `message_commonchans` in that it **prevents** users from privately messaging each other if they share a specific channel.  
There are a few exceptions (as usual ;]):
* People with any of the `+hoaq` access modes, as well as U-Lined users (e.g. BotServ bots), can send and receive normally to/from all channel members
* IRC opers can always send to other members but not receive, unless they have one of above-mentioned access modes

**Config block:**
```
nopmchannel {
	name "#somechannel";
	name "#another";
	// More go here
}
```
I went with a config block instead of a channel mode because the module imposes restrictions on user-to-user communications, which is more of a network thing instead of channel-level. ;]
___
## operoverride_ext
Originally named `m_forward`, a bigass chunk of that code is now included in core Unreal.
The remainder is implemented by this "new" module:

* Normally, even when an oper has OperOverride they still can't simply `/join #chan` when it's set to `+R` or `+i` or if they're banned.  
They have to do `/sajoin` or invite themselves. The module corrects that (it still generates an OperOverride notice though ;]).

Any other additions to OperOverride will also be done by this module. =]

**Required oper permissions:**  
* To override `+b` (banned lol): `channel:override:message:ban`
* To override `+i` (invite only): `channel:override:invite:self`
* To override `+R` (only regged users can join a channel): `channel:override:message:regonly`, which seems to be non-default
___
## operpasswd
Another port of one of `AngryWolf`'s modules. =]
This basically lets you see the username and password for a failed `OPER` attempt, you gotta set an extra snomask flag to see them though.  
You can do this wit `/mode <nick> +s +O` or just use it in `set::snomask-on-oper`.

I made some changes to this module as it relied on "netadmins" which Unreal 4.x/5.x doesn't have n0 m0ar.  
So I came up with a new operclass privilege to indicate who is able to set the snomask.  
You also had to explicitly enable the snomask in the 3.x version, but it's now enabled by default. ;]

**Config blocks:**
```
operclass netadmin-operpasswd {
	parent netadmin;
	permissions {
		operpasswd;
	}
}
```
```
operpasswd {
	enable-global-notices 1;
	enable-logging 0;
	max-failed-operups 3;
	failop-kill-reason "Too many failed OPER attempts lol";
}
```
Since snomask notices are only sent to a server's local opers, you gotta use `enable-global-notices` to broadcast that shit to errone.  
Also, you **have** to specify something for `max-failed-operups` in order to get the kill to work, since the module sorta assumes 0 which means "disable that shit fam".  
The kill reason defaults to "Too many failed OPER attempts".
___
## plainusers
A simple module to list all users **not** connected over SSL/TLS. Just run `/PUSERS` or `/PLAINUSERS` to get a nice lil' list. ;]  
It attempts to cram as many nicks on one line as it can, to avoid spamming the fuck out of you.  
Only opers can use it obviously. =]

You can also achieve the same result with the already existing `/WHO` command but the specific flags you need might be a little hard/confusing to remember (even more so now that `WHOX` is a thing).
___
## pmlist
Sort of a hybrid between umode `+D` (don't allow private messages at all) and `+R` (allow registered users only), this module allows you to keep a whitelist of people allowed to send you private messages. =]  
Their UID is stored so it persists through nickchanges too, but the module only shows you the original whitelisted nick in lists etc.

Opers and U-Lines can bypass the restriction 0bv. ;]

**Usage:**  
Load the module and set umode `+P` on yourself (anyone can (un)set the umode).  
You can't set up a list without it but if you do have one and then unset the umode, it will be kept.  
Only when you disconnect will it be cleared (so it even persists through rehashes).  
Also, if **you** have `+P` and message someone else, they will automatically be added to the list.  
If UIDs go stale (relevant user disconnects) the entry will also be removed if it's not persistent.  
See below (**Syntax**) for how2manage da whitelist. ;3

There are also a few configuration directives for netadmins to tweak that shit.

**Config block:**
```
pmlist {
	noticetarget 0; // Default is 0
	noticedelay 60; // Seconds only
}
```
* `noticetarget` => Whether to notice the target instead, **if the source is a regged and logged in user**
* `noticedelay` => How many seconds have to pass before a user will receive another notice

**Syntax:**  
`OPENPM <nick> [-persist]` => Allow messages from the given user (the argument **must be an actual, existing nick**)  
`CLOSEPM <nickmask>` => Clear matching entries from your list; supports wildcard matches too (`*` and `?`)  
`PMLIST` => Display your current whitelist  
`PMHELP` => Display built-in halp info

**Examples:**  
`OPENPM guest8` => Allow `guest8` to message yo  
`OPENPM muhb0i -persist` => Allow `muhb0i`, persistently  
`CLOSEPM guest*` => Remove all entries matching this mask (`guest8`, `guestxxxx`, etc)  
`CLOSEPM *` => Remove **all** entries (saves you a disconnect lol)

Now this is what happens when a non-whitelisted user tries to message you:
* For users who **haven't logged into services or if `noticetarget` is set to 0**, they will get a notice saying you don't accept private messages from them.  
  It also instructs them to tell **you** to do `/openpm` for them, like in a common channel.
* For users who **have logged into services and if `noticetarget` is set to 1** you will get a single notice asking you to do `/openpm` (the notice also includes their message).
* These notices only happen once in a certain time period (based on the config directive `noticedelay`). After the first one, everything else will be silently discarded until enough time passes.
___
## portsifresi
Using this module you can specify a different password for every port.  
This might be useful if e.g. you have backup servers that only certain people should be able to connect to.  
Or if you want a totally priv8 netw3rk. I think it was originally written by `Sky-Dancer`.

**Config block:**
```
psifre {
	6667 "hams";
	6697 "turds";
}
```
So any client connecting to port `6667` will first have to send a `PASS hams` command.  
People who use `6697` have to do `PASS turds`.  
Many clients (if not all?) have a `server password` field, so you just gotta enter em port-specific password in there.

Keep in mind that you still need a regular `listen` block to even accept connections on the specified port(s), **as this module does not handle any of that**.

**Protip:** Unreal 5 has [defines and conditional config](https://www.unrealircd.org/docs/Defines_and_conditional_config) hecks, which you could theoretically use with this module.  
For example, you could `@define $SERVER "foo.bar.baz"` then specify a password like `pre-$SERVER-post`. In order to connect to a specific server, you need to provide the proper server name too.  
Meaning the proper password is: `pre-foo.bar.baz-post`
___
## pubnetinfo
Another request/proposal by `PeGaSuS`, this module displays information that is allowed to be publicly available for every server.  
Right now it'll simply show if a server is linked over `localhost` and if it's using `SSL/TLS` to communicate with the other end.  
Simply execute `/pubnetinfo` and check the server notices sent to you. ;]

**Example output:**
```
-someleaf.myirc.net- [pubnetinfo] Link hub.myirc.net: localhost connection = no -- SSL/TLS = yes
-hub.myirc.net- [pubnetinfo] Link otherleaf.myirc.net: localhost connection = no -- SSL/TLS = yes
-hub.myirc.net- [pubnetinfo] Link services.myirc.net: localhost connection = yes -- SSL/TLS = no
```
So in this case I ran the command on `someleaf`, which only has information about the hub it's linked to.  
For the other servers this leaf asks its hub for the information instead (otherwise we can't tell if it's using TLS or nah).  
The hub has info on 2 additional servers; another leaf and the services node.

You can get similar information by using `/stats C`, but:
* it only outputs shit for directly attached servers
* it displays the server connection port too. ;]
___
## rehashgem
Implements an additional rehashing flag `-gem` (**g**lobal **e**xcept **m**e).  
I originally wrote this because of the `confprot` module.  
Since it sorta requires you to set `allowunknown` to `1` when making config changes, it would cause a triple rehash on the hub which is really not necessary. =]

Now that `confprot` is dead this module prolly serves little purpose anymore, but some people might find it useful lel.

Soooooo, when you do `/rehash -gem`, all **other** servers will rehash everything (appends `-all` flag by default).  
You can also rehash just MOTDs, SSL/TLS-related stuff, etc by appending their respective flags to `-gem`:
* `/rehash -gemtls`
* `/rehash -gemmotd`
* Any other "sub" flag Unreal already supports (`/helpop rehash` should show these)
___
## repeatprot
Sometimes there are faggots who spam the shit out of you, either through NOTICE, PRIVMSG, CTCP and/or INVITE commands.  
This module will GZ-Line/kill/block their ass if they do.  
Other than specifying the triggers and exceptions, you can tweak the action a lil' bit (more on that bel0w ;]).

The module will keep track of people's last and first to last messages, so it catches people who alternate their spam too (or bots). =]  
Also, **channels are excluded** as they probably have something better in place (like `+C` to disable `CTCP` in general, or `+f` for more fine-grained flood control).  
Colours and markup are also stripped prior to running the checks.

Any oper with the `SNO_EYES` snomask set (`/umode +s +e`) will get notices from the module.  
These include trigger type, nick, body and action.

There's also one built-in exception, namely sending **to** U-Lines.  
First of all they should have their own anti-abuse systems in place.  
Secondly, sometimes people forget which exact password they used so they have to try multiple times.

**Config block:**
```
repeatprot {
	triggers {
		notice;
		//privmsg;
		//ctcp;
		//invite;
	}
	exceptions {
		nick!user@host;
		*!gottem@*;
		ayy!*@*;
	}

	timespan 2m; // Only keep track of commands sent over this period of time, accepts formats like 50, 1h5m, etc (0 for always, which is also the default)
	action block; // Default = gzline
	//action kill;
	//action gzline;
	//action gline;
	banmsg "Nice spam m8"; // This is also the default message
	showblocked 1; // Display the banmsg above to the spammer when action is block
	//tkltime 60; // How long to G(Z)-Line for, accepts formats like 50, 1h5m, etc (default = 60 seconds)
	threshold 3; // After how many attempts the action kicks in (default = 3)
}
```
You need at least one trigger obviously. The exception masks must match `*!*@*`, so should be a full `nick!ident@host` mask m9.  
The **host**name used should match the **real** host (or IP) and not a cloaked one.
___
## report
Requested by `Valware`, this module allows users to report bad stuff to the assigned IRC operators.  
By "assigned" I mean they have the proper operclass permissions to handle deez ~~nuts~~ reports (see bel0).

This shit doesn't impose too many restrictions, just exact **case-insensitive** duplicates of a certain report.  
Colours etc are stripped beforehand ofc 0fc. ;]  
Currently reports are listed in descending order, meaning the **most recent rep0t is up top**.  
Reports are synced across the entire network and they each have a unique ID to keep track of em, they are also kept through rehashes.

**br0tip:**  
Combine this shit with the [restrict-commands config block](https://www.unrealircd.org/docs/Set_block#set::restrict-commands).  
Best idea is pr0lly to just set a minimum reputation, this should help prevent spam because people with enough reputation usually know better.  
Of course you could also enable m0ar restricti0nes. ;];];]

**Some n0tes:**
* Reports must be unique, but the ID is (obviously) shared across all servers.  
  If during a netsplit 2 **separate/different** reports are made on both ends, then when they sync that shit they'll both complain about a mismatch.  
  You'll have to delete the ID on one end to resolve it.  
  It doesn't matter if a report is added on **one** side only, because the other will simply accept the synced one in that case.
* Right now the synctime logic for verifying "sane" reports is a little basic, if it causes (too many) problems then I can always revise em. =]]  
  Like, right now you could have a report with the exact same comment but a different ID.  
  On one hand that makes sense because it's a "new" report (likely by someone else) so you wanna know about it, but on the other you just handled a similar report.  
  So, time will tell if this is excessive or nah. [==[=[[=[=[=
* While IDs are persistent through rehashes, they **will reset** if the report list is entirely empty when the IRCd is rehashed.
* Opers can still create reports themselves, I don't think it's really necessary to restrict that y0.

**Config blocks:**
```
operclass netadmin-report {
	parent netadmin;
	permissions {
		report {
			// These can be set individually, or to grant all of them you can just do like: permissions { report; };
			delete;
			list; // For e.g. "trainees" you could only let them view reports and have them let others know to delete a report when it's been handled
			notify; // All opers with this permission will receive notifications about new/deleted reports (both by users and those synced with other servers when linking)
		}
	}
}
```
```
report {
	// These are all optional ;]
	//min-chars 5; // Set a minimum amount of characters required for report comments (allowed range is 1-50, default = 10)
	//max-reports 100; // Set a maximum amount of stored rep0ts (allowed range is 1-200, default = 50)
}
```

**Syntax:**  
Shit should be p self-explanatory lmao:  
`REPORT <comment>`  
`REPORTLIST`  
`REPORTDEL <id>`
___
## rtkl
Allows privileged opers to remove remote servers' local `K/Z-Lines`.  
The required privileges are `server-ban:kline:remove` and `server-ban:zline:local:remove`, which the "netadmin" operclass should carry by default.  
The syntax is mostly the same as for regular `K/Z-Lines`, with the addition of one in the very front to indicate the target server.  
After some checks it will simply pass on a `KLINE` or `ZLINE` command to that server, so any further error handling is done by that module. ;]

**Syntax:**  
`RKLINE <server> <[-]identmask@hostmask> {duration} {reason}` => Add/Delete a local K-Line on `server` for the specified usermask (duration and reason are ignored for deletions)  
`RZLINE <server> <[-]identmask@ipmask> {duration} {reason}` => Add/Delete a local Z-Line on `server` for the specified usermask (duration and reason are ignored for deletions)

**Examples:**  
* `RKLINE myleaf.dom.tld *@top.kek.org 0 lolnope` => Adds a permanent K-Line on `myleaf.dom.tld` for everyone connecting from `top.kek.org`  
* `RKLINE myleaf.dom.tld -*@top.kek.org 0 lolnope` => Deletes this same K-Line  
* `RZLINE myleaf.dom.tld *@123.123.123.* 60 lolnope` => Adds a one-minute Z-Line on `myleaf.dom.tld` for everyone connecting from `123.123.123.*`

Since the snomask notices on the target server are only sent to **local** opers, I had to hack some hook bs to get the executing oper to see that shit. =]

**Protip:** I didn't include a method for listing the remote server's `*-Lines`, you can already use `/stats K myleaf.dom.tld` for that fam. ;]
___
## sacmds
I've gotten multiple requests to have a module to implement `SA*` commands (like `SANICK`, `SAUMODE`) within the IRCd instead of through services (usually `/os SVSNICK` etc).  
Right now it allows privileged opers to forcibly change someone's nickname and/or their usermodes. =]  
You cannot use any of these commands on U-Lined users, raisins should b fairly obvious.

This m0d also implements a snomask `+A` to indicate who can **see** the related server notices.  
I had to pick `A` (ykno, like **A**dmin?) cuz all of `cCnNsS` are already taken, so rip.  
There are also operprivs for every "subcommand" to indicate who can use which.  
You also gotta have one of the operprivs to set the snomask. ;]  
The target user won't get a visual cue at all for umode changes, unlike when using services.  
This means no notice, no `<nick> sets mode <modestr>` in their console, etc.

**Config blocks:**  
First, make sure an operclass has one or more of the operprivs:
```
operclass netadmin-sacmds {
	parent netadmin;
	permissions {
		sanick;
		saumode;
	}
}
```
And assign it to one or more oper(s).  
Then (optionally), to easily and automatically enable the snomask for them, **append** the `A` flag to your current `set::snomask-on-oper`:
```
set {
	// Rest of your set block goes here
	snomask-on-oper "cfkevGqsSoA";
}
```
This won't set the snomask for opers w/o the privilege, as the IRCd executes the `on-oper` thingy on behalf/in name of the oper.  
As such, operprivs **are** verified beforehand. ;]

**Syntax:**  
`SANICK <fromnick> <tonick>`  
`SAUMODE <nick> <umodes>`

**Examples:**  
`SANICK ayy bastem` => Changes the nick `ayy` to `bastem`  
`SAUMODE bastem -x+Z` => Removes `bastem`'s virtual host and enables the `secureonlymsg` restriction
___
## sacycle
Forces someone to /CYCLE a channel

Syntax: /SACYCLE username #channel

Requires sapart permissi0n
___
## setname
This one implements the IRCv3 [SETNAME capability](https://ircv3.net/specs/extensions/setname).
___
## showwebirc
This one appends swhois info to users that are connected with WEBIRC authorization.
___
## signore
Requested by `Jellis` and `Stanley`.  
This module allows privileged opers to set a server-side ignore of sorts, making 2 users' messages invisible to each other (in private only tho, just muteban them if they shitpost in-channel or something).  
Servers, U-Lines and opers are exempt for obvious raisins.

Expiring these so-called `I-Lines` is done with an `EVENT` thingy in Unreal, which is set to run every 10 seconds.  
This means if you set an ignore for 5 seconds (why would you anyways?) it may not expire after eggzacktly 5 seconds. ;]  
When no expiration is specified the value in `set::default-bantime` is used.  
Every server checks the expirations themselves. =]  
Also, they store it in a `ModData` struct so it persists through a rehash without the need for a `.db` fiel. ;]  
Furthermore, they sync their known `I-Lines` upon server linkage. =]]  
Other lines such as G-Lines, Z-Lines are also stored in memory and get resynced during a link so these are pretty similar aye.

**Config block:**
```
operclass netadmin-signore {
	parent netadmin;
	permissions {
		signore;
	}
}
```
**Syntax:**  
`SIGNORE [-]<ident@host> <ident2@host2> [expire] <reason>`

Also supports the alias `ILINE`.  
The first 2 arguments may also be online nicknames, they will be resolved to their respective full mask. =]  
Wildcards `*` and `?` are also supp0rted. Masks cannot overlap each other (e.g. `som*@*` and `s*@*`).
___
## textshun
Enables privileged 0pers to drop messages based on nick and body regexes (`T-Lines`), similar to badwords and spamfilter but more specific.  
It only supports (PCRE) regexes because regular wildcards seem ineffective to me, fucken deal w/ it. ;]  
Also, you can't have spaces (due to how IRC works) so you'll have to use `\s` and shit.  
Unreal creates a case-insensitive match so no worries there, it also tells you if you fucked up your regex (and what obv).  
Only opers with the new operpriv `textshun` will be able to use it, although all others will get the notices since these `T-Lines` are **netwerk wide** bruh.  
You'll also get a server notice for when a T-Line is matched.  
Servers, U-Lines and opers are exempt for obvious raisins.

Expiring shuns is done with an `EVENT` thingy in Unreal, which is set to run every 10 seconds.  
This means if you set a shun for 5 seconds (why would you anyways?) it may not expire after eggzacktly 5 seconds. ;]  
When no expiration is specified the value in `set::default-bantime` is used.  
Every server checks the expirations themselves. =]  
Also, they store it in a `ModData` struct so it persists through a rehash without the need for a `.db` fiel. ;]  
Furthermore, they sync their known `T-Lines` upon server linkage. =]]  
Other lines such as G-Lines, Z-Lines are also stored in memory and get resynced during a link so these are pretty similar aye.

**Config block:**
```
operclass netadmin-textshun {
	parent netadmin;
	permissions {
		textshun;
	}
}
```
**Syntax:**  
`TEXTSHUN <ADD/DEL> <nickrgx> <bodyrgx> [expire] <reason>`

Also supports the aliases `TS` and `TLINE`. =]

**Examples:**
* `TLINE add guest.+ h[o0]+m[o0]+ 0 nope` => All...
* `TEXTSHUN add guest.+ h[o0]+m[o0]+ nope` => ...of...
* `TS del guest.+ .+` => ...these add/delete the same T-Line, with no expiration
* `TLINE add guest.+ h[o0]+m[o0]+ 3600 ain't gonna happen` => Add a T-Line that expires in an hour =]
* `TLINE add guest.+ h[o0]+m[o0]+ 1h ain't gonna happen` => Ditto ;];]
* `TLINE` => Show all T-Lines
* `TS halp` => Show built-in halp

The nick regex is matched against both `nick!user@realhost` and `nick!user@vhost` masks.  
The timestring shit like `1h` supports up to weeks (so you **can't** do `1y`).
___
## topicgreeting
`X-Seti` asked for a module that changes a channel's topic to greet someone who just joined.  
This module implements channel mode `+g` so you can set that shit on a per-channel basis. =]

U-Lines such as NickServ or ButtServ channel bots won't trigger this of course. ;]
___
## uline_nickhost
You're probably familiar with the idea of having to do `/msg NickServ@services.my.net` as opposed to just `/msg NickServ`.  
This can be helpful to counter bots that try to auto-reg their nicks to join channels with `+R` set. ;]

Simply load the module and everyone will have to address **all** `U-Lines` with the above format.  
So if your services link's name is `baste.my.hams` you'll have to use `/msg NickServ@baste.my.hams`.  
This goes for both `PRIVMSG` and `NOTICE` (also includes `CTCP` as that's just `PRIVMSG` wrapped in special characters).
___
## unauthban
This one is created as an attempt of making behaviour of the +R chanmode more selective. It allows things like:

`~I:*!*@*.someisp.com` - lets users from someisp in only when they are registered - this is the particular target
of creating this module.

`~I:~q:~c:#channel` - allows users coming from #channel to talk only when they are registered.
___

## websocket_restrict
Some people may wanna impose some restrictions on websocket-connected users, one of deez ~~nuts~~ things could be limiting the ports they can use.  
Read more about Unreal's websocket support [right here](https://www.unrealircd.org/docs/WebSocket_support).

Since websocket support for non-SSL ports works right out of the box, you're going to need a new listen block for non-SSL connections from websocket users.  
I used port `8080` for the keks but you can really use as many as you want.

**Config block:**
```
listen {
	ip *;
	port 8080;
	options {
		clientsonly;
		websocket {
			// One of these must be specified, depending on your client
			type text; // Works with KiwiIRC websocket
			//type binary;
		}
	}
}
    
websocket_restrict {
	port 8080;
	//port 8443; // If you have an SSL listen block too
	zlinetime 60;
	channels {
		"#chan1"; // Don't forget the "" to make sure Unreal doesn't interpret it as a comment ;]
		"#moar";
	}
}
```
Now, if any **websocket users** connect to a port that **isn't** `8080` their IP will get `GZ-Lined` for the amount of seconds specified with `zlinetime` (defaults to 60).  
The other way around is true as well; regular clients connecting to a websocket port will be awarded the same `GZ-Line`.  
I originally had to do that for websocket users cuz they aren't fully online yet, so the special frames wouldn't be formed and sent.  
This resulted in the client sending the `GET /` command about 5 times and that resulted in 5 snotices too. =]  
I did the same for regular clients to remain consistent.

The `channels` list is optional; if omitted there will be no channel restrictions, otherwise they can only join the specified ones.  
They also only apply to websocket users, so regular clients can join their channels too.
___
## wwwstats

This one allows Unreal to cooperate with a web statistics system.  
This is the simpler version; see below for an extended module with MySQL support, unfortunately not installable with Unreal's module manager.  
Do NOT install them both.

A single interface is used: UNIX socket. The socket is created on a path specified in config block. When you connect to the socket, the module "spits out" all the current data in JSON format and closes. You can test it with the shell command `socat - UNIX-CONNECT:/tmp/wwwsocket.sock`. It can be used to generate channel lists, server lists, view user counts etc in realtime. Example data:
```json
{
	"clients": 19,
	"channels": 4,
	"operators": 18,
	"servers": 2,
	"messages": 1459,
	"serv": [{
		"name": "test1.example.com",
		"users": 2
	}],
	"chan": [{
		"name": "#help",
		"users": 1,
		"messages": 0
	}, {
		"name": "#services",
		"users": 8,
		"messages": 971
	}, {
		"name": "#opers",
		"users": 1,
		"messages": 0,
		"topic": "This channel has some topic"
	}, {
		"name": "#aszxcvbnm",
		"users": 2,
		"messages": 485
	}]
}
```
+p / +s channels are always ignored.

Message counters are not very precise, as the module counts only messages going through the server it is loaded on. That means that some channels at some time can not be counted.

The module looks for a config block:
```C
wwwstats {
	socket-path "/tmp/wwwstats.sock";	// this option is REQUIRED
};
```
___
