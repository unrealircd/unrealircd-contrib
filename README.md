This is the unrealircd-contrib repository. It contains 3rd party modules for UnrealIRCd.

## Use at your own risk

These third party modules are **not** written by the UnrealIRCd team and not tested by us.
Use these modules at your own risk. **In case of problems, contact
the author of the module.**

## For end-users
To view a list of available modules online, go to https://modules.unrealircd.org/

On the command line you can use the [UnrealIRCd 5 module manager](https://www.unrealircd.org/docs/Module_manager)
to list and (un)install modules, eg:
* ```./unrealircd module list``` - to list all modules
* ```./unrealircd module info third/something``` - to show all information about a specific module
* ```./unrealircd module install third/something``` - to install the specified module

## Bugs, fixes and enhancements
If you find a bug or would like to submit a fix or enhancement in a module, then
you must contact the author/maintainer of the module.
The [UnrealIRCd 5 modules forum](https://forums.unrealircd.org/viewforum.php?f=54)
may also be a useful place to visit.

**Do NOT contact the UnrealIRCd team and do not file a PR.**
We are not the correct person to judge if a fix or enhancement is good or bad.
After all, the UnrealIRCd team does not maintain or even test the functionality of the module,
that responsibility is delegated to the module maintainer.
Pull Requests (PRs) for existing modules are only accepted from the maintainer of that module.

Only if you do not receive a response from the module author and there are grave
issues then you can contact the UnrealIRCd team for removal of the module or
change of maintainership.

## For module coders
If you are a module coder and want to add your module to this repository
as well, then read the rules and procedure at:
https://www.unrealircd.org/docs/Rules_for_3rd_party_modules_in_unrealircd-contrib
