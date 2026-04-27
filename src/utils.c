#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "utils.h"
#include "sha256.h"

void compute_sha256(const char *text, const uint8_t *media, size_t media_len, int64_t reply_id, char *out_hex) {
    SHA256_CTX ctx;
    uint8_t hash[32];
    sha256_init(&ctx);
    if (text) sha256_update(&ctx, (uint8_t *)text, strlen(text));
    if (media) sha256_update(&ctx, media, media_len);
    sha256_update(&ctx, (uint8_t *)&reply_id, sizeof(reply_id));
    sha256_final(&ctx, hash);
    for (int i = 0; i < 32; i++) {
        sprintf(out_hex + (i * 2), "%02x", hash[i]);
    }
    out_hex[64] = '\0';
}

char *format_header(const char *author_name, const char *invite_link) {
    size_t len = (author_name ? strlen(author_name) : 0) + (invite_link ? strlen(invite_link) : 0) + 64;
    char *header = malloc(len);
    if (invite_link) {
        snprintf(header, len, "<b>%s</b> (<a href=\"%s\">Source</a>)", author_name ? author_name : "Source", invite_link);
    } else {
        snprintf(header, len, "<b>%s</b>", author_name ? author_name : "Source");
    }
    return header;
}

char **split_message(const char *text, size_t limit, size_t *count) {
    size_t len = strlen(text);
    *count = (len / limit) + 1;
    char **parts = malloc(sizeof(char *) * (*count));
    size_t current = 0;
    const char *ptr = text;
    while (current < *count && *ptr) {
        size_t chunk = strlen(ptr);
        if (chunk > limit) {
            chunk = limit;
            while (chunk > 0 && ptr[chunk] != ' ' && ptr[chunk] != '\n') chunk--;
            if (chunk == 0) chunk = limit;
        }
        parts[current] = malloc(chunk + 1);
        memcpy(parts[current], ptr, chunk);
        parts[current][chunk] = '\0';
        ptr += chunk;
        current++;
    }
    *count = current;
    return parts;
}

void free_split_message(char **parts, size_t count) {
    for (size_t i = 0; i < count; i++) free(parts[i]);
    free(parts);
}
