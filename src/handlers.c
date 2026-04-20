#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "handlers.h"
#include "network.h"
#include "storage.h"
#include "utils.h"

extern const char *td_json_client_execute(void *client, const char *request);

static void flush_group(uv_timer_t *handle) {
    AppContext *ctx = (AppContext *)handle->data;
    if (ctx->group.count == 0) return;

    cJSON *req = cJSON_CreateObject();
    cJSON_AddStringToObject(req, "@type", "forwardMessages");
    cJSON_AddNumberToObject(req, "chat_id", (double)ctx->cfg->aggregator_chat_id);
    cJSON_AddNumberToObject(req, "from_chat_id", (double)ctx->group.chat_id);
    cJSON *mids = cJSON_AddArrayToObject(req, "message_ids");
    for (size_t i = 0; i < ctx->group.count; i++) {
        cJSON_AddItemToArray(mids, cJSON_CreateNumber((double)ctx->group.msg_ids[i]));
    }
    td_client_send(ctx->td_client, req);
    printf("\033[1;32m[FORWARD]\033[0m Album from %lld (%zu msgs)\n", (long long)ctx->group.chat_id, ctx->group.count);

    free(ctx->group.msg_ids);
    memset(&ctx->group, 0, sizeof(PendingGroup));
}

static void handle_new_message(AppContext *ctx, cJSON *msg) {
    if (!msg) return;
    cJSON *cid_obj = cJSON_GetObjectItem(msg, "chat_id");
    if (!cid_obj) return;
    int64_t chat_id = (int64_t)cid_obj->valuedouble;
    
    int monitored = 0;
    for (size_t i = 0; i < ctx->cfg->source_count; i++) {
        if (ctx->cfg->source_channels[i] == chat_id) {
            monitored = 1;
            break;
        }
    }
    if (!monitored) return;

    cJSON *id_obj = cJSON_GetObjectItem(msg, "id");
    if (!id_obj) return;
    int64_t msg_id = (int64_t)id_obj->valuedouble;

    cJSON *gid_obj = cJSON_GetObjectItem(msg, "media_album_id");
    int64_t grouped_id = gid_obj ? (int64_t)atoll(gid_obj->valuestring) : 0;

    cJSON *content = cJSON_GetObjectItem(msg, "content");
    const char *text = "";
    if (content) {
        cJSON *type_obj = cJSON_GetObjectItem(content, "@type");
        if (type_obj && type_obj->valuestring) {
            if (strcmp(type_obj->valuestring, "messageText") == 0) {
                cJSON *txt_node = cJSON_GetObjectItem(content, "text");
                if (txt_node) {
                    cJSON *t_obj = cJSON_GetObjectItem(txt_node, "text");
                    if (t_obj && t_obj->valuestring) text = t_obj->valuestring;
                }
            } else {
                cJSON *caption_node = cJSON_GetObjectItem(content, "caption");
                if (caption_node) {
                    cJSON *t_obj = cJSON_GetObjectItem(caption_node, "text");
                    if (t_obj && t_obj->valuestring) text = t_obj->valuestring;
                }
            }
        }
    }

    int64_t reply_id = 0;
    cJSON *reply_to = cJSON_GetObjectItem(msg, "reply_to");
    if (reply_to) {
        cJSON *rt_type = cJSON_GetObjectItem(reply_to, "@type");
        if (rt_type && rt_type->valuestring && strcmp(rt_type->valuestring, "messageReplyToMessage") == 0) {
            cJSON *rmid = cJSON_GetObjectItem(reply_to, "message_id");
            if (rmid) reply_id = (int64_t)rmid->valuedouble;
        }
    }

    char hash_input[1024];
    if (grouped_id != 0) {
        snprintf(hash_input, sizeof(hash_input), "%lld-G%lld-%s-%lld", (long long)chat_id, (long long)grouped_id, text, (long long)reply_id);
    } else {
        snprintf(hash_input, sizeof(hash_input), "%lld-%lld-%s-%lld", (long long)chat_id, (long long)msg_id, text, (long long)reply_id);
    }

    char hash[65];
    compute_sha256(hash_input, NULL, 0, 0, hash);
    if (is_message_seen(hash)) return;
    mark_message_seen(hash);

    if (grouped_id != 0) {
        if (ctx->group.count > 0 && (ctx->group.grouped_id != grouped_id || ctx->group.chat_id != chat_id)) {
            uv_timer_stop(&ctx->group_timer);
            flush_group(&ctx->group_timer);
        }

        if (ctx->group.count == 0) {
            ctx->group.chat_id = chat_id;
            ctx->group.grouped_id = grouped_id;
            uv_timer_start(&ctx->group_timer, flush_group, 500, 0);
        }
        ctx->group.msg_ids = realloc(ctx->group.msg_ids, sizeof(int64_t) * (ctx->group.count + 1));
        ctx->group.msg_ids[ctx->group.count++] = msg_id;
    } else {
        cJSON *req = cJSON_CreateObject();
        cJSON_AddStringToObject(req, "@type", "forwardMessages");
        cJSON_AddNumberToObject(req, "chat_id", (double)ctx->cfg->aggregator_chat_id);
        cJSON_AddNumberToObject(req, "from_chat_id", (double)chat_id);
        cJSON *mids = cJSON_AddArrayToObject(req, "message_ids");
        cJSON_AddItemToArray(mids, cJSON_CreateNumber((double)msg_id));
        td_client_send(ctx->td_client, req);
        printf("\033[1;32m[FORWARD]\033[0m From chat %lld, msg %lld\n", (long long)chat_id, (long long)msg_id);
    }
}

