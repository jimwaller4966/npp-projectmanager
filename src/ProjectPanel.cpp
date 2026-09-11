#include "ProjectPanel.h"
#include "StrUtil.h"

#include <shellapi.h>
#include <shlobj.h>
#include <commdlg.h>
#include <objbase.h>
#include <algorithm>

// ---------------------------------------------------------------------
// Small local helpers: an "type a name" prompt, and folder/file pickers.
// Kept file-local (anonymous namespace) since nothing outside this file
// needs them.
// ---------------------------------------------------------------------
namespace
{
	struct InputBoxCtx
	{
		const wchar_t* title;
		const wchar_t* prompt;
		std::wstring value; // initial text in, result out
	};

	INT_PTR CALLBACK InputBoxProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
	{
		switch (message)
		{
			case WM_INITDIALOG:
			{
				auto* ctx = reinterpret_cast<InputBoxCtx*>(lParam);
				::SetWindowLongPtr(hDlg, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(ctx));
				::SetWindowTextW(hDlg, ctx->title);
				::SetDlgItemTextW(hDlg, IDC_INPUT_PROMPT, ctx->prompt);
				::SetDlgItemTextW(hDlg, IDC_INPUT_EDIT, ctx->value.c_str());
				::SendDlgItemMessage(hDlg, IDC_INPUT_EDIT, EM_SETSEL, 0, -1);
				::SetFocus(::GetDlgItem(hDlg, IDC_INPUT_EDIT));
				return FALSE; // we set focus ourselves
			}
			case WM_COMMAND:
			{
				auto* ctx = reinterpret_cast<InputBoxCtx*>(::GetWindowLongPtr(hDlg, GWLP_USERDATA));
				switch (LOWORD(wParam))
				{
					case IDOK:
					{
						wchar_t buf[512] = { 0 };
						::GetDlgItemTextW(hDlg, IDC_INPUT_EDIT, buf, 512);
						if (ctx)
							ctx->value = buf;
						::EndDialog(hDlg, IDOK);
						return TRUE;
					}
					case IDCANCEL:
						::EndDialog(hDlg, IDCANCEL);
						return TRUE;
					default:
						break;
				}
				break;
			}
			default:
				break;
		}
		return FALSE;
	}

	// Shows the prompt dialog. 'value' carries the initial text in and the
	// typed text out. Returns false if the user cancelled.
	bool showInputBox(HWND owner, HINSTANCE hInst, const wchar_t* title, const wchar_t* prompt, std::wstring& value)
	{
		InputBoxCtx ctx{ title, prompt, value };
		INT_PTR result = ::DialogBoxParamW(hInst, MAKEINTRESOURCEW(IDD_INPUTBOX), owner,
			InputBoxProc, reinterpret_cast<LPARAM>(&ctx));
		if (result != IDOK)
			return false;
		value = StrUtil::trim(ctx.value);
		return !value.empty();
	}

	std::wstring browseForFolder(HWND owner, const wchar_t* title)
	{
		wchar_t path[MAX_PATH] = { 0 };

		HRESULT coResult = ::CoInitialize(nullptr);

		BROWSEINFOW bi = {};
		bi.hwndOwner = owner;
		bi.lpszTitle = title;
		bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;

		std::wstring result;
		LPITEMIDLIST pidl = ::SHBrowseForFolderW(&bi);
		if (pidl)
		{
			if (::SHGetPathFromIDListW(pidl, path))
				result = path;
			::CoTaskMemFree(pidl);
		}

		if (SUCCEEDED(coResult))
			::CoUninitialize();

		return result;
	}

	// Lets the user pick one or more files at once (standard multi-select
	// Open dialog). Returns their full paths.
	std::vector<std::wstring> browseForFiles(HWND owner)
	{
		std::vector<std::wstring> result;

		const DWORD bufSize = 32768;
		std::vector<wchar_t> buffer(bufSize, L'\0');

		OPENFILENAMEW ofn = {};
		ofn.lStructSize = sizeof(ofn);
		ofn.hwndOwner = owner;
		ofn.lpstrFile = buffer.data();
		ofn.nMaxFile = bufSize;
		ofn.lpstrFilter = L"All Files (*.*)\0*.*\0";
		ofn.Flags = OFN_ALLOWMULTISELECT | OFN_EXPLORER | OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;
		ofn.lpstrTitle = L"Add Files to Project";

		if (!::GetOpenFileNameW(&ofn))
			return result;

		const wchar_t* p = buffer.data();
		std::wstring first(p);
		p += first.size() + 1;

		if (*p == L'\0')
		{
			// Single selection: 'first' is already the full path.
			result.push_back(first);
			return result;
		}

		// Multiple selection: 'first' is the containing directory, followed
		// by each chosen file name, double-null terminated.
		const std::wstring& dir = first;
		while (*p != L'\0')
		{
			std::wstring name(p);
			std::wstring full = dir;
			if (!full.empty() && full.back() != L'\\')
				full += L'\\';
			full += name;
			result.push_back(full);
			p += name.size() + 1;
		}
		return result;
	}

