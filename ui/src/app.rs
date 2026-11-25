//! What the window knows and how it reacts. No drawing in here: `views.rs` turns this into screens.
use crate::client::{Client, Incoming};
use serde_json::{json, Value};
use std::collections::HashMap;
use std::time::{Duration, Instant};
use turboui_core::{Event, Theme};

#[derive(Clone, Debug, PartialEq)]
pub struct Server {
    pub id: String,
    pub name: String,
    pub software: String,
    pub software_name: String,
    pub version: String,
    pub state: String,
    pub eula: bool,
}

impl Server {
    pub fn running(&self) -> bool {
        self.state != "stopped"
    }
}

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum Page {
    Console,
    Version,
    Plugins,
    Notes,
    Worlds,
    Properties,
    Backups,
    Settings,
}

impl Page {
    pub const ALL: [Page; 8] = [Page::Console, Page::Version, Page::Plugins, Page::Notes, Page::Worlds, Page::Properties, Page::Backups, Page::Settings];
    pub fn label(self) -> &'static str {
        match self {
            Page::Console => "Console",
            Page::Version => "Server version",
            Page::Plugins => "Mods and plugins",
            Page::Notes => "Notes",
            Page::Worlds => "Worlds",
            Page::Properties => "Properties",
            Page::Backups => "Backups",
            Page::Settings => "Settings",
        }
    }
    pub fn icon(self) -> &'static str {
        match self {
            Page::Console => "play",
            Page::Version | Page::Settings => "settings",
            Page::Plugins | Page::Worlds => "folder",
            Page::Notes | Page::Properties => "edit",
            Page::Backups => "export",
        }
    }
    pub fn key(self) -> &'static str {
        self.label()
    }
    fn from_key(k: &str) -> Option<Page> {
        Page::ALL.into_iter().find(|p| p.key() == k)
    }
}

#[derive(Clone, Debug, PartialEq)]
pub enum Screen {
    List,
    Server(String, Page),
}

#[derive(Clone, Debug, PartialEq)]
pub enum Dialog {
    NewServer,
    ConfirmVersion { id: String, software: String, version: String },
    Eula(String),
    Message { title: String, text: String },
    ConfirmDelete(String),
}

#[derive(Clone, Debug, PartialEq)]
pub struct Backup {
    pub path: String,
    pub name: String,
    pub size: u64,
}

#[derive(Clone, Debug, PartialEq)]
pub struct TaskInfo {
    pub number: i64,
    pub progress: f32,
    pub status: String,
}

#[derive(Clone, Debug, PartialEq)]
pub struct NewServerForm {
    pub software: usize,
    pub versions: Vec<(String, bool)>,
    pub version: Option<String>,
    pub unstable: bool,
    pub loading: bool,
}

enum Tag {
    VersionList,
    VersionChange,
    List,
    Software,
    Versions,
    Create,
    Start(String),
    Console(String),
    Props,
    Backups,
    Refresh(String),
    Quiet,
}

pub struct App {
    pub theme: Theme,
    client: Option<Client>,
    pub connection_error: Option<String>,
    pending: HashMap<u64, Tag>,
    pub servers: Vec<Server>,
    pub selected: Option<usize>,
    pub screen: Screen,
    pub dialog: Option<Dialog>,
    pub software: Vec<(String, String)>,
    pub form: NewServerForm,
    /// The version picker on a server's Server version page.
    pub vform: NewServerForm,
    pub task: Option<TaskInfo>,
    /// True when `task` is a version change (otherwise it is a new server).
    task_is_version: bool,
    pub console: HashMap<String, Vec<(String, String)>>,
    pub scroll: HashMap<String, f32>,
    pub follow: bool,
    pub hovered: Option<String>,
    pub focus: Option<String>,
    pub inputs: HashMap<String, String>,
    pub props: Vec<(String, String, String)>,
    pub backups: Vec<Backup>,
    pub backup_sel: Option<usize>,
    pub toast: Option<(String, Instant)>,
    select_after_refresh: Option<String>,
}

impl App {
    pub fn new(theme: Theme) -> App {
        let mut app = App {
            theme,
            client: None,
            connection_error: None,
            pending: HashMap::new(),
            servers: vec![],
            selected: None,
            screen: Screen::List,
            dialog: None,
            software: vec![],
            form: NewServerForm { software: 0, versions: vec![], version: None, unstable: false, loading: false },
            vform: NewServerForm { software: 0, versions: vec![], version: None, unstable: false, loading: false },
            task: None,
            task_is_version: false,
            console: HashMap::new(),
            scroll: HashMap::new(),
            follow: true,
            hovered: None,
            focus: None,
            inputs: HashMap::new(),
            props: vec![],
            backups: vec![],
            backup_sel: None,
            toast: None,
            select_after_refresh: None,
        };
        app.connect();
        app
    }