static void handle_command(AppContext *ctx, cJSON *msg) {
    if (!msg) return;
    cJSON *cid_obj = cJSON_GetObjectItem(msg, "chat_id");
    if (!cid_obj || (int64_t)cid_obj->valuedouble != ctx->cfg->aggregator_chat_id) return;
    
    cJSON *content = cJSON_GetObjectItem(msg, "content");
    if (!content || !cJSON_GetObjectItem(content, "@type") || strcmp(cJSON_GetObjectItem(content, "@type")->valuestring, "messageText") != 0) return;
    
    cJSON *txt_node = cJSON_GetObjectItem(content, "text");
    if (!txt_node) return;
    const char *text = cJSON_GetObjectItem(txt_node, "text")->valuestring;

    if (text && strncmp(text, "/status", 7) == 0) {
        printf("\033[1;36m[CMD]\033[0m /status command received\n");
        int link_count = get_link_count();
        cJSON *req = cJSON_CreateObject();
        cJSON_AddStringToObject(req, "@type", "sendMessage");
        cJSON_AddNumberToObject(req, "chat_id", (double)ctx->cfg->aggregator_chat_id);
        cJSON *input = cJSON_AddObjectToObject(req, "input_message_content");
        cJSON_AddStringToObject(input, "@type", "inputMessageText");
        cJSON *reply_txt = cJSON_AddObjectToObject(input, "text");
        char status_msg[256];
        snprintf(status_msg, sizeof(status_msg), "🤖 <b>Aggregator Status</b>\n\nChannels Monitored: %zu\nLinks Found: %d\nUptime: Active", ctx->cfg->source_count, link_count);
        cJSON_AddStringToObject(reply_txt, "text", status_msg);
        td_client_send(ctx->td_client, req);
    }
}

static void handle_chat_update(AppContext *ctx, int64_t cid, cJSON *chat) {
    if (!chat) return;
    cJSON *title_obj = cJSON_GetObjectItem(chat, "title");
    if (title_obj && title_obj->valuestring) store_chat_title(cid, title_obj->valuestring);
    
    cJSON *type_obj = cJSON_GetObjectItem(chat, "type");
    if (type_obj) {
        cJSON *at_type = cJSON_GetObjectItem(type_obj, "@type");
        bool has_link = false;
        
        cJSON *uname_obj = cJSON_GetObjectItem(type_obj, "username");
        if (uname_obj && uname_obj->valuestring && strlen(uname_obj->valuestring) > 0) {
            char link[256];
            snprintf(link, sizeof(link), "https://t.me/%s", uname_obj->valuestring);
            store_chat_invite_link(cid, link);
            has_link = true;
        }

        if (at_type && at_type->valuestring && strcmp(at_type->valuestring, "chatTypeSupergroup") == 0) {
            cJSON *sid_obj = cJSON_GetObjectItem(type_obj, "supergroup_id");
            if (sid_obj) {
                int64_t sid = (int64_t)sid_obj->valuedouble;
                store_supergroup_map(sid, cid);
                if (!has_link) {
                    cJSON *req = cJSON_CreateObject();
                    cJSON_AddStringToObject(req, "@type", "getSupergroupFullInfo");
                    cJSON_AddNumberToObject(req, "supergroup_id", (double)sid);
                    td_client_send(ctx->td_client, req);
                }
            }
        }
    }
}

