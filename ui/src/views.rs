//! Turns the app state into screens. Everything here is built from TurboUI blocks and theme tokens.
use crate::app::{App, Dialog, Page, Screen};
use turboui_core::{button, checkbox, col, cube, dialog, icon, label, progress, row, spacer, surface, text_input, Align, ButtonKind, Color, Node, Theme};

fn hov(app: &App, id: &str) -> bool {
    app.hovered.as_deref() == Some(id)
}

fn hline(t: &Theme) -> Node {
    col().h(1.0).bg(t.role("border"))
}
fn vline(t: &Theme) -> Node {
    col().w(1.0).bg(t.role("border"))
}

/// A toolbar or action-list entry: icon then text.
fn tool(app: &App, id: &str, ico: &str, text: &str, enabled: bool) -> Node {
    let t = &app.theme;
    let c = if enabled { t.role("text") } else { t.role("disabled") };
    let mut n = row().gap(7.0).pad_xy(10.0, 6.0).align(Align::Center).radius(t.num("radius_ctl"))
        .child(icon(ico, 16.0, c))
        .child(label(text).size(t.num("text_md")).color(c));
    if enabled {
        n = n.id(id);
        if hov(app, id) {
            n = n.bg(t.role("hover"));
        }
    }
    n
}

fn state_color(t: &Theme, state: &str) -> Color {
    match state {
        "running" => t.role("ok"),
        "starting" | "stopping" => t.role("warn"),
        _ => t.role("disabled"),
    }
}

fn status_bar(app: &App, left: String) -> Node {
    let t = &app.theme;
    row().h(26.0).pad_xy(10.0, 0.0).align(Align::Center)
        .child(label(left).size(t.num("text_sm")).color(t.role("muted")))
        .child(spacer())
        .child(label("TurboMC Server").size(t.num("text_sm")).color(t.role("muted")))
}

/// The window background: ambient light in glass styles, a plain fill otherwise.
fn backdrop(t: &Theme) -> Node {
    let n = col().bg(if t.glass().is_some() { t.role("view") } else { t.role("window") });
    if t.glass().is_some() {
        n.radials(t.ambient().to_vec()).pad(t.num("float")).gap(t.num("float"))
    } else {
        n
    }
}

pub fn root(app: &App) -> Node {
    let t = &app.theme;
    let content = if let Some(err) = &app.connection_error {
        disconnected(app, err)
    } else {
        match &app.screen {
            Screen::List => list_screen(app),
            Screen::Server(..) => server_screen(app),
        }
    };
    let mut page = col().bg(t.role("view")).child(content.grow(1.0));
    if let Some((text, _)) = &app.toast {
        page = page.child(
            col().overlay().align(Align::Center).justify(turboui_core::Justify::End).pad(40.0)
                .child(surface(t, row().pad_xy(18.0, 10.0).child(label(text.clone()).size(t.num("text_md")).color(t.role("text"))), 18.0)),
        );
    }
    if let Some(d) = &app.dialog {
        page = page.child(dialog_view(app, d));
    }
    page
}

fn disconnected(app: &App, err: &str) -> Node {
    let t = &app.theme;
    backdrop(t).grow(1.0).align(Align::Center).justify(turboui_core::Justify::Center).gap(14.0)
        .child(cube(72.0, t.role("accent")))
        .child(label("Can't reach the TurboMC core").bold().size(t.num("text_lg")).color(t.role("text")))
        .child(label(err.to_string()).size(t.num("text_md")).color(t.role("muted")))
        .child(button(t, "retry", "Try again", ButtonKind::Primary, hov(app, "retry"), true))
}

// ---------------------------------------------------------------- the list of servers

