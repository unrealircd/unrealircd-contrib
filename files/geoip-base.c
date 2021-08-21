/* Copyright (C) All Rights Reserved
** Written by k4be
** Website: https://github.com/pirc-pl/unrealircd-modules/
** License: GPLv3 https://www.gnu.org/licenses/gpl-3.0.html
*/

/*** <<<MODULE MANAGER START>>>
module
{
        documentation "https://github.com/pirc-pl/unrealircd-modules/blob/master/README.md#geoip-base";
        troubleshooting "In case of problems, contact k4be on irc.pirc.pl.";
        min-unrealircd-version "5.*";
        post-install-text {
                "The module is installed. Now you need to copy necessary files to your conf/ directory";
                "(see docs on how to obtain these), and add a loadmodule line:";
                "loadmodule \"third/geoip-base\";";
  				"And /REHASH the IRCd.";
				"Remember that you need other \"geoip\" module to make a real use of this one.";
				"DON'T load the \"geoip-transfer\" module on this server.";
				"Detailed documentation is available on https://github.com/pirc-pl/unrealircd-modules/blob/master/README.md#geoip-base";
        }
}
*** <<<MODULE MANAGER END>>>
*/

#include "unrealircd.h"

#define BUFLEN 8191

// default data file paths; place them in conf directory
#define IPv4PATH "GeoLite2-Country-Blocks-IPv4.csv"
#define COUNTRIESPATH "GeoLite2-Country-Locations-en.csv"
#define IPv6PATH "GeoLite2-Country-Blocks-IPv6.csv"

/*
// Config example:
geoip {
	ipv4-blocks-file "GeoLite2-Country-Blocks-IPv4.csv";
	ipv6-blocks-file "GeoLite2-Country-Blocks-IPv6.csv";
	countries-file "GeoLite2-Country-Locations-en.csv";
};
*/

#define MYCONF "geoip"

// suggested by Gottem to allow Windows compilation
#ifndef R_OK
#define R_OK 4
#endif

struct ip_range {
	uint32_t addr;
	uint32_t mask;
	int geoid;
	struct ip_range *next;
};

struct ip6_range {
	uint16_t addr[8];
	uint16_t mask[8];
	int geoid;
	struct ip6_range *next;
};

struct country {
	char code[10];
	char name[100];
	char continent[25];
	int id;
	struct country *next;
};

struct ip_range *ip_range_list[256]; // we are keeping a separate list for each possible first octet to speed up searching
struct ip6_range *ip6_range_list = NULL; // for ipv6 there would be too many separate lists so just use a single one
struct country *country_list = NULL;
int have_config = 0;
ModDataInfo *geoipMD;

// function declarations here
static int geoip_userconnect(Client *);
static void free_ipv4(void);
static void free_ipv6(void);
static void free_countries(void);
static void free_all(void);
int hexval(char c);
static int read_ipv4(char *file);
static int ip6_convert(char *ip, uint16_t out[8]);
static int read_ipv6(char *file);
static int read_countries(char *file);
static struct country *get_country(int id);
static int get_v4_geoid(char *iip);
static int get_v6_geoid(char *iip);
static struct country *get_country_by_ip(char *iip);
int geoip_base_configtest(ConfigFile *cf, ConfigEntry *ce, int type, int *errs);
int geoip_base_configposttest(int *errs);
int geoip_base_configrun(ConfigFile *cf, ConfigEntry *ce, int type);
void geoip_moddata_free(ModData *m);
char *geoip_moddata_serialize(ModData *m);
void geoip_moddata_unserialize(char *str, ModData *m);
CMD_FUNC(cmd_geocheck);

ModuleHeader MOD_HEADER = {
	"third/geoip-base",   /* Name of module */
	"5.0.5", /* Version */
	"GeoIP data provider module", /* Short description of module */
	"k4be@PIRC",
	"unrealircd-5"
};
	
