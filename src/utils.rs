use sha2::{Sha256, Digest};
use hex;

pub fn compute_sha256(text: &str, media: Option<&[u8]>, reply_id: i64) -> String {
    let mut hasher = Sha256::new();
    hasher.update(text.as_bytes());
    if let Some(m) = media {
        hasher.update(m);
    }
    hasher.update(reply_id.to_le_bytes());
    hex::encode(hasher.finalize())
}

pub fn format_source_link(chat_id: i64, msg_id: i64) -> String {
    let mut stripped_id = chat_id;
    if chat_id < -1000000000000i64 {
        stripped_id = -(chat_id + 1000000000000i64);
    } else if chat_id < 0 {
        stripped_id = -chat_id;
    }
    format!("https://t.me/c/{}/{}", stripped_id, msg_id >> 20)
}
