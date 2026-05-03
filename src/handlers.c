#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "handlers.h"
#include "network.h"
#include "storage.h"
#include "utils.h"

static int64_t *info_requested_ids = NULL;
static size_t info_requested_count = 0;

static bool was_info_requested(int64_t sid) {
    if (!info_requested_ids) return false;
    for (size_t i = 0; i < info_requested_count; i++) {
        if (info_requested_ids[i] == sid) return true;
    }
    return false;
}

static void mark_info_requested(int64_t sid) {
    if (was_info_requested(sid)) return;
    int64_t *new_ids = realloc(info_requested_ids, sizeof(int64_t) * (info_requested_count + 1));
    if (new_ids) {
        info_requested_ids = new_ids;
        info_requested_ids[info_requested_count++] = sid;
    }
}

static char *construct_msg_link(int64_t chat_id, int64_t msg_id) {
    char *link = malloc(128);
    if (!link) return NULL;
    int64_t stripped_id = chat_id;
    if (chat_id < -1000000000000LL) {
        stripped_id = -(chat_id + 1000000000000LL);
    } else if (chat_id < 0) {
        stripped_id = -chat_id;
    }
    snprintf(link, 128, "https://t.me/c/%lld/%lld", (long long)stripped_id, (long long)(msg_id >> 20));
    return link;
}

static void parse_and_send(AppContext *ctx, int64_t from_chat_id, int64_t msg_id, const char *header, const char *original_text) {
    if (!ctx || !header || !original_text) return;

    size_t hlen = strlen(header);
    size_t olen = strlen(original_text);
    char *full_text = malloc(hlen + olen + 1);
    if (!full_text) return;
    memcpy(full_text, header, hlen);
    memcpy(full_text + hlen, original_text, olen + 1);

    cJSON *parse_req = cJSON_CreateObject();
    if (!parse_req) { free(full_text); return; }
    cJSON_AddStringToObject(parse_req, "@type", "parseTextEntities");
    cJSON_AddStringToObject(parse_req, "text", full_text);
    cJSON *parse_mode = cJSON_AddObjectToObject(parse_req, "parse_mode");
    if (parse_mode) cJSON_AddStringToObject(parse_mode, "@type", "textParseModeHTML");

    cJSON *formatted = td_client_execute(parse_req);
    cJSON_Delete(parse_req);
    free(full_text);

    if (formatted) {
        cJSON *type_obj = cJSON_GetObjectItem(formatted, "@type");
        if (type_obj && type_obj->valuestring && strcmp(type_obj->valuestring, "formattedText") == 0) {
            cJSON *req = cJSON_CreateObject();
            if (req) {
                cJSON_AddStringToObject(req, "@type", "sendMessage");
                cJSON_AddNumberToObject(req, "chat_id", (double)ctx->cfg->aggregator_chat_id);
                cJSON *input = cJSON_AddObjectToObject(req, "input_message_content");
                if (input) {
                    cJSON_AddStringToObject(input, "@type", "inputMessageForwarded");
                    cJSON_AddNumberToObject(input, "from_chat_id", (double)from_chat_id);
                    cJSON_AddNumberToObject(input, "message_id", (double)msg_id);
                    cJSON *copy_opts = cJSON_AddObjectToObject(input, "copy_options");
                    if (copy_opts) {
                        cJSON_AddBoolToObject(copy_opts, "send_copy", true);
                        cJSON_AddBoolToObject(copy_opts, "replace_caption", true);
                        cJSON_AddItemToObject(copy_opts, "new_caption", formatted);
                        formatted = NULL;
                        td_client_send(ctx->td_client, req);
                        cJSON_Delete(req);
                        return;
                    }
                }
                cJSON_Delete(req);
            }
        }
        cJSON_Delete(formatted);
    }
    fprintf(stderr, "\033[1;31m[ERROR]\033[0m Failed to process forward request for msg %lld\n", (long long)msg_id);
}

