#ifndef UTILS_H
#define UTILS_H

#include <stddef.h>
#include <stdint.h>

void compute_sha256(const char *text, const uint8_t *media, size_t media_len, int64_t reply_id, char *out_hex);
char *format_header(const char *chat_title, const char *invite_link);
char **split_message(const char *text, size_t limit, size_t *count);
void free_split_message(char **parts, size_t count);

#endif