fn list_screen(app: &App) -> Node {
    let t = &app.theme;
    let sel = app.selected_server();
    let on = sel.is_some();
    let running = sel.map_or(false, |s| s.running());
    let toolbar = row().pad(4.0).gap(2.0).align(Align::Center)
        .child(tool(app, "add", "plus", "Add Server", true))
        .child(col().w(1.0).h(22.0).bg(t.role("border")))
        .child(tool(app, "refresh", "folder", "Refresh", true));
    let actions = col().w(176.0).pad(6.0).gap(1.0)
        .child(tool(app, "act:start", "play", "Start", on && !running))
        .child(tool(app, "act:stop", "stop", "Stop", on && running))
        .child(tool(app, "act:kill", "kill", "Kill", on && running))
        .child(col().h(1.0).bg(t.role("border")))
        .child(tool(app, "act:edit", "edit", "Open", on))
        .child(tool(app, "act:delete", "delete", "Delete", on && !running));
    let tiles = if app.servers.is_empty() {
        col().gap(10.0).pad(30.0).align(Align::Center)
            .child(cube(64.0, t.role("accent")))
            .child(label("No servers yet").bold().size(t.num("text_lg")).color(t.role("text")))
            .child(label("Add one to get started: pick the software and a Minecraft version.").size(t.num("text_md")).color(t.role("muted")))
    } else {
        row().wrap().gap(6.0).children(app.servers.iter().enumerate().map(|(i, s)| {
            let id = format!("srv:{i}");
            let mut n = col().id(id.clone()).w(116.0).h(104.0).pad(8.0).gap(6.0).align(Align::Center).radius(t.num("radius_card").min(16.0))
                .child(cube(56.0, t.role("accent")))
                .child(
                    row().gap(5.0).align(Align::Center)
                        .child(icon("dot", 8.0, state_color(t, &s.state)))
                        .child(label(s.name.clone()).size(t.num("text_md")).color(t.role("text"))),
                );
            if app.selected == Some(i) {
                n = n.bg(t.role("selection")).border(1.0, t.role("accent"));
            } else if hov(app, &id) {
                n = n.bg(t.role("hover"));
            }
            n
        }))
    };
    let group = col().gap(8.0)
        .child(row().gap(8.0).align(Align::Center).child(label("Servers").size(t.num("text_md")).color(t.role("muted"))).child(hline(t).grow(1.0)))
        .child(tiles);
    let status = match sel {
        Some(s) => format!("{} {}  -  {}", s.software_name, s.version, s.state),
        None => format!("{} servers", app.servers.len()),
    };
    if t.glass().is_some() {
        let f = t.num("float");
        backdrop(t)
            .child(surface(t, toolbar.pad_xy(10.0, 4.0), 9999.0))
            .child(row().grow(1.0).gap(f).child(col().grow(1.0).pad(10.0).clip().child(group)).child(surface(t, actions.pad(12.0), t.num("radius_bar"))))
            .child(status_bar(app, status))
    } else {
        col().bg(t.role("window"))
            .child(toolbar)
            .child(hline(t))
            .child(row().grow(1.0).child(col().grow(1.0).bg(t.role("view")).pad(10.0).clip().child(group)).child(vline(t)).child(actions))
            .child(hline(t))
            .child(status_bar(app, status))
    }
}

// ---------------------------------------------------------------- one server

fn level_color(t: &Theme, level: &str) -> Color {
    match level {
        "error" => t.role("err"),
        "warn" => t.role("warn"),
        "manager" => t.role("accent"),
        "debug" => t.role("muted"),
        _ => t.role("text"),
    }
}

fn console_page(app: &App, sid: &str) -> Node {
    let t = &app.theme;
    let lines = app.console.get(sid).map(Vec::as_slice).unwrap_or(&[]);
    let list = col().gap(2.0).children(lines.iter().map(|(level, text)| label(text.clone()).mono().size(12.0).color(level_color(t, level))));
    let area = col().grow(1.0).pad(10.0).bg(t.role("console_bg")).border(1.0, t.role("border")).radius(t.num("radius_card").min(12.0))
        .child(col().grow(1.0).child(list).scroll_area("console", app.scroll_of("console")));
    let focused = app.focus.as_deref() == Some("cmd");
    let running = app.open_server().map_or(false, |s| s.running());
    col().grow(1.0).gap(8.0)
        .child(area)
        .child(text_input(t, "input:cmd", app.input("cmd"), if running { "Type a command and press Enter" } else { "Start the server to send commands" }, focused))
}

