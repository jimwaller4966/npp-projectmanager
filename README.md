# Project Manager for Notepad++

A native Notepad++ plugin that adds real project/workspace management: group
folders and loose files into named **projects**, browse them in a dockable
panel (with lazy-loaded, live folder contents — not a static snapshot),
open every project file with one click, and remember which files were open
so you can pick up exactly where you left off.

It's a genuine Win32 plugin DLL (C++17), built on Notepad++'s own official
plugin interface — not a script or a macro.

## What it does

- **Projects** are just a name plus a list of root folders and/or loose
  files, saved as a small, human-readable `.nppproj` text file.
- The docking panel (**Plugins → Project Manager → Show Project Panel**)
  shows every open project as a tree. Folders are scanned from disk lazily,
  as you expand them, so adding a huge folder to a project costs nothing
  until you actually look inside it.
- Double-click any file to open it (or switch to it, if it's already open).
- Right-click for a context menu: add/remove folders and files, open every
  file in a project at once, refresh a folder, reveal a file in Explorer,
  etc.
- **Plugins → Project Manager → Add Active Tab to Project** adds whichever
  file is currently active straight to a project, no picker dialog needed.
  (An earlier version tried to add this as an entry on Notepad++'s own
  *tab-bar* right-click menu directly. Notepad++ has no official plugin API
  for that, and the undocumented trick needed to fake it turned out to be
  unreliable in practice - it's been pulled back out in favor of this
  Plugins-menu command, which uses Notepad++'s normal, documented interface
  and just works.)
- **Save Session** snapshots exactly which files are currently open in
  Notepad++ (both views) into the project, so **Open All Project Files**
  later reopens that same set.
- Whatever projects you had open are remembered across Notepad++ restarts
  automatically.
- A tracked file or folder that no longer exists on disk - renamed, moved,
  or deleted, by Notepad++, Windows Explorer, or anything else - quietly
  drops out of the project the next time its tree refreshes (which is after
  almost every action in the panel, and at startup), rather than sitting
  there as a permanent stale entry. Note that this means a *rename* isn't
  auto-followed to the file's new name - the old entry just disappears, and
  you re-add the file under its new name if you still want it tracked.
- Everything is also reachable from the **Plugins → Project Manager** menu,
  for when you don't want to touch the panel directly.

## Repository layout

```
CMakeLists.txt              Build definition
src/                        The plugin's own source (yours to read/modify)
  PluginEntry.cpp             DllMain
  PluginDefinition.h/.cpp      Notepad++'s mandatory plugin interface + menu wiring
  Project.h/.cpp                One project: folders/files/session, .nppproj load & save
  ProjectManager.h/.cpp          Owns all open projects; talks to Notepad++ (open files, etc.)
  ProjectPanel.h/.cpp             The dockable tree view, its context menu, dialogs
  StrUtil.h                      UTF-8/UTF-16 helpers
  WinFileIO.h                    Small Win32-based whole-file read/write helpers
  resource.h / PluginResource.rc  The two tiny dialog templates the panel needs
sdk/                         The official Notepad++ plugin SDK headers, vendored
                             verbatim from the notepad-plus-plus and npp-plugins
                             GitHub repositories (PluginInterface.h,
                             Notepad_plus_msgs.h, Scintilla.h, menuCmdID.h) plus
                             the classic, dependency-free "DockingFeature"
                             framework (DockingFeature/) that every third-party
                             docking plugin builds on. You shouldn't need to
                             touch anything under sdk/.
```

## Building automatically with GitHub Actions (no local compiler needed)

The repo already includes `.github/workflows/build.yml`, which builds both a
64-bit and a 32-bit `NppProjectManager.dll` on a hosted Windows runner every
time you push. You never need to install Visual Studio or CMake yourself.

1. **Create an empty repo on GitHub** — go to github.com → the **+** in the
   top right → **New repository**. Give it a name (e.g.
   `npp-project-manager`), leave it empty (don't add a README/.gitignore —
   this folder already has one), and create it.
2. **Push this folder to it.** In VS Code, open a terminal in this folder
   (`` Ctrl+` ``) and run:
   ```bat
   git init
   git add .
   git commit -m "Project Manager plugin for Notepad++"
   git branch -M main
   git remote add origin https://github.com/<your-username>/<repo-name>.git
   git push -u origin main
   ```
3. **Watch it build.** On GitHub, open your repo's **Actions** tab — a
   "Build Notepad++ Plugin" run starts automatically. It takes a couple of
   minutes.
4. **Download the DLL.** Once the run finishes (green check), open it and
   scroll to the **Artifacts** section at the bottom. Download
   **NppProjectManager-x64** (or `-Win32` if your Notepad++ is 32-bit) — it's
   a small zip containing just the DLL.
5. From then on, every time you push a change, a fresh build appears in
   Actions automatically. If you'd rather have a permanent download link
   instead of digging through Actions runs, push a tag (`git tag v1.0 && git
   push origin v1.0`) and the workflow also publishes both DLLs to a GitHub
   Release.

Then skip straight to **Installing it into Notepad++** below — everything
after this section describing a *local* build is optional if you go this
route.

## Building locally instead

If you'd rather not use GitHub Actions, or want to iterate on the code
without pushing each time, you can still build directly on your machine.
This produces a native Windows DLL, so it has to be built **on Windows**.
You'll need:

- Visual Studio 2019 or later with the **"Desktop development with C++"**
  workload (this gives you both a C++ compiler and CMake — nothing else to
  install), **or** any other MSVC/MinGW-w64 toolchain plus a standalone
  CMake install.

### Option A — Visual Studio "Open Folder" (easiest)

1. In Visual Studio: **File → Open → Folder...**, select the
   `npp-project-manager` folder (the one with `CMakeLists.txt` in it).
   Visual Studio detects the `CMakeLists.txt` and configures itself
   automatically.
2. At the top toolbar, pick a configuration matching your Notepad++
   install's bitness — **x64-Release** for a normal 64-bit Notepad++
   install (the common case today), or an x86 configuration if you're on
   32-bit Notepad++.
3. **Build → Build All** (or press Ctrl+Shift+B).
4. The built DLL lands under
   `out/build/<configuration-name>/NppProjectManager.dll`.

### Option B — command line

From a "Developer Command Prompt for VS" (or any shell with `cmake` and a
Windows compiler on PATH):

```bat
cmake -S . -B build -A x64
cmake --build build --config Release
```

Drop `-A x64` (or change it to `-A Win32`) to match a 32-bit Notepad++
install instead. The DLL ends up at `build\Release\NppProjectManager.dll`.

## Installing it into Notepad++

Notepad++ loads a plugin from its own subfolder, named after the DLL,
under one of:

- **Per-user (no admin rights needed, recommended):**
  `%APPDATA%\Notepad++\plugins\NppProjectManager\NppProjectManager.dll`
- **Machine-wide:**
  `<your Notepad++ install folder>\plugins\NppProjectManager\NppProjectManager.dll`
  (typically `C:\Program Files\Notepad++\plugins\...`)

Create the `NppProjectManager` folder yourself if it doesn't exist, copy
the built `.dll` into it, then (re)start Notepad++. You'll find everything
under the new **Plugins → Project Manager** menu.

> **Bitness must match.** A 64-bit Notepad++ can only load a 64-bit plugin
> DLL, and likewise for 32-bit. If the plugin doesn't show up at all after
> restarting, this is the first thing to check — Notepad++ silently skips
> plugins built for the wrong architecture.

## The `.nppproj` file format

Deliberately plain text (UTF-8), not JSON/XML, so it's easy to read, hand-edit,
or put under source control:

```ini
; Notepad++ Project File
[Project]
Name=My Website

[Folders]
C:\Users\Jim\Sites\mywebsite

[Files]
C:\Users\Jim\Notes\todo.txt

[OpenFiles]
C:\Users\Jim\Sites\mywebsite\index.html
C:\Users\Jim\Sites\mywebsite\style.css
```

`[OpenFiles]` is the remembered "session" — written whenever you use
**Save Session**, and replayed by **Open All Project Files**.

The plugin also keeps a tiny `ProjectManager.session` file (just a list of
`.nppproj` paths, one per line) in its own Notepad++ plugin config folder,
so it knows which projects to reopen automatically next time.

## Extending it

The code is organized so the three concerns stay separate:

- `Project` — pure data + file I/O, no Notepad++ or UI dependency at all.
- `ProjectManager` — the only class that talks to Notepad++ itself (opening
  files, reading which files are open).
- `ProjectPanel` — the only class that touches Win32 UI.

Most feature ideas (a "recent projects" list, filtering the tree by file
type, drag-and-drop reordering, per-project settings) fit into exactly one
of those three files without touching the others.