    pub fn connect(&mut self) {
        match Client::connect() {
            Ok(c) => {
                self.client = Some(c);
                self.connection_error = None;
                self.call(Tag::List, "servers.list", json!({}));
                self.call(Tag::Software, "software.list", json!({}));
            }
            Err(e) => self.connection_error = Some(e),
        }
    }

    fn call(&mut self, tag: Tag, method: &str, params: Value) {
        if let Some(c) = self.client.as_mut() {
            let id = c.call(method, params);
            self.pending.insert(id, tag);
        }
    }

    pub fn selected_server(&self) -> Option<&Server> {
        self.selected.and_then(|i| self.servers.get(i))
    }

    pub fn open_server(&self) -> Option<&Server> {
        match &self.screen {
            Screen::Server(id, _) => self.servers.iter().find(|s| &s.id == id),
            Screen::List => None,
        }
    }

    pub fn toast(&mut self, text: impl Into<String>) {
        self.toast = Some((text.into(), Instant::now()));
    }

    pub fn input(&self, id: &str) -> &str {
        self.inputs.get(id).map(String::as_str).unwrap_or("")
    }

    /// The scroll offset to show for an area (the console sticks to the end while `follow` is on).
    pub fn scroll_of(&self, id: &str) -> f32 {
        if id == "console" && self.follow {
            f32::MAX
        } else {
            self.scroll.get(id).copied().unwrap_or(0.0)
        }
    }

    fn fetch_versions(&mut self) {
        let Some((key, _)) = self.software.get(self.form.software).cloned() else { return };
        self.form.loading = true;
        self.form.versions.clear();
        self.form.version = None;
        let unstable = self.form.unstable;
        self.call(Tag::Versions, "software.versions", json!({ "software": key, "unstable": unstable }));
    }

    fn fetch_version_page_versions(&mut self) {
        let Some((key, _)) = self.software.get(self.vform.software).cloned() else { return };
        self.vform.loading = true;
        self.vform.versions.clear();
        self.vform.version = None;
        let unstable = self.vform.unstable;
        self.call(Tag::VersionList, "software.versions", json!({ "software": key, "unstable": unstable }));
    }

    fn open_page(&mut self, id: &str, page: Page) {
        self.screen = Screen::Server(id.to_string(), page);
        self.focus = None;
        match page {
            Page::Console => {
                self.follow = true;
                self.call(Tag::Console(id.to_string()), "servers.console", json!({ "id": id, "max": 500 }));
            }
            Page::Properties => {
                for k in self.inputs.keys().filter(|k| k.starts_with("prop:")).cloned().collect::<Vec<_>>() {
                    self.inputs.remove(&k);
                }
                self.call(Tag::Props, "servers.properties.get", json!({ "id": id }));
            }
            Page::Version => {
                let current = self.servers.iter().find(|s| s.id == id).map(|s| s.software.clone()).unwrap_or_default();
                self.vform.software = self.software.iter().position(|(k, _)| *k == current).unwrap_or(0);
                self.vform.unstable = false;
                self.fetch_version_page_versions();
            }
            Page::Backups => {
                self.backup_sel = None;
                self.call(Tag::Backups, "servers.backups.list", json!({ "id": id }));
            }
            _ => {}
        }
    }

    /// Applies something the user did or the clock. Returns whether the screen must be redrawn.
    pub fn event(&mut self, ev: &Event) -> bool {
        match ev {
            Event::Tick => self.tick(),
            Event::Hover(h) => {
                let changed = self.hovered != *h;
                self.hovered = h.clone();
                changed
            }
            Event::Scroll { id, dy, viewport, content } => {
                let max = (content - viewport).max(0.0);
                let current = if id == "console" && self.follow { max } else { self.scroll.get(id).copied().unwrap_or(0.0).min(max) };
                let next = (current + dy).clamp(0.0, max);
                if id == "console" {
                    self.follow = next >= max - 1.0;
                }
                self.scroll.insert(id.clone(), next);
                true
            }
            Event::Key { key, text, ctrl, .. } => self.key(key, text.as_deref(), *ctrl),
            Event::Click(id) => self.click(id),
        }
    }

