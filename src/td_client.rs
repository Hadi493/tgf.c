use std::ffi::{CStr, CString};
use libc::{c_char, c_double, c_void};
use serde_json::Value;

#[link(name = "tdjson")]
extern "C" {
    fn td_json_client_create() -> *mut c_void;
    fn td_json_client_send(client: *mut c_void, request: *const c_char);
    fn td_json_client_receive(client: *mut c_void, timeout: c_double) -> *const c_char;
    fn td_json_client_execute(client: *mut c_void, request: *const c_char) -> *const c_char;
    fn td_json_client_destroy(client: *mut c_void);
}

pub struct TdClient {
    client: *mut c_void,
}

unsafe impl Send for TdClient {}
unsafe impl Sync for TdClient {}

impl TdClient {
    pub fn new() -> Self {
        Self {
            client: unsafe { td_json_client_create() },
        }
    }

    pub fn send(&self, request: Value) {
        let c_str = CString::new(request.to_string()).unwrap();
        unsafe { td_json_client_send(self.client, c_str.as_ptr()) };
    }

    pub fn receive(&self, timeout: f64) -> Option<Value> {
        let res = unsafe { td_json_client_receive(self.client, timeout as c_double) };
        if res.is_null() {
            return None;
        }
        let c_str = unsafe { CStr::from_ptr(res) };
        serde_json::from_str(c_str.to_str().unwrap_or("{}")).ok()
    }

    pub fn execute(&self, request: Value) -> Option<Value> {
        let c_str = CString::new(request.to_string()).unwrap();
        let res = unsafe { td_json_client_execute(std::ptr::null_mut(), c_str.as_ptr()) };
        if res.is_null() {
            return None;
        }
        let c_str = unsafe { CStr::from_ptr(res) };
        serde_json::from_str(c_str.to_str().unwrap_or("{}")).ok()
    }
}

impl Drop for TdClient {
    fn drop(&mut self) {
        unsafe { td_json_client_destroy(self.client) };
    }
}
