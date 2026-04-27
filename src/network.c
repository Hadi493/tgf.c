#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "network.h"

static bool should_use_bot_auth(const Config *cfg) {
    bool has_bot = (cfg->bot_token && strlen(cfg->bot_token) > 0);
    bool has_phone = (cfg->phone && strlen(cfg->phone) > 0);
    bool needs_folders = (cfg->folder_count > 0);
    if (!has_bot) return false;
    if (needs_folders && has_phone) return false;
    return true;
}

static const char *td_storage_dir(const Config *cfg) {
    return should_use_bot_auth(cfg) ? "tdlib_bot" : "tdlib_user";
}

void *td_client_create(void) {
    return td_json_client_create();
}

void td_client_send(void *client, cJSON *request) {
    char *json = cJSON_PrintUnformatted(request);
    if (!json) return;
    fprintf(stderr, "Sending to TDLib: %s\n", json);
    td_json_client_send(client, json);
    free(json);
}

cJSON *td_client_execute(cJSON *request) {
    char *json = cJSON_PrintUnformatted(request);
    if (!json) return NULL;
    const char *resp_json = td_json_client_execute(NULL, json);
    free(json);
    if (!resp_json) return NULL;
    return cJSON_Parse(resp_json);
}

cJSON *td_client_receive(void *client, double timeout) {
    const char *json = td_json_client_receive(client, timeout);
    if (!json) return NULL;
    return cJSON_Parse(json);
}

void td_client_destroy(void *client) {
    td_json_client_destroy(client);
}

void handle_auth(void *client, Config *cfg, cJSON *auth_state) {
    if (!auth_state) return;
    cJSON *type_obj = cJSON_GetObjectItemCaseSensitive(auth_state, "@type");
    if (!type_obj || !type_obj->valuestring) return;
    const char *state = type_obj->valuestring;
    
    fprintf(stderr, "[DEBUG] Auth state: %s\n", state);
    
    if (strcmp(state, "authorizationStateWaitTdlibParameters") == 0) {
        const char *storage_dir = td_storage_dir(cfg);
        cJSON *req = cJSON_CreateObject();
        cJSON_AddStringToObject(req, "@type", "setTdlibParameters");
        cJSON_AddBoolToObject(req, "use_test_dc", false);
        cJSON_AddStringToObject(req, "database_directory", storage_dir);
        cJSON_AddStringToObject(req, "files_directory", storage_dir);
        cJSON_AddBoolToObject(req, "use_file_database", true);
        cJSON_AddBoolToObject(req, "use_chat_info_database", true);
        cJSON_AddBoolToObject(req, "use_message_database", true);
        cJSON_AddBoolToObject(req, "use_secret_chats", false);
        cJSON_AddNumberToObject(req, "api_id", (double)cfg->api_id);
        cJSON_AddStringToObject(req, "api_hash", cfg->api_hash);
        cJSON_AddStringToObject(req, "system_language_code", "en");
        cJSON_AddStringToObject(req, "device_model", "Desktop");
        cJSON_AddStringToObject(req, "application_version", "1.0");
        cJSON_AddBoolToObject(req, "enable_storage_optimizer", true);
        td_client_send(client, req);
        cJSON_Delete(req);
    } else if (strcmp(state, "authorizationStateWaitEncryptionKey") == 0) {
        cJSON *req = cJSON_CreateObject();
        cJSON_AddStringToObject(req, "@type", "setDatabaseEncryptionKey");
        cJSON_AddStringToObject(req, "new_encryption_key", "");
        td_client_send(client, req);
        cJSON_Delete(req);
    } else if (strcmp(state, "authorizationStateWaitPhoneNumber") == 0) {
        if (should_use_bot_auth(cfg)) {
            cJSON *req = cJSON_CreateObject();
            cJSON_AddStringToObject(req, "@type", "checkAuthenticationBotToken");
            cJSON_AddStringToObject(req, "token", cfg->bot_token);
            td_client_send(client, req);
            cJSON_Delete(req);
        } else if (cfg->phone && strlen(cfg->phone) > 0) {
            if (cfg->folder_count > 0 && cfg->bot_token && strlen(cfg->bot_token) > 0) {
                fprintf(stderr, "[INFO] Folder sources detected. Using phone authentication to support folder discovery.\n");
            }
            cJSON *req = cJSON_CreateObject();
            cJSON_AddStringToObject(req, "@type", "setAuthenticationPhoneNumber");
            cJSON_AddStringToObject(req, "phone_number", cfg->phone);
            td_client_send(client, req);
            cJSON_Delete(req);
        }
    } else if (strcmp(state, "authorizationStateWaitCode") == 0) {
        char code[32];
        printf("\n\033[1;32mEnter Telegram authentication code: \033[0m");
        fflush(stdout);
        if (scanf("%31s", code) == 1) {
            cJSON *req = cJSON_CreateObject();
            cJSON_AddStringToObject(req, "@type", "checkAuthenticationCode");
            cJSON_AddStringToObject(req, "code", code);
            td_client_send(client, req);
            cJSON_Delete(req);
        }
    } else if (strcmp(state, "authorizationStateReady") == 0) {
        fprintf(stderr, "[DEBUG] Authorized! Sending initial sync requests...\n");
        
        cJSON *req_me = cJSON_CreateObject();
        cJSON_AddStringToObject(req_me, "@type", "getMe");
        td_client_send(client, req_me);
        cJSON_Delete(req_me);

    }
}