// config file stuff, based on Gottem's module
int geoip_base_configtest(ConfigFile *cf, ConfigEntry *ce, int type, int *errs) {
	ConfigEntry *cep; // For looping through our bl0cc
	int errors = 0; // Error count
	int i; // iter8or m8
	int have_blocks_file = 0;
	int have_countries_file = 0;

	// Since we'll add a new top-level block to unrealircd.conf, need to filter on CONFIG_MAIN lmao
	if(type != CONFIG_MAIN)
		return 0; // Returning 0 means idgaf bout dis

	// Check for valid config entries first
	if(!ce || !ce->ce_varname)
		return 0;

	// If it isn't our bl0ck, idc
	if(strcmp(ce->ce_varname, MYCONF))
		return 0;

	have_config = 1;

	// Loop dat shyte fam
	for(cep = ce->ce_entries; cep; cep = cep->ce_next) {
		// Do we even have a valid name l0l?
		if(!cep->ce_varname) {
			config_error("%s:%i: blank %s item", cep->ce_fileptr->cf_filename, cep->ce_varlinenum, MYCONF); // Rep0t error
			errors++; // Increment err0r count fam
			continue; // Next iteration imo tbh
		}

		if(!strcmp(cep->ce_varname, "ipv4-blocks-file")) {
			if(!cep->ce_vardata) {
				config_error("%s:%i: %s::%s must be a filename", cep->ce_fileptr->cf_filename, cep->ce_varlinenum, MYCONF, cep->ce_varname);
				errors++; // Increment err0r count fam
				continue;
			}
			char *filename = strdup(cep->ce_vardata);
			convert_to_absolute_path(&filename, CONFDIR);
			if(access(filename, R_OK)){
				config_error("%s:%i: %s::%s: cannot open file \"%s\" for reading", cep->ce_fileptr->cf_filename, cep->ce_varlinenum, MYCONF, cep->ce_varname, cep->ce_vardata);
				errors++;
				safe_free(filename);
				continue;
			}
			safe_free(filename);
			have_blocks_file = 1;
			continue;
		}

		if(!strcmp(cep->ce_varname, "ipv6-blocks-file")) {
			if(!cep->ce_vardata) {
				config_error("%s:%i: %s::%s must be a filename", cep->ce_fileptr->cf_filename, cep->ce_varlinenum, MYCONF, cep->ce_varname);
				errors++; // Increment err0r count fam
				continue;
			}
			char *filename = strdup(cep->ce_vardata);
			convert_to_absolute_path(&filename, CONFDIR);
			if(access(filename, R_OK)){
				config_error("%s:%i: %s::%s: cannot open file \"%s\" for reading", cep->ce_fileptr->cf_filename, cep->ce_varlinenum, MYCONF, cep->ce_varname, cep->ce_vardata);
				errors++;
				safe_free(filename);
				continue;
			}
			safe_free(filename);
			have_blocks_file = 1;
			continue;
		}

		if(!strcmp(cep->ce_varname, "countries-file")) {
			if(!cep->ce_vardata) {
				config_error("%s:%i: %s::%s must be a filename", cep->ce_fileptr->cf_filename, cep->ce_varlinenum, MYCONF, cep->ce_varname);
				errors++; // Increment err0r count fam
				continue;
			}
			char *filename = strdup(cep->ce_vardata);
			convert_to_absolute_path(&filename, CONFDIR);
			if(access(filename, R_OK)){
				config_error("%s:%i: %s::%s: cannot open file \"%s\" for reading", cep->ce_fileptr->cf_filename, cep->ce_varlinenum, MYCONF, cep->ce_varname, cep->ce_vardata);
				errors++;
				safe_free(filename);
				continue;
			}
			safe_free(filename);
			have_countries_file = 1;
			continue;
		}

		// Anything else is unknown to us =]
		config_warn("%s:%i: unknown item %s::%s", cep->ce_fileptr->cf_filename, cep->ce_varlinenum, MYCONF, cep->ce_varname); // So display just a warning
	}
	
	// note that we will get here only if the config block is present
	if(!have_blocks_file){
		config_error("geoip-base: no (correct) address blocks file specified.");
		errors++;
	}

	if(!have_countries_file){
		config_error("geoip-base: no (correct) countries file specified.");
		errors++;
	}

	*errs = errors;
	return errors ? -1 : 1; // Returning 1 means "all good", -1 means we shat our panties
}