    fn tick(&mut self) -> bool {
        let mut changed = false;
        if let Some((_, since)) = &self.toast {
            if since.elapsed() > Duration::from_secs(5) {
                self.toast = None;
                changed = true;
            }
        }
        let incoming = self.client.as_mut().map(|c| c.poll()).unwrap_or_default();
        for msg in incoming {
            changed = true;
            match msg {
                Incoming::Response { id, ok, result, error } => {
                    if let Some(tag) = self.pending.remove(&id) {
                        self.on_response(tag, ok, result, error);
                    }
                }
                Incoming::Event(v) => self.on_event(&v),
                Incoming::Closed => {
                    self.client = None;
                    self.connection_error = Some("The connection to the core was lost.".into());
                }
            }
        }
        changed
    }

    fn on_event(&mut self, v: &Value) {
        let str_of = |k: &str| v.get(k).and_then(Value::as_str).unwrap_or("").to_string();
        match v.get("event").and_then(Value::as_str).unwrap_or("") {
            "servers.changed" => self.call(Tag::List, "servers.list", json!({})),
            "server.state" => {
                let (id, state) = (str_of("server"), str_of("state"));
                if let Some(s) = self.servers.iter_mut().find(|s| s.id == id) {
                    s.state = state;
                }
            }
            "server.exited" => {
                if v.get("crashed").and_then(Value::as_bool).unwrap_or(false) {
                    let name = self.servers.iter().find(|s| s.id == str_of("server")).map(|s| s.name.clone()).unwrap_or_default();
                    self.toast(format!("{name} stopped unexpectedly. See its console."));
                }
            }
            "console.line" => {
                let lines = self.console.entry(str_of("server")).or_default();
                lines.push((str_of("level"), str_of("text")));
                if lines.len() > 500 {
                    lines.drain(0..lines.len() - 500);
                }
            }
            "task" => {
                let number = v.get("task").and_then(Value::as_i64).unwrap_or(0);
                if self.task.as_ref().map(|t| t.number) != Some(number) {
                    return;
                }
                match v.get("state").and_then(Value::as_str).unwrap_or("") {
                    "done" => {
                        self.task = None;
                        if self.task_is_version {
                            self.toast("Installed. Start the server to use it.");
                        } else {
                            self.dialog = None;
                            self.select_after_refresh = Some(self.input("new-name").to_string());
                        }
                        self.call(Tag::List, "servers.list", json!({}));
                    }
                    "failed" => {
                        self.task = None;
                        let title = if self.task_is_version { "Could not change the version" } else { "Could not create the server" };
                        self.dialog = Some(Dialog::Message { title: title.into(), text: str_of("error") });
                    }
                    _ => {
                        if let Some(t) = self.task.as_mut() {
                            if let Some(p) = v.get("progress").and_then(Value::as_f64) {
                                t.progress = p as f32;
                            }
                            if v.get("status").is_some() {
                                t.status = str_of("status");
                            }
                        }
                    }
                }
            }
            _ => {}
        }
    }