static void flush_media_group(AppContext *ctx, MediaGroup *group) {
    if (!ctx || !group || group->count == 0) return;

    int64_t chat_id = group->chat_id;
    size_t count = group->count;
    int64_t *msg_ids = group->msg_ids;
    char **texts = group->texts;

    if (!msg_ids || !texts) {
        if (msg_ids) free(msg_ids);
        if (texts) {
            for (size_t i = 0; i < count; i++) if (texts[i]) free(texts[i]);
            free(texts);
        }
        return;
    }

    char *title = get_chat_title(chat_id);
    const char *main_caption = "";
    for (size_t i = 0; i < count; i++) {
        if (texts[i] && strlen(texts[i]) > 0) {
            main_caption = texts[i];
            break;
        }
    }

    char *msg_link = construct_msg_link(chat_id, msg_ids[0]);
    char header[512];
    if (msg_link) {
        if (title) snprintf(header, sizeof(header), "<a href=\"%s\">Source</a>: %s\n\n", msg_link, title);
        else snprintf(header, sizeof(header), "<a href=\"%s\">Source</a>: Unknown\n\n", msg_link);
        free(msg_link);
    } else {
        header[0] = '\0';
    }

    size_t hlen = strlen(header);
    size_t clen = strlen(main_caption);
    char *full_text = malloc(hlen + clen + 1);
    if (full_text) {
        memcpy(full_text, header, hlen);
        memcpy(full_text + hlen, main_caption, clen + 1);
    }

    cJSON *parse_req = cJSON_CreateObject();
    if (parse_req && full_text) {
        cJSON_AddStringToObject(parse_req, "@type", "parseTextEntities");
        cJSON_AddStringToObject(parse_req, "text", full_text);
        cJSON *parse_mode = cJSON_AddObjectToObject(parse_req, "parse_mode");
        if (parse_mode) cJSON_AddStringToObject(parse_mode, "@type", "textParseModeHTML");

        cJSON *formatted = td_client_execute(parse_req);
        cJSON_Delete(parse_req);
        if (formatted) {
            if (count > 1) {
                cJSON *req = cJSON_CreateObject();
                if (req) {
                    cJSON_AddStringToObject(req, "@type", "sendMessage");
                    cJSON_AddNumberToObject(req, "chat_id", (double)ctx->cfg->aggregator_chat_id);
                }
                cJSON *input = req ? cJSON_AddObjectToObject(req, "input_message_content") : NULL;
                if (input) {
                    cJSON_AddStringToObject(input, "@type", "inputMessageAlbum");
                    cJSON *contents = cJSON_AddArrayToObject(input, "input_message_contents");
                    if (contents) {
                        for (size_t i = 0; i < count; i++) {
                            cJSON *item = cJSON_CreateObject();
                            cJSON_AddStringToObject(item, "@type", "inputMessageForwarded");
                            cJSON_AddNumberToObject(item, "from_chat_id", (double)chat_id);
                            cJSON_AddNumberToObject(item, "message_id", (double)msg_ids[i]);
                            cJSON *copy_opts = cJSON_AddObjectToObject(item, "copy_options");
                            if (copy_opts) {
                                cJSON_AddBoolToObject(copy_opts, "send_copy", true);
                                cJSON_AddBoolToObject(copy_opts, "replace_caption", true);
                                if (i == 0) {
                                    cJSON *new_caption = cJSON_CreateObject();
                                    cJSON_AddStringToObject(new_caption, "@type", "formattedText");
                                    cJSON *text_item = cJSON_GetObjectItem(formatted, "text");
                                    cJSON_AddStringToObject(new_caption, "text", (text_item && text_item->valuestring) ? text_item->valuestring : "");
                                    cJSON *entities = cJSON_GetObjectItem(formatted, "entities");
                                    if (entities) cJSON_AddItemToObject(new_caption, "entities", cJSON_Duplicate(entities, 1));
                                    cJSON_AddItemToObject(copy_opts, "new_caption", new_caption);
                                } else {
                                    cJSON *empty_formatted = cJSON_CreateObject();
                                    cJSON_AddStringToObject(empty_formatted, "@type", "formattedText");
                                    cJSON_AddStringToObject(empty_formatted, "text", "");
                                    cJSON_AddItemToObject(copy_opts, "new_caption", empty_formatted);
                                }
                            }
                            cJSON_AddItemToArray(contents, item);
                        }
                        td_client_send(ctx->td_client, req);
                    }
                }
                if (req) cJSON_Delete(req);
            } else {
                parse_and_send(ctx, chat_id, msg_ids[0], header, main_caption);
            }
            cJSON_Delete(formatted);
        }
    }
    if (parse_req && !full_text) cJSON_Delete(parse_req);
    if (full_text) free(full_text);
    if (title) free(title);

    for (size_t i = 0; i < count; i++) free(texts[i]);
    free(msg_ids);
    free(texts);
    printf("\033[1;32m[FORWARDED]\033[0m Album from %lld (%zu msgs) flushed\n", (long long)chat_id, count);
}