// if the config would make this module unusable, we try to detect it here
int geoip_base_configposttest(int *errs) {
	int errors = 0;
	char *filename;
	if(!have_config){
		config_warn("geoip-base: no \"%s\" config block found, using default file locations", MYCONF);
		filename = strdup(IPv4PATH);
		convert_to_absolute_path(&filename, CONFDIR);
		if(access(filename, R_OK)){
			config_error("geoip-base: File %s not found in the default location or can't be loaded. Please either provide this file or create a \"%s\" config block.", IPv4PATH, MYCONF);
			errors++;
		}
		safe_free(filename);
		filename = strdup(COUNTRIESPATH);
		convert_to_absolute_path(&filename, CONFDIR);
		if(access(filename, R_OK)){
			config_error("geoip-base: File %s not found in the default location or can't be loaded. Please either provide this file or create a \"%s\" config block.", COUNTRIESPATH, MYCONF);
			errors++;
		}
		safe_free(filename);
		filename = strdup(IPv6PATH);
		convert_to_absolute_path(&filename, CONFDIR);
		if(access(filename, R_OK)){
			config_error("geoip-base: File %s not found in the default location or can't be loaded. Please either provide this file or create a \"%s\" config block.", IPv6PATH, MYCONF);
			errors++;
		}
		safe_free(filename);
	}
	*errs = errors;
	if(errors)
		return -1;
	return 1;
}

// "Run" the config (everything should be valid at this point)
int geoip_base_configrun(ConfigFile *cf, ConfigEntry *ce, int type) {
	ConfigEntry *cep; // For looping through our bl0cc

	// Since we'll add a new top-level block to unrealircd.conf, need to filter on CONFIG_MAIN lmao
	if(type != CONFIG_MAIN)
		return 0; // Returning 0 means idgaf bout dis

	// Check for valid config entries first
	if(!ce || !ce->ce_varname)
		return 0;

	// If it isn't our bl0cc, idc
	if(strcmp(ce->ce_varname, MYCONF))
		return 0;
	
	// Loop dat shyte fam
	for(cep = ce->ce_entries; cep; cep = cep->ce_next) {
		// Do we even have a valid name l0l?
		if(!cep->ce_varname)
			continue; // Next iteration imo tbh

		if(cep->ce_vardata && !strcmp(cep->ce_varname, "ipv4-blocks-file")) {
			if(read_ipv4(cep->ce_vardata)) return -1;
			continue;
		}

		if(cep->ce_vardata && !strcmp(cep->ce_varname, "ipv6-blocks-file")) {
			if(read_ipv6(cep->ce_vardata)) return -1;
			continue;
		}

		if(cep->ce_vardata && !strcmp(cep->ce_varname, "countries-file")) {
			if(read_countries(cep->ce_vardata)) return -1;
			continue;
		}
	}
	return 1; // We good
}

// functions for freeing allocated memory

static void free_ipv4(void){
	struct ip_range *ptr, *oldptr;
	int i;
	for(i=0; i<256; i++){
		ptr = ip_range_list[i];
		ip_range_list[i] = NULL;
		while(ptr){
			oldptr = ptr;
			ptr = ptr->next;
			safe_free(oldptr);
		}
	}
}

static void free_ipv6(void){
	struct ip6_range *ptr, *oldptr;
	ptr = ip6_range_list;
	ip6_range_list = NULL;
	while(ptr){
		oldptr = ptr;
		ptr = ptr->next;
		safe_free(oldptr);
	}
}

static void free_countries(void){
	struct country *ptr, *oldptr;
	ptr = country_list;
	country_list = NULL;
	while(ptr){
		oldptr = ptr;
		ptr = ptr->next;
		safe_free(oldptr);
	}
}

static void free_all(void){
	free_ipv4();
	free_ipv6();
	free_countries();
}

// convert hex digit to binary nibble

