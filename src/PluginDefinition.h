// PluginDefinition.h — the mandatory Notepad++ plugin entry points
// (declared extern "C" __declspec(dllexport) in PluginInterface.h) plus the
// menu command callbacks they wire up to ProjectPanel/ProjectManager.
#pragma once

#include "PluginInterface.h"

// --- mandatory plugin interface (called by Notepad++ itself) -----------
//
// setInfo / getName / getFuncsArray / beNotified / messageProc / isUnicode
// are declared __declspec(dllexport) already, in PluginInterface.h; we just
// provide their bodies in PluginDefinition.cpp. Nothing here needs to be
// called directly by our own code.

// Called once from DllMain(DLL_PROCESS_ATTACH, ...) — stashes the plugin's
// own module handle (needed to load our dialog resources) before setInfo()
// arrives with Notepad++'s window handles.
void pluginInit(HANDLE hModule);

// Called once from DllMain(DLL_PROCESS_DETACH, ...).
void pluginCleanUp();

// --- menu command callbacks (each is one FuncItem's _pFunc) -------------
//
// These simply forward to the ProjectPanel, which owns all the actual
// UI/logic. Kept as free functions (rather than member functions) because
// PFUNCPLUGINCMD is a plain "void (__cdecl *)()" with no way to carry a
// 'this' pointer.

void cmdNewProject();
void cmdOpenProject();
void cmdSaveProject();
void cmdSaveProjectAs();
void cmdSaveSession();
void cmdCloseProject();
void cmdAddFolderToProject();
void cmdAddFilesToProject();
void cmdOpenAllProjectFiles();
void cmdTogglePanel();
void cmdAbout();