static void group_timer_cb(uv_timer_t *handle) {
    if (!handle || !handle->data) return;
    AppContext *ctx = (AppContext *)handle->data;

    uint64_t now = uv_now(ctx->loop);
    MediaGroup **curr = &ctx->groups;
    while (*curr) {
        MediaGroup *group = *curr;
        if (now - group->first_seen_ms > 2000) {
            flush_media_group(ctx, group);
            *curr = group->next;
            free(group);
        } else {
            curr = &group->next;
        }
    }

    if (!ctx->groups) {
        uv_timer_stop(handle);
    }
}

static void handle_new_message(AppContext *ctx, cJSON *msg) {
    if (!ctx || !msg) return;
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
    int64_t grouped_id = 0;
    if (gid_obj) {
        if (cJSON_IsString(gid_obj) && gid_obj->valuestring) {
            grouped_id = (int64_t)strtoll(gid_obj->valuestring, NULL, 10);
        } else if (cJSON_IsNumber(gid_obj)) {
            grouped_id = (int64_t)gid_obj->valuedouble;
        }
    }

    const char *text = "";
    cJSON *content = cJSON_GetObjectItem(msg, "content");
    if (content) {
        cJSON *type_obj = cJSON_GetObjectItem(content, "@type");
        if (type_obj && type_obj->valuestring) {
            cJSON *target_node = NULL;
            if (strcmp(type_obj->valuestring, "messageText") == 0) {
                target_node = cJSON_GetObjectItem(content, "text");
            } else {
                target_node = cJSON_GetObjectItem(content, "caption");
            }
            if (target_node) {
                cJSON *t_obj = cJSON_GetObjectItem(target_node, "text");
                if (t_obj && t_obj->valuestring) text = t_obj->valuestring;
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
    if (grouped_id == 0) {
        snprintf(hash_input, sizeof(hash_input), "%lld-0-%lld-%s", (long long)chat_id, (long long)reply_id, text);
    } else {
        snprintf(hash_input, sizeof(hash_input), "album-%lld-%lld-%lld", (long long)chat_id, (long long)grouped_id, (long long)msg_id);
    }

    char hash[65];
    compute_sha256(hash_input, NULL, 0, 0, hash);
    if (is_message_seen(hash)) return;
    mark_message_seen(hash);

    if (grouped_id != 0) {
        MediaGroup *group = ctx->groups;
        while (group) {
            if (group->chat_id == chat_id && group->grouped_id == grouped_id) break;
            group = group->next;
        }

        if (!group) {
            group = malloc(sizeof(MediaGroup));
            if (!group) return;
            group->chat_id = chat_id;
            group->grouped_id = grouped_id;
            group->msg_ids = NULL;
            group->texts = NULL;
            group->count = 0;
            group->first_seen_ms = uv_now(ctx->loop);
            group->next = ctx->groups;
            ctx->groups = group;

            if (!uv_is_active((uv_handle_t *)&ctx->group_timer)) {
                uv_timer_start(&ctx->group_timer, group_timer_cb, 500, 500);
            }
        }

        if (group->count < 100) {
            int64_t *new_ids = realloc(group->msg_ids, sizeof(int64_t) * (group->count + 1));
            char **new_texts = realloc(group->texts, sizeof(char *) * (group->count + 1));
            if (new_ids && new_texts) {
                group->msg_ids = new_ids;
                group->texts = new_texts;
                group->msg_ids[group->count] = msg_id;
                group->texts[group->count] = strdup(text);
                if (group->texts[group->count]) {
                    group->count++;
                    group->first_seen_ms = uv_now(ctx->loop);
                }
            }
        }
    } else {
        char *title = get_chat_title(chat_id);
        char *msg_link = construct_msg_link(chat_id, msg_id);
        char header[512];
        if (msg_link) {
            if (title) snprintf(header, sizeof(header), "<a href=\"%s\">Source</a>: %s\n\n", msg_link, title);
            else snprintf(header, sizeof(header), "<a href=\"%s\">Source</a>: Unknown\n\n", msg_link);
            parse_and_send(ctx, chat_id, msg_id, header, text);
            printf("\033[1;32m[FORWARDED]\033[0m From %lld, msg %lld\n", (long long)chat_id, (long long)msg_id);
            free(msg_link);
        }
        if (title) free(title);
    }
}

static void handle_command(AppContext *ctx, cJSON *msg) {
    if (!ctx || !msg) return;
    cJSON *cid_obj = cJSON_GetObjectItem(msg, "chat_id");
    if (!cid_obj || (int64_t)cid_obj->valuedouble != ctx->cfg->aggregator_chat_id) return;

    cJSON *content = cJSON_GetObjectItem(msg, "content");
    if (!content) return;
    cJSON *type_node = cJSON_GetObjectItem(content, "@type");
    if (!type_node || !type_node->valuestring || strcmp(type_node->valuestring, "messageText") != 0) return;

    cJSON *txt_node = cJSON_GetObjectItem(content, "text");
    if (!txt_node) return;
    cJSON *t_node = cJSON_GetObjectItem(txt_node, "text");
    if (!t_node || !t_node->valuestring) return;
    const char *text = t_node->valuestring;

    if (text && strncmp(text, "/status", 7) == 0) {
        int link_count = get_link_count();
        cJSON *req = cJSON_CreateObject();
        if (!req) return;
        cJSON_AddStringToObject(req, "@type", "sendMessage");
        cJSON_AddNumberToObject(req, "chat_id", (double)ctx->cfg->aggregator_chat_id);
        cJSON *input = cJSON_AddObjectToObject(req, "input_message_content");
        if (input) {
            cJSON_AddStringToObject(input, "@type", "inputMessageText");
            cJSON *reply_txt = cJSON_AddObjectToObject(input, "text");
            if (reply_txt) {
                char status_msg[256];
                snprintf(status_msg, sizeof(status_msg), "🤖 Aggregator Status\n\nChannels: %zu\nLinks: %d", ctx->cfg->source_count, link_count);
                cJSON_AddStringToObject(reply_txt, "text", status_msg);
            }
        }
        td_client_send(ctx->td_client, req);
        cJSON_Delete(req);
    }
}

static void handle_chat_update(AppContext *ctx, int64_t cid, cJSON *chat) {
    if (!ctx || !chat) return;
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
                if (!has_link && !was_info_requested(sid)) {
                    mark_info_requested(sid);
                    cJSON *req = cJSON_CreateObject();
                    if (req) {
                        cJSON_AddStringToObject(req, "@type", "getSupergroupFullInfo");
                        cJSON_AddNumberToObject(req, "supergroup_id", (double)sid);
                        td_client_send(ctx->td_client, req);
                        cJSON_Delete(req);
                    }
                }
            }
        }
    }
}