int hexval(char c){
	if(c >= '0' && c <= '9') return c-'0';
	if(c >= 'a' && c <= 'f') return c-'a'+10;
	if(c >= 'A' && c <= 'F') return c-'A'+10;
	return -1;
}

// reading data from files

#define STR_HELPER(x) #x
#define STR(x) STR_HELPER(x)

static int read_ipv4(char *file){
	FILE *u;
	char buf[BUFLEN+1];
	int cidr, geoid;
	char ip[24];
	char netmask[24];
	uint32_t addr;
	uint32_t mask;
	struct ip_range *curr[256];
	struct ip_range *ptr;
	memset(curr, 0, sizeof(curr));
	int i;
	
	char *filename = strdup(file);
	convert_to_absolute_path(&filename, CONFDIR);
	u = fopen(filename, "r");
	safe_free(filename);
	if(!u){
		config_warn("[geoip-base] Cannot open IPv4 ranges list file");
		return 1;
	}
	
	if(!fgets(buf, BUFLEN, u)){
		config_warn("[geoip-base] IPv4 list file is empty");
		return 1;
	}
	buf[BUFLEN] = '\0';
	while(fscanf(u, "%23[^/\n]/%d,%" STR(BUFLEN) "[^\n]\n", ip, &cidr, buf) == 3){
		if(sscanf(buf, "%d,", &geoid) != 1){
//			sendto_realops("Can't read geoid for ip: %s", ip); // this happens normally so don't send a warning
			continue;
		}

		if(cidr < 1 || cidr > 32){
			config_warn("[geoip-base] Invalid CIDR found! IP=%s CIDR=%d\n", ip, cidr);
			continue;
		}

		if(inet_pton(AF_INET, ip, &addr) < 1){
			config_warn("[geoip-base] Invalid IP found! \"%s\"", ip);
			continue;
		}
		addr = htonl(addr);
		
		mask = 0;
		while(cidr){ // calculate netmask
			mask >>= 1;
			mask |= (1<<31);
			cidr--;
		}
		
		i=0;
		do { // multiple iterations in case CIDR is <8 and we have multiple first octets matching
			uint8_t index = addr>>24;
			if(!curr[index]){
				ip_range_list[index] = safe_alloc(sizeof(struct ip_range));
				curr[index] = ip_range_list[index];
			} else {
				curr[index]->next = safe_alloc(sizeof(struct ip_range));
				curr[index] = curr[index]->next;
			}
			ptr = curr[index];
			ptr->next = NULL;
			ptr->addr = addr;
			ptr->mask = mask;
			ptr->geoid = geoid;
			i++;
			index++;
		} while(i<=((~mask)>>24));
	}
	fclose(u);
	return 0;
}

static int ip6_convert(char *ip, uint16_t out[8]){ // convert text to binary form
	uint16_t tmp[8];
	int i;
	if(inet_pton(AF_INET6, ip, out) < 1)
		return 0;
	for(i=0; i<8; i++){
		out[i] = htons(out[i]);
	}
	return 1;
}

#define IPV6_STRING_SIZE	40

