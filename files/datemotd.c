/* GPL v3
   Valware © 2025
   Shows a specific message in the MOTD on a specific date
*/
/*** <<<MODULE MANAGER START>>>
module
{
        documentation "https://github.com/ValwareIRC/valware-unrealircd-mods/blob/main/datemotd/README.md";
        troubleshooting "In case of problems, check the documentation or e-mail me at v.a.pond@outlook.com";
        min-unrealircd-version "6.2.2";
        max-unrealircd-version "6.*";
        post-install-text
        {
                "The module is installed. Now all you need to do is add a loadmodule line:";
                "loadmodule \"third/datemotd\";";
                "Configure the block as described in the documentation.";
                "Once you're good to go, type ./unrealircd rehash";
        }
}
*** <<<MODULE MANAGER END>>>
*/

#include "unrealircd.h"

#define DATEMOTD_CONF "datemotd"

struct datemotd_item {
    char *name;
    char *file;
    char *date;               // NEW: date in format YYYY-MM-DD
    struct datemotd_item *next;
};

struct datemotd_conf {
    struct datemotd_item *datemotds;
    unsigned short int got_datemotd;
};

static struct datemotd_conf datemotd_conf;


void set_config_defaults(void);
void free_config(void);
int datemotd_configtest(ConfigFile *cf, ConfigEntry *ce, int type, int *errs);
int datemotd_configrun(ConfigFile *cf, ConfigEntry *ce, int type);
int motd_hook(Client *client);

ModuleHeader MOD_HEADER = {
	"third/datemotd",
	"1.1",
	"Shows a specific message in the MOTD on a specific date",
	"Valware",
	"unrealircd-6"
};


static int is_valid_date_string(const char *datestr)
{
    if (!datestr) return 0;

    // YYYY-MM-DD
    if (isdigit(datestr[0])) {
        int y, m, d;
        if (sscanf(datestr, "%d-%d-%d", &y, &m, &d) != 3)
            return 0;
        if (y < 1970 || m < 1 || m > 12 || d < 1 || d > 31)
            return 0;
        return 1;
    }

    // Weekday names
    const char *days_short[] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
    const char *days_long[]  = { "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday" };

    char lower_input[16];
    int i;
    for (i = 0; i < sizeof(lower_input)-1 && datestr[i]; i++)
        lower_input[i] = tolower((unsigned char)datestr[i]);
    lower_input[i] = '\0';

    for (i = 0; i < 7; i++) {
        char tmp[16];
        snprintf(tmp, sizeof(tmp), "%s", days_short[i]);
        for (int j = 0; tmp[j]; j++) tmp[j] = tolower((unsigned char)tmp[j]);
        if (!strcmp(lower_input, tmp)) return 1;

        snprintf(tmp, sizeof(tmp), "%s", days_long[i]);
        for (int j = 0; tmp[j]; j++) tmp[j] = tolower((unsigned char)tmp[j]);
        if (!strcmp(lower_input, tmp)) return 1;
    }

    return 0; // invalid string
}

static int is_today(const char *datestr)
{
    if (!datestr)
        return 0;

    time_t now = time(NULL);
    struct tm *tm_now = localtime(&now);

    // --- Case 1: YYYY-MM-DD format ---
    if (isdigit(datestr[0])) {
        char today[11];
        snprintf(today, sizeof(today), "%04d-%02d-%02d",
            tm_now->tm_year + 1900, tm_now->tm_mon + 1, tm_now->tm_mday);
        return !strcmp(today, datestr);
    }

    // --- Case 2: Day of week (Mon, Monday, etc.) ---
    const char *days_short[] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
    const char *days_long[]  = { "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday" };

    char lower_input[16];
    int i;
    for (i = 0; i < sizeof(lower_input)-1 && datestr[i]; i++)
        lower_input[i] = tolower((unsigned char)datestr[i]);
    lower_input[i] = '\0';

    // get today's weekday in both short and long form (lowercase)
    char today_short[8], today_long[16];
    snprintf(today_short, sizeof(today_short), "%s", days_short[tm_now->tm_wday]);
    snprintf(today_long, sizeof(today_long), "%s", days_long[tm_now->tm_wday]);

    for (i = 0; today_short[i]; i++) today_short[i] = tolower((unsigned char)today_short[i]);
    for (i = 0; today_long[i]; i++)  today_long[i]  = tolower((unsigned char)today_long[i]);


    if (!strcmp(lower_input, today_short) || !strcmp(lower_input, today_long)){
        return 1;
    }
    return 0;
}