static void process_folders(AppContext *ctx, cJSON *fs) {
    if (!ctx || !fs || !cJSON_IsArray(fs)) return;
    int size = cJSON_GetArraySize(fs);
    for (int i = 0; i < size; i++) {
        cJSON *f = cJSON_GetArrayItem(fs, i);
        if (!f) continue;
        cJSON *name_obj = cJSON_GetObjectItem(f, "name");
        cJSON *id_obj = cJSON_GetObjectItem(f, "id");
        if (name_obj && id_obj) {
            cJSON *txt_node = cJSON_GetObjectItem(name_obj, "text");
            if (txt_node && cJSON_GetObjectItem(txt_node, "text")) {
                const char *title = cJSON_GetObjectItem(txt_node, "text")->valuestring;
                if (!title) continue;
                for (size_t k = 0; k < ctx->cfg->folder_count; k++) {
                    if (ctx->cfg->folder_names[k] && strcmp(title, ctx->cfg->folder_names[k]) == 0) {
                        cJSON *req = cJSON_CreateObject();
                        if (req) {
                            cJSON_AddStringToObject(req, "@type", "getChatFolder");
                            cJSON_AddNumberToObject(req, "chat_folder_id", (double)id_obj->valueint);
                            td_client_send(ctx->td_client, req);
                            cJSON_Delete(req);
                        }
                        break;
                    }
                }
            }
        }
    }
}

