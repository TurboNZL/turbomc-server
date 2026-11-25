//! The connection to the core (`turbomc-server --serve`): one JSON object per line over a local socket.
use serde_json::{json, Value};
use std::io::{BufRead, BufReader, Write};
use std::os::unix::net::UnixStream;
use std::path::PathBuf;
use std::sync::mpsc::{channel, Receiver};
use std::time::{Duration, Instant};

/// What arrives from the core.
#[derive(Debug)]
pub enum Incoming {
    Response { id: u64, ok: bool, result: Value, error: String },
    Event(Value),
    Closed,
}

pub struct Client {
    writer: UnixStream,
    rx: Receiver<Incoming>,
    next_id: u64,
}

/// Where the core listens: `$TURBOMC_SOCKET`, else `$XDG_RUNTIME_DIR/turbomc-server.sock`, else `/tmp/turbomc-server-<uid>.sock`.
pub fn socket_path() -> PathBuf {
    if let Ok(p) = std::env::var("TURBOMC_SOCKET") {
        return PathBuf::from(p);
    }
    if let Ok(dir) = std::env::var("XDG_RUNTIME_DIR") {
        return PathBuf::from(dir).join("turbomc-server.sock");
    }
    let uid = unsafe_uid();
    PathBuf::from(format!("/tmp/turbomc-server-{uid}.sock"))
}

fn unsafe_uid() -> u32 {
    // no libc dependency: the owner of /proc/self is this process's user
    use std::os::unix::fs::MetadataExt;
    std::fs::metadata("/proc/self").map(|m| m.uid()).unwrap_or(0)
}

/// Looks for the core program: `$TURBOMC_CORE`, next to this program, then on the PATH.
fn find_core() -> Option<PathBuf> {
    if let Ok(p) = std::env::var("TURBOMC_CORE") {
        return Some(PathBuf::from(p));
    }
    if let Ok(me) = std::env::current_exe() {
        if let Some(dir) = me.parent() {
            let next = dir.join("turbomc-server");
            if next.is_file() {
                return Some(next);
            }
        }
    }
    std::env::var_os("PATH").and_then(|paths| std::env::split_paths(&paths).map(|d| d.join("turbomc-server")).find(|p| p.is_file()))
}

impl Client {
    /// Connects, starting the core first when it is not running.
    pub fn connect() -> Result<Client, String> {
        let path = socket_path();
        let stream = match UnixStream::connect(&path) {
            Ok(s) => s,
            Err(_) => {
                let core = find_core().ok_or("could not find the turbomc-server core program (set TURBOMC_CORE to its path)")?;
                std::process::Command::new(&core)
                    .arg("--serve")
                    .stdin(std::process::Stdio::null())
                    .stdout(std::process::Stdio::null())
                    .stderr(std::process::Stdio::null())
                    .spawn()
                    .map_err(|e| format!("could not start {}: {e}", core.display()))?;
                let deadline = Instant::now() + Duration::from_secs(30);
                loop {
                    if let Ok(s) = UnixStream::connect(&path) {
                        break s;
                    }
                    if Instant::now() > deadline {
                        return Err("the core did not start listening in time".into());
                    }
                    std::thread::sleep(Duration::from_millis(150));
                }
            }
        };
        let reader = BufReader::new(stream.try_clone().map_err(|e| e.to_string())?);
        let (tx, rx) = channel();
        std::thread::spawn(move || {
            for line in reader.lines() {
                let Ok(line) = line else { break };
                let Ok(v) = serde_json::from_str::<Value>(&line) else { continue };
                let msg = if let Some(id) = v.get("id").and_then(Value::as_u64) {
                    Incoming::Response {
                        id,
                        ok: v.get("ok").and_then(Value::as_bool).unwrap_or(false),
                        result: v.get("result").cloned().unwrap_or(Value::Null),
                        error: v.get("error").and_then(Value::as_str).unwrap_or("").to_string(),
                    }
                } else {
                    Incoming::Event(v)
                };
                if tx.send(msg).is_err() {
                    return;
                }
            }
            let _ = tx.send(Incoming::Closed);
        });
        Ok(Client { writer: stream, rx, next_id: 0 })
    }

    /// Sends a request and returns its id (the answer arrives through [`Client::poll`]).
    pub fn call(&mut self, method: &str, params: Value) -> u64 {
        self.next_id += 1;
        let line = json!({ "id": self.next_id, "method": method, "params": params }).to_string() + "\n";
        let _ = self.writer.write_all(line.as_bytes());
        self.next_id
    }

    /// Everything that has arrived since the last call.
    pub fn poll(&mut self) -> Vec<Incoming> {
        self.rx.try_iter().collect()
    }
}
