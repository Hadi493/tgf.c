#ifndef CONFIG_H
#define CONFIG_H

#include <stdint.h>
#include <toml.h>

typedef struct {
    int32_t api_id;
    char *api_hash;
    char *bot_token;
    char *phone;
    char *catch_up;
    char *db_path;
    int64_t *source_channels;
    size_t source_count;
    char **folder_names;
    size_t folder_count;
    int64_t aggregator_chat_id;
} Config;

Config *load_config(const char *env_path, const char *toml_path);
void free_config(Config *cfg);
void add_source(const char *toml_path, int64_t id, int is_folder);

#endif
