#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "storage.h"

static sqlite3 *db;

int init_storage(const char *path) {
    if (sqlite3_open(path, &db) != SQLITE_OK) return -1;
    char *err;
    const char *schema = 
        "CREATE TABLE IF NOT EXISTS seen_messages (content_hash TEXT PRIMARY KEY, timestamp DATETIME DEFAULT CURRENT_TIMESTAMP);"
        "CREATE TABLE IF NOT EXISTS message_mappings (source_chat_id INTEGER, source_msg_id INTEGER, aggregator_id INTEGER, aggregator_msg_id INTEGER, PRIMARY KEY (source_chat_id, source_msg_id, aggregator_id));"
        "CREATE TABLE IF NOT EXISTS chat_info (chat_id INTEGER PRIMARY KEY, title TEXT, invite_link TEXT);"
        "CREATE TABLE IF NOT EXISTS supergroup_map (supergroup_id INTEGER PRIMARY KEY, chat_id INTEGER);"
        "CREATE INDEX IF NOT EXISTS idx_source ON message_mappings (source_chat_id, source_msg_id);";
    if (sqlite3_exec(db, schema, NULL, NULL, &err) != SQLITE_OK) return -1;
    return 0;
}

void close_storage(void) {
    if (db) sqlite3_close(db);
}

int is_message_seen(const char *hash) {
    sqlite3_stmt *stmt;
    const char *query = "SELECT 1 FROM seen_messages WHERE content_hash = ?;";
    if (sqlite3_prepare_v2(db, query, -1, &stmt, NULL) != SQLITE_OK) return 0;
    sqlite3_bind_text(stmt, 1, hash, -1, SQLITE_STATIC);
    int seen = (sqlite3_step(stmt) == SQLITE_ROW);
    sqlite3_finalize(stmt);
    return seen;
}

void mark_message_seen(const char *hash) {
    sqlite3_stmt *stmt;
    const char *query = "INSERT OR IGNORE INTO seen_messages (content_hash) VALUES (?);";
    if (sqlite3_prepare_v2(db, query, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, hash, -1, SQLITE_STATIC);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
}

void store_chat_title(int64_t chat_id, const char *title) {
    sqlite3_stmt *stmt;
    const char *query = "INSERT INTO chat_info (chat_id, title) VALUES (?, ?) ON CONFLICT(chat_id) DO UPDATE SET title = excluded.title;";
    if (sqlite3_prepare_v2(db, query, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_int64(stmt, 1, chat_id);
        sqlite3_bind_text(stmt, 2, title, -1, SQLITE_STATIC);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
}

char *get_chat_title(int64_t chat_id) {
    sqlite3_stmt *stmt;
    const char *query = "SELECT title FROM chat_info WHERE chat_id = ?;";
    char *title = NULL;
    if (sqlite3_prepare_v2(db, query, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_int64(stmt, 1, chat_id);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            const unsigned char *t = sqlite3_column_text(stmt, 0);
            if (t) title = strdup((const char *)t);
        }
        sqlite3_finalize(stmt);
    }
    return title;
}

void store_chat_invite_link(int64_t chat_id, const char *link) {
    sqlite3_stmt *stmt;
    const char *query = "INSERT INTO chat_info (chat_id, invite_link) VALUES (?, ?) ON CONFLICT(chat_id) DO UPDATE SET invite_link = excluded.invite_link;";
    if (sqlite3_prepare_v2(db, query, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_int64(stmt, 1, chat_id);
        sqlite3_bind_text(stmt, 2, link, -1, SQLITE_STATIC);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
}

char *get_chat_invite_link(int64_t chat_id) {
    sqlite3_stmt *stmt;
    const char *query = "SELECT invite_link FROM chat_info WHERE chat_id = ?;";
    char *link = NULL;
    if (sqlite3_prepare_v2(db, query, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_int64(stmt, 1, chat_id);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            const unsigned char *l = sqlite3_column_text(stmt, 0);
            if (l) link = strdup((const char *)l);
        }
        sqlite3_finalize(stmt);
    }
    return link;
}

void store_supergroup_map(int64_t supergroup_id, int64_t chat_id) {
    sqlite3_stmt *stmt;
    const char *query = "INSERT OR REPLACE INTO supergroup_map (supergroup_id, chat_id) VALUES (?, ?);";
    if (sqlite3_prepare_v2(db, query, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_int64(stmt, 1, supergroup_id);
        sqlite3_bind_int64(stmt, 2, chat_id);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
}

int64_t get_chat_id_by_supergroup(int64_t supergroup_id) {
    sqlite3_stmt *stmt;
    const char *query = "SELECT chat_id FROM supergroup_map WHERE supergroup_id = ?;";
    int64_t chat_id = 0;
    if (sqlite3_prepare_v2(db, query, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_int64(stmt, 1, supergroup_id);
        if (sqlite3_step(stmt) == SQLITE_ROW) chat_id = sqlite3_column_int64(stmt, 0);
        sqlite3_finalize(stmt);
    }
    return chat_id;
}

int get_link_count(void) {
    sqlite3_stmt *stmt;
    int count = 0;
    if (sqlite3_prepare_v2(db, "SELECT count(*) FROM chat_info WHERE invite_link IS NOT NULL", -1, &stmt, NULL) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) count = sqlite3_column_int(stmt, 0);
        sqlite3_finalize(stmt);
    }
    return count;
}

void add_message_mapping(int64_t s_chat_id, int64_t s_msg_id, int64_t a_chat_id, int64_t a_msg_id) {
    sqlite3_stmt *stmt;
    const char *query = "INSERT OR REPLACE INTO message_mappings VALUES (?, ?, ?, ?);";
    if (sqlite3_prepare_v2(db, query, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_int64(stmt, 1, s_chat_id);
        sqlite3_bind_int64(stmt, 2, s_msg_id);
        sqlite3_bind_int64(stmt, 3, a_chat_id);
        sqlite3_bind_int64(stmt, 4, a_msg_id);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
}

int64_t get_aggregator_msg_id(int64_t s_chat_id, int64_t s_msg_id, int64_t a_chat_id) {
    sqlite3_stmt *stmt;
    const char *query = "SELECT aggregator_msg_id FROM message_mappings WHERE source_chat_id = ? AND source_msg_id = ? AND aggregator_id = ?;";
    int64_t id = 0;
    if (sqlite3_prepare_v2(db, query, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_int64(stmt, 1, s_chat_id);
        sqlite3_bind_int64(stmt, 2, s_msg_id);
        sqlite3_bind_int64(stmt, 3, a_chat_id);
        if (sqlite3_step(stmt) == SQLITE_ROW) id = sqlite3_column_int64(stmt, 0);
        sqlite3_finalize(stmt);
    }
    return id;
}

void remove_message_mapping(int64_t s_chat_id, int64_t s_msg_id) {
    sqlite3_stmt *stmt;
    const char *query = "DELETE FROM message_mappings WHERE source_chat_id = ? AND source_msg_id = ?;";
    if (sqlite3_prepare_v2(db, query, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_int64(stmt, 1, s_chat_id);
        sqlite3_bind_int64(stmt, 2, s_msg_id);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
}
