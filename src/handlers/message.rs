use crate::td_client::TdClient;
use crate::config::Config;
use crate::storage::Storage;
use crate::utils::{compute_sha256, format_source_link};
use serde_json::{json, Value};
use std::sync::Arc;
use tokio::sync::Mutex;
use std::collections::{HashMap, HashSet};
use std::time::{Duration, Instant};

pub struct MediaGroup {
    pub chat_id: i64,
    pub _grouped_id: i64,
    pub msg_ids: Vec<i64>,
    pub texts: Vec<String>,
    pub first_seen: Instant,
}

pub struct MessageHandler {
    client: Arc<TdClient>,
    cfg: Arc<Config>,
    storage: Arc<Storage>,
    groups: Arc<Mutex<HashMap<i64, MediaGroup>>>,
    monitored_chats: Arc<Mutex<HashSet<i64>>>,
}

impl MessageHandler {
    pub fn new(client: Arc<TdClient>, cfg: Arc<Config>, storage: Arc<Storage>) -> Self {
        let mut monitored = HashSet::new();
        for &id in &cfg.source_channels {
            monitored.insert(id);
        }
        Self {
            client,
            cfg,
            storage,
            groups: Arc::new(Mutex::new(HashMap::new())),
            monitored_chats: Arc::new(Mutex::new(monitored)),
        }
    }

    pub async fn add_monitored_chat(&self, chat_id: i64) {
        let mut monitored = self.monitored_chats.lock().await;
        monitored.insert(chat_id);
    }

    pub async fn handle_new_message(&self, msg: &Value) {
        let chat_id = msg["chat_id"].as_i64().unwrap_or(0);
        {
            let monitored = self.monitored_chats.lock().await;
            if !monitored.contains(&chat_id) {
                return;
            }
        }

        let msg_id = msg["id"].as_i64().unwrap_or(0);
        let grouped_id = msg["media_album_id"]
            .as_str()
            .and_then(|s| s.parse::<i64>().ok())
            .or_else(|| msg["media_album_id"].as_i64())
            .unwrap_or(0);

        let mut text = String::new();
        if let Some(content) = msg.get("content") {
            if let Some(txt_obj) = content.get("text").or(content.get("caption")) {
                if let Some(t) = txt_obj.get("text").and_then(|v| v.as_str()) {
                    text = t.to_string();
                }
            }
        }

        let reply_id = msg["reply_to"]["message_id"].as_i64().unwrap_or(0);
        let hash_input = if grouped_id == 0 {
            format!("{}-0-{}-{}", chat_id, reply_id, text)
        } else {
            format!("album-{}-{}-{}", chat_id, grouped_id, msg_id)
        };

        let hash = compute_sha256(&hash_input, None, 0);
        if self.storage.is_message_seen(&hash) {
            return;
        }
        self.storage.mark_message_seen(&hash);

        if grouped_id != 0 {
            let mut groups = self.groups.lock().await;
            let group = groups.entry(grouped_id).or_insert_with(|| MediaGroup {
                chat_id,
                _grouped_id: grouped_id,
                msg_ids: Vec::new(),
                texts: Vec::new(),
                first_seen: Instant::now(),
            });
            group.msg_ids.push(msg_id);
            group.texts.push(text);
            
            let groups_clone = Arc::clone(&self.groups);
            let client_clone = Arc::clone(&self.client);
            let cfg_clone = Arc::clone(&self.cfg);
            let storage_clone = Arc::clone(&self.storage);

            tokio::spawn(async move {
                tokio::time::sleep(Duration::from_millis(2000)).await;
                let mut groups = groups_clone.lock().await;
                if let Some(group) = groups.get(&grouped_id) {
                    if group.first_seen.elapsed() >= Duration::from_millis(2000) {
                        let group = groups.remove(&grouped_id).unwrap();
                        Self::flush_media_group(&client_clone, &cfg_clone, &storage_clone, group).await;
                    }
                }
            });
        } else {
            self.forward_single_message(chat_id, msg_id, &text).await;
        }
    }

    async fn forward_single_message(&self, chat_id: i64, msg_id: i64, text: &str) {
        let title = self.storage.get_chat_title(chat_id).unwrap_or_else(|| {
            self.client.send(json!({ "@type": "getChat", "chat_id": chat_id }));
            format!("{}", chat_id)
        });
        let link = format_source_link(chat_id, msg_id);
        let header = format!("<a href=\"{}\">Source</a>: {}\n\n", link, title);
        
        self.parse_and_send(chat_id, msg_id, &header, text).await;
        println!("\x1b[1;32m[FORWARD]\x1b[0m From {} ({}), msg {}", title, chat_id, msg_id);
    }

