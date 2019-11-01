/** third/examplemod
 * (C) Copyright 2019 Bram Matthys ("Syzop")
 * License: GPLv2
 */
#include "unrealircd.h"

ModuleHeader MOD_HEADER
  = {
	"third/examplemod", /* name */
	"1.0.0", /* version */
	"This is a simple module test", /* description */
	"Bram Matthys (Syzop)", /* author */
	"unrealircd-5",
};

MOD_INIT()
{
	return MOD_SUCCESS;
}

/*** <<<MODULE MANAGER START>>>
module
{
	// THE FOLLOWING FIELDS ARE MANDATORY:

	// Documentation, as displayed in './unrealircd module info nameofmodule', and possibly at other places:
	documentation "https://www.unrealircd.org/docs/";

	// This is displayed in './unrealircd module info ..' and also if compilation of the module fails:
	troubleshooting "In case of problems, check the FAQ at ... or e-mail me at ...";

	// Minimum version necessary for this module to work:
	min-unrealircd-version "5.*";

	// THE FOLLOWING FIELDS ARE OPTIONAL:

	// Maximum version that this module supports:
	max-unrealircd-version "5.*";

	// This text is displayed after running './unrealircd module install ...'
	// It is recommended not to make this an insane number of lines and refer to a URL instead
	// if you have lots of text/information to share:
	post-install-text {
		"The module is installed. Now all you need to do is add a loadmodule line:";
		"loadmodule \"third/examplemod\";";
		"And /REHASH the IRCd.";
		"The module does not need any other configuration.";
	}
}
*** <<<MODULE MANAGER END>>>
*/
