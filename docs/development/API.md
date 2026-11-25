# Core API

`turbomc-server --serve` runs the manager with no window (Qt's offscreen platform, no first-run wizard) and listens on a local socket:
`$TURBOMC_SOCKET`, else `$XDG_RUNTIME_DIR/turbomc-server.sock`, else `/tmp/turbomc-server-<uid>.sock`. `-d <dir>` picks the data folder.

One JSON object per line. Request `{"id":1,"method":"servers.list","params":{}}`, answer `{"id":1,"ok":true,"result":...}` or `{"id":1,"ok":false,"error":"text"}`. Anything without an `id` is an event.

| Method | Params | Result |
|---|---|---|
| `hello` | | name, protocol, dataDir |
| `software.list` | | `[{key, name}]` |
| `software.versions` | software, unstable | `[{id, stable, time}]` (answers when the lookup finishes) |
| `servers.list` / `servers.get` | id | server(s): id, name, software, softwareName, version, state, group, path, eula, addons |
| `servers.create` | name, software, version, group | `{task}`; progress arrives as `task` events |
| `servers.start` | id | error `eula` (ask the user, then `servers.acceptEula`), `java:<n>:<text>` (no suitable Java), or text |
| `servers.acceptEula` / `servers.stop` / `servers.kill` | id | |
| `servers.command` | id, text | |
| `servers.console` | id, max | `[{level, text}]` |
| `servers.rename` / `servers.delete` | id, name | delete refuses a running server |
| `servers.version.change` | id, software, version, backup (default true) | `{task}`; backs the world up first, then installs; progress as `task` events |
| `servers.properties.get` | id | `[{key, value, description}]` |
| `servers.properties.set` | id, values `{key: value}` | |
| `servers.backups.list` / `.create` / `.restore` / `.delete` | id, label / path | restore backs up the current world first |

Events: `console.line {server, text, level}`, `server.state {server, state}` (stopped, starting, running, stopping), `server.exited {server, code, crashed}`, `servers.changed`, `task {task, state: running|done|failed, progress, status, error}`.

Java: each server records the Java it needs when it is installed; the core scans the usual places (`/usr/lib/jvm`, `~/.sdkman`, `~/.jdks`, ...) and picks the exact major version or the next newer one. A per-server Java path override wins.

Not in the API yet: Modrinth mods and plugins, worlds, notes, playit.gg, port tools, settings.
