#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "config.h"

static char *trim(char *s) {
    while (isspace((unsigned char)*s)) s++;
    if (*s == 0) return s;
    char *back = s + strlen(s) - 1;
    while (back > s && (isspace((unsigned char)*back) || *back == '\'' || *back == '"')) {
        *back = '\0';
        back--;
    }
    if (*s == '\'' || *s == '"') s++;
    return s;
}

Config *load_config(const char *env_path, const char *toml_path) {
    Config *cfg = calloc(1, sizeof(Config));
    FILE *fe = fopen(env_path, "r");
    if (!fe) {
        fprintf(stderr, "Error: Could not open env file %s\n", env_path);
    } else {
        char line[512];
        while (fgets(line, sizeof(line), fe)) {
            char *eq = strchr(line, '=');
            if (!eq) continue;
            *eq = '\0';
            char *key = trim(line);
            char *val = trim(eq + 1);
            if (strcmp(key, "TELEGRAM_API_ID") == 0) {
                cfg->api_id = atoi(val);
                fprintf(stderr, "Loaded API_ID: %d\n", cfg->api_id);
            } else if (strcmp(key, "TELEGRAM_API_HASH") == 0) {
                cfg->api_hash = strdup(val);
                fprintf(stderr, "Loaded API_HASH: %s\n", cfg->api_hash);
            } else if (strcmp(key, "TELEGRAM_BOT_TOKEN") == 0) {
                cfg->bot_token = strdup(val);
            } else if (strcmp(key, "TELEGRAM_PHONE") == 0) {
                cfg->phone = strdup(val);
                fprintf(stderr, "Loaded PHONE: %s\n", cfg->phone);
            } else if (strcmp(key, "TELEGRAM_AGGREGATOR_CHANNEL") == 0) {
                cfg->aggregator_chat_id = atoll(val);
                fprintf(stderr, "Loaded AGGREGATOR_ID: %lld\n", (long long)cfg->aggregator_chat_id);
            } else if (strcmp(key, "CATCH_UP") == 0) {
                cfg->catch_up = strdup(val);
            } else if (strcmp(key, "DB_PATH") == 0) {
                cfg->db_path = strdup(val);
            }
        }
        fclose(fe);
    }
    FILE *ft = fopen(toml_path, "r");
    if (ft) {
        char errbuf[200];
        toml_table_t *conf = toml_parse_file(ft, errbuf, sizeof(errbuf));
        fclose(ft);
        if (conf) {
            toml_table_t *source_channels = toml_table_in(conf, "source_channels");
            if (source_channels) {
                toml_array_t *chans = toml_array_in(source_channels, "channels");
                if (chans) {
                    cfg->source_count = toml_array_nelem(chans);
                    cfg->source_channels = malloc(sizeof(int64_t) * cfg->source_count);
                    for (int i = 0; i < (int)cfg->source_count; i++) {
                        cfg->source_channels[i] = toml_int_at(chans, i).u.i;
                    }
                }
            }
            toml_table_t *source = toml_table_in(conf, "source");
            if (source) {
                toml_array_t *folders = toml_array_in(source, "folder");
                if (folders) {
                    cfg->folder_count = toml_array_nelem(folders);
                    cfg->folder_names = malloc(sizeof(char *) * cfg->folder_count);
                    for (int i = 0; i < (int)cfg->folder_count; i++) {
                        cfg->folder_names[i] = toml_string_at(folders, i).u.s;
                        fprintf(stderr, "Config: Monitoring folder '%s'\n", cfg->folder_names[i]);
                    }
                }
            }
            toml_free(conf);
        }
    }
    if (!cfg->db_path) cfg->db_path = strdup("tgf.db");
    return cfg;
}

void free_config(Config *cfg) {
    if (!cfg) return;
    free(cfg->api_hash);
    free(cfg->bot_token);
    free(cfg->phone);
    free(cfg->catch_up);
    free(cfg->db_path);
    free(cfg->source_channels);
    for (size_t i = 0; i < cfg->folder_count; i++) free(cfg->folder_names[i]);
    free(cfg->folder_names);
    free(cfg);
}

void add_source(const char *toml_path, int64_t id, int is_folder) {
    FILE *f = fopen(toml_path, "a");
    if (f) {
        if (is_folder) fprintf(f, "\n[source]\nfolder = [%lld]\n", (long long)id);
        else fprintf(f, "\n[source_channels]\nchannels = [%lld]\n", (long long)id);
        fclose(f);
    }
}