	// Local (non-persisted) IDs for the panel's right-click context menu.
	// TrackPopupMenu(TPM_RETURNCMD) hands one of these straight back; they
	// never go through WM_COMMAND, so they only need to be distinct from
	// each other and from 0.
	enum ContextCmd
	{
		CM_NEW_PROJECT = 1,
		CM_OPEN_PROJECT,
		CM_OPEN_FILE,
		CM_OPEN_ALL_FILES,
		CM_SAVE_PROJECT,
		CM_SAVE_PROJECT_AS,
		CM_SAVE_SESSION,
		CM_RENAME_PROJECT,
		CM_CLOSE_PROJECT,
		CM_ADD_FOLDER,
		CM_ADD_FILES,
		CM_REMOVE_FOLDER,
		CM_REMOVE_FILE,
		CM_REFRESH_FOLDER,
		CM_OPEN_IN_EXPLORER,
		CM_OPEN_CONTAINING_FOLDER,
	};
}

// ---------------------------------------------------------------------
// ProjectPanel
// ---------------------------------------------------------------------

INT_PTR CALLBACK ProjectPanel::run_dlgProc(UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message)
	{
		case WM_INITDIALOG:
			createTreeView();
			rebuildTree();
			return TRUE;

		case WM_SIZE:
			resizeTreeView();
			return TRUE;

		case WM_NOTIFY:
		{
			LPNMHDR pnmh = reinterpret_cast<LPNMHDR>(lParam);

			if (pnmh->hwndFrom == _hParent)
				return DockingDlgInterface::run_dlgProc(message, wParam, lParam);

			if (pnmh->hwndFrom == _hTreeView)
			{
				switch (pnmh->code)
				{
					case NM_DBLCLK:
						onDblClick();
						return TRUE;

					case NM_RCLICK:
						onRightClick();
						return TRUE;

					case TVN_ITEMEXPANDING:
						onItemExpanding(reinterpret_cast<NMTREEVIEW*>(lParam));
						::SetWindowLongPtr(_hSelf, DWLP_MSGRESULT, FALSE); // allow the expand/collapse
						return TRUE;

					case TVN_DELETEITEM:
						onDeleteItem(reinterpret_cast<NMTREEVIEW*>(lParam));
						return TRUE;

					default:
						break;
				}
			}
			break;
		}

		default:
			break;
	}

	return DockingDlgInterface::run_dlgProc(message, wParam, lParam);
}

void ProjectPanel::createTreeView()
{
	INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_TREEVIEW_CLASSES };
	::InitCommonControlsEx(&icc);

	RECT rc;
	::GetClientRect(_hSelf, &rc);

	_hTreeView = ::CreateWindowExW(
		WS_EX_CLIENTEDGE, WC_TREEVIEW, L"",
		WS_CHILD | WS_VISIBLE | WS_TABSTOP | TVS_HASLINES | TVS_LINESATROOT | TVS_HASBUTTONS | TVS_SHOWSELALWAYS,
		rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top,
		_hSelf, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_PROJECT_TREE)), getHinst(), nullptr);

	SHFILEINFOW sfi = {};
	HIMAGELIST sysImageList = reinterpret_cast<HIMAGELIST>(
		::SHGetFileInfoW(L"C:\\", 0, &sfi, sizeof(sfi), SHGFI_SYSICONINDEX | SHGFI_SMALLICON));
	if (sysImageList)
	{
		_hImageList = sysImageList;
		TreeView_SetImageList(_hTreeView, _hImageList, TVSIL_NORMAL);
	}
}

void ProjectPanel::resizeTreeView()
{
	if (!_hTreeView)
		return;
	RECT rc;
	::GetClientRect(_hSelf, &rc);
	::MoveWindow(_hTreeView, 0, 0, rc.right - rc.left, rc.bottom - rc.top, TRUE);
}

