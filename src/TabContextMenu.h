// TabContextMenu.h — adds an "Add to Project" entry to Notepad++'s own,
// native document-tab right-click context menu (the one with Close / Save /
// Rename / etc. on it), as a shortcut for adding just that tab's file to a
// project without opening the Project Manager panel at all.
//
// There is no official Notepad++ plugin API for this: NPPM_GETMENUHANDLE
// (see Notepad_plus_msgs.h) only ever hands back the main menu bar or the
// "Plugins" submenu, never the tab bar's own context menu. What *is*
// documented, standard Win32 behavior is that TrackPopupMenu() sends
// WM_INITMENUPOPUP to the menu's owner window just before the menu is shown,
// with the about-to-be-displayed HMENU as wParam - so if we subclass the
// right window (via the standard, chain-safe SetWindowSubclass API, which
// never permanently replaces anything and un-does itself cleanly) we can
// catch that notification and append our own item(s) to Notepad++'s menu an
// instant before it appears, then catch the resulting WM_COMMAND if the user
// picks one of them. There's no documented way to ask Notepad++ which window
// it actually passes as that owner, so this subclasses several candidates at
// once - the tab bar control(s) themselves, their parent container, and
// Notepad++'s main window - and only whichever one turns out to be the real
// owner ever does anything; the rest are harmless no-ops.
//
// Because this leans on undocumented, unverified behavior of Notepad++'s
// internals rather than a published API, treat it as best-effort: if a
// future Notepad++ version changes how its tab bar shows that menu, this
// quietly stops adding the item instead of breaking anything else. The
// Plugins menu's "Add Active Tab to Project" command (PluginDefinition.cpp)
// does the exact same job through a fully documented mechanism, as a
// guaranteed fallback.
#pragma once

#include "PluginInterface.h"

class ProjectManager;
class ProjectPanel;

namespace TabContextMenu
{
	// Locates Notepad++'s document tab bar(s) and subclasses them so the
	// "Add to Project" entry starts appearing on right-click. Call once,
	// after the docking panel exists (from NPPN_READY) — safe to call again,
	// later calls are ignored.
	void install(const NppData& nppData, ProjectManager* manager, ProjectPanel* panel);
}
