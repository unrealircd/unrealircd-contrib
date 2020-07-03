/* Copyright (C) All Rights Reserved
** Written by k4be, partially inspired by https://github.com/benmcollins/libjwt and https://github.com/TomzBench/jsmn-web-tokens
** Website: https://github.com/pirc-pl/unrealircd-modules/
** License: GPLv3 https://www.gnu.org/licenses/gpl-3.0.html
*/
 
 /*** <<<MODULE MANAGER START>>>
module
{
	documentation "https://github.com/pirc-pl/unrealircd-modules/blob/master/README.md#extjwt";
	troubleshooting "In case of problems, contact k4be on irc.pirc.pl.";
	min-unrealircd-version "5.*";
	post-install-text {
		"The module is installed. Now all you need to do is add a loadmodule line:";
		"loadmodule \"third/extjwt\";";
		"And create relevant config block, then /REHASH the IRCd.";
		"The configuration is described in the documentation:";
		"https://github.com/pirc-pl/unrealircd-modules/blob/master/README.md#extjwt";
	}
}
*** <<<MODULE MANAGER END>>>
*/

#include "unrealircd.h"

// internal definitions

#define MSG_EXTJWT	"EXTJWT"
#define MYCONF "extjwt"

#undef NEW_ISUPPORT

#define METHOD_NOT_SET 0
#define METHOD_HS256 1
#define METHOD_HS384 2
#define METHOD_HS512 3
#define METHOD_RS256 4
#define METHOD_RS384 5
#define METHOD_RS512 6
#define METHOD_ES256 7
#define METHOD_ES384 8
#define METHOD_ES512 9
#define METHOD_NONE 10

#define NEEDS_KEY(x) (x>=METHOD_RS256 && x<=METHOD_ES512)

#define URL_LENGTH 4096
#define MODES_SIZE 41 // about 10 mode chars
#define TS_LENGTH 19 // 64-bit integer
#define MAX_TOKEN_CHUNK (510-sizeof(extjwt_message_pattern)-HOSTLEN-CHANNELLEN)
#define PAYLOAD_CHAN_SIZE (sizeof(payload_chan_pattern)+CHANNELLEN+TS_LENGTH+MODES_SIZE)
#define PAYLOAD_SIZE (sizeof(payload_pattern_with_url)+sizeof(payload_chan_pattern)+TS_LENGTH+HOSTLEN+NICKLEN+NICKLEN+URL_LENGTH+MODES_SIZE+PAYLOAD_CHAN_SIZE)

// struct definitions

struct extjwt_config {
	time_t exp_delay;
	char *secret;
	int method;
	char *vfy;
};

struct jwt_service {
	char *name;
	struct extjwt_config *cfg;
	struct jwt_service *next;
};

// function declarations

CMD_FUNC(cmd_extjwt);
char *make_payload(Client *client, Channel *channel, struct extjwt_config *config);
char *generate_token(const char *payload, struct extjwt_config *config);
void b64url(char *b64);
unsigned char* hmac_hash(int method, const void *key, int keylen, const unsigned char *data, int datalen, unsigned int* resultlen);
unsigned char* sha_pem_hash(int method, const void *key, int keylen, const unsigned char *data, int datalen, unsigned int* resultlen);
unsigned char *hash(int method, const void *key, int keylen, const unsigned char *data, int datalen, unsigned int* resultlen);
const char *gen_header(int method);
int extjwt_configtest(ConfigFile *cf, ConfigEntry *ce, int type, int *errs);
int extjwt_configrun(ConfigFile *cf, ConfigEntry *ce, int type);
int extjwt_configposttest(int *errs);
void free_services(struct jwt_service **services);
struct jwt_service *find_jwt_service(struct jwt_service *services, const char *name);
int valid_integer_string(const char *in, int min, int max);
char *test_key(const char *file, int method);
char *read_file_contents(const char *file, int absolute, int *size);
int method_from_string(const char *in);
#ifdef NEW_ISUPPORT
char *extjwt_isupport_param(void);
#endif

// string constants

const char extjwt_message_pattern[] = ":%s EXTJWT %s %s %s%s";
const char payload_pattern[] = "{\"exp\":%lu,\"iss\":\"%s\",\"sub\":\"%s\",\"account\":\"%s\",\"umodes\":[%s]%s}";
const char payload_pattern_with_url[] = "{\"exp\":%lu,\"iss\":\"%s\",\"sub\":\"%s\",\"account\":\"%s\",\"vfy\":\"%s\",\"umodes\":[%s]%s}";
const char payload_chan_pattern[] = ",\"channel\":\"%s\",\"joined\":%lu,\"cmodes\":[%s]";

// global structs

ModuleHeader MOD_HEADER = {
	"third/extjwt",
	"5.0",
	"Command /EXTJWT (web service authorization)", 
	"k4be@PIRC",
	"unrealircd-5",
};

struct {
	int have_secret;
	int have_key;
	int have_method;
	int have_expire;
	int have_vfy;
	char *key_filename;
} cfg_state;

struct extjwt_config cfg;
struct jwt_service *jwt_services;

