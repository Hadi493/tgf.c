use crate::td_client::TdClient;
use crate::config::Config;
use serde_json::{json, Value};
use std::io::{self, Write};

pub fn should_use_bot_auth(cfg: &Config) -> bool {
    let has_bot = cfg.bot_token.as_ref().map_or(false, |s| !s.is_empty());
    let has_phone = cfg.phone.as_ref().map_or(false, |s| !s.is_empty());
    let needs_folders = !cfg.folder_names.is_empty();
    if !has_bot { return false; }
    if needs_folders && has_phone { return false; }
    true
}

pub fn handle_auth(client: &TdClient, cfg: &Config, auth_state: &Value) {
    let state_type = auth_state["@type"].as_str().unwrap_or("");
    match state_type {
        "authorizationStateWaitTdlibParameters" => {
            let storage_dir = if should_use_bot_auth(cfg) { "tdlib_bot" } else { "tdlib_user" };
            client.send(json!({
                "@type": "setTdlibParameters",
                "use_test_dc": false,
                "database_directory": storage_dir,
                "files_directory": storage_dir,
                "use_file_database": true,
                "use_chat_info_database": true,
                "use_message_database": true,
                "use_secret_chats": false,
                "api_id": cfg.api_id,
                "api_hash": cfg.api_hash,
                "system_language_code": "en",
                "device_model": "Desktop",
                "system_version": "Linux",
                "application_version": "1.0",
                "enable_storage_optimizer": true
            }));
        }
        "authorizationStateWaitEncryptionKey" => {
            client.send(json!({
                "@type": "checkDatabaseEncryptionKey",
                "encryption_key": ""
            }));
        }
        "authorizationStateWaitPhoneNumber" => {
            if should_use_bot_auth(cfg) {
                client.send(json!({
                    "@type": "checkAuthenticationBotToken",
                    "token": cfg.bot_token.as_ref().unwrap()
                }));
            } else if let Some(phone) = &cfg.phone {
                client.send(json!({
                    "@type": "setAuthenticationPhoneNumber",
                    "phone_number": phone
                }));
            } else {
                print!("Enter phone number: ");
                io::stdout().flush().unwrap();
                let mut phone = String::new();
                io::stdin().read_line(&mut phone).unwrap();
                client.send(json!({
                    "@type": "setAuthenticationPhoneNumber",
                    "phone_number": phone.trim()
                }));
            }
        }
        "authorizationStateWaitCode" => {
            print!("Enter authentication code: ");
            io::stdout().flush().unwrap();
            let mut code = String::new();
            io::stdin().read_line(&mut code).unwrap();
            client.send(json!({
                "@type": "checkAuthenticationCode",
                "code": code.trim()
            }));
        }
        "authorizationStateWaitPassword" => {
            print!("Enter password: ");
            io::stdout().flush().unwrap();
            let mut password = String::new();
            io::stdin().read_line(&mut password).unwrap();
            client.send(json!({
                "@type": "checkAuthenticationPassword",
                "password": password.trim()
            }));
        }
        "authorizationStateReady" => {
            println!("Logged in successfully.");
        }
        _ => {}
    }
}
