#include "TabContextMenu.h"
#include "ProjectManager.h"
#include "ProjectPanel.h"
#include "StrUtil.h"

#include <windowsx.h>  // GET_X_LPARAM / GET_Y_LPARAM
#include <commctrl.h>  // SetWindowSubclass / TabCtrl_HitTest / TabCtrl_GetItem
#include <vector>

namespace
{
	// Command IDs for the items this module injects into Notepad++'s tab
	// context menu. Chosen far away from any ID Notepad++ itself hands out
	// (its own menu commands, and the cmdID it dynamically assigns to this
	// plugin's own FuncItems, both live in much lower ranges) so a collision
	// is effectively impossible; we only ever act on WM_COMMAND ids in this
	// exact range and let everything else fall through untouched.
	constexpr UINT_PTR ID_ADD_TO_PROJECT_BASE = 0xE000;
	constexpr size_t   ID_ADD_TO_PROJECT_SLOTS = 64;
	constexpr UINT_PTR ID_ADD_TO_PROJECT_END = ID_ADD_TO_PROJECT_BASE + ID_ADD_TO_PROJECT_SLOTS;
	constexpr UINT_PTR ID_ADD_TO_NEW_PROJECT = ID_ADD_TO_PROJECT_END;

	constexpr UINT_PTR SUBCLASS_ID = 0xE500;

	// refData for a subclassed window: which of the two jobs it does.
	// A single proc handles both, branching on this, because we genuinely
	// don't know in advance which HWND Notepad++ passes as the owner to
	// TrackPopupMenu() for its tab context menu (see the long comment in
	// TabContextMenu.h) - so several candidate windows are all subclassed
	// defensively, and only the one that turns out to be the real owner
	// ever does anything.
	enum class Mode : DWORD_PTR
	{
		// A real document tab bar: tracks right-clicks on it (so we know
		// which tab/file is involved) in addition to relaying.
		TabBar = 1,
		// Some other window that might be the TrackPopupMenu owner (the tab
		// bar's parent, or Notepad++'s main window): relays only, never
		// hit-tests anything itself.
		RelayOnly = 0,
	};

	NppData g_nppData{};
	ProjectManager* g_manager = nullptr;
	ProjectPanel* g_panel = nullptr;
	bool g_installed = false;

	// State for the one tab-context-menu invocation that can ever be
	// in-flight at a time: set when a right-click on a real tab is about to
	// show a menu, read back when WM_INITMENUPOPUP and then WM_COMMAND
	// follow. Plain globals are fine here - all of this happens on the UI
	// thread, and only one native popup menu can be open at once.
	bool g_pending = false;
	std::wstring g_pendingFilePath;
	std::vector<ProjectPtr> g_menuProjects; // parallel to ID_ADD_TO_PROJECT_BASE + i

	std::wstring filePathForBufferId(UINT_PTR bufferId)
	{
		if (bufferId == 0)
			return std::wstring();
		wchar_t path[MAX_PATH * 4] = { 0 };
		::SendMessage(g_nppData._nppHandle, NPPM_GETFULLPATHFROMBUFFERID,
			static_cast<WPARAM>(bufferId), reinterpret_cast<LPARAM>(path));
		return path;
	}