MOD_TEST(){
	memset(&cfg_state, 0, sizeof(cfg_state));
	HookAdd(modinfo->handle, HOOKTYPE_CONFIGTEST, 0, extjwt_configtest);
	HookAdd(modinfo->handle, HOOKTYPE_CONFIGPOSTTEST, 0, extjwt_configposttest);
	return MOD_SUCCESS;
}

MOD_INIT(){
	CommandAdd(modinfo->handle, MSG_EXTJWT, cmd_extjwt, 2, CMD_USER);
	HookAdd(modinfo->handle, HOOKTYPE_CONFIGRUN, 0, extjwt_configrun);
	return MOD_SUCCESS;
}

MOD_LOAD(){
	struct jwt_service *service = jwt_services;
#ifdef NEW_ISUPPORT
	ISupportAdd(modinfo->handle, "EXTJWT", extjwt_isupport_param());
#else
	ISupportAdd(modinfo->handle, "EXTJWT", "1");
#endif
	while(service){ // copy default exp to all services not having one specified
		if(service->cfg->exp_delay == 0)
			service->cfg->exp_delay = cfg.exp_delay;
		service = service->next;
	}
	return MOD_SUCCESS;
}

MOD_UNLOAD(){
	free_services(&jwt_services);
	return MOD_SUCCESS;
}

#ifdef NEW_ISUPPORT
char *extjwt_isupport_param(void){
	struct jwt_service *services = jwt_services;
	int count = 0;
	static char buf[500];
	strlcpy(buf, "V:1", sizeof(buf));
	while(services){
		strlcat(buf, count?",":"&S:", sizeof(buf));
		strlcat(buf, services->name, sizeof(buf));
		count++;
		services = services->next;
	}
	return buf;
}
#endif

void free_services(struct jwt_service **services){
	struct jwt_service *ss, *next;
	ss = *services;
	while(ss){
		next = ss->next;
		safe_free(ss->name);
		if(ss->cfg)
			safe_free(ss->cfg->secret);
		safe_free(ss->cfg);
		safe_free(ss);
		ss = next;
	}
	*services = NULL;
}

struct jwt_service *find_jwt_service(struct jwt_service *services, const char *name){
	if(!name)
		return NULL;
	while(services){
		if(services->name && !strcmp(services->name, name))
			return services;
		services = services->next;
	}
	return NULL;
}

int valid_integer_string(const char *in, int min, int max){
	int i, val;
	if(!in && !*in)
		return 0;
	for(i=0; in[i]; i++){
		if(!isdigit(in[i]))
			return 0;
	}
	val = atoi(in);
	if(val < min || val > max)
		return 0;
	return 1;
}

int vfy_url_is_valid(const char *string){
	if(strstr(string, "http://") == string || strstr(string, "https://") == string){
		if(strstr(string, "%s"))
			return 1;
	}
	return 0;
}

char *test_key(const char *file, int method){ // returns NULL when valid
	int fsize;
	char *fcontent = NULL;
	char *retval = NULL;
	BIO *bufkey = NULL;
	EVP_PKEY *pkey = NULL;
	int type, pkey_type;
	do {
		switch(method){
			case METHOD_RS256: case METHOD_RS384: case METHOD_RS512:
				type = EVP_PKEY_RSA;
				break;
			case METHOD_ES256: case METHOD_ES384: case METHOD_ES512:
				type = EVP_PKEY_EC;
				break;
			default:
				retval = "Internal error (invalid type)";
				continue;
		}
		fcontent = read_file_contents(file, 1, &fsize);
		if(!fcontent){
			retval = "Cannot open file";
			break;
		}
		if(fsize == 0){
			retval = "File is empty";
			break;
		}
		if(!(bufkey = BIO_new_mem_buf(fcontent, fsize))){
			retval = "Unknown error";
			break;
		}
		if(!(pkey = PEM_read_bio_PrivateKey(bufkey, NULL, NULL, NULL))){
			retval = "Key is invalid";
			break;
		}
		pkey_type = EVP_PKEY_id(pkey);
		if(type != pkey_type){
			retval = "Key does not match method";
			break;
		}
	} while(0);
	safe_free(fcontent);
	if(bufkey)
		BIO_free(bufkey);
	if(pkey)
		EVP_PKEY_free(pkey);
	return retval;
}

int method_from_string(const char *in){
	if(!strcmp(in, "HS256"))
		return METHOD_HS256;
	if(!strcmp(in, "HS384"))
		return METHOD_HS384;
	if(!strcmp(in, "HS512"))
		return METHOD_HS512;
	if(!strcmp(in, "RS256"))
		return METHOD_RS256;
	if(!strcmp(in, "RS384"))
		return METHOD_RS384;
	if(!strcmp(in, "RS512"))
		return METHOD_RS512;
	if(!strcmp(in, "ES256"))
		return METHOD_ES256;
	if(!strcmp(in, "ES384"))
		return METHOD_ES384;
	if(!strcmp(in, "ES512"))
		return METHOD_ES512;
	if(!strcmp(in, "NONE"))
		return METHOD_NONE;
	return METHOD_NOT_SET;
}