fn properties_page(app: &App) -> Node {
    let t = &app.theme;
    let rows = app.props.iter().map(|(k, v, desc)| {
        let key = format!("prop:{k}");
        let value = app.inputs.get(&key).cloned().unwrap_or_else(|| v.clone());
        let focused = app.focus.as_deref() == Some(key.as_str());
        let editor = if value == "true" || value == "false" {
            checkbox(t, &format!("togg:{k}"), value == "true", "")
        } else {
            text_input(t, &format!("input:{key}"), &value, "", focused).w(260.0)
        };
        let mut r = col().gap(2.0).pad_xy(6.0, 5.0)
            .child(row().gap(12.0).align(Align::Center).child(col().w(230.0).child(label(k.clone()).size(t.num("text_md")).color(t.role("text")))).child(editor));
        if !desc.is_empty() {
            r = r.child(label(desc.clone()).size(11.0).color(t.role("muted")));
        }
        r
    });
    let list = col().gap(2.0).children(rows);
    col().grow(1.0).gap(8.0)
        .child(col().grow(1.0).pad(6.0).bg(t.role("console_bg")).border(1.0, t.role("border")).radius(t.num("radius_card").min(12.0)).child(col().grow(1.0).child(list).scroll_area("props", app.scroll_of("props"))))
        .child(row().gap(8.0).child(spacer()).child(button(t, "props:save", "Save changes", ButtonKind::Primary, hov(app, "props:save"), true)))
}

fn fmt_size(bytes: u64) -> String {
    if bytes >= 1 << 30 {
        format!("{:.1} GB", bytes as f64 / (1u64 << 30) as f64)
    } else if bytes >= 1 << 20 {
        format!("{:.1} MB", bytes as f64 / (1u64 << 20) as f64)
    } else {
        format!("{} KB", bytes / 1024)
    }
}

fn backups_page(app: &App) -> Node {
    let t = &app.theme;
    let rows = app.backups.iter().enumerate().map(|(i, b)| {
        let id = format!("bk:sel:{i}");
        let mut r = row().id(id.clone()).gap(12.0).pad_xy(10.0, 8.0).align(Align::Center).radius(t.num("radius_ctl").min(10.0))
            .child(icon("export", 16.0, t.role("text")))
            .child(label(b.name.clone()).size(t.num("text_md")).color(t.role("text")))
            .child(spacer())
            .child(label(fmt_size(b.size)).size(t.num("text_sm")).color(t.role("muted")));
        if app.backup_sel == Some(i) {
            r = r.bg(t.role("selection"));
        } else if hov(app, &id) {
            r = r.bg(t.role("hover"));
        }
        r
    });
    let body = if app.backups.is_empty() {
        col().grow(1.0).align(Align::Center).justify(turboui_core::Justify::Center)
            .child(label("No backups yet.").size(t.num("text_md")).color(t.role("muted")))
    } else {
        col().grow(1.0).child(col().gap(2.0).children(rows).scroll_area("backups", app.scroll_of("backups")))
    };
    let sel = app.backup_sel.is_some();
    let stopped = app.open_server().map_or(true, |s| !s.running());
    col().grow(1.0).gap(8.0)
        .child(row().gap(8.0)
            .child(button(t, "bk:create", "Create backup", ButtonKind::Primary, hov(app, "bk:create"), true))
            .child(spacer())
            .child(button(t, "bk:restore", "Restore", ButtonKind::Normal, hov(app, "bk:restore"), sel && stopped))
            .child(button(t, "bk:delete", "Delete", ButtonKind::Danger, hov(app, "bk:delete"), sel)))
        .child(col().grow(1.0).pad(6.0).bg(t.role("console_bg")).border(1.0, t.role("border")).radius(t.num("radius_card").min(12.0)).child(body))
        .child(label(if stopped { "Backups are zip files of the world folders." } else { "Stop the server to restore a backup." }).size(t.num("text_sm")).color(t.role("muted")))
}