    fn on_response(&mut self, tag: Tag, ok: bool, result: Value, error: String) {
        match tag {
            Tag::List if ok => {
                let keep = self.selected_server().map(|s| s.id.clone());
                self.servers = result
                    .as_array()
                    .map(|a| {
                        a.iter()
                            .map(|s| {
                                let g = |k: &str| s.get(k).and_then(Value::as_str).unwrap_or("").to_string();
                                Server { id: g("id"), name: g("name"), software: g("software"), software_name: g("softwareName"), version: g("version"), state: g("state"), eula: s.get("eula").and_then(Value::as_bool).unwrap_or(false) }
                            })
                            .collect()
                    })
                    .unwrap_or_default();
                let want = self.select_after_refresh.take();
                self.selected = match want {
                    Some(name) if !name.is_empty() => self.servers.iter().position(|s| s.name == name),
                    _ => keep.and_then(|id| self.servers.iter().position(|s| s.id == id)),
                }
                .or(if self.servers.is_empty() { None } else { Some(0) });
            }
            Tag::Software if ok => {
                self.software = result.as_array().map(|a| a.iter().map(|s| (s["key"].as_str().unwrap_or("").to_string(), s["name"].as_str().unwrap_or("").to_string())).collect()).unwrap_or_default();
            }
            Tag::Versions => {
                self.form.loading = false;
                if ok {
                    self.form.versions = result.as_array().map(|a| a.iter().map(|v| (v["id"].as_str().unwrap_or("").to_string(), v["stable"].as_bool().unwrap_or(true))).collect()).unwrap_or_default();
                    self.form.version = self.form.versions.first().map(|v| v.0.clone());
                } else {
                    self.toast(format!("Could not load versions: {error}"));
                }
            }
            Tag::VersionList => {
                self.vform.loading = false;
                if ok {
                    self.vform.versions = result.as_array().map(|a| a.iter().map(|v| (v["id"].as_str().unwrap_or("").to_string(), v["stable"].as_bool().unwrap_or(true))).collect()).unwrap_or_default();
                } else {
                    self.toast(format!("Could not load versions: {error}"));
                }
            }
            Tag::VersionChange => {
                if ok {
                    let number = result["task"].as_i64().unwrap_or(0);
                    self.task_is_version = true;
                    self.task = Some(TaskInfo { number, progress: 0.0, status: "Starting...".into() });
                } else {
                    self.task = None;
                    self.dialog = Some(Dialog::Message { title: "Could not change the version".into(), text: error });
                }
            }
            Tag::Create => {
                if ok {
                    let number = result["task"].as_i64().unwrap_or(0);
                    self.task_is_version = false;
                    self.task = Some(TaskInfo { number, progress: 0.0, status: "Starting...".into() });
                } else {
                    self.task = None;
                    self.dialog = Some(Dialog::Message { title: "Could not create the server".into(), text: error });
                }
            }
            Tag::Start(id) => {
                if !ok {
                    if error == "eula" {
                        self.dialog = Some(Dialog::Eula(id));
                    } else if let Some(rest) = error.strip_prefix("java:") {
                        let text = rest.split_once(':').map(|x| x.1).unwrap_or(rest).to_string();
                        self.dialog = Some(Dialog::Message { title: "Java needed".into(), text });
                    } else {
                        self.dialog = Some(Dialog::Message { title: "Could not start the server".into(), text: error });
                    }
                }
            }
            Tag::Console(id) if ok => {
                let lines = result.as_array().map(|a| a.iter().map(|l| (l["level"].as_str().unwrap_or("info").to_string(), l["text"].as_str().unwrap_or("").to_string())).collect()).unwrap_or_default();
                self.console.insert(id, lines);
            }
            Tag::Props if ok => {
                self.props = result.as_array().map(|a| a.iter().map(|p| (p["key"].as_str().unwrap_or("").to_string(), p["value"].as_str().unwrap_or("").to_string(), p["description"].as_str().unwrap_or("").to_string())).collect()).unwrap_or_default();
            }
            Tag::Backups if ok => {
                self.backups = result.as_array().map(|a| a.iter().map(|b| Backup { path: b["path"].as_str().unwrap_or("").to_string(), name: b["name"].as_str().unwrap_or("").to_string(), size: b["size"].as_f64().unwrap_or(0.0) as u64 }).collect()).unwrap_or_default();
            }
            Tag::Refresh(id) => {
                if ok {
                    self.toast("Done.");
                    self.call(Tag::Backups, "servers.backups.list", json!({ "id": id }));
                } else {
                    self.dialog = Some(Dialog::Message { title: "That did not work".into(), text: error });
                }
            }
            Tag::Quiet => {
                if !ok {
                    self.dialog = Some(Dialog::Message { title: "That did not work".into(), text: error });
                }
            }
            _ => {
                if !ok {
                    self.toast(error);
                }
            }
        }
    }