/*
config file stuff, based on Gottem's module

extjwt {
	method "HS256"; // must be one of: NONE (not recommended), HS256, HS384, HS512, ES256, ES384, ES512, RS256, RS384, RS512
	expire-after 30; // seconds
	secret "somepassword"; // do not set when using ES*, RS* or NONE method, required for HS* method
//	key "somefile.pem"; // do not set when using HS* or NONE method, required for ES* and RS* method
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
*/

int extjwt_configtest(ConfigFile *cf, ConfigEntry *ce, int type, int *errs){
	int errors = 0;
	ConfigEntry *cep, *cep2;
	int i;
	char *sfilename = NULL;
	struct jwt_service *services = NULL;
	struct jwt_service **ss = &services; // list for checking whether service names repeat
	int have_ssecret, have_smethod, have_svfy, have_scert;

	if (type != CONFIG_MAIN)
		return 0;

	if (!ce || strcmp(ce->ce_varname, MYCONF))
		return 0;

	for (cep = ce->ce_entries; cep; cep = cep->ce_next)
	{
		if (!cep->ce_vardata) {
			config_error("%s:%i: blank %s::%s without value", cep->ce_fileptr->cf_filename, cep->ce_varlinenum, MYCONF, cep->ce_varname);
			errors++;
			continue;
		}
		if (!strcmp(cep->ce_varname, "method")) {
			if(cfg_state.have_method){
				config_error("%s:%i: duplicate %s::%s item", cep->ce_fileptr->cf_filename, cep->ce_varlinenum, MYCONF, cep->ce_varname);
				errors++;
				continue;
			}
			cfg_state.have_method = method_from_string(cep->ce_vardata);
			if(cfg_state.have_method == METHOD_NOT_SET){
				config_error("%s:%i: invalid value %s::%s \"%s\" (check docs for allowed options)", cep->ce_fileptr->cf_filename, cep->ce_varlinenum, MYCONF, cep->ce_varname, cep->ce_vardata);
				errors++;
			}
			continue;
		}
		if (!strcmp(cep->ce_varname, "expire-after")) {
			// Should be an integer yo
			if(!valid_integer_string(cep->ce_vardata, 1, 9999)){
				config_error("%s:%i: %s::%s must be an integer between 1 and 9999 (seconds)", cep->ce_fileptr->cf_filename, cep->ce_varlinenum, MYCONF, cep->ce_varname);
				errors++; // Increment err0r count fam
			}
			continue;
		}
		if (!strcmp(cep->ce_varname, "secret")) {
			if(cfg_state.have_secret){
				config_error("%s:%i: duplicate %s::%s item", cep->ce_fileptr->cf_filename, cep->ce_varlinenum, MYCONF, cep->ce_varname);
				errors++;
				continue;
			}
			cfg_state.have_secret = 1;
			if(strlen(cep->ce_vardata) < 4){
				config_error("%s:%i: Secret specified in %s::%s is too short!", cep->ce_fileptr->cf_filename, cep->ce_varlinenum, MYCONF, cep->ce_varname);
				errors++; // Increment err0r count fam
			}
			continue;
		}
		if (!strcmp(cep->ce_varname, "key")) {
			if(cfg_state.have_key){
				config_error("%s:%i: duplicate %s::%s item", cep->ce_fileptr->cf_filename, cep->ce_varlinenum, MYCONF, cep->ce_varname);
				errors++;
				continue;
			}
			cfg_state.have_key = 1;
			safe_free(cfg_state.key_filename);
			safe_strdup(cfg_state.key_filename, cep->ce_vardata);
			convert_to_absolute_path(&cfg_state.key_filename, CONFDIR);
			if(access(cfg_state.key_filename, R_OK)){
				config_error("%s:%i: Cannot open file \"%s\" specified in %s::%s for reading", cep->ce_fileptr->cf_filename, cep->ce_varlinenum, cep->ce_vardata, MYCONF, cep->ce_varname);
				errors++; // Increment err0r count fam
			}
			continue;
		}
		if(!strcmp(cep->ce_varname, "verify-url")){
			if(cfg_state.have_vfy){
				config_error("%s:%i: duplicate %s:%s item", cep->ce_fileptr->cf_filename, cep->ce_varlinenum, MYCONF, cep->ce_varname);
				errors++;
				continue;
			}
			cfg_state.have_vfy = 1;
			if(!vfy_url_is_valid(cep->ce_vardata)){
				config_error("%s:%i: Optional URL specified in %s::%s is invalid!", cep->ce_fileptr->cf_filename, cep->ce_varlinenum, MYCONF, cep->ce_varname);
				errors++;
				continue;
			}
			if(strlen(cep->ce_vardata) > URL_LENGTH){
				config_error("%s:%i: Optional URL specified in %s::%s is too long!", cep->ce_fileptr->cf_filename, cep->ce_varlinenum, MYCONF, cep->ce_varname);
				errors++;
			}
			continue;
		}
		// Here comes a nested block =]
		if(!strcmp(cep->ce_varname, "service")) {
			have_ssecret = 0;
			have_smethod = 0;
			have_svfy = 0;
			have_scert = 0;
			if(strchr(cep->ce_vardata, ' ') || strchr(cep->ce_vardata, ',')){
				config_error("%s:%i: Invalid %s::%s name (contains spaces or commas)", cep->ce_fileptr->cf_filename, cep->ce_varlinenum, MYCONF, cep->ce_varname);
				errors++;
				continue;
			}
			if(find_jwt_service(services, cep->ce_vardata)){
				config_error("%s:%i: Duplicate %s::%s name \"%s\"", cep->ce_fileptr->cf_filename, cep->ce_varlinenum, MYCONF, cep->ce_varname, cep->ce_vardata);
				errors++;
				continue;
			}
			*ss = safe_alloc(sizeof(struct jwt_service)); // store the new name for further checking
			safe_strdup((*ss)->name, cep->ce_vardata);
			ss = &(*ss)->next;
			for(cep2 = cep->ce_entries; cep2; cep2 = cep2->ce_next) {
				if(!cep2->ce_varname || !cep2->ce_vardata || !cep2->ce_vardata[0]) {
					config_error("%s:%i: blank/incomplete %s::service entry", cep2->ce_fileptr->cf_filename, cep2->ce_varlinenum, MYCONF); // Rep0t error
					errors++; // Increment err0r count fam
					continue; // Next iteration imo tbh
				}

				if (!strcmp(cep2->ce_varname, "method")) {
					if(have_smethod){
						config_error("%s:%i: duplicate %s::service::%s item", cep2->ce_fileptr->cf_filename, cep2->ce_varlinenum, MYCONF, cep2->ce_varname);
						errors++;
						continue;
					}
					have_smethod = method_from_string(cep2->ce_vardata);
					if(have_smethod == METHOD_NOT_SET || have_smethod == METHOD_NONE){
						config_error("%s:%i: invalid value of optional %s::service::%s \"%s\" (check docs for allowed options)", cep2->ce_fileptr->cf_filename, cep2->ce_varlinenum, MYCONF, cep2->ce_varname, cep2->ce_vardata);
						errors++;
					}
					continue;
				}

				if(!strcmp(cep2->ce_varname, "secret")) {
					if(have_ssecret){
						config_error("%s:%i: duplicate %s::service::%s item", cep2->ce_fileptr->cf_filename, cep2->ce_varlinenum, MYCONF, cep2->ce_varname);
						errors++;
						continue;
					}
					have_ssecret = 1;
					if(strlen(cep2->ce_vardata) < 4) {
						config_error("%s:%i: Secret specified in %s::service::%s is too short!", cep2->ce_fileptr->cf_filename, cep2->ce_varlinenum, MYCONF, cep2->ce_varname); // Rep0t error
						errors++;
					}
					continue;
				}

				if (!strcmp(cep2->ce_varname, "key")) {
					if(have_scert){
						config_error("%s:%i: duplicate %s::service::%s item", cep2->ce_fileptr->cf_filename, cep2->ce_varlinenum, MYCONF, cep2->ce_varname);
						errors++;
						continue;
					}
					have_scert = 1;
					safe_free(sfilename);
					safe_strdup(sfilename, cep2->ce_vardata);
					convert_to_absolute_path(&sfilename, CONFDIR);
					if(access(sfilename, R_OK)){
						config_error("%s:%i: Cannot open file \"%s\" specified in %s::service::%s for reading", cep2->ce_fileptr->cf_filename, cep2->ce_varlinenum, cep2->ce_vardata, MYCONF, cep2->ce_varname);
						errors++; // Increment err0r count fam
					}
					continue;
				}

				if(!strcmp(cep2->ce_varname, "expire-after")){
					if(!valid_integer_string(cep2->ce_vardata, 1, 9999)){
						config_error("%s:%i: %s::%s must be an integer between 1 and 9999 (seconds)", cep2->ce_fileptr->cf_filename, cep2->ce_varlinenum, MYCONF, cep2->ce_varname);
						errors++; // Increment err0r count fam
					}
					continue;
				}

				if(!strcmp(cep2->ce_varname, "verify-url")){
					if(have_svfy){
						config_error("%s:%i: duplicate %s::service::%s item", cep2->ce_fileptr->cf_filename, cep2->ce_varlinenum, MYCONF, cep2->ce_varname);
						errors++;
						continue;
					}
					have_svfy = 1;
					if(!vfy_url_is_valid(cep2->ce_vardata)){
						config_error("%s:%i: Optional URL specified in %s::service::%s is invalid!", cep2->ce_fileptr->cf_filename, cep2->ce_varlinenum, MYCONF, cep2->ce_varname);
						errors++;
						continue;
					}
					if(strlen(cep2->ce_vardata) > URL_LENGTH){
						config_error("%s:%i: Optional URL specified in %s::service::%s is too long!", cep2->ce_fileptr->cf_filename, cep2->ce_varlinenum, MYCONF, cep2->ce_varname);
						errors++;
					}
					continue;
				}

				config_error("%s:%i: invalid %s::service attribute %s (must be one of: name, secret, expire-after)", cep2->ce_fileptr->cf_filename, cep2->ce_varlinenum, MYCONF, cep2->ce_varname); // Rep0t error
				errors++; // Increment err0r count fam
			}
			if(!have_smethod){
				config_error("%s:%i: invalid %s::service entry (no %s::service::method specfied)", cep->ce_fileptr->cf_filename, cep->ce_varlinenum, MYCONF, MYCONF);
				errors++;
				continue;
			}
			if(have_ssecret && NEEDS_KEY(have_smethod)) {
				config_error("%s:%i: invalid %s::service entry (this method needs %s::service::key and not %s::service::secret option)", cep->ce_fileptr->cf_filename, cep->ce_varlinenum, MYCONF, MYCONF, MYCONF); // Rep0t error
				errors++;
				continue;
			}
			if(have_scert && !NEEDS_KEY(have_smethod)) {
				config_error("%s:%i: invalid %s::service entry (this method needs %s::service::secret and not %s::service::key option)", cep->ce_fileptr->cf_filename, cep->ce_varlinenum, MYCONF, MYCONF, MYCONF); // Rep0t error
				errors++;
				continue;
			}
			if(!have_ssecret && !NEEDS_KEY(have_smethod)) {
				config_error("%s:%i: invalid %s::service entry (must contain %s::service::secret option)", cep->ce_fileptr->cf_filename, cep->ce_varlinenum, MYCONF, MYCONF); // Rep0t error
				errors++;
				continue;
			}
			if(!have_scert && NEEDS_KEY(have_smethod)) {
				config_error("%s:%i: invalid %s::service entry (must contain %s::service::key option)", cep->ce_fileptr->cf_filename, cep->ce_varlinenum, MYCONF, MYCONF); // Rep0t error
				errors++;
				continue;
			}
			if(NEEDS_KEY(have_smethod) && have_scert && sfilename){
				char *keyerr;
				keyerr = test_key(sfilename, have_smethod);
				if(keyerr){
					config_error("Invalid key file specified for %s::key: %s", MYCONF, keyerr);
					errors++;
				}
			}
			continue;
		}
		config_error("%s:%i: unknown directive %s::%s", cep->ce_fileptr->cf_filename, cep->ce_varlinenum, MYCONF, cep->ce_varname);
		errors++;
	}
	*errs = errors;
	free_services(&services);
	if(errors){
		safe_free(cfg_state.key_filename);
	}
	return errors ? -1 : 1;
}

