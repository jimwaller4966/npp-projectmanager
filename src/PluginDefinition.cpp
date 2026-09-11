#include "PluginDefinition.h"
#include "ProjectManager.h"
#include "ProjectPanel.h"
#include "resource.h"

#include <cwchar>

// ---------------------------------------------------------------------
// Plugin-wide state. A Notepad++ plugin DLL is loaded once per Notepad++
// process (not once per document/window), so ordinary globals are the
// normal, expected way to hold this.
// ---------------------------------------------------------------------

namespace
{
	NppData g_nppData{};
	HINSTANCE g_hModule = nullptr;

	ProjectManager g_projectManager;
	ProjectPanel g_projectPanel;

	bool g_panelRegistered = false;
	std::wstring g_configDir;

	// Index of each command within g_funcItems — also doubles as the
	// docking dialog's "dlgID" for the toggle-panel command (see
	// NPPM_DMMREGASDCKDLG's contract: dlgID must be the index of the
	// FuncItem that shows/hides this panel).
	enum FuncIndex
	{
		FI_NEW_PROJECT = 0,
		FI_OPEN_PROJECT,
		FI_SAVE_PROJECT,
		FI_SAVE_PROJECT_AS,
		FI_SAVE_SESSION,
		FI_SEP_1,
		FI_ADD_FOLDER,
		FI_ADD_FILES,
		FI_OPEN_ALL_FILES,
		FI_CLOSE_PROJECT,
		FI_SEP_2,
		FI_TOGGLE_PANEL,
		FI_SEP_3,
		FI_ABOUT,
		FI_COUNT
	};

	FuncItem g_funcItems[FI_COUNT];
	bool g_funcItemsInitialized = false;

	void setItem(int index, const wchar_t* name, PFUNCPLUGINCMD fn)
	{
		FuncItem& item = g_funcItems[index];
		size_t len = ::wcslen(name);
		if (len >= menuItemSize)
			len = menuItemSize - 1;
		::wmemcpy(item._itemName, name, len);
		item._itemName[len] = L'\0';
		item._pFunc = fn;
		item._cmdID = 0;         // Notepad++ assigns the real command ID
		item._init2Check = false;
		item._pShKey = nullptr;
	}

	void setSeparator(int index)
	{
		// A FuncItem with a null function pointer renders as a menu
		// separator; its name/shortcut fields are ignored.
		FuncItem& item = g_funcItems[index];
		item._itemName[0] = L'\0';
		item._pFunc = nullptr;
		item._cmdID = 0;
		item._init2Check = false;
		item._pShKey = nullptr;
	}

	void ensureFuncItemsInitialized()
	{
		if (g_funcItemsInitialized)
			return;

		setItem(FI_NEW_PROJECT, L"New Project...", cmdNewProject);
		setItem(FI_OPEN_PROJECT, L"Open Project...", cmdOpenProject);
		setItem(FI_SAVE_PROJECT, L"Save Project", cmdSaveProject);
		setItem(FI_SAVE_PROJECT_AS, L"Save Project As...", cmdSaveProjectAs);
		setItem(FI_SAVE_SESSION, L"Remember Open Files (Save Session)", cmdSaveSession);
		setSeparator(FI_SEP_1);
		setItem(FI_ADD_FOLDER, L"Add Folder to Project...", cmdAddFolderToProject);
		setItem(FI_ADD_FILES, L"Add Files to Project...", cmdAddFilesToProject);
		setItem(FI_OPEN_ALL_FILES, L"Open All Project Files", cmdOpenAllProjectFiles);
		setItem(FI_CLOSE_PROJECT, L"Close Project", cmdCloseProject);
		setSeparator(FI_SEP_2);
		setItem(FI_TOGGLE_PANEL, L"Show Project Panel", cmdTogglePanel);
		setSeparator(FI_SEP_3);
		setItem(FI_ABOUT, L"About...", cmdAbout);

		g_funcItemsInitialized = true;
	}

	// Creates and registers the docking panel with Notepad++'s docking
	// manager if that hasn't happened yet this session. Safe to call from
	// more than one place (NPPN_READY and the toggle command both do).
	void ensurePanelCreated()
	{
		if (g_panelRegistered)
			return;

		g_projectPanel.setManager(&g_projectManager);

		tTbData data = {};
		g_projectPanel.create(&data);

		data.uMask = DWS_DF_CONT_LEFT;
		data.pszModuleName = g_projectPanel.getPluginFileName();
		data.dlgID = FI_TOGGLE_PANEL;

		::SendMessage(g_nppData._nppHandle, NPPM_DMMREGASDCKDLG, 0, reinterpret_cast<LPARAM>(&data));

		g_panelRegistered = true;
	}