void handle_update(AppContext *ctx, cJSON *update) {
    if (!ctx || !update) return;
    cJSON *type_obj = cJSON_GetObjectItem(update, "@type");
    if (!type_obj || !type_obj->valuestring) return;
    const char *type = type_obj->valuestring;

    // fprintf(stderr, "[DEBUG] Received update type: %s\n", type);

    if (strcmp(type, "user") == 0) {
        cJSON *first_name = cJSON_GetObjectItem(update, "first_name");
        cJSON *is_bot = cJSON_GetObjectItem(update, "type");
        const char *bot_status = "User";
        if (is_bot) {
            cJSON *bot_type = cJSON_GetObjectItem(is_bot, "@type");
            if (bot_type && bot_type->valuestring && strcmp(bot_type->valuestring, "userTypeBot") == 0) {
                bot_status = "Bot";
                bool wants_folders = (ctx->cfg->folder_count > 0);
                bool has_phone = (ctx->cfg->phone && strlen(ctx->cfg->phone) > 0);
                bool has_bot_token = (ctx->cfg->bot_token && strlen(ctx->cfg->bot_token) > 0);
                if (wants_folders && has_phone && has_bot_token && !ctx->reauth_requested) {
                    ctx->reauth_requested = true;
                    fprintf(stderr, "[WARN] Folder mode needs a user account. This run is still authenticated as bot.\n");
                }
            } else if (!ctx->initial_sync_requested) {
                ctx->initial_sync_requested = true;
                cJSON *req_chats = cJSON_CreateObject();
                if (req_chats) {
                    cJSON_AddStringToObject(req_chats, "@type", "getChats");
                    cJSON *chat_list = cJSON_AddObjectToObject(req_chats, "chat_list");
                    if (chat_list) cJSON_AddStringToObject(chat_list, "@type", "chatListMain");
                    cJSON_AddNumberToObject(req_chats, "limit", 100);
                    td_client_send(ctx->td_client, req_chats);
                    cJSON_Delete(req_chats);
                }
            }
        } else if (!ctx->initial_sync_requested) {
            ctx->initial_sync_requested = true;
            cJSON *req_chats = cJSON_CreateObject();
            if (req_chats) {
                cJSON_AddStringToObject(req_chats, "@type", "getChats");
                cJSON *chat_list = cJSON_AddObjectToObject(req_chats, "chat_list");
                if (chat_list) cJSON_AddStringToObject(chat_list, "@type", "chatListMain");
                cJSON_AddNumberToObject(req_chats, "limit", 100);
                td_client_send(ctx->td_client, req_chats);
                cJSON_Delete(req_chats);
            }
        }
        // fprintf(stderr, "[DEBUG] Current Account: %s (%s)\n",
                // (first_name && first_name->valuestring) ? first_name->valuestring : "Unknown",
                // bot_status);
    } else if (strcmp(type, "updateAuthorizationState") == 0) {
        ctx->saw_auth_update = true;
        cJSON *auth_state = cJSON_GetObjectItem(update, "authorization_state");
        if (auth_state) handle_auth(ctx->td_client, ctx->cfg, auth_state);
    } else if (strncmp(type, "authorizationState", strlen("authorizationState")) == 0) {
        if (!ctx->saw_auth_update) handle_auth(ctx->td_client, ctx->cfg, update);
    } else if (strcmp(type, "updateChatFolders") == 0 || strcmp(type, "chatFolders") == 0) {
        cJSON *fs = cJSON_GetObjectItem(update, "chat_folders");
        if (fs) process_folders(ctx, fs);
    } else if (strcmp(type, "chatFolder") == 0) {
        cJSON *ids = cJSON_GetObjectItem(update, "included_chat_ids");
        if (ids && cJSON_IsArray(ids)) {
            for (int j = 0; j < cJSON_GetArraySize(ids); j++) {
                cJSON *id_item = cJSON_GetArrayItem(ids, j);
                if (!id_item) continue;
                int64_t cid = (int64_t)id_item->valuedouble;
                bool already = false;
                for (size_t n = 0; n < ctx->cfg->source_count; n++) {
                    if (ctx->cfg->source_channels[n] == cid) { already = true; break; }
                }
                if (!already) {
                    int64_t *new_sources = realloc(ctx->cfg->source_channels, sizeof(int64_t) * (ctx->cfg->source_count + 1));
                    if (new_sources) {
                        ctx->cfg->source_channels = new_sources;
                        ctx->cfg->source_channels[ctx->cfg->source_count++] = cid;
                        cJSON *get_chat = cJSON_CreateObject();
                        if (get_chat) {
                            cJSON_AddStringToObject(get_chat, "@type", "getChat");
                            cJSON_AddNumberToObject(get_chat, "chat_id", (double)cid);
                            td_client_send(ctx->td_client, get_chat);
                            cJSON_Delete(get_chat);
                        }
                    }
                }
            }
        }
    } else if (strcmp(type, "updateNewMessage") == 0) {
        cJSON *msg = cJSON_GetObjectItem(update, "message");
        if (msg) {
            handle_new_message(ctx, msg);
            handle_command(ctx, msg);
        }
    } else if (strcmp(type, "updateChatTitle") == 0) {
        cJSON *cid_node = cJSON_GetObjectItem(update, "chat_id");
        cJSON *title_node = cJSON_GetObjectItem(update, "title");
        if (cid_node && title_node && title_node->valuestring) {
            store_chat_title((int64_t)cid_node->valuedouble, title_node->valuestring);
        }
    } else if (strcmp(type, "chat") == 0 || strcmp(type, "updateNewChat") == 0) {
        cJSON *chat = strcmp(type, "chat") == 0 ? update : cJSON_GetObjectItem(update, "chat");
        if (chat) {
            cJSON *id_node = cJSON_GetObjectItem(chat, "id");
            if (id_node) handle_chat_update(ctx, (int64_t)id_node->valuedouble, chat);
        }
    } else if (strcmp(type, "updateSupergroupFullInfo") == 0) {
        cJSON *sid_node = cJSON_GetObjectItem(update, "supergroup_id");
        if (sid_node) {
            int64_t sid = (int64_t)sid_node->valuedouble;
            int64_t cid = get_chat_id_by_supergroup(sid);
            cJSON *full_info = cJSON_GetObjectItem(update, "full_info");
            if (cid != 0 && full_info) {
                cJSON *invite = cJSON_GetObjectItem(full_info, "invite_link");
                if (!invite) invite = cJSON_GetObjectItem(full_info, "primary_invite_link");
                if (invite && invite->valuestring && strlen(invite->valuestring) > 0) {
                    store_chat_invite_link(cid, invite->valuestring);
                }
            }
        }
    } else if (strcmp(type, "error") == 0) {
        cJSON *msg = cJSON_GetObjectItem(update, "message");
        if (msg && msg->valuestring) fprintf(stderr, "\033[1;31m[ERROR]\033[0m TDLib: %s\n", msg->valuestring);
    }
}