void ProjectPanel::rebuildTree()
{
	if (!_hTreeView || !_manager)
		return;

	// Remember which projects were expanded so a rebuild after e.g. adding a
	// file doesn't visually collapse everything back to just the roots.
	// (Kept simple: we just re-expand every project root — folders inside
	// re-collapse, which is an acceptable trade-off for how rarely the
	// structural list itself changes.)
	TreeView_DeleteAllItems(_hTreeView);

	for (const auto& project : _manager->projects())
		insertProjectRoot(project);
}

HTREEITEM ProjectPanel::insertProjectRoot(const ProjectPtr& project)
{
	auto* data = new TreeNodeData{ NodeType::ProjectRoot, project, std::wstring(), false };

	std::wstring label = project->name();
	if (project->isDirty())
		label += L" *";

	int icon = genericIconIndex(true);

	TVINSERTSTRUCTW tvis = {};
	tvis.hParent = TVI_ROOT;
	tvis.hInsertAfter = TVI_LAST;
	tvis.item.mask = TVIF_TEXT | TVIF_PARAM | TVIF_IMAGE | TVIF_SELECTEDIMAGE;
	tvis.item.pszText = const_cast<wchar_t*>(label.c_str());
	tvis.item.lParam = reinterpret_cast<LPARAM>(data);
	tvis.item.iImage = icon;
	tvis.item.iSelectedImage = icon;

	HTREEITEM root = TreeView_InsertItem(_hTreeView, &tvis);

	for (const auto& folder : project->folders())
		insertFolderNode(root, project, folder);
	for (const auto& file : project->files())
		insertFileNode(root, project, file);

	TreeView_Expand(_hTreeView, root, TVE_EXPAND);
	return root;
}

HTREEITEM ProjectPanel::insertFolderNode(HTREEITEM parent, const ProjectPtr& project, const std::wstring& folderPath)
{
	auto* data = new TreeNodeData{ NodeType::Folder, project, folderPath, false };

	std::wstring label = StrUtil::fileNameFromPath(folderPath);
	if (label.empty())
		label = folderPath; // e.g. a bare drive root such as "D:\"

	int icon = shellIconIndex(folderPath, true);

	TVINSERTSTRUCTW tvis = {};
	tvis.hParent = parent;
	tvis.hInsertAfter = TVI_LAST;
	tvis.item.mask = TVIF_TEXT | TVIF_PARAM | TVIF_IMAGE | TVIF_SELECTEDIMAGE | TVIF_CHILDREN;
	tvis.item.pszText = const_cast<wchar_t*>(label.c_str());
	tvis.item.lParam = reinterpret_cast<LPARAM>(data);
	tvis.item.iImage = icon;
	tvis.item.iSelectedImage = icon;
	tvis.item.cChildren = 1; // draw the expand box; real children are lazy-loaded

	HTREEITEM item = TreeView_InsertItem(_hTreeView, &tvis);
	addDummyChild(item);
	return item;
}

HTREEITEM ProjectPanel::insertFileNode(HTREEITEM parent, const ProjectPtr& project, const std::wstring& filePath)
{
	auto* data = new TreeNodeData{ NodeType::File, project, filePath, false };

	std::wstring label = StrUtil::fileNameFromPath(filePath);
	int icon = shellIconIndex(filePath, false);

	TVINSERTSTRUCTW tvis = {};
	tvis.hParent = parent;
	tvis.hInsertAfter = TVI_LAST;
	tvis.item.mask = TVIF_TEXT | TVIF_PARAM | TVIF_IMAGE | TVIF_SELECTEDIMAGE;
	tvis.item.pszText = const_cast<wchar_t*>(label.c_str());
	tvis.item.lParam = reinterpret_cast<LPARAM>(data);
	tvis.item.iImage = icon;
	tvis.item.iSelectedImage = icon;

	return TreeView_InsertItem(_hTreeView, &tvis);
}

void ProjectPanel::addDummyChild(HTREEITEM parent)
{
	TVINSERTSTRUCTW tvis = {};
	tvis.hParent = parent;
	tvis.hInsertAfter = TVI_LAST;
	tvis.item.mask = TVIF_TEXT | TVIF_PARAM;
	tvis.item.pszText = const_cast<wchar_t*>(L"Loading...");
	tvis.item.lParam = 0; // sentinel: this placeholder never gets a TreeNodeData
	TreeView_InsertItem(_hTreeView, &tvis);
}

