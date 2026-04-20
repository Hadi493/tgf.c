#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <uv.h>
#include "config.h"
#include "network.h"
#include "storage.h"
#include "handlers.h"

static void on_update(uv_async_t *handle) {
    AppContext *ctx = (AppContext *)handle->data;
    while (1) {
        uv_mutex_lock(&ctx->queue_mutex);
        UpdateNode *node = ctx->queue_head;
        if (!node) {
            uv_mutex_unlock(&ctx->queue_mutex);
            break;
        }
        ctx->queue_head = node->next;
        if (!ctx->queue_head) ctx->queue_tail = NULL;
        uv_mutex_unlock(&ctx->queue_mutex);
        handle_update(ctx, node->update);
        cJSON_Delete(node->update);
        free(node);
    }
}

static void receiver_thread(void *arg) {
    AppContext *ctx = (AppContext *)arg;
    uv_async_t *async = (uv_async_t *)ctx->loop->data;
    while (1) {
        cJSON *update = td_client_receive(ctx->td_client, 1.0);
        if (update) {
            UpdateNode *node = malloc(sizeof(UpdateNode));
            node->update = update;
            node->next = NULL;
            uv_mutex_lock(&ctx->queue_mutex);
            if (ctx->queue_tail) ctx->queue_tail->next = node;
            else ctx->queue_head = node;
            ctx->queue_tail = node;
            uv_mutex_unlock(&ctx->queue_mutex);
            uv_async_send(async);
        }
    }
}

int main(int argc, char **argv) {
    if (argc > 1) {
        if (strcmp(argv[1], "add") == 0 && argc > 3) {
            add_source("config.toml", atoll(argv[3]), strcmp(argv[2], "folder") == 0);
            return 0;
        }
    }

    Config *cfg = load_config(".env", "config.toml");
    if (!cfg->api_id || !cfg->api_hash) {
        fprintf(stderr, "Error: TELEGRAM_API_ID and TELEGRAM_API_HASH must be set in .env\n");
        return 1;
    }

    if (init_storage(cfg->db_path) != 0) return 1;

    void *client = td_client_create();
    
    cJSON *verb = cJSON_CreateObject();
    cJSON_AddStringToObject(verb, "@type", "setLogVerbosityLevel");
    cJSON_AddNumberToObject(verb, "new_verbosity_level", 1);
    td_client_send(client, verb);
    cJSON_Delete(verb);

    uv_loop_t *loop = uv_default_loop();
    AppContext ctx = {
        .td_client = client,
        .cfg = cfg,
        .loop = loop,
        .queue_head = NULL,
        .queue_tail = NULL,
        .groups = NULL
    };
    uv_mutex_init(&ctx.queue_mutex);
    uv_timer_init(loop, &ctx.group_timer);
    ctx.group_timer.data = &ctx;

    uv_async_t async;
    async.data = &ctx;
    loop->data = &async;
    uv_async_init(loop, &async, on_update);

    uv_thread_t thread;
    uv_thread_create(&thread, receiver_thread, &ctx);

    printf("Aggregator started. Waiting for Telegram synchronization...\n");
    uv_run(loop, UV_RUN_DEFAULT);
    return 0;
}
