mod config;
mod storage;
mod utils;
mod td_client;
mod handlers;

use std::sync::Arc;
use tokio::sync::mpsc;
use crate::td_client::TdClient;
use crate::config::load_config;
use crate::storage::Storage;
use crate::handlers::UpdateDispatcher;
use serde_json::json;

#[tokio::main]
async fn main() -> anyhow::Result<()> {
    let cfg = Arc::new(load_config()?);
    let storage = Arc::new(Storage::init(&cfg.db_path)?);
    let client = Arc::new(TdClient::new());

    client.send(json!({
        "@type": "setLogVerbosityLevel",
        "new_verbosity_level": 1
    }));

    client.send(json!({
        "@type": "getAuthorizationState"
    }));

    println!("Aggregator started. Configuration:");
    println!("  - Aggregator Chat ID: {}", cfg.aggregator_chat_id);
    println!("  - Source Channels: {:?}", cfg.source_channels);
    println!("  - Folders: {:?}", cfg.folder_names);
    println!("  - DB Path: {}", cfg.db_path);
    println!("Waiting for Telegram synchronization...");

    let (tx, mut rx) = mpsc::channel(100);

    let client_receiver = Arc::clone(&client);
    std::thread::spawn(move || {
        loop {
            if let Some(update) = client_receiver.receive(1.0) {
                if let Err(_) = tx.blocking_send(update) {
                    break;
                }
            }
        }
    });

    let dispatcher = UpdateDispatcher::new(Arc::clone(&client), Arc::clone(&cfg), Arc::clone(&storage));

    let is_debug = std::env::var("DEBUG").map(|v| v == "1").unwrap_or(false);

    while let Some(update) = rx.recv().await {
        if is_debug {
            if let Some(update_type) = update["@type"].as_str() {
                if update_type == "error" {
                    println!("[ERROR] TDLib error: {:?}", update);
                } else if update_type == "updateConnectionState" {
                    println!("[DEBUG] Connection State: {}", update["connection_state"]["@type"].as_str().unwrap_or("unknown"));
                } else {
                    println!("[DEBUG] Update: {}", update_type);
                }
            }
        }
        dispatcher.dispatch(update).await;
    }

    Ok(())
}
