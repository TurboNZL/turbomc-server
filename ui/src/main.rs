//! TurboMC Server: the window. It talks to the core program (`turbomc-server --serve`) over a local socket.
mod app;
mod client;
mod views;

use app::App;
use turboui_gpui::Options;

fn main() {
    let look = std::env::var("TURBOMC_THEME").unwrap_or_else(|_| "macos-dark/liquid".to_string());
    let theme = turboui_core::builtin::theme_from_arg(&look).unwrap_or_else(|| {
        eprintln!("unknown theme `{look}`, using macos-dark/liquid");
        turboui_core::builtin::theme("macos-dark", "liquid").expect("built-in theme")
    });
    #[cfg(feature = "headless")]
    {
        let args: Vec<String> = std::env::args().skip(1).collect();
        if let Some(pos) = args.iter().position(|a| a == "--shot") {
            let out = args.get(pos + 1).expect("--shot needs an output path").clone();
            let steps: Vec<String> = args[pos + 2..].to_vec();
            let (w, h) = (1000.0, 640.0);
            let app = App::new(theme);
            let actions: Vec<turboui_gpui::Action> = steps
                .iter()
                .map(|s| match s.split_once(':') {
                    Some(("click", id)) => turboui_gpui::Action::Click(id),
                    Some(("key", k)) => turboui_gpui::Action::Key(k),
                    Some(("type", t)) => turboui_gpui::Action::Type(t),
                    Some(("wait", ms)) => turboui_gpui::Action::Wait(ms.parse().unwrap_or(200)),
                    _ => turboui_gpui::Action::Tick,
                })
                .collect();
            let (pw, ph, px) = turboui_gpui::screenshot_after(app, |app| views::root(app), |app, event| app.event(event), &actions, w, h);
            std::fs::write(&out, turboui_raster::png_from_rgba(pw, ph, &px)).expect("write png");
            println!("wrote {out}");
            return;
        }
    }
    eprintln!("Connecting to the core at {} ...", client::socket_path().display());
    let app = App::new(theme);
    turboui_gpui::run_with(
        Options { title: "TurboMC Server".into(), size: (1000.0, 640.0), tick_ms: 60 },
        app,
        |app| views::root(app),
        |app, event| app.event(event),
    );
}