fn soon_page(app: &App, page: Page) -> Node {
    let t = &app.theme;
    col().grow(1.0).align(Align::Center).justify(turboui_core::Justify::Center).gap(8.0)
        .child(label(page.label()).bold().size(t.num("text_lg")).color(t.role("text")))
        .child(label("This page is not built yet.").size(t.num("text_md")).color(t.role("muted")))
}

fn server_screen(app: &App) -> Node {
    let t = &app.theme;
    let Screen::Server(sid, page) = &app.screen else { return col() };
    let Some(server) = app.open_server() else { return list_screen(app) };
    let pages = col().w(190.0).pad(6.0).gap(1.0).children(Page::ALL.iter().map(|p| {
        let id = format!("page:{}", p.key());
        let mut n = row().id(id.clone()).gap(8.0).pad_xy(10.0, 7.0).align(Align::Center).radius(t.num("radius_ctl"))
            .child(icon(p.icon(), 16.0, t.role("text")))
            .child(label(p.label()).size(t.num("text_md")).color(t.role("text")));
        if p == page {
            n = n.bg(t.role("selection"));
        } else if hov(app, &id) {
            n = n.bg(t.role("hover"));
        }
        n
    }));
    let body = match page {
        Page::Console => console_page(app, sid),
        Page::Properties => properties_page(app),
        Page::Backups => backups_page(app),
        other => soon_page(app, *other),
    };
    let running = server.running();
    let buttons = row().gap(8.0)
        .child(button(t, "back", "Back to servers", ButtonKind::Normal, hov(app, "back"), true))
        .child(spacer())
        .child(if running { button(t, "stop", "Stop", ButtonKind::Primary, hov(app, "stop"), server.state == "running") } else { button(t, "start", "Start", ButtonKind::Primary, hov(app, "start"), true) })
        .child(button(t, "kill", "Kill", ButtonKind::Danger, hov(app, "kill"), running));
    let title = row().gap(10.0).align(Align::Center).pad_xy(12.0, 8.0)
        .child(cube(26.0, t.role("accent")))
        .child(label(server.name.clone()).bold().size(t.num("text_lg")).color(t.role("text")))
        .child(label(format!("{} {}", server.software_name, server.version)).size(t.num("text_md")).color(t.role("muted")))
        .child(icon("dot", 9.0, state_color(t, &server.state)))
        .child(label(server.state.clone()).size(t.num("text_sm")).color(t.role("muted")));
    let content = col().grow(1.0).pad(8.0).gap(8.0).clip().child(body).child(buttons);
    if t.glass().is_some() {
        let f = t.num("float");
        backdrop(t)
            .child(title)
            .child(row().grow(1.0).gap(f).child(surface(t, pages.pad(12.0), t.num("radius_bar"))).child(content))
    } else {
        col().bg(t.role("window")).child(title).child(hline(t)).child(row().grow(1.0).child(pages).child(vline(t)).child(content))
    }
}

// ---------------------------------------------------------------- dialogs

