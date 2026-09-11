// Project.h — the data model for a single "project": a named collection of
// root folders and/or loose files, plus (optionally) a remembered set of
// files that were open in Notepad++ the last time the project was saved
// ("session"). Persisted as a small UTF-8 text file, extension .nppproj.
#pragma once

#include <string>
#include <vector>
#include <memory>

class Project
{
public:
	Project() = default;
	explicit Project(std::wstring name) : _name(std::move(name)) {}

	// --- accessors -----------------------------------------------------
	const std::wstring& name() const { return _name; }
	void setName(const std::wstring& name) { _name = name; _dirty = true; }

	// Full path to the .nppproj file on disk. Empty if never saved.
	const std::wstring& filePath() const { return _filePath; }
	void setFilePath(const std::wstring& path) { _filePath = path; }

	const std::vector<std::wstring>& folders() const { return _folders; }
	const std::vector<std::wstring>& files() const { return _files; }
	const std::vector<std::wstring>& openFiles() const { return _openFiles; }

	bool isDirty() const { return _dirty; }
	void setDirty(bool d) { _dirty = d; }

	// --- mutation --------------------------------------------------------
	// Returns false if the folder/file is already present (no duplicate added).
	bool addFolder(const std::wstring& folderPath);
	bool addFile(const std::wstring& filePath);
	void removeFolder(const std::wstring& folderPath);
	void removeFile(const std::wstring& filePath);

	// Updates any tracked folder, loose file, or remembered-session entry
	// that matches 'oldPath' (case-insensitively) to 'newPath' instead.
	// Used to keep a project in sync when Notepad++ itself renames a file
	// (its tab context menu's Rename command). Returns true if anything in
	// this project actually changed.
	bool renamePath(const std::wstring& oldPath, const std::wstring& newPath);

	// Removes any tracked folder, loose file, or remembered-session entry
	// that matches 'path' (case-insensitively). Used to keep a project in
	// sync when Notepad++ itself deletes a file (its tab context menu's
	// "Move to Recycle Bin" command). Returns true if anything changed.
	bool forgetPath(const std::wstring& path);

	// Drops any tracked folder, loose file, or remembered-session entry that
	// no longer exists on disk, checked directly against the filesystem.
	// This is the reliable, no-assumptions-about-Notepad++'s-internals fix
	// for a stale entry left behind by a rename or delete done *outside*
	// Notepad++ (Explorer, another program, a moved network share, ...) -
	// renamePath()/forgetPath() above only catch it when Notepad++ itself
	// did the renaming/deleting. Called every time the panel's tree is
	// rebuilt, so a gone file simply disappears the next time anything
	// refreshes rather than lingering until someone notices and removes it
	// by hand. Returns true if anything was dropped.
	bool pruneMissing();

	// Replaces the remembered "open files" session list wholesale.
	void setOpenFiles(std::vector<std::wstring> paths);

	// --- persistence -----------------------------------------------------
	// Loads a .nppproj file. Returns false (and leaves *this unchanged) on failure.
	bool loadFromFile(const std::wstring& path);

	// Saves to _filePath (must be non-empty; use setFilePath first for "Save As").
	bool save();
	bool saveAs(const std::wstring& path);

private:
	std::wstring _name;
	std::wstring _filePath;
	std::vector<std::wstring> _folders;
	std::vector<std::wstring> _files;
	std::vector<std::wstring> _openFiles;
	bool _dirty = false;
};

using ProjectPtr = std::shared_ptr<Project>;
