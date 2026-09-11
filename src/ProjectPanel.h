// ProjectPanel.h — the dockable tree-view panel that shows every open
// project, its folders (scanned from disk, lazily, as they're expanded) and
// its explicitly-added loose files. Double-click (or Enter) opens a file;
// right-click brings up a context menu appropriate to whatever was clicked.
#pragma once

#include "DockingDlgInterface.h"
#include "ProjectManager.h"
#include "resource.h"

#include <commctrl.h>
#include <vector>

enum class NodeType { ProjectRoot, Folder, File };

// Attached to every real tree item via TVITEM.lParam. The dummy "loading..."
// placeholder child used for lazy folder expansion carries a null lParam
// instead, so it never needs one of these.
struct TreeNodeData
{
	NodeType type;
	ProjectPtr project;   // the project this node belongs to (always set)
	std::wstring path;    // full path for Folder/File nodes; unused for ProjectRoot
	bool folderScanned = false; // Folder nodes only: real children populated yet?
};

class ProjectPanel : public DockingDlgInterface
{
public:
	ProjectPanel() : DockingDlgInterface(IDD_PROJECTPANEL) {}

	void setManager(ProjectManager* manager) { _manager = manager; }

	// (Re)builds the whole tree from the manager's current project list.
	// Cheap enough to call after any structural change (add/remove/close).
	void rebuildTree();

	// The project the panel currently considers "active": whichever
	// project owns the selected tree node, or the sole open project if
	// none is selected. Used by the Plugins-menu commands (as opposed to
	// the panel's own right-click menu, which always knows exactly which
	// node was clicked).
	ProjectPtr activeProject() const { return selectedProject(); }

	// Thin wrappers so PluginDefinition's menu callbacks (which take no
	// arguments) can drive the same logic as the panel's context menu.
	void menuNewProject() { cmdNewProject(); }
	void menuOpenProject() { cmdOpenProject(); }
	void menuSaveProject() { withActiveProject(&ProjectPanel::cmdSaveProject); }
	void menuSaveProjectAs() { withActiveProject(&ProjectPanel::cmdSaveProjectAs); }
	void menuCloseProject() { withActiveProject(&ProjectPanel::cmdCloseProject); }
	void menuAddFolder() { withActiveProject(&ProjectPanel::cmdAddFolder); }
	void menuAddFiles() { withActiveProject(&ProjectPanel::cmdAddFiles); }
	void menuOpenAllFiles() { withActiveProject(&ProjectPanel::cmdOpenAllFiles); }
	void menuSaveSession() { withActiveProject(&ProjectPanel::cmdSaveSession); }

protected:
	INT_PTR CALLBACK run_dlgProc(UINT message, WPARAM wParam, LPARAM lParam) override;

private:
	ProjectManager* _manager = nullptr;
	HWND _hTreeView = nullptr;
	HIMAGELIST _hImageList = nullptr;

	void createTreeView();
	void resizeTreeView();

	HTREEITEM insertProjectRoot(const ProjectPtr& project);
	HTREEITEM insertFolderNode(HTREEITEM parent, const ProjectPtr& project, const std::wstring& folderPath);
	HTREEITEM insertFileNode(HTREEITEM parent, const ProjectPtr& project, const std::wstring& filePath);
	void addDummyChild(HTREEITEM parent);
	void populateFolderChildren(HTREEITEM folderItem, TreeNodeData* data);

	TreeNodeData* nodeDataAt(HTREEITEM item) const;
	TreeNodeData* selectedNodeData() const;
	ProjectPtr selectedProject() const; // owning project of the current selection, or the sole open project

	int shellIconIndex(const std::wstring& path, bool isDir) const;
	int genericIconIndex(bool isDir) const;

	// WM_NOTIFY handlers for the tree control.
	void onDblClick();
	void onItemExpanding(NMTREEVIEW* pnmtv);
	void onRightClick();
	void onDeleteItem(NMTREEVIEW* pnmtv);

	// Context-menu command handlers.
	void cmdNewProject();
	void cmdOpenProject();
	void cmdSaveProject(const ProjectPtr& project);
	void cmdSaveProjectAs(const ProjectPtr& project);
	void cmdSaveSession(const ProjectPtr& project);
	void cmdRenameProject(const ProjectPtr& project);
	void cmdCloseProject(const ProjectPtr& project);
	void cmdOpenAllFiles(const ProjectPtr& project);
	void cmdAddFolder(const ProjectPtr& project);
	void cmdAddFiles(const ProjectPtr& project);
	void cmdRemoveFolder(const ProjectPtr& project, const std::wstring& folderPath);
	void cmdRemoveFile(const ProjectPtr& project, const std::wstring& filePath);
	void cmdRefreshFolder(HTREEITEM item, TreeNodeData* data);
	void cmdOpenInExplorer(const std::wstring& folderPath);
	void cmdShowFileInExplorer(const std::wstring& filePath);

	// Used by the menu* wrappers above: runs 'fn' against activeProject(),
	// or tells the user to pick one first if none is active.
	void withActiveProject(void (ProjectPanel::*fn)(const ProjectPtr&));
	void warnNoActiveProject() const;
};