    fn click(&mut self, id: &str) -> bool {
        // dialogs first: nothing under them can be clicked
        if self.dialog.is_some() {
            return self.click_dialog(id);
        }
        if let Some(rest) = id.strip_prefix("input:") {
            self.focus = Some(rest.to_string());
            return true;
        }
        if let Some(n) = id.strip_prefix("srv:").and_then(|n| n.parse::<usize>().ok()) {
            if n < self.servers.len() {
                if self.selected == Some(n) {
                    let sid = self.servers[n].id.clone();
                    self.open_page(&sid, Page::Console);
                } else {
                    self.selected = Some(n);
                }
            }
            return true;
        }
        if let Some(p) = id.strip_prefix("page:").and_then(Page::from_key) {
            if let Screen::Server(sid, _) = self.screen.clone() {
                self.open_page(&sid, p);
            }
            return true;
        }
        if let Some(i) = id.strip_prefix("vsw:").and_then(|n| n.parse::<usize>().ok()) {
            self.vform.software = i;
            self.fetch_version_page_versions();
            return true;
        }
        if let Some(v) = id.strip_prefix("vver:") {
            self.vform.version = Some(v.to_string());
            return true;
        }
        if id == "vunstable" {
            self.vform.unstable = !self.vform.unstable;
            self.fetch_version_page_versions();
            return true;
        }
        if id == "vinstall" {
            if let (Screen::Server(sid, _), Some(version), Some((software, _))) = (self.screen.clone(), self.vform.version.clone(), self.software.get(self.vform.software).cloned()) {
                self.dialog = Some(Dialog::ConfirmVersion { id: sid, software, version });
            }
            return true;
        }
        if let Some(k) = id.strip_prefix("togg:") {
            let key = format!("prop:{k}");
            let current = self.inputs.get(&key).cloned().or_else(|| self.props.iter().find(|p| p.0 == k).map(|p| p.1.clone())).unwrap_or_default();
            self.inputs.insert(key, if current == "true" { "false".into() } else { "true".into() });
            return true;
        }
        if let Some(n) = id.strip_prefix("bk:sel:").and_then(|n| n.parse::<usize>().ok()) {
            self.backup_sel = Some(n);
            return true;
        }
        match id {
            "add" => {
                self.dialog = Some(Dialog::NewServer);
                self.inputs.insert("new-name".into(), "My server".into());
                self.focus = Some("new-name".into());
                self.form.unstable = false;
                if self.software.is_empty() {
                    self.call(Tag::Software, "software.list", json!({}));
                }
                self.fetch_versions();
            }
            "refresh" => {
                self.call(Tag::List, "servers.list", json!({}));
            }
            "retry" => self.connect(),
            "back" => {
                self.screen = Screen::List;
                self.focus = None;
            }
            "act:start" | "start" => {
                let target = self.open_server().or_else(|| self.selected_server()).map(|s| s.id.clone());
                if let Some(sid) = target {
                    self.call(Tag::Start(sid.clone()), "servers.start", json!({ "id": sid }));
                }
            }
            "act:stop" | "stop" => {
                if let Some(sid) = self.open_server().or_else(|| self.selected_server()).map(|s| s.id.clone()) {
                    self.call(Tag::Quiet, "servers.stop", json!({ "id": sid }));
                }
            }
            "act:kill" | "kill" => {
                if let Some(sid) = self.open_server().or_else(|| self.selected_server()).map(|s| s.id.clone()) {
                    self.call(Tag::Quiet, "servers.kill", json!({ "id": sid }));
                }
            }
            "act:edit" => {
                if let Some(sid) = self.selected_server().map(|s| s.id.clone()) {
                    self.open_page(&sid, Page::Console);
                }
            }
            "act:delete" => {
                if let Some(sid) = self.selected_server().map(|s| s.id.clone()) {
                    self.dialog = Some(Dialog::ConfirmDelete(sid));
                }
            }
            "props:save" => {
                if let Screen::Server(sid, _) = self.screen.clone() {
                    let mut values = serde_json::Map::new();
                    for (k, v, _) in &self.props {
                        if let Some(edited) = self.inputs.get(&format!("prop:{k}")) {
                            if edited != v {
                                values.insert(k.clone(), json!(edited));
                            }
                        }
                    }
                    if values.is_empty() {
                        self.toast("Nothing changed.");
                    } else {
                        self.toast("Saved. Changes apply the next time the server starts.");
                        self.call(Tag::Quiet, "servers.properties.set", json!({ "id": sid, "values": values }));
                        self.call(Tag::Props, "servers.properties.get", json!({ "id": sid }));
                    }
                }
            }
            "bk:create" => {
                if let Screen::Server(sid, _) = self.screen.clone() {
                    self.toast("Creating the backup...");
                    self.call(Tag::Refresh(sid.clone()), "servers.backups.create", json!({ "id": sid, "label": "" }));
                }
            }
            "bk:restore" => {
                if let (Screen::Server(sid, _), Some(i)) = (self.screen.clone(), self.backup_sel) {
                    if let Some(b) = self.backups.get(i) {
                        let path = b.path.clone();
                        self.toast("Restoring...");
                        self.call(Tag::Refresh(sid.clone()), "servers.backups.restore", json!({ "id": sid, "path": path }));
                    }
                }
            }
            "bk:delete" => {
                if let (Screen::Server(sid, _), Some(i)) = (self.screen.clone(), self.backup_sel) {
                    if let Some(b) = self.backups.get(i) {
                        let path = b.path.clone();
                        self.call(Tag::Refresh(sid.clone()), "servers.backups.delete", json!({ "id": sid, "path": path }));
                        self.backup_sel = None;
                    }
                }
            }
            _ => return false,
        }
        true
    }

