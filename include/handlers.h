#ifndef HANDLERS_H
#define HANDLERS_H

#include <cJSON.h>
#include <uv.h>
#include "config.h"

typedef struct UpdateNode {
    cJSON *update;
    struct UpdateNode *next;
} UpdateNode;

typedef struct MediaGroup {
    int64_t chat_id;
    int64_t grouped_id;
    int64_t *msg_ids;
    char **texts;
    size_t count;
    uint64_t first_seen_ms;
    struct MediaGroup *next;
} MediaGroup;

typedef struct {
    void *td_client;
    Config *cfg;
    uv_loop_t *loop;
    UpdateNode *queue_head;
    UpdateNode *queue_tail;
    uv_mutex_t queue_mutex;
    MediaGroup *groups;
    uv_timer_t group_timer;
    bool saw_auth_update;
    bool reauth_requested;
    bool initial_sync_requested;
} AppContext;

void handle_update(AppContext *ctx, cJSON *update);

#endif
