use rusqlite::{params, Connection};
use std::sync::{Arc, Mutex};

pub struct Storage {
    conn: Arc<Mutex<Connection>>,
}

impl Storage {
    pub fn init(path: &str) -> anyhow::Result<Self> {
        let conn = Connection::open(path)?;
        conn.execute_batch(
            "CREATE TABLE IF NOT EXISTS seen_messages (content_hash TEXT PRIMARY KEY, timestamp DATETIME DEFAULT CURRENT_TIMESTAMP);
             CREATE TABLE IF NOT EXISTS message_mappings (source_chat_id INTEGER, source_msg_id INTEGER, aggregator_id INTEGER, aggregator_msg_id INTEGER, PRIMARY KEY (source_chat_id, source_msg_id, aggregator_id));
             CREATE TABLE IF NOT EXISTS chat_info (chat_id INTEGER PRIMARY KEY, title TEXT, invite_link TEXT);
             CREATE TABLE IF NOT EXISTS supergroup_map (supergroup_id INTEGER PRIMARY KEY, chat_id INTEGER);
             CREATE INDEX IF NOT EXISTS idx_source ON message_mappings (source_chat_id, source_msg_id);"
        )?;
        Ok(Self { conn: Arc::new(Mutex::new(conn)) })
    }

    pub fn is_message_seen(&self, hash: &str) -> bool {
        let conn = self.conn.lock().unwrap();
        conn.query_row("SELECT 1 FROM seen_messages WHERE content_hash = ?", params![hash], |_| Ok(1)).is_ok()
    }

    pub fn mark_message_seen(&self, hash: &str) {
        let conn = self.conn.lock().unwrap();
        let _ = conn.execute("INSERT OR IGNORE INTO seen_messages (content_hash) VALUES (?)", params![hash]);
    }

    pub fn store_chat_title(&self, chat_id: i64, title: &str) {
        let conn = self.conn.lock().unwrap();
        let _ = conn.execute(
            "INSERT INTO chat_info (chat_id, title) VALUES (?, ?) ON CONFLICT(chat_id) DO UPDATE SET title = excluded.title",
            params![chat_id, title]
        );
    }

    pub fn get_chat_title(&self, chat_id: i64) -> Option<String> {
        let conn = self.conn.lock().unwrap();
        conn.query_row("SELECT title FROM chat_info WHERE chat_id = ?", params![chat_id], |row| row.get(0)).ok()
    }

    pub fn store_chat_invite_link(&self, chat_id: i64, link: &str) {
        let conn = self.conn.lock().unwrap();
        let _ = conn.execute(
            "INSERT INTO chat_info (chat_id, invite_link) VALUES (?, ?) ON CONFLICT(chat_id) DO UPDATE SET invite_link = excluded.invite_link",
            params![chat_id, link]
        );
    }

    pub fn store_supergroup_map(&self, supergroup_id: i64, chat_id: i64) {
        let conn = self.conn.lock().unwrap();
        let _ = conn.execute("INSERT OR REPLACE INTO supergroup_map (supergroup_id, chat_id) VALUES (?, ?)", params![supergroup_id, chat_id]);
    }

    pub fn get_chat_id_by_supergroup(&self, supergroup_id: i64) -> Option<i64> {
        let conn = self.conn.lock().unwrap();
        conn.query_row("SELECT chat_id FROM supergroup_map WHERE supergroup_id = ?", params![supergroup_id], |row| row.get(0)).ok()
    }

    pub fn get_link_count(&self) -> i32 {
        let conn = self.conn.lock().unwrap();
        conn.query_row("SELECT count(*) FROM chat_info WHERE invite_link IS NOT NULL", [], |row| row.get(0)).unwrap_or(0)
    }
}