int extjwt_configposttest(int *errs) {
	int errors = 0;
	if(cfg_state.have_method == METHOD_NOT_SET){
		config_error("No %s::method specfied!", MYCONF);
		errors++;
	} else {
		if(cfg_state.have_method != METHOD_NONE && !NEEDS_KEY(cfg_state.have_method) && !cfg_state.have_secret){
			config_error("No %s::secret specfied as required by requested method!", MYCONF);
			errors++;
		}
		if((cfg_state.have_method == METHOD_NONE || NEEDS_KEY(cfg_state.have_method)) && cfg_state.have_secret){
			config_error("A %s::secret specfied but it should not be when using requested method!", MYCONF);
			errors++;
		}
		if(NEEDS_KEY(cfg_state.have_method) && !cfg_state.have_key){
			config_error("No %s::key specfied as required by requested method!", MYCONF);
			errors++;
		}
		if(!NEEDS_KEY(cfg_state.have_method) && cfg_state.have_key){
			config_error("A %s::key specfied but it should not be when using requested method!", MYCONF);
			errors++;
		}
		if(NEEDS_KEY(cfg_state.have_method) && cfg_state.have_key && cfg_state.key_filename){
			char *keyerr;
			keyerr = test_key(cfg_state.key_filename, cfg_state.have_method);
			if(keyerr){
				config_error("Invalid key file specified for %s::key: %s", MYCONF, keyerr);
				errors++;
			}
		}
	}
	safe_free(cfg_state.key_filename);
	if(errors){
		*errs = errors;
		return -1;
	}
	// setting defaults, FIXME this may behave incorrectly if there's another module failing POSTTEST
	if(!cfg_state.have_expire)
		cfg.exp_delay = 30;
	// prepare service list to load new data
	free_services(&jwt_services);
	return 1;
}

