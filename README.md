# LGA Mighty Tools

Small desktop tools for Nuke artists, in one tray app for Windows and macOS. Each tool has its own
switch: turn on the ones you want. A tool that is off isn't loaded at all: no memory, no CPU, no
shortcuts, no system hooks.

| Tool | What it does | Platforms |
| --- | --- | --- |
| **Nuke Shortcuts** | Two shortcuts for Nuke: set a key on the knob under the pointer, and frame every key in the Dope Sheet. | Windows, macOS |
| **Disk Space** | Watches your local drives and warns you when one runs low. It also shows what fills a drive, sorted by size, and frees space: caches, folders and files. | Windows, macOS |
| **Open in NukeX** | Double-click a `.nk` file to open it in the NukeX you already have open, or in your preferred version. | Windows, macOS |
| **Folder Switch** | Open and Save dialogs jump to the folder you have open in Explorer or XYplorer (Finder on macOS). | Windows, macOS |
| **Link Redirector** | Opens each link in the right browser: links with your keywords go to an alternative browser. | Windows, macOS |
| **Window Layouts** | Move or minimize the active window with a shortcut. Combine horizontal and vertical halves, thirds or quarters, draw an area, or copy an open window's bounds. | Windows, macOS |
| **App Toggle** | One shortcut per app: opens it if it isn't running, brings it to the front, or minimizes it when it's already in front (on macOS it hides). | Windows, macOS |
| **Meet Frame** | Share just part of your screen in Teams, Meet or Zoom. Press a shortcut (Alt+Shift+Win+M by default, ⌘⌥⇧M on macOS), place and size the frame (free or locked to 16:9, 4:3 or 1:1) and share the window named Meet Frame; notifications, the desktop, WhatsApp, Telegram and any app you choose are covered for the others, and Pause freezes the picture. | Windows, macOS |
| **Mouse Buttons** | Hold a mouse button to hold keys and the left button, only in the apps you choose. For example, the middle button pans in Photoshop like Space + drag. | Windows |
| **Dictation** | Press a shortcut (Win+H by default) in any text field and speak: you see the text while you talk, and it's typed when you finish. It runs on your computer; the speech model is downloaded once from the tool's page. | Windows |
| **Desktop Icons** | Saves where your desktop icons are, once a day and whenever you want, and puts them back when Windows mixes them up, with undo. | Windows |

Nothing is on after you install it. Open the window from the tray icon (the menu bar on macOS), pick
a tool on the left and turn it on.

## Download

Installers are on the [releases page](https://github.com/legandrop/LGA_MightyTools/releases).
The app checks for new versions and updates itself.

The tools come from separate LGA apps (LGA Nuke Shortcuts, LGA OpenInNukeX, LGA FolderSwitch and
LGA LinkRedirector), which this app replaces.

## Build

Qt 6.5 and CMake (MinGW and Ninja on Windows, the official Qt 6.5 build on macOS).

- Windows: `compilar.bat` (Debug in `build\`), `compilar.bat --release`, `deploy.bat` (portable
  folder in `deploy\`) and `instalador.bat` (Inno Setup 6 installer).
  Use `instalador_local.bat` (or `instalador.bat --local`) to generate the installer and
  SHA256SUMS in `installer/` without GitHub checks or publication. In an interactive
  console it offers to install and to show the installer in Windows Explorer.
  Pass `--no-run` to skip both offers.
- macOS: `./compilar.sh` (Debug in `build/`), `./compilar.sh --release` (universal: Apple Silicon and
  Intel), `./deploy.sh` (self-contained bundle, installed in `/Applications`) and
  `./deploy.sh --zip --dmg` (the update ZIP and the disk image, in `deploy/`).
- `LGA_MightyTools --self-test` checks the logic that doesn't need a screen.

## License

MIT, see [LICENSE](LICENSE). The app uses Qt under the LGPL v3 and embeds the Inter typeface under the
SIL Open Font License 1.1. The macOS disk image is built with dmgbuild, ds_store and mac_alias (MIT),
vendored in `tools/macos/vendor/` with their licenses. Dictation downloads, only when you press its
button, the transcribe.cpp engine (MIT) and the Whisper large-v3 turbo weights (MIT); neither ships
with the installer.

Lega Pugliese · [github.com/legandrop](https://github.com/legandrop)