	// Figures out which open file a hit-tested tab index on 'tabHwnd' really
	// is, without needing to already know whether that control is the main
	// view's tab bar or the second view's (there's no documented way to ask
	// Notepad++ that directly). Two independent strategies are combined so
	// neither one has to be relied on alone:
	//
	//  1. If Notepad++ stores each tab's own BufferID as that tab item's
	//     lParam (the ordinary way to implement a control like this), it can
	//     be read straight off the exact control the user clicked - no
	//     guessing which "view" is involved at all.
	//  2. Otherwise, ask Notepad++ for the Nth open file in each view in
	//     turn (NPPM_GETBUFFERIDFROMPOS) and, if both views have a file at
	//     that index (only possible with an active split view), use the
	//     tab's own displayed text to tell which candidate is the real one.
	std::wstring resolveClickedFilePath(HWND tabHwnd, int index)
	{
		TCITEMW paramItem{};
		paramItem.mask = TCIF_PARAM;
		if (TabCtrl_GetItem(tabHwnd, index, &paramItem) && paramItem.lParam != 0)
		{
			std::wstring path = filePathForBufferId(static_cast<UINT_PTR>(paramItem.lParam));
			if (!path.empty())
				return path;
		}

		std::wstring candidates[2];
		const int views[2] = { PRIMARY_VIEW, SECOND_VIEW };
		for (int v = 0; v < 2; ++v)
		{
			int nbFiles = static_cast<int>(::SendMessage(g_nppData._nppHandle, NPPM_GETNBOPENFILES, 0, views[v]));
			if (index >= nbFiles)
				continue;
			UINT_PTR bufferId = static_cast<UINT_PTR>(
				::SendMessage(g_nppData._nppHandle, NPPM_GETBUFFERIDFROMPOS, index, views[v]));
			candidates[v] = filePathForBufferId(bufferId);
		}

		if (!candidates[0].empty() && candidates[1].empty())
			return candidates[0];
		if (candidates[0].empty() && !candidates[1].empty())
			return candidates[1];
		if (candidates[0].empty() && candidates[1].empty())
			return std::wstring();

		// Both views have a file at this index: only possible with an
		// active split view. Disambiguate using the tab's own text.
		wchar_t textBuf[512] = { 0 };
		TCITEMW textItem{};
		textItem.mask = TCIF_TEXT;
		textItem.pszText = textBuf;
		textItem.cchTextMax = 512;
		if (TabCtrl_GetItem(tabHwnd, index, &textItem))
		{
			std::wstring clickedText = textBuf;
			for (const auto& c : candidates)
			{
				std::wstring name = StrUtil::fileNameFromPath(c);
				if (!name.empty() && clickedText.find(name) != std::wstring::npos)
					return c;
			}
		}

		return candidates[0]; // genuinely ambiguous: guess the main view
	}

	// Builds the "Add to Project" submenu appended to the native tab menu:
	// one entry per currently open project (there is almost never more than
	// a handful), then "New Project...".
	HMENU buildAddToProjectSubmenu()
	{
		g_menuProjects = g_manager->projects();
		if (g_menuProjects.size() > ID_ADD_TO_PROJECT_SLOTS)
			g_menuProjects.resize(ID_ADD_TO_PROJECT_SLOTS);

		HMENU sub = ::CreatePopupMenu();
		for (size_t i = 0; i < g_menuProjects.size(); ++i)
			::AppendMenuW(sub, MF_STRING, ID_ADD_TO_PROJECT_BASE + i, g_menuProjects[i]->name().c_str());

		if (!g_menuProjects.empty())
			::AppendMenuW(sub, MF_SEPARATOR, 0, nullptr);
		::AppendMenuW(sub, MF_STRING, ID_ADD_TO_NEW_PROJECT, L"New Project...");

		return sub;
	}

	void handleCommand(UINT_PTR id)
	{
		if (g_pendingFilePath.empty() || !g_panel)
			return;

		if (id == ID_ADD_TO_NEW_PROJECT)
		{
			g_panel->addFileToNewProject(g_pendingFilePath);
			return;
		}

		size_t index = id - ID_ADD_TO_PROJECT_BASE;
		if (index < g_menuProjects.size())
			g_panel->addFileToProject(g_menuProjects[index], g_pendingFilePath);
	}

	// Called right when a right-click on a tab bar is about to trigger
	// Notepad++'s own context menu. Hit-tests to find which tab (if any)
	// was actually clicked and, if so, remembers that tab's file path for
	// the WM_INITMENUPOPUP / WM_COMMAND that follow.
	void prepareForPossibleMenu(HWND tabHwnd, POINT screenPt)
	{
		g_pending = false;
		g_pendingFilePath.clear();

		POINT clientPt = screenPt;
		::ScreenToClient(tabHwnd, &clientPt);

		TCHITTESTINFO hit{};
		hit.pt = clientPt;
		int index = TabCtrl_HitTest(tabHwnd, &hit);
		if (index < 0)
			return;

		std::wstring path = resolveClickedFilePath(tabHwnd, index);
		if (path.empty())
			return;

		g_pendingFilePath = path;
		g_pending = true;
	}