static int read_ipv6(char *file){
	FILE *u;
	char buf[BUFLEN+1];
	char *bptr, *optr;
	int cidr, geoid;
	char ip[IPV6_STRING_SIZE];
	uint16_t addr[8];
	uint16_t mask[8];
	struct ip6_range *curr = NULL;
	struct ip6_range *ptr;
	int error;
	int length;

	char *filename = strdup(file);
	convert_to_absolute_path(&filename, CONFDIR);
	u = fopen(filename, "r");
	safe_free(filename);
	if(!u){
		config_warn("[geoip-base] Cannot open IPv6 ranges list file");
		return 1;
	}
	if(!fgets(buf, BUFLEN, u)){
		config_warn("[geoip-base] IPv6 list file is empty");
		return 1;
	}
	while(fgets(buf, BUFLEN, u)){
		error = 0;
		bptr = buf;
		optr = ip;
		length = 0;
		while(*bptr != '/'){
			if(!*bptr){
				error = 1;
				break;
			}
			if(++length >= IPV6_STRING_SIZE){
				ip[IPV6_STRING_SIZE-1] = '\0';
				config_warn("[geoip-base] Too long IP address found, starts with %s", ip);
				error = 1;
				break;
			}
			*optr++ = *bptr++;
		}
		if(error) continue;
		*optr = '\0';
		bptr++;
		if(!ip6_convert(ip, addr)){
			config_warn("[geoip-base] Invalid IP found! \"%s\"", ip);
			continue;
		}
		sscanf(bptr, "%d,%d,", &cidr, &geoid);
		if(cidr < 1 || cidr > 128){
			config_warn("[geoip-base] Invalid CIDR found! CIDR=%d", cidr);
			continue;
		}

		memset(mask, 0, 16);
		
		int mask_bit = 0;
		while(cidr){ // calculate netmask
			mask[mask_bit/16] |= 1<<(15-(mask_bit%16));
			mask_bit++;
			cidr--;
		}

		if(!curr){
			ip6_range_list = safe_alloc(sizeof(struct ip6_range));
			curr = ip6_range_list;
		} else {
			curr->next = safe_alloc(sizeof(struct ip6_range));
			curr = curr->next;
		}
		ptr = curr;
		ptr->next = NULL;
		memcpy(ptr->addr, addr, 16);
		memcpy(ptr->mask, mask, 16);
		ptr->geoid = geoid;
	}
	fclose(u);
	return 0;

}

// CSV fields
// no STATE_GEONAME_ID because of using %d in fscanf
#define STATE_LOCALE_CODE	0
#define STATE_CONTINENT_CODE	1
#define STATE_CONTINENT_NAME	2
#define STATE_COUNTRY_ISO_CODE	3
#define STATE_COUNTRY_NAME	4
#define STATE_IS_IN_EU	5

#define MEMBER_SIZE(type,member) sizeof(((type *)0)->member)

static int read_countries(char *file){
	FILE *u;
	char code[MEMBER_SIZE(struct country, code)];
	char continent[MEMBER_SIZE(struct country, continent)];
	char name[MEMBER_SIZE(struct country, name)];
	char buf[BUFLEN+1];
	int state;
	int id;
	struct country *curr = NULL;
	
	char *filename = strdup(file);
	convert_to_absolute_path(&filename, CONFDIR);
	u = fopen(filename, "r");
	safe_free(filename);
	if(!u){
		config_warn("[geoip-base] Cannot open countries list file");
		return 1;
	}
	
	if(!fgets(buf, BUFLEN, u)){
		config_warn("[geoip-base] Countries list file is empty");
		return 1;
	}
	while(fscanf(u, "%d,%" STR(BUFLEN) "[^\n]", &id, buf) == 2){ //getting country ID integer and all other data in string
		char *ptr = buf;
		char *codeptr = code;
		char *contptr = continent;
		char *nptr = name;
		int quote_open = 0;
		int length = 0;
		state = STATE_LOCALE_CODE;
		while(*ptr){
			switch(state){
				case STATE_CONTINENT_NAME:
					if(*ptr == ','){
						goto next_line; // no continent?
					}
					if(length >= MEMBER_SIZE(struct country, continent)){
						*contptr = '\0';
						config_warn("[geoip-base] Too long continent name found: `%s`. If you are sure your countries file is correct, please report this to the module author.", continent);
						goto next_line;
					}
					*contptr = *ptr; // scan for continent name
					contptr++;
					length++;
					break;
				case STATE_COUNTRY_ISO_CODE:
					if(*ptr == ','){	// country code is empty
						goto next_line;	// -- that means only the continent is specified - we ignore it completely
					}
					if(length >= MEMBER_SIZE(struct country, code)){
						*codeptr = '\0';
						config_warn("[geoip-base] Too long country code found: `%s`. If you are sure your countries file is correct, please report this to the module author.", code);
						goto next_line;
					}
					*codeptr = *ptr; // scan for country code (DE, PL, US etc)
					codeptr++;
					length++;
					break;
				case STATE_COUNTRY_NAME:
					goto read_country_name;
				default:
					break; // ignore this field and wait for next one
			}
			ptr++;
			if(*ptr == ','){
				length = 0;
				ptr++;
				state++;
			}
		}
		read_country_name:
		*codeptr = '\0';
		*contptr = '\0';
		length = 0;
		while(*ptr){
			switch(*ptr){
				case '"': quote_open = !quote_open; ptr++; continue;
				case ',': if(!quote_open) goto end_country_name; // we reached the end of current CSV field
				/* fall through */
				default:
					*nptr++ = *ptr++;
					if(length >= MEMBER_SIZE(struct country, name)){
						*nptr = '\0';
						config_warn("[geoip-base] Too long country name found: `%s`. If you are sure your countries file is correct, please report this to the module author.", name);
						goto next_line;
					}
					break; // scan for country name
			}
		}
		end_country_name:
		*nptr = '\0';
		if(country_list){
			curr->next = safe_alloc(sizeof(struct country));
			curr = curr->next;
		} else {
			country_list = safe_alloc(sizeof(struct country));
			curr = country_list;
		}
		curr->next = NULL;
		strcpy(curr->code, code);
		strcpy(curr->name, name);
		strcpy(curr->continent, continent);
		curr->id = id;
		next_line: continue;
	}
	fclose(u);
	return 0;
}