void ProjectPanel::populateFolderChildren(HTREEITEM folderItem, TreeNodeData* data)
{
	// Drop whatever children are there now — the lazy-load dummy the first
	// time, or stale entries on a manual Refresh.
	HTREEITEM child = TreeView_GetChild(_hTreeView, folderItem);
	while (child)
	{
		HTREEITEM next = TreeView_GetNextSibling(_hTreeView, child);
		TreeView_DeleteItem(_hTreeView, child); // fires TVN_DELETEITEM, frees its TreeNodeData if any
		child = next;
	}

	std::vector<std::wstring> subDirs, subFiles;

	std::wstring searchPath = data->path;
	if (!searchPath.empty() && searchPath.back() != L'\\')
		searchPath += L'\\';
	searchPath += L"*";

	WIN32_FIND_DATAW fd = {};
	HANDLE hFind = ::FindFirstFileW(searchPath.c_str(), &fd);
	if (hFind != INVALID_HANDLE_VALUE)
	{
		do
		{
			std::wstring name = fd.cFileName;
			if (name == L"." || name == L"..")
				continue;
			if (fd.dwFileAttributes & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM))
				continue;

			std::wstring full = data->path;
			if (!full.empty() && full.back() != L'\\')
				full += L'\\';
			full += name;

			if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
				subDirs.push_back(full);
			else
				subFiles.push_back(full);
		} while (::FindNextFileW(hFind, &fd));
		::FindClose(hFind);
	}

	auto icase = [](const std::wstring& a, const std::wstring& b) { return ::_wcsicmp(a.c_str(), b.c_str()) < 0; };
	std::sort(subDirs.begin(), subDirs.end(), icase);
	std::sort(subFiles.begin(), subFiles.end(), icase);

	for (const auto& d : subDirs)
		insertFolderNode(folderItem, data->project, d);
	for (const auto& f : subFiles)
		insertFileNode(folderItem, data->project, f);

	data->folderScanned = true;
}

TreeNodeData* ProjectPanel::nodeDataAt(HTREEITEM item) const
{
	if (!item)
		return nullptr;

	TVITEMW tvi = {};
	tvi.mask = TVIF_PARAM;
	tvi.hItem = item;
	if (!TreeView_GetItem(_hTreeView, &tvi))
		return nullptr;

	return reinterpret_cast<TreeNodeData*>(tvi.lParam);
}

TreeNodeData* ProjectPanel::selectedNodeData() const
{
	return nodeDataAt(TreeView_GetSelection(_hTreeView));
}

ProjectPtr ProjectPanel::selectedProject() const
{
	if (auto* data = selectedNodeData())
		return data->project;
	if (_manager && _manager->projects().size() == 1)
		return _manager->projects().front();
	return nullptr;
}

int ProjectPanel::shellIconIndex(const std::wstring& path, bool isDir) const
{
	SHFILEINFOW sfi = {};
	DWORD attrs = ::GetFileAttributesW(path.c_str());
	bool exists = (attrs != INVALID_FILE_ATTRIBUTES);

	UINT flags = SHGFI_SYSICONINDEX | SHGFI_SMALLICON;
	DWORD fakeAttrs = isDir ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
	if (!exists)
		flags |= SHGFI_USEFILEATTRIBUTES;

	::SHGetFileInfoW(path.c_str(), exists ? 0 : fakeAttrs, &sfi, sizeof(sfi), flags);
	return sfi.iIcon;
}

int ProjectPanel::genericIconIndex(bool isDir) const
{
	SHFILEINFOW sfi = {};
	DWORD attrs = isDir ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
	// SHGFI_USEFILEATTRIBUTES means the path string itself doesn't need to
	// exist - the shell just needs a plausible extension/attribute to look
	// up a generic icon by, which is exactly what we want for a project
	// root (it isn't a real filesystem entry).
	::SHGetFileInfoW(isDir ? L"folder" : L"file.txt", attrs, &sfi, sizeof(sfi),
		SHGFI_SYSICONINDEX | SHGFI_SMALLICON | SHGFI_USEFILEATTRIBUTES);
	return sfi.iIcon;
}

// --- WM_NOTIFY handlers -------------------------------------------------

void ProjectPanel::onDblClick()
{
	TreeNodeData* data = selectedNodeData();
	if (!data || !_manager)
		return;

	if (data->type == NodeType::File)
		_manager->openFile(data->path);
}