int extjwt_configrun(ConfigFile *cf, ConfigEntry *ce, int type){ // actually use the new configuration data
	ConfigEntry *cep, *cep2;
	struct jwt_service **ss = &jwt_services;
	if(*ss)
		ss = &((*ss)->next);

	if (type != CONFIG_MAIN)
		return 0;

	if (!ce || strcmp(ce->ce_varname, MYCONF))
		return 0;

	for (cep = ce->ce_entries; cep; cep = cep->ce_next){
		if(!strcmp(cep->ce_varname, "method")){
			cfg.method = method_from_string(cep->ce_vardata);
			continue;
		}
		if(!strcmp(cep->ce_varname, "expire-after")){
			cfg.exp_delay = atoi(cep->ce_vardata);
			continue;
		}
		if(!strcmp(cep->ce_varname, "secret")){
			cfg.secret = strdup(cep->ce_vardata);
			continue;
		}
		if(!strcmp(cep->ce_varname, "key")){
			cfg.secret = read_file_contents(cep->ce_vardata, 0, NULL);
			continue;
		}
		if(!strcmp(cep->ce_varname, "verify-url")){
			cfg.vfy = strdup(cep->ce_vardata);
			continue;
		}
		if(!strcmp(cep->ce_varname, "service")){ // nested block
			*ss = safe_alloc(sizeof(struct jwt_service));
			(*ss)->cfg = safe_alloc(sizeof(struct extjwt_config));
			safe_strdup((*ss)->name, cep->ce_vardata); // copy the service name
			for (cep2 = cep->ce_entries; cep2; cep2 = cep2->ce_next){
				if(!strcmp(cep2->ce_varname, "method")){
					(*ss)->cfg->method = method_from_string(cep2->ce_vardata);
					continue;
				}
				if(!strcmp(cep2->ce_varname, "expire-after")){
					(*ss)->cfg->exp_delay = atoi(cep2->ce_vardata);
					continue;
				}
				if(!strcmp(cep2->ce_varname, "secret")){
					(*ss)->cfg->secret = strdup(cep2->ce_vardata);
					continue;
				}
				if(!strcmp(cep2->ce_varname, "key")){
					(*ss)->cfg->secret = read_file_contents(cep2->ce_vardata, 0, NULL);
					continue;
				}
				if(!strcmp(cep2->ce_varname, "verify-url")){
					(*ss)->cfg->vfy = strdup(cep2->ce_vardata);
					continue;
				}
			}
			ss = &((*ss)->next);
		}
	}
	return 1;
}