	// Shared by every subclassed window (tab bars and relay-only
	// candidates alike). Which branches actually do anything depends on
	// 'refData' (see Mode).
	LRESULT CALLBACK sharedSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
		UINT_PTR /*subclassId*/, DWORD_PTR refData)
	{
		Mode mode = static_cast<Mode>(refData);

		switch (msg)
		{
			case WM_CONTEXTMENU:
			{
				if (mode == Mode::TabBar)
				{
					POINT pt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
					if (pt.x == -1 && pt.y == -1)
						::GetCursorPos(&pt); // keyboard-invoked (Shift+F10 / VK_APPS)
					prepareForPossibleMenu(hwnd, pt);
				}
				break;
			}

			// Some tab bar implementations show their context menu straight
			// from the button-up handler rather than relying on Windows to
			// synthesize WM_CONTEXTMENU afterwards. Handling both is
			// harmless: whichever path Notepad++ actually takes, the
			// pending state ends up the same.
			case WM_RBUTTONDOWN:
			case WM_RBUTTONUP:
			{
				if (mode == Mode::TabBar)
				{
					POINT pt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
					::ClientToScreen(hwnd, &pt);
					prepareForPossibleMenu(hwnd, pt);
				}
				break;
			}

			case WM_INITMENUPOPUP:
			{
				// HIWORD(lParam) != 0 means this is a window's system menu,
				// never our target.
				if (g_pending && HIWORD(lParam) == 0)
				{
					HMENU hMenu = reinterpret_cast<HMENU>(wParam);
					if (hMenu && ::GetMenuItemCount(hMenu) > 0)
					{
						// Only the first window to see this particular popup
						// should inject into it. Once injected, drop
						// g_pending's "armed" state for the popup part (but
						// keep the file path around) by checking whether our
						// own item is already there.
						bool alreadyInjected = false;
						int count = ::GetMenuItemCount(hMenu);
						wchar_t label[64] = { 0 };
						if (::GetMenuStringW(hMenu, count - 1, label, 64, MF_BYPOSITION) > 0)
							alreadyInjected = (wcscmp(label, L"Add to Project") == 0);

						if (!alreadyInjected)
						{
							::AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
							HMENU sub = buildAddToProjectSubmenu();
							::AppendMenuW(hMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(sub), L"Add to Project");
						}
					}
				}
				break;
			}

			case WM_COMMAND:
			{
				UINT_PTR id = LOWORD(wParam);
				if (g_pending && (id == ID_ADD_TO_NEW_PROJECT ||
					(id >= ID_ADD_TO_PROJECT_BASE && id < ID_ADD_TO_PROJECT_END)))
				{
					handleCommand(id);
					g_pending = false;
					return 0;
				}
				break;
			}

			case WM_EXITMENULOOP:
				g_pending = false;
				break;

			case WM_NCDESTROY:
				::RemoveWindowSubclass(hwnd, sharedSubclassProc, SUBCLASS_ID);
				break;

			default:
				break;
		}

		return ::DefSubclassProc(hwnd, msg, wParam, lParam);
	}

	std::vector<HWND> g_subclassed;

	void subclassOnce(HWND hwnd, Mode mode)
	{
		if (!hwnd)
			return;
		for (HWND existing : g_subclassed)
			if (existing == hwnd)
				return; // already subclassed (e.g. two tab bars sharing one parent)

		if (::SetWindowSubclass(hwnd, sharedSubclassProc, SUBCLASS_ID, static_cast<DWORD_PTR>(mode)))
			g_subclassed.push_back(hwnd);
	}

	BOOL CALLBACK enumTabBarsProc(HWND hwnd, LPARAM /*unused*/)
	{
		wchar_t className[64] = { 0 };
		::GetClassNameW(hwnd, className, 64);
		if (::lstrcmpiW(className, L"SysTabControl32") != 0)
			return TRUE;

		subclassOnce(hwnd, Mode::TabBar);

		// We don't know for certain which window Notepad++ passes as the
		// owner to TrackPopupMenu() for this control's context menu (there's
		// no documented way to ask) - it might be this tab bar itself, or it
		// might be its parent container - so cover the parent defensively
		// too, relay-only (it must never hit-test clicks meant for other
		// children of that parent).
		HWND parent = ::GetParent(hwnd);
		if (parent)
			subclassOnce(parent, Mode::RelayOnly);

		return TRUE; // keep enumerating - there are normally two tab bars
	}
}

namespace TabContextMenu
{
	void install(const NppData& nppData, ProjectManager* manager, ProjectPanel* panel)
	{
		if (g_installed)
			return;

		g_nppData = nppData;
		g_manager = manager;
		g_panel = panel;

		::EnumChildWindows(g_nppData._nppHandle, enumTabBarsProc, 0);

		// And Notepad++'s own main window, in case that's the actual
		// TrackPopupMenu owner rather than the tab bar or its parent.
		subclassOnce(g_nppData._nppHandle, Mode::RelayOnly);

		g_installed = true;
	}
}