void ProjectPanel::onItemExpanding(NMTREEVIEW* pnmtv)
{
	if (!(pnmtv->action & TVE_EXPAND))
		return;

	TreeNodeData* data = reinterpret_cast<TreeNodeData*>(pnmtv->itemNew.lParam);
	if (!data || data->type != NodeType::Folder || data->folderScanned)
		return;

	populateFolderChildren(pnmtv->itemNew.hItem, data);
}

void ProjectPanel::onDeleteItem(NMTREEVIEW* pnmtv)
{
	delete reinterpret_cast<TreeNodeData*>(pnmtv->itemOld.lParam);
}

void ProjectPanel::onRightClick()
{
	POINT screenPt;
	::GetCursorPos(&screenPt);
	POINT clientPt = screenPt;
	::ScreenToClient(_hTreeView, &clientPt);

	TVHITTESTINFO hit = {};
	hit.pt = clientPt;
	HTREEITEM hitItem = TreeView_HitTest(_hTreeView, &hit);

	if (hitItem && (hit.flags & (TVHT_ONITEM | TVHT_ONITEMLABEL | TVHT_ONITEMICON | TVHT_ONITEMRIGHT)))
		TreeView_SelectItem(_hTreeView, hitItem);
	else
		hitItem = nullptr;

	TreeNodeData* data = hitItem ? nodeDataAt(hitItem) : nullptr;

	bool isTopLevelEntry = false;
	if (hitItem && data && (data->type == NodeType::Folder || data->type == NodeType::File))
	{
		HTREEITEM parent = TreeView_GetParent(_hTreeView, hitItem);
		TreeNodeData* parentData = nodeDataAt(parent);
		isTopLevelEntry = (parentData && parentData->type == NodeType::ProjectRoot);
	}

	HMENU hMenu = ::CreatePopupMenu();

	if (!data)
	{
		::AppendMenuW(hMenu, MF_STRING, CM_NEW_PROJECT, L"New Project...");
		::AppendMenuW(hMenu, MF_STRING, CM_OPEN_PROJECT, L"Open Project...");
	}
	else if (data->type == NodeType::ProjectRoot)
	{
		::AppendMenuW(hMenu, MF_STRING, CM_OPEN_ALL_FILES, L"Open All Project Files");
		::AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
		::AppendMenuW(hMenu, MF_STRING, CM_ADD_FOLDER, L"Add Folder to Project...");
		::AppendMenuW(hMenu, MF_STRING, CM_ADD_FILES, L"Add Files to Project...");
		::AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
		::AppendMenuW(hMenu, MF_STRING, CM_SAVE_PROJECT, L"Save Project");
		::AppendMenuW(hMenu, MF_STRING, CM_SAVE_PROJECT_AS, L"Save Project As...");
		::AppendMenuW(hMenu, MF_STRING, CM_SAVE_SESSION, L"Remember Open Files (Save Session)");
		::AppendMenuW(hMenu, MF_STRING, CM_RENAME_PROJECT, L"Rename Project...");
		::AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
		::AppendMenuW(hMenu, MF_STRING, CM_CLOSE_PROJECT, L"Close Project");
	}
	else if (data->type == NodeType::Folder)
	{
		::AppendMenuW(hMenu, MF_STRING, CM_OPEN_IN_EXPLORER, L"Open in Explorer");
		::AppendMenuW(hMenu, MF_STRING, CM_REFRESH_FOLDER, L"Refresh");
		if (isTopLevelEntry)
		{
			::AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
			::AppendMenuW(hMenu, MF_STRING, CM_REMOVE_FOLDER, L"Remove Folder from Project");
		}
	}
	else // File
	{
		::AppendMenuW(hMenu, MF_STRING, CM_OPEN_FILE, L"Open");
		::AppendMenuW(hMenu, MF_STRING, CM_OPEN_CONTAINING_FOLDER, L"Open Containing Folder");
		if (isTopLevelEntry)
		{
			::AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
			::AppendMenuW(hMenu, MF_STRING, CM_REMOVE_FILE, L"Remove File from Project");
		}
	}

	::SetForegroundWindow(_hSelf);
	int cmd = ::TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_RIGHTBUTTON, screenPt.x, screenPt.y, 0, _hSelf, nullptr);
	::PostMessage(_hSelf, WM_NULL, 0, 0);
	::DestroyMenu(hMenu);

	if (cmd == 0)
		return;

	ProjectPtr proj = data ? data->project : nullptr;

	switch (cmd)
	{
		case CM_NEW_PROJECT:            cmdNewProject(); break;
		case CM_OPEN_PROJECT:           cmdOpenProject(); break;
		case CM_OPEN_FILE:              if (_manager && data) _manager->openFile(data->path); break;
		case CM_OPEN_ALL_FILES:         cmdOpenAllFiles(proj); break;
		case CM_SAVE_PROJECT:           cmdSaveProject(proj); break;
		case CM_SAVE_PROJECT_AS:        cmdSaveProjectAs(proj); break;
		case CM_SAVE_SESSION:           cmdSaveSession(proj); break;
		case CM_RENAME_PROJECT:         cmdRenameProject(proj); break;
		case CM_CLOSE_PROJECT:          cmdCloseProject(proj); break;
		case CM_ADD_FOLDER:             cmdAddFolder(proj); break;
		case CM_ADD_FILES:              cmdAddFiles(proj); break;
		case CM_REMOVE_FOLDER:          if (data) cmdRemoveFolder(proj, data->path); break;
		case CM_REMOVE_FILE:            if (data) cmdRemoveFile(proj, data->path); break;
		case CM_REFRESH_FOLDER:         if (data) cmdRefreshFolder(hitItem, data); break;
		case CM_OPEN_IN_EXPLORER:       if (data) cmdOpenInExplorer(data->path); break;
		case CM_OPEN_CONTAINING_FOLDER: if (data) cmdShowFileInExplorer(data->path); break;
		default: break;
	}
}

