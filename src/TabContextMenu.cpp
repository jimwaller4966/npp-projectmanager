#include "TabContextMenu.h"
#include "ProjectManager.h"
#include "ProjectPanel.h"

#include <windowsx.h>  // GET_X_LPARAM / GET_Y_LPARAM
#include <commctrl.h>  // SetWindowSubclass / TabCtrl_HitTest
#include <algorithm>
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

	constexpr UINT_PTR TABBAR_SUBCLASS_ID = 0xE500;

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

	long horizontalOverlap(const RECT& a, const RECT& b)
	{
		long left = (std::max)(a.left, b.left);
		long right = (std::min)(a.right, b.right);
		return right > left ? (right - left) : 0;
	}

	// Decides whether 'hwnd' (already known to be a SysTabControl32) is one
	// of Notepad++'s two *document* tab bars, and if so which view it
	// belongs to - PRIMARY_VIEW or SECOND_VIEW (see Notepad_plus_msgs.h).
	// Judged by screen geometry against the two Scintilla edit controls
	// Notepad++ hands every plugin in NppData: a real document tab bar sits
	// directly beside its view's editor and overlaps it almost fully along
	// the horizontal axis, while anything else that happens to also be a
	// SysTabControl32 (e.g. a docking panel's own tab strip, if more than
	// one plugin panel shares a dock) won't come close to that much overlap.
	// Returns -1 for "don't touch this one".
	int classifyTabBar(HWND hwnd)
	{
		RECT tabRect{};
		if (!::GetWindowRect(hwnd, &tabRect))
			return -1;
		long tabWidth = tabRect.right - tabRect.left;
		if (tabWidth <= 0)
			return -1;

		RECT mainRect{}, subRect{};
		bool haveMain = g_nppData._scintillaMainHandle && ::GetWindowRect(g_nppData._scintillaMainHandle, &mainRect);
		bool haveSub = g_nppData._scintillaSecondHandle && ::GetWindowRect(g_nppData._scintillaSecondHandle, &subRect);

		long overlapMain = haveMain ? horizontalOverlap(tabRect, mainRect) : 0;
		long overlapSub = haveSub ? horizontalOverlap(tabRect, subRect) : 0;
		long best = (std::max)(overlapMain, overlapSub);

		// Require the tab bar to overlap its view by at least 3/4 of its own
		// width - comfortably true for a real document tab bar, comfortably
		// false for anything narrow tucked into a side dock.
		if (best < (tabWidth * 3) / 4)
			return -1;

		return (overlapMain >= overlapSub) ? PRIMARY_VIEW : SECOND_VIEW;
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

	// Called right when a right-click on a tab bar we've classified as real
	// is about to trigger Notepad++'s own context menu. Hit-tests to find
	// which tab (if any) was actually clicked and, if so, remembers that
	// tab's file path for the WM_INITMENUPOPUP / WM_COMMAND that follow.
	void prepareForPossibleMenu(HWND tabHwnd, int view, POINT screenPt)
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

		UINT_PTR bufferId = static_cast<UINT_PTR>(
			::SendMessage(g_nppData._nppHandle, NPPM_GETBUFFERIDFROMPOS, index, view));
		std::wstring path = filePathForBufferId(bufferId);
		if (path.empty())
			return;

		g_pendingFilePath = path;
		g_pending = true;
	}

	LRESULT CALLBACK tabBarSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
		UINT_PTR /*subclassId*/, DWORD_PTR refData)
	{
		int view = static_cast<int>(refData);

		switch (msg)
		{
			case WM_CONTEXTMENU:
			{
				POINT pt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
				if (pt.x == -1 && pt.y == -1)
					::GetCursorPos(&pt); // keyboard-invoked (Shift+F10 / VK_APPS)
				prepareForPossibleMenu(hwnd, view, pt);
				break;
			}

			// Some tab bar implementations show their context menu straight
			// from the button-up handler rather than relying on Windows to
			// synthesize WM_CONTEXTMENU afterwards. Handling both is
			// harmless: whichever path Notepad++ actually takes, the pending
			// state ends up the same.
			case WM_RBUTTONDOWN:
			case WM_RBUTTONUP:
			{
				POINT pt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
				::ClientToScreen(hwnd, &pt);
				prepareForPossibleMenu(hwnd, view, pt);
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
						::AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
						HMENU sub = buildAddToProjectSubmenu();
						::AppendMenuW(hMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(sub), L"Add to Project");
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
				::RemoveWindowSubclass(hwnd, tabBarSubclassProc, TABBAR_SUBCLASS_ID);
				break;

			default:
				break;
		}

		return ::DefSubclassProc(hwnd, msg, wParam, lParam);
	}

	BOOL CALLBACK enumTabBarsProc(HWND hwnd, LPARAM /*unused*/)
	{
		wchar_t className[64] = { 0 };
		::GetClassNameW(hwnd, className, 64);
		if (::lstrcmpiW(className, L"SysTabControl32") != 0)
			return TRUE;

		int view = classifyTabBar(hwnd);
		if (view >= 0)
			::SetWindowSubclass(hwnd, tabBarSubclassProc, TABBAR_SUBCLASS_ID, static_cast<DWORD_PTR>(view));

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

		g_installed = true;
	}
}
