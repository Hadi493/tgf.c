use serde::Deserialize;
use std::fs;
use dotenvy::dotenv;
use std::env;

#[derive(Debug, Deserialize, Clone)]
pub struct Config {
    pub api_id: i32,
    pub api_hash: String,
    pub bot_token: Option<String>,
    pub phone: Option<String>,
    pub db_path: String,
    pub source_channels: Vec<i64>,
    pub folder_names: Vec<String>,
    pub aggregator_chat_id: i64,
}

#[derive(Deserialize)]
struct SourceChannels {
    pub channels: Option<Vec<i64>>,
}

#[derive(Deserialize)]
struct SourceFolders {
    pub folder: Option<Vec<String>>,
}

#[derive(Deserialize)]
struct TomlConfig {
    pub api_id: Option<i32>,
    pub api_hash: Option<String>,
    pub bot_token: Option<String>,
    pub phone: Option<String>,
    pub db_path: Option<String>,
    pub source_channels: Option<SourceChannels>,
    pub source: Option<SourceFolders>,
    pub aggregator_chat_id: Option<i64>,
}

pub fn load_config() -> anyhow::Result<Config> {
    dotenv().ok();
    
    let toml_content = fs::read_to_string("config.toml").unwrap_or_default();
    let toml_cfg: TomlConfig = toml::from_str(&toml_content).unwrap_or_else(|_| TomlConfig {
        api_id: None,
        api_hash: None,
        bot_token: None,
        phone: None,
        db_path: None,
        source_channels: None,
        source: None,
        aggregator_chat_id: None,
    });

    // ... (api_id, api_hash, etc. stay same)
    let api_id = env::var("TELEGRAM_API_ID")
        .ok()
        .and_then(|s| s.parse().ok())
        .or(toml_cfg.api_id)
        .expect("TELEGRAM_API_ID must be set");

    let api_hash = env::var("TELEGRAM_API_HASH")
        .ok()
        .or(toml_cfg.api_hash)
        .expect("TELEGRAM_API_HASH must be set");

    let bot_token = env::var("TELEGRAM_BOT_TOKEN")
        .ok()
        .or(toml_cfg.bot_token);

    let phone = env::var("TELEGRAM_PHONE")
        .ok()
        .or(toml_cfg.phone);
    
    let db_path = env::var("DB_PATH")
        .ok()
        .or(toml_cfg.db_path)
        .unwrap_or_else(|| "tgf.db".to_string());
    
    let source_channels = toml_cfg.source_channels.and_then(|s| s.channels).unwrap_or_default();
    let folder_names = toml_cfg.source.and_then(|s| s.folder).unwrap_or_default();
    
    let aggregator_chat_id = env::var("TELEGRAM_AGGREGATOR_CHANNEL")
        .ok()
        .and_then(|s| s.parse().ok())
        .or(toml_cfg.aggregator_chat_id)
        .expect("TELEGRAM_AGGREGATOR_CHANNEL must be set");

    Ok(Config {
        api_id,
        api_hash,
        bot_token,
        phone,
        db_path,
        source_channels,
        folder_names,
        aggregator_chat_id,
    })
}
