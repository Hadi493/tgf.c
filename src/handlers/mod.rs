pub mod auth;
pub mod message;

use crate::td_client::TdClient;
use crate::config::Config;
use crate::storage::Storage;
use serde_json::{json, Value};
use std::sync::Arc;
use message::MessageHandler;

pub struct UpdateDispatcher {
    client: Arc<TdClient>,
    cfg: Arc<Config>,
    storage: Arc<Storage>,
    msg_handler: MessageHandler,
}

impl UpdateDispatcher {
    pub fn new(client: Arc<TdClient>, cfg: Arc<Config>, storage: Arc<Storage>) -> Self {
        let msg_handler = MessageHandler::new(Arc::clone(&client), Arc::clone(&cfg), Arc::clone(&storage));
        Self {
            client,
            cfg,
            storage,
            msg_handler,
        }
    }

    pub async fn dispatch(&self, update: Value) {
        let update_type = update["@type"].as_str().unwrap_or("");

        match update_type {
            "updateAuthorizationState" => {
                let state = &update["authorization_state"];
                auth::handle_auth(&self.client, &self.cfg, state);
                if state["@type"].as_str() == Some("authorizationStateReady") {
                    self.client.send(json!({ "@type": "getMe" }));
                    for &id in &self.cfg.source_channels {
                        self.client.send(json!({ "@type": "getChat", "chat_id": id }));
                    }
                    if !auth::should_use_bot_auth(&self.cfg) {
                        self.client.send(json!({
                            "@type": "getChats",
                            "chat_list": { "@type": "chatListMain" },
                            "limit": 100
                        }));
                    }
                }
            }
            "user" | "updateUser" => {
                let user = if update_type == "user" { &update } else { &update["user"] };
                if user["is_self"].as_bool() == Some(true) {
                    if !auth::should_use_bot_auth(&self.cfg) {
                        self.client.send(json!({
                            "@type": "getChats",
                            "chat_list": { "@type": "chatListMain" },
                            "limit": 100
                        }));
                    }
                }
            }
            "updateNewMessage" => {
                let msg = &update["message"];
                self.msg_handler.handle_new_message(msg).await;
                self.msg_handler.handle_command(msg).await;
            }
            "updateChatTitle" => {
                let chat_id = update["chat_id"].as_i64().unwrap_or(0);
                if let Some(title) = update["title"].as_str() {
                    self.storage.store_chat_title(chat_id, title);
                }
            }
            "chat" | "updateNewChat" => {
                let chat = if update_type == "chat" { &update } else { &update["chat"] };
                let chat_id = chat["id"].as_i64().unwrap_or(0);
                if let Some(title) = chat["title"].as_str() {
                    self.storage.store_chat_title(chat_id, title);
                }
                if let Some(username) = chat["type"]["username"].as_str() {
                    let link = format!("https://t.me/{}", username);
                    self.storage.store_chat_invite_link(chat_id, &link);
                }
                if let Some(sid) = chat["type"]["supergroup_id"].as_i64() {
                    self.storage.store_supergroup_map(sid, chat_id);
                }
            }
            "updateSupergroupFullInfo" => {
                let sid = update["supergroup_id"].as_i64().unwrap_or(0);
                if let Some(cid) = self.storage.get_chat_id_by_supergroup(sid) {
                    let full_info = &update["full_info"];
                    let invite = full_info["invite_link"].as_str().or(full_info["primary_invite_link"].as_str());
                    if let Some(link) = invite {
                        self.storage.store_chat_invite_link(cid, link);
                    }
                }
            }
            "updateChatFolders" | "chatFolders" => {
                if let Some(folders) = update["chat_folders"].as_array() {
                    for f in folders {
                        if let Some(title) = f["name"]["text"]["text"].as_str() {
                            if self.cfg.folder_names.contains(&title.to_string()) {
                                if let Some(id) = f["id"].as_i64() {
                                    self.client.send(json!({
                                        "@type": "getChatFolder",
                                        "chat_folder_id": id
                                    }));
                                }
                            }
                        }
                    }
                }
            }
            "chatFolder" => {
                if let Some(ids) = update["included_chat_ids"].as_array() {
                    for id_val in ids {
                        if let Some(cid) = id_val.as_i64() {
                            self.msg_handler.add_monitored_chat(cid).await;
                            self.client.send(json!({
                                "@type": "getChat",
                                "chat_id": cid
                            }));
                        }
                    }
                }
            }
            _ => {}
        }
    }
}
