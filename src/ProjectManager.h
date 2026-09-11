// ProjectManager.h — owns the set of currently-open Project instances and
// talks to Notepad++ (via NPPM_* messages) to open files and to snapshot /
// restore which files were open ("session").
#pragma once

#include "Project.h"
#include "PluginInterface.h"

#include <vector>

class ProjectManager
{
public:
	void init(const NppData& nppData) { _nppData = nppData; }

	std::vector<ProjectPtr>& projects() { return _projects; }
	const std::vector<ProjectPtr>& projects() const { return _projects; }

	// Adds a brand-new, not-yet-saved project (caller should prompt for a
	// save path before it's useful across restarts).
	ProjectPtr newProject(const std::wstring& name);

	// Loads a .nppproj file from disk and adds it to the open list. If it's
	// already open, returns the existing instance instead of loading twice.
	ProjectPtr openProjectFile(const std::wstring& nppprojPath);

	void closeProject(const ProjectPtr& project);

	ProjectPtr findByFilePath(const std::wstring& path) const;

	// Opens every file that belongs to the project: its loose Files list plus
	// (if includeSession) its remembered OpenFiles session list. Folders
	// themselves aren't opened, only files inside them that were explicitly
	// added or remembered.
	void openAllFiles(const ProjectPtr& project, bool includeSession = true) const;

	// Reads Notepad++'s currently open documents (both views) and stores them
	// as the project's session, ready to be saved with Project::save().
	void captureOpenFilesAsSession(const ProjectPtr& project) const;

	// Opens a single file in Notepad++ (or switches to it if already open).
	void openFile(const std::wstring& path) const;

	// Returns the full path of every currently open document in Notepad++.
	std::vector<std::wstring> getOpenFilePaths() const;

	// Remembers / restores which .nppproj files were open across Notepad++
	// restarts, stored as a small text file in the plugin's config folder.
	void loadLastSession(const std::wstring& configDir);
	void saveLastSession(const std::wstring& configDir) const;

private:
	NppData _nppData{};
	std::vector<ProjectPtr> _projects;

	static std::wstring lastSessionFilePath(const std::wstring& configDir);
};