static struct country *get_country(int id){
	struct country *curr = country_list;
	if(!curr){
		sendto_realops("[geoip-base] Countries list is empty! Try /rehash ing to fix");
		return NULL;
	}
	int found = 0;
	for(;curr;curr = curr->next){
		if(curr->id == id){
			found = 1;
			break;
		}
	}
	if(found) return curr;
	return NULL;
}

static int get_v4_geoid(char *iip){
	uint32_t addr, tmp_addr;
	struct ip_range *curr;
	int i;
	int found = 0;
	if(inet_pton(AF_INET, iip, &addr) < 1){
		config_warn("[geoip-base] Invalid or unsupported client IP \"%s\"", iip);
		return 0;
	}
	addr = htonl(addr);
	curr = ip_range_list[addr>>24];
	if(curr){
		i = 0;
		for(;curr;curr = curr->next){
			tmp_addr = addr;
			tmp_addr &= curr->mask; // mask the address to filter out net prefix only
			if(tmp_addr == curr->addr){ // ... and match it to the loaded data
				found = 1;
				break;
			}
			if(found) break;
			i++;
		}
	}
	if(found) return curr->geoid;
	return 0;
}

static int get_v6_geoid(char *iip){
	uint16_t addr[8];
	struct ip6_range *curr;
	int i;
	int found = 0;
	
	if(!ip6_convert(iip, addr)){
		sendto_realops("[geoip-base] Invalid or unsupported client IP \"%s\"", iip);
		return 0;
	}
	curr = ip6_range_list;
	if(curr){
		for(;curr;curr = curr->next){
			found = 1;
			for(i=0; i<8; i++){
				if(curr->addr[i] != (addr[i] & curr->mask[i])){ // compare net address to loaded data
					found = 0;
					break;
				}
			}
			if(found) break;
		}
	}
	if(found){
		return curr->geoid;
	}
	return 0;
}

static struct country *get_country_by_ip(char *iip){
	int geoid;
	
	struct country *curr_country;
	
	if(!iip) return NULL;
	
	if(strchr(iip, ':')){ // IPV6 contains :, IPV4 does not
		geoid = get_v6_geoid(iip);
	} else {
		geoid = get_v4_geoid(iip);
	}
	if(geoid == 0) return NULL;
	curr_country = get_country(geoid);
	if(!curr_country) return NULL;
	return curr_country;
}

void geoip_moddata_free(ModData *m){
	if(m->ptr) safe_free(m->ptr);
	m->ptr = NULL;
}

char *geoip_moddata_serialize(ModData *m){
	static char buf[140];
	if(!m->ptr) return NULL;
	struct country *country = (struct country *)m->ptr;
	ircsnprintf(buf, 140, "%s!%s!%s", country->code, country->name, country->continent);
	return buf;
}

