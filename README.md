# LGA Mighty Tools

Small desktop tools for Nuke artists, in one tray app for Windows and macOS. Each tool has its own
switch: turn on the ones you want. A tool that is off isn't loaded at all: no memory, no CPU, no
shortcuts, no system hooks.

| Tool | What it does | Platforms |
| --- | --- | --- |
| **Nuke Shortcuts** | Two shortcuts for Nuke: set a key on the knob under the pointer, and frame every key in the Dope Sheet. | Windows, macOS |
| **Disk Space** | Watches your local drives and warns you when one runs low. On Windows it also shows what fills a drive, sorted by size, and frees space: caches, folders and files. | Windows, macOS |
| **Open in NukeX** | Double-click a `.nk` file to open it in the NukeX you already have open, or in your preferred version. | Windows, macOS |
| **Folder Switch** | Open and Save dialogs jump to the folder you have open in Explorer or XYplorer. | Windows |
| **Link Redirector** | Opens each link in the right browser: links with your keywords go to an alternative browser. | Windows, macOS |

Nothing is on after you install it. Open the window from the tray icon (the menu bar on macOS), pick
a tool on the left and turn it on.

## Status

Work in progress. The tools come from separate LGA apps (LGA Nuke Shortcuts, LGA OpenInNukeX,
LGA FolderSwitch and LGA LinkRedirector) and are being moved here one by one.

## Build

Qt 6.5 with MinGW (Windows) or Homebrew Qt 6 (macOS), CMake and Ninja.

- Windows: `compilar.bat` (Debug in `build\`), `compilar.bat --release`, `deploy.bat` (portable
  folder in `deploy\`) and `instalador.bat` (Inno Setup 6 installer).
- macOS: `./compilar.sh` (Debug in `build/`), `./compilar.sh --release`.
- `LGA_MightyTools --self-test` checks the logic that doesn't need a screen.

## License

MIT, see [LICENSE](LICENSE). The app uses Qt under the LGPL v3 and embeds the Inter typeface under the
SIL Open Font License 1.1.

Lega Pugliese · [github.com/legandrop](https://github.com/legandrop)