char *read_file_contents(const char *file, int absolute, int *size){
	FILE *f = NULL;
	int fsize;
	char *filename = NULL;
	char *buf = NULL;
	safe_strdup(filename, file);
	do {
		if(!absolute)
			convert_to_absolute_path(&filename, CONFDIR);
		f = fopen(filename, "rb");
		if(!f)
			break;
		fseek(f, 0, SEEK_END);
		fsize = ftell(f);
		fseek(f, 0, SEEK_SET);
		buf = safe_alloc(fsize + 1);
		fsize = fread(buf, 1, fsize, f);
		buf[fsize] = '\0';
		if(size)
			*size = fsize;
		fclose(f);
	} while(0);
	safe_free(filename);
	if(!buf && size)
		*size = 0;
	return buf;
}

CMD_FUNC(cmd_extjwt){
	Channel *channel;
	char *payload;
	char *token, *full_token;
	struct jwt_service *service = NULL;
	struct extjwt_config *config;
	int last = 0;
	char message[MAX_TOKEN_CHUNK+1];
	if(parc < 2 || BadPtr(parv[1])){
		sendnumeric(client, ERR_NEEDMOREPARAMS, MSG_EXTJWT);
		return;
	}
	if(parv[1][0] == '*' && parv[1][1] == '\0'){
		channel = NULL; // not linked to a channel
	} else {
		channel = find_channel(parv[1], NULL);
		if(!channel){
			sendnumeric(client, ERR_NOSUCHNICK, parv[1]);
			return;
		}
	}
	if(parc > 2 && !BadPtr(parv[2])){
		service = find_jwt_service(jwt_services, parv[2]);
		if(!service){
			sendto_one(client, NULL, ":%s FAIL %s NO_SUCH_SERVICE :No such service", me.name, MSG_EXTJWT);
			return;
		}
	}
	if(service){
		config = service->cfg; // service config
	} else {
		config = &cfg; // default config
	}
	if(!(payload = make_payload(client, channel, config)) || !(full_token = generate_token(payload, config))){
		sendto_one(client, NULL, ":%s FAIL %s UNKNOWN_ERROR :Failed to generate token", me.name, MSG_EXTJWT);
		return;
	}
	token = full_token;
	do {
		if(strlen(token) <= MAX_TOKEN_CHUNK){ // the remaining data (or whole token) will fit a single irc message
			last = 1;
			strcpy(message, token);
		} else { // send a chunk and shift buffer
			strlcpy(message, token, MAX_TOKEN_CHUNK+1);
			token += MAX_TOKEN_CHUNK;
		}
		sendto_one(client, NULL, extjwt_message_pattern, me.name, parv[1], "*", last?"":"* ", message);
	} while(!last);
	safe_free(full_token);
}