fn dialog_view(app: &App, d: &Dialog) -> Node {
    let t = &app.theme;
    let mono = |s: &str| label(s.to_string()).size(t.num("text_md")).color(t.role("text"));
    match d {
        Dialog::Message { title, text } => dialog(t, title, mono(text), vec![button(t, "dlg:ok", "OK", ButtonKind::Primary, hov(app, "dlg:ok"), true)], 440.0),
        Dialog::Eula(_) => dialog(
            t,
            "Minecraft EULA",
            col().gap(6.0)
                .child(mono("To run a Minecraft server you must accept the Minecraft End User License Agreement:"))
                .child(label("https://aka.ms/MinecraftEULA").size(t.num("text_md")).color(t.role("accent")))
                .child(mono("Do you accept it?")),
            vec![button(t, "dlg:no", "No", ButtonKind::Normal, hov(app, "dlg:no"), true), button(t, "dlg:yes", "I accept", ButtonKind::Primary, hov(app, "dlg:yes"), true)],
            460.0,
        ),
        Dialog::ConfirmDelete(sid) => {
            let name = app.servers.iter().find(|s| &s.id == sid).map(|s| s.name.clone()).unwrap_or_default();
            dialog(
                t,
                "Delete server",
                mono(&format!("Delete \"{name}\" and everything in its folder, including its worlds? This cannot be undone.")),
                vec![button(t, "dlg:no", "Cancel", ButtonKind::Normal, hov(app, "dlg:no"), true), button(t, "dlg:yes", "Delete", ButtonKind::Danger, hov(app, "dlg:yes"), true)],
                460.0,
            )
        }
        Dialog::NewServer => new_server_dialog(app),
    }
}

fn new_server_dialog(app: &App) -> Node {
    let t = &app.theme;
    let f = &app.form;
    let busy = app.task.is_some();
    let sw_buttons = row().gap(6.0).children(app.software.iter().enumerate().map(|(i, (_, name))| {
        let kind = if i == f.software { ButtonKind::Primary } else { ButtonKind::Normal };
        button(t, &format!("sw:{i}"), name, kind, hov(app, &format!("sw:{i}")), !busy)
    }));
    let versions: Node = if f.loading {
        col().pad(12.0).child(label("Loading versions...").size(t.num("text_md")).color(t.role("muted")))
    } else if f.versions.is_empty() {
        col().pad(12.0).child(label("No versions found.").size(t.num("text_md")).color(t.role("muted")))
    } else {
        col().gap(1.0).children(f.versions.iter().map(|(v, stable)| {
            let id = format!("ver:{v}");
            let mut r = row().id(id.clone()).gap(8.0).pad_xy(10.0, 5.0).align(Align::Center).radius(6.0)
                .child(label(v.clone()).size(t.num("text_md")).color(t.role("text")));
            if !stable {
                r = r.child(label("snapshot").size(t.num("text_sm")).color(t.role("warn")));
            }
            if f.version.as_deref() == Some(v.as_str()) {
                r = r.bg(t.role("selection"));
            } else if hov(app, &id) {
                r = r.bg(t.role("hover"));
            }
            r
        }))
    };
    let list = col().h(170.0).pad(4.0).bg(t.role("input_bg")).border(1.0, t.role("border")).radius(10.0).clip()
        .child(col().child(versions).scroll_area("versions", app.scroll.get("versions").copied().unwrap_or(0.0)));
    let body = col().gap(10.0)
        .child(label("Name").size(t.num("text_sm")).color(t.role("muted")))
        .child(text_input(t, "input:new-name", app.input("new-name"), "Server name", app.focus.as_deref() == Some("new-name")))
        .child(label("Software").size(t.num("text_sm")).color(t.role("muted")))
        .child(sw_buttons)
        .child(row().align(Align::Center).child(label("Minecraft version").size(t.num("text_sm")).color(t.role("muted"))).child(spacer()).child(checkbox(t, "unstable", f.unstable, "Show snapshots")))
        .child(list)
        .child(match &app.task {
            Some(task) => col().gap(4.0).child(progress(t, task.progress, 420.0)).child(label(if task.status.is_empty() { "Working...".to_string() } else { task.status.clone() }).size(t.num("text_sm")).color(t.role("muted"))),
            None => col(),
        });
    dialog(
        t,
        "New server",
        body,
        vec![
            button(t, "dlg:cancel", "Cancel", ButtonKind::Normal, hov(app, "dlg:cancel"), true),
            button(t, "dlg:create", "Create", ButtonKind::Primary, hov(app, "dlg:create"), !busy && f.version.is_some()),
        ],
        460.0,
    )
}