void geoip_moddata_unserialize(char *str, ModData *m){
	if(m->ptr) safe_free(m->ptr);
	struct country *country = safe_alloc(sizeof(struct country));
	if(sscanf(str, "%9[^!]!%99[^!]!%24[^!]", country->code, country->name, country->continent) != 3){ // invalid argument
		safe_free(country);
		m->ptr = NULL;
	} else {
		m->ptr = country;
	}
}

MOD_TEST(){
	have_config = 0;
	HookAdd(modinfo->handle, HOOKTYPE_CONFIGTEST, 0, geoip_base_configtest);
	HookAdd(modinfo->handle, HOOKTYPE_CONFIGPOSTTEST, 0, geoip_base_configposttest);
	return MOD_SUCCESS;
}

MOD_INIT(){
	HookAdd(modinfo->handle, HOOKTYPE_CONFIGRUN, 0, geoip_base_configrun);
	HookAdd(modinfo->handle, HOOKTYPE_REMOTE_CONNECT, -50, geoip_userconnect);
	HookAdd(modinfo->handle, HOOKTYPE_LOCAL_CONNECT, -50, geoip_userconnect);
	ModDataInfo mreq;
	memset(&mreq, 0, sizeof(mreq));
	mreq.type = MODDATATYPE_CLIENT;
	mreq.name = "geoip";
	mreq.sync = 1;
	mreq.free = geoip_moddata_free;
	mreq.serialize = geoip_moddata_serialize;
	mreq.unserialize = geoip_moddata_unserialize;
#if UNREAL_VERSION_TIME >= 202125
	mreq.remote_write = 1;
#endif
	geoipMD = ModDataAdd(modinfo->handle, mreq);
	if(!geoipMD){
		config_error("%s: critical error for ModDataAdd: %s. Don't load geoip-base and geoip-transfer on the same server. Remove the 'loadmodule \"third/geoip-transfer\";' line from your config.", MOD_HEADER.name, ModuleGetErrorStr(modinfo->handle));
		return MOD_FAILED;
	}
	CommandAdd(modinfo->handle, "GEOCHECK", cmd_geocheck, MAXPARA, CMD_USER);
	return MOD_SUCCESS;
}

MOD_LOAD(){
	Client *acptr;
	if(!have_config){
		if(read_ipv4(IPv4PATH)){
			free_all();
			return MOD_FAILED;
		}
		if(read_countries(COUNTRIESPATH)){
			free_all();
			return MOD_FAILED;
		}
		if(read_ipv6(IPv6PATH)){
			free_all();
			return MOD_FAILED;
		}
	}

	list_for_each_entry(acptr, &client_list, client_node){
		if (!IsUser(acptr)) continue;
		geoip_userconnect(acptr); // add info for all users upon module loading
	}

	return MOD_SUCCESS;
}

MOD_UNLOAD(){
	Client *acptr;
	free_all();
	list_for_each_entry(acptr, &client_list, client_node){
		if (!IsUser(acptr)) continue;
		moddata_client(acptr, geoipMD).ptr = NULL;
	}
	return MOD_SUCCESS;
}

static int geoip_userconnect(Client *cptr) {
	struct country *country = get_country_by_ip(cptr->ip);
	if(!country) return HOOK_CONTINUE;
	struct country *data = safe_alloc(sizeof(struct country));
	*data = *country;

	moddata_client(cptr, geoipMD).ptr = data;
	broadcast_md_client_cmd(NULL, &me, cptr, geoipMD->name, geoip_moddata_serialize(&moddata_client(cptr, geoipMD)));
	return HOOK_CONTINUE;
}

CMD_FUNC(cmd_geocheck){
	struct country *country;
	if(parc < 2 || BadPtr(parv[1])){
		sendnumeric(client, ERR_NEEDMOREPARAMS, "GEOCHECK");
		return;
	}
	country = get_country_by_ip(parv[1]);
	if(!country){
		sendnotice(client, "No country found for %s", parv[1]);
		return;
	}
	sendnotice(client, "Country for %s is (%s/%s) %s (id: %d)", parv[1], country->continent, country->code, country->name, country->id);
}