    fn click_dialog(&mut self, id: &str) -> bool {
        match (self.dialog.clone(), id) {
            (Some(Dialog::NewServer), _) => {
                if let Some(i) = id.strip_prefix("sw:").and_then(|n| n.parse::<usize>().ok()) {
                    self.form.software = i;
                    self.fetch_versions();
                } else if let Some(v) = id.strip_prefix("ver:") {
                    self.form.version = Some(v.to_string());
                } else if id == "unstable" {
                    self.form.unstable = !self.form.unstable;
                    self.fetch_versions();
                } else if id == "input:new-name" {
                    self.focus = Some("new-name".into());
                } else if id == "dlg:cancel" {
                    self.dialog = None;
                    self.task = None;
                } else if id == "dlg:create" {
                    self.create_server();
                } else {
                    return false;
                }
                true
            }
            (Some(Dialog::ConfirmVersion { id: sid, software, version }), "dlg:yes") => {
                self.dialog = None;
                self.task = Some(TaskInfo { number: -1, progress: 0.0, status: "Starting...".into() });
                self.task_is_version = true;
                self.call(Tag::VersionChange, "servers.version.change", json!({ "id": sid, "software": software, "version": version, "backup": true }));
                true
            }
            (Some(Dialog::Eula(sid)), "dlg:yes") => {
                self.dialog = None;
                self.call(Tag::Quiet, "servers.acceptEula", json!({ "id": sid }));
                self.call(Tag::Start(sid.clone()), "servers.start", json!({ "id": sid }));
                true
            }
            (Some(Dialog::ConfirmDelete(sid)), "dlg:yes") => {
                self.dialog = None;
                self.screen = Screen::List;
                self.call(Tag::Quiet, "servers.delete", json!({ "id": sid }));
                true
            }
            (Some(_), "dlg:no" | "dlg:ok" | "dlg:cancel") => {
                self.dialog = None;
                true
            }
            _ => false,
        }
    }

    fn create_server(&mut self) {
        if self.task.is_some() {
            return;
        }
        let name = self.input("new-name").trim().to_string();
        let (Some(version), Some((software, _))) = (self.form.version.clone(), self.software.get(self.form.software).cloned()) else {
            self.toast("Pick a version first.");
            return;
        };
        if name.is_empty() {
            self.toast("Give the server a name.");
            return;
        }
        self.task = Some(TaskInfo { number: -1, progress: 0.0, status: "Starting...".into() });
        self.call(Tag::Create, "servers.create", json!({ "name": name, "software": software, "version": version }));
    }

    fn key(&mut self, key: &str, text: Option<&str>, ctrl: bool) -> bool {
        if key == "escape" {
            if self.dialog.is_some() && self.task.is_none() {
                self.dialog = None;
            } else {
                self.focus = None;
            }
            return true;
        }
        let Some(focus) = self.focus.clone() else {
            if key == "enter" && matches!(self.dialog, Some(Dialog::Eula(_) | Dialog::ConfirmDelete(_))) {
                return self.click_dialog("dlg:yes");
            }
            if key == "enter" && matches!(self.dialog, Some(Dialog::Message { .. })) {
                self.dialog = None;
                return true;
            }
            return false;
        };
        // editing a property starts from its current value
        if !self.inputs.contains_key(&focus) {
            if let Some(name) = focus.strip_prefix("prop:") {
                if let Some(p) = self.props.iter().find(|p| p.0 == name) {
                    self.inputs.insert(focus.clone(), p.1.clone());
                }
            }
        }
        match key {
            "backspace" => {
                self.inputs.entry(focus).or_default().pop();
            }
            "enter" => {
                if focus == "cmd" {
                    let text = self.input("cmd").trim().to_string();
                    if !text.is_empty() {
                        if let Some(sid) = self.open_server().map(|s| s.id.clone()) {
                            self.call(Tag::Quiet, "servers.command", json!({ "id": sid, "text": text }));
                        }
                    }
                    self.inputs.insert("cmd".into(), String::new());
                } else if focus == "new-name" {
                    self.create_server();
                }
            }
            "v" if ctrl => {}
            _ => match text {
                Some(t) if !ctrl => self.inputs.entry(focus).or_default().push_str(t),
                _ => return false,
            },
        }
        true
    }
}
