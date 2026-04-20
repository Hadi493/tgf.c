#ifndef STORAGE_H
#define STORAGE_H

#include <sqlite3.h>
#include <stdint.h>

int init_storage(const char *path);
void close_storage(void);
int is_message_seen(const char *hash);
void mark_message_seen(const char *hash);
void store_chat_title(int64_t chat_id, const char *title);
char *get_chat_title(int64_t chat_id);
void store_chat_invite_link(int64_t chat_id, const char *link);
char *get_chat_invite_link(int64_t chat_id);
void store_supergroup_map(int64_t supergroup_id, int64_t chat_id);
int64_t get_chat_id_by_supergroup(int64_t supergroup_id);
int get_link_count(void);
void add_message_mapping(int64_t s_chat_id, int64_t s_msg_id, int64_t a_chat_id, int64_t a_msg_id);
int64_t get_aggregator_msg_id(int64_t s_chat_id, int64_t s_msg_id, int64_t a_chat_id);
void remove_message_mapping(int64_t s_chat_id, int64_t s_msg_id);

#endif
