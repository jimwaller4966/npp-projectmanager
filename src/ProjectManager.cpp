#include "ProjectManager.h"
#include "StrUtil.h"
#include "WinFileIO.h"

#include <sstream>
#include <algorithm>

ProjectPtr ProjectManager::newProject(const std::wstring& name)
{
	auto p = std::make_shared<Project>(name);
	_projects.push_back(p);
	return p;
}

ProjectPtr ProjectManager::openProjectFile(const std::wstring& nppprojPath)
{
	if (auto existing = findByFilePath(nppprojPath))
		return existing;

	auto p = std::make_shared<Project>();
	if (!p->loadFromFile(nppprojPath))
		return nullptr;

	_projects.push_back(p);
	return p;
}

void ProjectManager::closeProject(const ProjectPtr& project)
{
	_projects.erase(std::remove(_projects.begin(), _projects.end(), project), _projects.end());
}

ProjectPtr ProjectManager::findByFilePath(const std::wstring& path) const
{
	for (const auto& p : _projects)
		if (!p->filePath().empty() && StrUtil::iequals(p->filePath(), path))
			return p;
	return nullptr;
}

void ProjectManager::openFile(const std::wstring& path) const
{
	// NPPM_DOOPEN opens the file, or simply switches to it if it's already
	// open in either view - exactly what we want for both "open project" and
	// "restore session".
	::SendMessage(_nppData._nppHandle, NPPM_DOOPEN, 0, reinterpret_cast<LPARAM>(path.c_str()));
}

void ProjectManager::openAllFiles(const ProjectPtr& project, bool includeSession) const
{
	if (!project)
		return;

	for (const auto& f : project->files())
		openFile(f);

	if (includeSession)
	{
		for (const auto& f : project->openFiles())
			openFile(f);
	}
}

std::vector<std::wstring> ProjectManager::getOpenFilePaths() const
{
	std::vector<std::wstring> result;

	const int views[2] = { PRIMARY_VIEW, SECOND_VIEW };
	for (int view : views)
	{
		int nbFiles = static_cast<int>(::SendMessage(_nppData._nppHandle, NPPM_GETNBOPENFILES, 0, view));
		for (int i = 0; i < nbFiles; ++i)
		{
			UINT_PTR bufferId = static_cast<UINT_PTR>(
				::SendMessage(_nppData._nppHandle, NPPM_GETBUFFERIDFROMPOS, i, view));
			if (bufferId == 0)
				continue;

			wchar_t path[MAX_PATH * 4] = { 0 };
			::SendMessage(_nppData._nppHandle, NPPM_GETFULLPATHFROMBUFFERID,
				static_cast<WPARAM>(bufferId), reinterpret_cast<LPARAM>(path));

			if (path[0] != L'\0')
				result.emplace_back(path);
		}
	}

	return result;
}

void ProjectManager::captureOpenFilesAsSession(const ProjectPtr& project) const
{
	if (!project)
		return;
	project->setOpenFiles(getOpenFilePaths());
}

// --- "last open projects" persistence -----------------------------------
//
// A tiny text file (one .nppproj path per line) living in the plugin's own
// config directory, so the panel can restore whatever projects were open the
// last time Notepad++ closed.

std::wstring ProjectManager::lastSessionFilePath(const std::wstring& configDir)
{
	std::wstring dir = StrUtil::stripTrailingSlash(configDir);
	return dir + L"\\ProjectManager.session";
}

void ProjectManager::loadLastSession(const std::wstring& configDir)
{
	std::wstring path = lastSessionFilePath(configDir);
	std::string raw;
	if (!WinFileIO::readFile(path, raw))
		return;

	raw = WinFileIO::stripUtf8Bom(raw);
	std::wstring content = StrUtil::utf8ToWide(raw);
	std::wistringstream lines(content);
	std::wstring line;
	while (std::getline(lines, line))
	{
		if (!line.empty() && line.back() == L'\r')
			line.pop_back();
		std::wstring trimmed = StrUtil::trim(line);
		if (trimmed.empty())
			continue;

		openProjectFile(trimmed);
	}
}

void ProjectManager::saveLastSession(const std::wstring& configDir) const
{
	std::wstring path = lastSessionFilePath(configDir);

	std::wostringstream out;
	for (const auto& p : _projects)
	{
		if (!p->filePath().empty())
			out << p->filePath() << L"\r\n";
	}

	std::string utf8 = WinFileIO::withUtf8Bom(StrUtil::wideToUtf8(out.str()));
	WinFileIO::writeFile(path, utf8);
}