char *make_payload(Client *client, Channel *channel, struct extjwt_config *config){
	static char payload[PAYLOAD_SIZE];
	char payload_channel[PAYLOAD_CHAN_SIZE];
	char modes[MODES_SIZE] = "";
	Membership *lp;
	int array_empty;
	if(!IsUser(client))
		return NULL;
	if(channel){ // fill in channel information and user flags
		lp = find_membership_link(client->user->channel, channel);
		if(lp){
			array_empty = 1;
			if(lp->flags & CHFL_VOICE){
				strlcat(modes, "\"v\"", MODES_SIZE);
				array_empty = 0;
			}
			if(lp->flags & CHFL_HALFOP){
				if(!array_empty)
					strlcat(modes, ",", MODES_SIZE);
				strlcat(modes, "\"h\"", MODES_SIZE);
				array_empty = 0;
			}
			if(lp->flags & CHFL_CHANOP){
				if(!array_empty)
					strlcat(modes, ",", MODES_SIZE);
				strlcat(modes, "\"o\"", MODES_SIZE);
				array_empty = 0;
			}
#ifdef PREFIX_AQ
			if(lp->flags & CHFL_CHANADMIN){
				if(!array_empty)
					strlcat(modes, ",", MODES_SIZE);
				strlcat(modes, "\"a\"", MODES_SIZE);
				array_empty = 0;
			}
			if(lp->flags & CHFL_CHANOWNER){
				if(!array_empty)
					strlcat(modes, ",", MODES_SIZE);
				strlcat(modes, "\"q\"", MODES_SIZE);
				array_empty = 0;
			}
#endif
		}
		snprintf(payload_channel, PAYLOAD_CHAN_SIZE, payload_chan_pattern, channel->chname, (long unsigned int)(IsMember(client, channel)?1:0), modes);
	} else {
		payload_channel[0] = '\0';
	}
	modes[0] = '\0';
	if(IsOper(client)){ // add "o" ircop flag
		strcpy(modes, "\"o\"");
	}
	if(config->vfy){ // also add the URL
		snprintf(payload, PAYLOAD_SIZE, payload_pattern_with_url, TStime()+config->exp_delay, me.name, client->name, (client->user->svid[0]=='0')?"":client->user->svid, config->vfy, modes, payload_channel);
	} else {
		snprintf(payload, PAYLOAD_SIZE, payload_pattern, TStime()+config->exp_delay, me.name, client->name, (client->user->svid[0]=='0')?"":client->user->svid, modes, payload_channel);
	}
	return payload;
}

void b64url(char *b64){ // convert base64 to base64-url
	while(*b64){
		if(*b64 == '+')
			*b64 = '-';
		if(*b64 == '/')
			*b64 = '_';
		if(*b64 == '='){
			*b64 = '\0';
			return;
		}
		b64++;
	}
}

unsigned char *hash(int method, const void *key, int keylen, const unsigned char *data, int datalen, unsigned int* resultlen){
	switch(method){
		case METHOD_HS256: case METHOD_HS384: case METHOD_HS512:
			return hmac_hash(method, key, keylen, data, datalen, resultlen);
		case METHOD_RS256: case METHOD_RS384: case METHOD_RS512: case METHOD_ES256: case METHOD_ES384: case METHOD_ES512:
			return sha_pem_hash(method, key, keylen, data, datalen, resultlen);
	}
	return NULL;
}

unsigned char* sha_pem_hash(int method, const void *key, int keylen, const unsigned char *data, int datalen, unsigned int* resultlen){
	EVP_MD_CTX *mdctx = NULL;
	ECDSA_SIG *ec_sig = NULL;
	const BIGNUM *ec_sig_r = NULL;
	const BIGNUM *ec_sig_s = NULL;
	BIO *bufkey = NULL;
	const EVP_MD *alg;
	int type;
	EVP_PKEY *pkey = NULL;
	int pkey_type;
	unsigned char *sig = NULL;
	int ret = 0;
	size_t slen;
	char *retval = NULL;
	char *output = NULL;
	char *sig_ptr;

	do {
		switch(method){
			case METHOD_RS256:
				alg = EVP_sha256();
				type = EVP_PKEY_RSA;
				break;
			case METHOD_RS384:
				alg = EVP_sha384();
				type = EVP_PKEY_RSA;
				break;
			case METHOD_RS512:
				alg = EVP_sha512();
				type = EVP_PKEY_RSA;
				break;
			case METHOD_ES256:
				alg = EVP_sha256();
				type = EVP_PKEY_EC;
				break;
			case METHOD_ES384:
				alg = EVP_sha384();
				type = EVP_PKEY_EC;
				break;
			case METHOD_ES512:
				alg = EVP_sha512();
				type = EVP_PKEY_EC;
				break;
			default:
				continue;
		}

		if(!(bufkey = BIO_new_mem_buf(key, keylen)))
			break; // out of memory
		if(!(pkey = PEM_read_bio_PrivateKey(bufkey, NULL, NULL, NULL)))
			break; // invalid key?
		pkey_type = EVP_PKEY_id(pkey);
		if(type != pkey_type)
			break; // invalid key type
		if(!(mdctx = EVP_MD_CTX_create()))
			break; // out of memory
		if(EVP_DigestSignInit(mdctx, NULL, alg, NULL, pkey) != 1)
			break; // initialize error
		if(EVP_DigestSignUpdate(mdctx, data, datalen) != 1)
			break; // signing error
		if(EVP_DigestSignFinal(mdctx, NULL, &slen) != 1) // get required buffer length
			break;
		sig = safe_alloc(slen);
		if(EVP_DigestSignFinal(mdctx, sig, &slen) != 1)
			break;
		if(pkey_type != EVP_PKEY_EC){
			*resultlen = slen;
			output = safe_alloc(slen);
			memcpy(output, sig, slen);
			retval = output;
		} else {
			unsigned int degree, bn_len, r_len, s_len, buf_len;
			unsigned char *raw_buf = NULL;
			EC_KEY *ec_key;
			if(!(ec_key = EVP_PKEY_get1_EC_KEY(pkey)))
				break; // out of memory
			degree = EC_GROUP_get_degree(EC_KEY_get0_group(ec_key));
			EC_KEY_free(ec_key);
			sig_ptr = sig;
			if(!(ec_sig = d2i_ECDSA_SIG(NULL, (const unsigned char **)&sig_ptr, slen)))
				break; // out of memory
			ECDSA_SIG_get0(ec_sig, &ec_sig_r, &ec_sig_s);
			r_len = BN_num_bytes(ec_sig_r);
			s_len = BN_num_bytes(ec_sig_s);
			bn_len = (degree+7)/8;
			if(r_len>bn_len || s_len > bn_len)
				break;
			buf_len = bn_len*2;
			raw_buf = safe_alloc(buf_len);
			BN_bn2bin(ec_sig_r, raw_buf+bn_len-r_len);
			BN_bn2bin(ec_sig_s, raw_buf+buf_len-s_len);
			output = safe_alloc(buf_len);
			*resultlen = buf_len;
			memcpy(output, raw_buf, buf_len);
			retval = output;
			safe_free(raw_buf);
		}
	} while(0);

	if(bufkey)
		BIO_free(bufkey);
	if(pkey)
		EVP_PKEY_free(pkey);
	if(mdctx)
		EVP_MD_CTX_destroy(mdctx);
	if(ec_sig)
		ECDSA_SIG_free(ec_sig);
	safe_free(sig);
	return retval;
}