void send_datemotd_file(Client *client, const char *file)
{
    FILE *fp = fopen(file, "r");
    if (!fp)
        return;

    char line[512];
    while (fgets(line, sizeof(line), fp)) {
        size_t len = strlen(line);
        if (len > 0 && line[len-1] == '\n') {
            line[len-1] = '\0';
            sendnumeric(client, RPL_MOTD, line);
        }
    }
    fclose(fp);
}

void set_config_defaults(void)
{
    datemotd_conf.datemotds = NULL;
}

void free_config(void)
{
    struct datemotd_item *p = datemotd_conf.datemotds;
    while (p) {
        struct datemotd_item *next = p->next;
        safe_free(p->name);
        safe_free(p->file);
        safe_free(p->date); // NEW
        safe_free(p);
        p = next;
    }
    datemotd_conf.datemotds = NULL;
}
int datemotd_configtest(ConfigFile *cf, ConfigEntry *ce, int type, int *errs)
{
    int errors = 0;

    if (type != CONFIG_MAIN || !ce || !ce->name || strcmp(ce->name, DATEMOTD_CONF))
        return 0;

    for (ConfigEntry *entry = ce->items; entry; entry = entry->next) {
        if (strcmp(entry->name, "item") != 0)
            continue;

        int has_name = 0, has_file = 0, has_date = 0;
        for (ConfigEntry *item = entry->items; item; item = item->next) {

            if (!item->name || !item->value)
                continue;

            if (strcmp(item->name, "name") == 0) has_name = 1;
            else if (strcmp(item->name, "file") == 0) has_file = 1;
            else if (strcmp(item->name, "date") == 0) {
                if (!is_valid_date_string(item->value)) {
                    config_error("%s:%i: invalid date '%s' (must be YYYY-MM-DD or weekday name)",
                                    item->file->filename, item->line_number, item->value);

                    errors++;
                }
                else
                    has_date = 1;
            } else {
                config_warn("%s:%i: unknown key '%s' in datemotd entry",
                            item->file->filename, item->line_number, item->name);
            }
        }
        if (!has_name || !has_file || !has_date) {
            config_error("%s:%i: each datemotd entry must have name (%i), file (%i), and date (%i)",
                            entry->file->filename, entry->line_number, has_name, has_file, has_date);
            errors++;
        }
    }

    *errs = errors;
    return errors ? -1 : 1;
}

int datemotd_configrun(ConfigFile *cf, ConfigEntry *ce, int type)
{
    if (type != CONFIG_MAIN || !ce || !ce->name || strcmp(ce->name, DATEMOTD_CONF))
        return 0;

    for (ConfigEntry *entry = ce->items; entry; entry = entry->next) {
        if (strcmp(entry->name, "item") != 0)
            continue;

        struct datemotd_item *new_item = safe_alloc(sizeof(*new_item));
        memset(new_item, 0, sizeof(*new_item));
        for (ConfigEntry *item = entry->items; item; item = item->next) {

            if (!item->name || !item->value)
                continue;

            if (strcmp(item->name, "name") == 0) safe_strdup(new_item->name, item->value);
            else if (strcmp(item->name, "file") == 0) safe_strdup(new_item->file, item->value);
            else if (strcmp(item->name, "date") == 0) safe_strdup(new_item->date, item->value);

        }
        new_item->next = datemotd_conf.datemotds;
        datemotd_conf.datemotds = new_item;
    }

    return 1;
}


// NEW: hook to send MOTD only on the matching date
int motd_hook(Client *client)
{
    struct datemotd_item *p;
    for (p = datemotd_conf.datemotds; p; p = p->next) {
        if (is_today(p->date)) {
            send_datemotd_file(client, p->file);
        }
    }
    return 0;
}

MOD_INIT()
{
    set_config_defaults();
    HookAdd(modinfo->handle, HOOKTYPE_CONFIGRUN, 0, datemotd_configrun);
    HookAdd(modinfo->handle, HOOKTYPE_MOTD, 0, motd_hook);
    return MOD_SUCCESS;
}

MOD_TEST()
{
    memset(&datemotd_conf, 0, sizeof(datemotd_conf));
    HookAdd(modinfo->handle, HOOKTYPE_CONFIGTEST, 0, datemotd_configtest);
    return MOD_SUCCESS;
}

MOD_LOAD()
{
    return MOD_SUCCESS;
}

MOD_UNLOAD()
{
    free_config();
    return MOD_SUCCESS;
}