// --- context-menu command handlers --------------------------------------

void ProjectPanel::cmdNewProject()
{
	std::wstring name;
	if (!showInputBox(_hSelf, getHinst(), L"New Project", L"Project name:", name))
		return;

	auto project = _manager->newProject(name);
	rebuildTree();
	cmdSaveProjectAs(project);
}

void ProjectPanel::cmdOpenProject()
{
	wchar_t buffer[MAX_PATH] = { 0 };

	OPENFILENAMEW ofn = {};
	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = _hSelf;
	ofn.lpstrFile = buffer;
	ofn.nMaxFile = MAX_PATH;
	ofn.lpstrFilter = L"Notepad++ Project (*.nppproj)\0*.nppproj\0All Files (*.*)\0*.*\0";
	ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;
	ofn.lpstrTitle = L"Open Project";

	if (!::GetOpenFileNameW(&ofn))
		return;

	auto project = _manager->openProjectFile(buffer);
	if (!project)
	{
		::MessageBoxW(_hSelf, L"Could not read that project file.", L"Open Project", MB_OK | MB_ICONERROR);
		return;
	}
	rebuildTree();
}

void ProjectPanel::cmdSaveProject(const ProjectPtr& project)
{
	if (!project)
		return;

	if (project->filePath().empty())
	{
		cmdSaveProjectAs(project);
		return;
	}

	if (!project->save())
		::MessageBoxW(_hSelf, L"Could not save the project file.", L"Save Project", MB_OK | MB_ICONERROR);

	rebuildTree();
}

void ProjectPanel::cmdSaveProjectAs(const ProjectPtr& project)
{
	if (!project)
		return;

	wchar_t buffer[MAX_PATH] = { 0 };
	std::wstring suggested = project->name() + L".nppproj";
	size_t copyLen = suggested.size() < MAX_PATH - 1 ? suggested.size() : MAX_PATH - 1;
	std::copy(suggested.begin(), suggested.begin() + copyLen, buffer);
	buffer[copyLen] = L'\0';

	OPENFILENAMEW ofn = {};
	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = _hSelf;
	ofn.lpstrFile = buffer;
	ofn.nMaxFile = MAX_PATH;
	ofn.lpstrFilter = L"Notepad++ Project (*.nppproj)\0*.nppproj\0All Files (*.*)\0*.*\0";
	ofn.lpstrDefExt = L"nppproj";
	ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
	ofn.lpstrTitle = L"Save Project As";

	if (!::GetSaveFileNameW(&ofn))
		return;

	if (!project->saveAs(buffer))
		::MessageBoxW(_hSelf, L"Could not save the project file.", L"Save Project", MB_OK | MB_ICONERROR);

	rebuildTree();
}