static void process_folders(AppContext *ctx, cJSON *fs) {
    if (!fs || !cJSON_IsArray(fs)) return;
    int size = cJSON_GetArraySize(fs);
    printf("Scanning %d Telegram folders...\n", size);
    for (int i = 0; i < size; i++) {
        cJSON *f = cJSON_GetArrayItem(fs, i);
        cJSON *name_obj = cJSON_GetObjectItem(f, "name");
        cJSON *id_obj = cJSON_GetObjectItem(f, "id");
        if (name_obj && id_obj) {
            cJSON *txt_node = cJSON_GetObjectItem(name_obj, "text");
            if (txt_node && cJSON_GetObjectItem(txt_node, "text")) {
                const char *title = cJSON_GetObjectItem(txt_node, "text")->valuestring;
                for (size_t k = 0; k < ctx->cfg->folder_count; k++) {
                    if (ctx->cfg->folder_names[k] && strcmp(title, ctx->cfg->folder_names[k]) == 0) {
                        printf("\033[1;32m[FOLDER]\033[0m Monitoring: '%s'\n", title);
                        cJSON *req = cJSON_CreateObject();
                        cJSON_AddStringToObject(req, "@type", "getChatFolder");
                        cJSON_AddNumberToObject(req, "chat_folder_id", (double)id_obj->valueint);
                        td_client_send(ctx->td_client, req);
                        break;
                    }
                }
            }
        }
    }
}

void handle_update(AppContext *ctx, cJSON *update) {
    if (!update) return;
    const char *type = cJSON_GetObjectItem(update, "@type")->valuestring;

    if (strcmp(type, "updateAuthorizationState") == 0) {
        handle_auth(ctx->td_client, ctx->cfg, update);
    } else if (strcmp(type, "updateChatFolders") == 0 || strcmp(type, "chatFolders") == 0) {
        cJSON *fs = cJSON_GetObjectItem(update, "chat_folders");
        if (fs) process_folders(ctx, fs);
    } else if (strcmp(type, "chatFolder") == 0) {
        cJSON *ids = cJSON_GetObjectItem(update, "included_chat_ids");
        if (ids && cJSON_IsArray(ids)) {
            for (int j = 0; j < cJSON_GetArraySize(ids); j++) {
                int64_t cid = (int64_t)cJSON_GetArrayItem(ids, j)->valuedouble;
                bool already = false;
                for (size_t n = 0; n < ctx->cfg->source_count; n++) {
                    if (ctx->cfg->source_channels[n] == cid) { already = true; break; }
                }
                if (!already) {
                    ctx->cfg->source_channels = realloc(ctx->cfg->source_channels, sizeof(int64_t) * (ctx->cfg->source_count + 1));
                    ctx->cfg->source_channels[ctx->cfg->source_count++] = cid;
                    cJSON *get_chat = cJSON_CreateObject();
                    cJSON_AddStringToObject(get_chat, "@type", "getChat");
                    cJSON_AddNumberToObject(get_chat, "chat_id", (double)cid);
                    td_client_send(ctx->td_client, get_chat);
                }
            }
        }
    } else if (strcmp(type, "updateNewMessage") == 0) {
        handle_new_message(ctx, cJSON_GetObjectItem(update, "message"));
        handle_command(ctx, cJSON_GetObjectItem(update, "message"));
    } else if (strcmp(type, "updateChatTitle") == 0) {
        store_chat_title((int64_t)cJSON_GetObjectItem(update, "chat_id")->valuedouble, cJSON_GetObjectItem(update, "title")->valuestring);
    } else if (strcmp(type, "chat") == 0 || strcmp(type, "updateNewChat") == 0) {
        cJSON *chat = strcmp(type, "chat") == 0 ? update : cJSON_GetObjectItem(update, "chat");
        handle_chat_update(ctx, (int64_t)cJSON_GetObjectItem(chat, "id")->valuedouble, chat);
    } else if (strcmp(type, "updateSupergroupFullInfo") == 0) {
        int64_t sid = (int64_t)cJSON_GetObjectItem(update, "supergroup_id")->valuedouble;
        int64_t cid = get_chat_id_by_supergroup(sid);
        cJSON *full_info = cJSON_GetObjectItem(update, "full_info");
        if (cid != 0 && full_info) {
            cJSON *invite = cJSON_GetObjectItem(full_info, "invite_link");
            if (!invite) invite = cJSON_GetObjectItem(full_info, "primary_invite_link");
            if (invite && invite->valuestring && strlen(invite->valuestring) > 0) {
                store_chat_invite_link(cid, invite->valuestring);
            }
        }
    } else if (strcmp(type, "error") == 0) {
        cJSON *msg = cJSON_GetObjectItem(update, "message");
        if (msg) fprintf(stderr, "\033[1;31m[ERROR]\033[0m TDLib: %s\n", msg->valuestring);
    }
}