    async fn parse_and_send(&self, from_chat_id: i64, msg_id: i64, header: &str, original_text: &str) {
        let full_text = format!("{}{}", header, original_text);
        let formatted = self.client.execute(json!({
            "@type": "parseTextEntities",
            "text": full_text,
            "parse_mode": { "@type": "textParseModeHTML" }
        }));

        if let Some(fmt) = formatted {
            if fmt["@type"].as_str() == Some("error") {
                eprintln!("[ERROR] Parsing failed: {:?}", fmt);
                return;
            }
            self.client.send(json!({
                "@type": "sendMessage",
                "chat_id": self.cfg.aggregator_chat_id,
                "input_message_content": {
                    "@type": "inputMessageForwarded",
                    "from_chat_id": from_chat_id,
                    "message_id": msg_id,
                    "copy_options": {
                        "send_copy": true,
                        "replace_caption": true,
                        "new_caption": fmt
                    }
                }
            }));
        } else {
            eprintln!("[ERROR] No response from parseTextEntities");
        }
    }

    async fn flush_media_group(client: &TdClient, cfg: &Config, storage: &Storage, group: MediaGroup) {
        let title = storage.get_chat_title(group.chat_id).unwrap_or_else(|| {
            client.send(json!({ "@type": "getChat", "chat_id": group.chat_id }));
            format!("{}", group.chat_id)
        });
        let link = format_source_link(group.chat_id, group.msg_ids[0]);
        let header = format!("<a href=\"{}\">Source</a>: {}\n\n", link, title);
        
        let main_caption = group.texts.iter().find(|t| !t.is_empty()).cloned().unwrap_or_default();
        let full_text = format!("{}{}", header, main_caption);
        
        let formatted = client.execute(json!({
            "@type": "parseTextEntities",
            "text": full_text,
            "parse_mode": { "@type": "textParseModeHTML" }
        }));

        if let Some(fmt) = formatted {
            if fmt["@type"].as_str() == Some("error") {
                eprintln!("[ERROR] Parsing failed: {:?}", fmt);
                return;
            }
            if group.msg_ids.len() > 1 {
                let mut contents = Vec::new();
                for (i, &mid) in group.msg_ids.iter().enumerate() {
                    let mut item = json!({
                        "@type": "inputMessageForwarded",
                        "from_chat_id": group.chat_id,
                        "message_id": mid,
                        "copy_options": {
                            "send_copy": true,
                            "replace_caption": true
                        }
                    });
                    if i == 0 {
                        item["copy_options"]["new_caption"] = fmt.clone();
                    } else {
                        item["copy_options"]["new_caption"] = json!({
                            "@type": "formattedText",
                            "text": ""
                        });
                    }
                    contents.push(item);
                }

                client.send(json!({
                    "@type": "sendMessage",
                    "chat_id": cfg.aggregator_chat_id,
                    "input_message_content": {
                        "@type": "inputMessageAlbum",
                        "input_message_contents": contents
                    }
                }));
            } else {
                client.send(json!({
                    "@type": "sendMessage",
                    "chat_id": cfg.aggregator_chat_id,
                    "input_message_content": {
                        "@type": "inputMessageForwarded",
                        "from_chat_id": group.chat_id,
                        "message_id": group.msg_ids[0],
                        "copy_options": {
                            "send_copy": true,
                            "replace_caption": true,
                            "new_caption": fmt
                        }
                    }
                }));
            }
            println!("\x1b[1;32m[FORWARD]\x1b[0m Album from {} ({}) ({} msgs) flushed", title, group.chat_id, group.msg_ids.len());
        }
    }

    pub async fn handle_command(&self, msg: &Value) {
        let chat_id = msg["chat_id"].as_i64().unwrap_or(0);
        if chat_id != self.cfg.aggregator_chat_id {
            return;
        }

        if let Some(text) = msg["content"]["text"]["text"].as_str() {
            if text.starts_with("/status") {
                let monitored_count = self.monitored_chats.lock().await.len();
                let status_msg = format!(
                    "🤖 Aggregator Status\n\nChannels: {}\nLinks: {}",
                    monitored_count,
                    self.storage.get_link_count()
                );
                self.client.send(json!({
                    "@type": "sendMessage",
                    "chat_id": self.cfg.aggregator_chat_id,
                    "input_message_content": {
                        "@type": "inputMessageText",
                        "text": {
                            "text": status_msg
                        }
                    }
                }));
            }
        }
    }
}