void ProjectPanel::cmdSaveSession(const ProjectPtr& project)
{
	if (!project || !_manager)
		return;
	_manager->captureOpenFilesAsSession(project);
	cmdSaveProject(project);
}

void ProjectPanel::cmdRenameProject(const ProjectPtr& project)
{
	if (!project)
		return;

	std::wstring name = project->name();
	if (!showInputBox(_hSelf, getHinst(), L"Rename Project", L"Project name:", name))
		return;

	project->setName(name);
	rebuildTree();
}

void ProjectPanel::cmdCloseProject(const ProjectPtr& project)
{
	if (!project || !_manager)
		return;

	if (project->isDirty())
	{
		int answer = ::MessageBoxW(_hSelf,
			L"This project has unsaved changes. Save before closing?",
			L"Close Project", MB_YESNOCANCEL | MB_ICONQUESTION);
		if (answer == IDCANCEL)
			return;
		if (answer == IDYES)
			cmdSaveProject(project);
	}

	_manager->closeProject(project);
	rebuildTree();
}

void ProjectPanel::cmdOpenAllFiles(const ProjectPtr& project)
{
	if (!project || !_manager)
		return;
	_manager->openAllFiles(project);
}

void ProjectPanel::cmdAddFolder(const ProjectPtr& project)
{
	if (!project)
		return;

	std::wstring folder = browseForFolder(_hSelf, L"Add Folder to Project");
	if (folder.empty())
		return;

	project->addFolder(folder);
	rebuildTree();
}

void ProjectPanel::cmdAddFiles(const ProjectPtr& project)
{
	if (!project)
		return;

	auto files = browseForFiles(_hSelf);
	if (files.empty())
		return;

	for (const auto& f : files)
		project->addFile(f);
	rebuildTree();
}

void ProjectPanel::cmdRemoveFolder(const ProjectPtr& project, const std::wstring& folderPath)
{
	if (!project)
		return;
	project->removeFolder(folderPath);
	rebuildTree();
}

void ProjectPanel::cmdRemoveFile(const ProjectPtr& project, const std::wstring& filePath)
{
	if (!project)
		return;
	project->removeFile(filePath);
	rebuildTree();
}

void ProjectPanel::cmdRefreshFolder(HTREEITEM item, TreeNodeData* data)
{
	if (!data)
		return;
	data->folderScanned = false;
	populateFolderChildren(item, data);
	TreeView_Expand(_hTreeView, item, TVE_EXPAND);
}

void ProjectPanel::cmdOpenInExplorer(const std::wstring& folderPath)
{
	::ShellExecuteW(_hSelf, L"explore", folderPath.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void ProjectPanel::cmdShowFileInExplorer(const std::wstring& filePath)
{
	std::wstring param = L"/select,\"" + filePath + L"\"";
	::ShellExecuteW(_hSelf, L"open", L"explorer.exe", param.c_str(), nullptr, SW_SHOWNORMAL);
}

void ProjectPanel::addFileToProject(const ProjectPtr& project, const std::wstring& filePath)
{
	if (!project || filePath.empty())
		return;
	project->addFile(filePath);
	rebuildTree();
}

void ProjectPanel::addFileToNewProject(const std::wstring& filePath)
{
	if (filePath.empty())
		return;

	std::wstring name;
	if (!showInputBox(_hSelf, getHinst(), L"New Project", L"Project name:", name))
		return;

	auto project = _manager->newProject(name);
	project->addFile(filePath);
	rebuildTree();
	cmdSaveProjectAs(project);
}

void ProjectPanel::menuAddActiveTabToProject(const std::wstring& filePath)
{
	if (filePath.empty() || !_manager)
		return;

	// No projects open at all yet: go straight to "create one", rather than
	// telling the user to go create one first.
	if (_manager->projects().empty())
	{
		addFileToNewProject(filePath);
		return;
	}

	ProjectPtr project = activeProject();
	if (!project)
	{
		warnNoActiveProject();
		return;
	}

	addFileToProject(project, filePath);
}

void ProjectPanel::withActiveProject(void (ProjectPanel::*fn)(const ProjectPtr&))
{
	ProjectPtr project = activeProject();
	if (!project)
	{
		warnNoActiveProject();
		return;
	}
	(this->*fn)(project);
}

void ProjectPanel::warnNoActiveProject() const
{
	::MessageBoxW(_hSelf,
		L"Select a project in the panel first (or open just one project).",
		L"Project Manager", MB_OK | MB_ICONINFORMATION);
}