	std::wstring readPluginConfigDir()
	{
		wchar_t buffer[MAX_PATH] = { 0 };
		::SendMessage(g_nppData._nppHandle, NPPM_GETPLUGINSCONFIGDIR, MAX_PATH, reinterpret_cast<LPARAM>(buffer));
		std::wstring dir = buffer;
		if (!dir.empty())
			::CreateDirectoryW(dir.c_str(), nullptr); // no-op if it already exists
		return dir;
	}
}

// ---------------------------------------------------------------------
// pluginInit / pluginCleanUp — called from DllMain, see PluginEntry.cpp
// ---------------------------------------------------------------------

void pluginInit(HANDLE hModule)
{
	g_hModule = static_cast<HINSTANCE>(hModule);
}

void pluginCleanUp()
{
	if (!g_configDir.empty())
		g_projectManager.saveLastSession(g_configDir);
}

// ---------------------------------------------------------------------
// Mandatory Notepad++ plugin interface
// ---------------------------------------------------------------------

extern "C" __declspec(dllexport) void setInfo(NppData notpadPlusData)
{
	g_nppData = notpadPlusData;
	g_projectPanel.init(g_hModule, g_nppData._nppHandle);
	g_projectManager.init(g_nppData);
}

extern "C" __declspec(dllexport) const wchar_t* getName()
{
	return L"Project Manager";
}

extern "C" __declspec(dllexport) FuncItem* getFuncsArray(int* nbF)
{
	ensureFuncItemsInitialized();
	*nbF = FI_COUNT;
	return g_funcItems;
}

extern "C" __declspec(dllexport) void beNotified(SCNotification* notifyCode)
{
	switch (notifyCode->nmhdr.code)
	{
		case NPPN_READY:
		{
			g_configDir = readPluginConfigDir();
			ensurePanelCreated();
			g_projectManager.loadLastSession(g_configDir);
			g_projectPanel.rebuildTree();
			break;
		}

		case NPPN_SHUTDOWN:
		{
			if (!g_configDir.empty())
				g_projectManager.saveLastSession(g_configDir);
			break;
		}

		default:
			break;
	}
}

extern "C" __declspec(dllexport) LRESULT messageProc(UINT, WPARAM, LPARAM)
{
	return TRUE;
}

extern "C" __declspec(dllexport) BOOL isUnicode()
{
	return TRUE;
}

// ---------------------------------------------------------------------
// Menu command callbacks — thin forwards to ProjectPanel, which owns all
// the real logic (it already knows how to figure out the "active" project
// and, for the ones that came from its own context menu, exactly which
// tree node was clicked).
// ---------------------------------------------------------------------

void cmdNewProject() { ensurePanelCreated(); g_projectPanel.display(); g_projectPanel.menuNewProject(); }
void cmdOpenProject() { ensurePanelCreated(); g_projectPanel.display(); g_projectPanel.menuOpenProject(); }
void cmdSaveProject() { g_projectPanel.menuSaveProject(); }
void cmdSaveProjectAs() { g_projectPanel.menuSaveProjectAs(); }
void cmdSaveSession() { g_projectPanel.menuSaveSession(); }
void cmdCloseProject() { g_projectPanel.menuCloseProject(); }
void cmdAddFolderToProject() { g_projectPanel.menuAddFolder(); }
void cmdAddFilesToProject() { g_projectPanel.menuAddFiles(); }
void cmdOpenAllProjectFiles() { g_projectPanel.menuOpenAllFiles(); }

void cmdTogglePanel()
{
	ensurePanelCreated();

	bool nowVisible = !g_projectPanel.isVisible();
	g_projectPanel.display(nowVisible);

	::SendMessage(g_nppData._nppHandle, NPPM_SETMENUITEMCHECK, g_funcItems[FI_TOGGLE_PANEL]._cmdID, nowVisible);
}

void cmdAbout()
{
	::MessageBoxW(g_nppData._nppHandle,
		L"Project Manager for Notepad++\r\n\r\n"
		L"Lightweight project/workspace management: group folders and files "
		L"into named projects, reopen them with one click, and remember which "
		L"files were open so you can pick up right where you left off.",
		L"About Project Manager", MB_OK | MB_ICONINFORMATION);
}