unsigned char* hmac_hash(int method, const void *key, int keylen, const unsigned char *data, int datalen, unsigned int* resultlen){
	const EVP_MD* typ;
	char *hmac = safe_alloc(EVP_MAX_MD_SIZE);
	switch(method){
		default:
		case METHOD_HS256:
			typ = EVP_sha256();
			break;
		case METHOD_HS384:
			typ = EVP_sha384();
			break;
		case METHOD_HS512:
			typ = EVP_sha512();
			break;
	}
	if(HMAC(typ, key, keylen, data, datalen, hmac, resultlen)){ // openssl call
		return hmac;
	} else {
		safe_free(hmac);
		return NULL;
	}
}

const char *gen_header(int method){ // returns header json
	switch(method){
		default:
		case METHOD_HS256:
			return "{\"alg\":\"HS256\",\"typ\":\"JWT\"}";
		case METHOD_HS384:
			return "{\"alg\":\"HS384\",\"typ\":\"JWT\"}";
		case METHOD_HS512:
			return "{\"alg\":\"HS512\",\"typ\":\"JWT\"}";
		case METHOD_RS256:
			return "{\"alg\":\"RS256\",\"typ\":\"JWT\"}";
		case METHOD_RS384:
			return "{\"alg\":\"RS384\",\"typ\":\"JWT\"}";
		case METHOD_RS512:
			return "{\"alg\":\"RS512\",\"typ\":\"JWT\"}";
		case METHOD_ES256:
			return "{\"alg\":\"ES256\",\"typ\":\"JWT\"}";
		case METHOD_ES384:
			return "{\"alg\":\"ES384\",\"typ\":\"JWT\"}";
		case METHOD_ES512:
			return "{\"alg\":\"ES512\",\"typ\":\"JWT\"}";
		case METHOD_NONE:
			return "{\"alg\":\"none\",\"typ\":\"JWT\"}";
	}
}

char *generate_token(const char *payload, struct extjwt_config *config){
	const char *header = gen_header(config->method);
	size_t b64header_size = strlen(header)*4/3 + 8; // base64 has 4/3 overhead
	size_t b64payload_size = strlen(payload)*4/3 + 8;
	size_t b64sig_size = 4096*4/3 + 8;
	size_t b64data_size = b64header_size + b64payload_size + b64sig_size + 4;
	char *b64header = safe_alloc(b64header_size);
	char *b64payload = safe_alloc(b64payload_size);
	char *b64sig = safe_alloc(b64sig_size);
	char *b64data = safe_alloc(b64data_size);
	unsigned int hashsize;
	char *hash_val = NULL;
	char *retval = NULL;
	b64_encode(header, strlen(header), b64header, b64header_size);
	b64_encode(payload, strlen(payload), b64payload, b64payload_size);
	b64url(b64header);
	b64url(b64payload);
	snprintf(b64data, b64data_size, "%s.%s", b64header, b64payload); // generate first part of the token
	if(config->method != METHOD_NONE){
		hash_val = hash(config->method, config->secret, strlen(config->secret), b64data, strlen(b64data), &hashsize); // calculate the signature hash
		if(hash_val){
			b64_encode(hash_val, hashsize, b64sig, b64sig_size);
			b64url(b64sig);
			strlcat(b64data, ".", b64data_size); // append signature hash to token
			strlcat(b64data, b64sig, b64data_size);
			retval = b64data;
		}
	} else {
		retval = b64data;
	}
	safe_free(b64header);
	safe_free(b64payload);
	if(config->method != METHOD_NONE){
		safe_free(b64sig);
		safe_free(hash_val);
	}

	if(retval != b64data){
		safe_free(b64data);
	}

	return retval;
}

