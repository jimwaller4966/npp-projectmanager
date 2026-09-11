#include "Project.h"
#include "StrUtil.h"
#include "WinFileIO.h"

#include <sstream>
#include <algorithm>

namespace
{
	bool containsPath(const std::vector<std::wstring>& v, const std::wstring& path)
	{
		for (const auto& p : v)
			if (StrUtil::iequals(p, path))
				return true;
		return false;
	}

	void removePath(std::vector<std::wstring>& v, const std::wstring& path)
	{
		v.erase(std::remove_if(v.begin(), v.end(),
			[&](const std::wstring& p) { return StrUtil::iequals(p, path); }),
			v.end());
	}
}

bool Project::addFolder(const std::wstring& folderPath)
{
	std::wstring clean = StrUtil::stripTrailingSlash(folderPath);
	if (containsPath(_folders, clean))
		return false;
	_folders.push_back(clean);
	_dirty = true;
	return true;
}

bool Project::addFile(const std::wstring& filePath)
{
	if (containsPath(_files, filePath))
		return false;
	_files.push_back(filePath);
	_dirty = true;
	return true;
}

void Project::removeFolder(const std::wstring& folderPath)
{
	removePath(_folders, StrUtil::stripTrailingSlash(folderPath));
	_dirty = true;
}

void Project::removeFile(const std::wstring& filePath)
{
	removePath(_files, filePath);
	_dirty = true;
}

void Project::setOpenFiles(std::vector<std::wstring> paths)
{
	_openFiles = std::move(paths);
	_dirty = true;
}

// --- persistence -------------------------------------------------------
//
// File format (UTF-8 text, simple INI-like sections — deliberately not JSON/XML
// so it's trivial to hand-edit and diff in source control):
//
//   ; Notepad++ Project File
//   [Project]
//   Name=My Website
//
//   [Folders]
//   C:\Users\Jim\Sites\mywebsite
//
//   [Files]
//   C:\Users\Jim\Notes\todo.txt
//
//   [OpenFiles]
//   C:\Users\Jim\Sites\mywebsite\index.html
//   C:\Users\Jim\Sites\mywebsite\style.css

namespace
{
	enum class Section { None, Project, Folders, Files, OpenFiles };
}

bool Project::loadFromFile(const std::wstring& path)
{
	std::string raw;
	if (!WinFileIO::readFile(path, raw))
		return false;

	raw = WinFileIO::stripUtf8Bom(raw);
	std::wstring content = StrUtil::utf8ToWide(raw);

	std::wstring newName;
	std::vector<std::wstring> newFolders, newFiles, newOpenFiles;
	Section section = Section::None;

	std::wistringstream lines(content);
	std::wstring line;
	while (std::getline(lines, line))
	{
		// getline on a wstring built from \r\n text may leave a trailing \r.
		if (!line.empty() && line.back() == L'\r')
			line.pop_back();

		std::wstring trimmed = StrUtil::trim(line);
		if (trimmed.empty() || trimmed[0] == L';' || trimmed[0] == L'#')
			continue;

		if (trimmed.front() == L'[' && trimmed.back() == L']')
		{
			std::wstring sectionName = trimmed.substr(1, trimmed.size() - 2);
			if (StrUtil::iequals(sectionName, L"Project"))
				section = Section::Project;
			else if (StrUtil::iequals(sectionName, L"Folders"))
				section = Section::Folders;
			else if (StrUtil::iequals(sectionName, L"Files"))
				section = Section::Files;
			else if (StrUtil::iequals(sectionName, L"OpenFiles"))
				section = Section::OpenFiles;
			else
				section = Section::None;
			continue;
		}

		switch (section)
		{
			case Section::Project:
			{
				size_t eq = trimmed.find(L'=');
				if (eq != std::wstring::npos)
				{
					std::wstring key = StrUtil::trim(trimmed.substr(0, eq));
					std::wstring value = StrUtil::trim(trimmed.substr(eq + 1));
					if (StrUtil::iequals(key, L"Name"))
						newName = value;
				}
				break;
			}
			case Section::Folders:
				newFolders.push_back(trimmed);
				break;
			case Section::Files:
				newFiles.push_back(trimmed);
				break;
			case Section::OpenFiles:
				newOpenFiles.push_back(trimmed);
				break;
			default:
				break;
		}
	}

	if (newName.empty())
		newName = StrUtil::fileNameFromPath(path);

	_name = newName;
	_folders = std::move(newFolders);
	_files = std::move(newFiles);
	_openFiles = std::move(newOpenFiles);
	_filePath = path;
	_dirty = false;
	return true;
}

bool Project::save()
{
	if (_filePath.empty())
		return false;

	std::wostringstream out;
	out << L"; Notepad++ Project File\r\n";
	out << L"[Project]\r\n";
	out << L"Name=" << _name << L"\r\n\r\n";

	out << L"[Folders]\r\n";
	for (const auto& f : _folders)
		out << f << L"\r\n";
	out << L"\r\n";

	out << L"[Files]\r\n";
	for (const auto& f : _files)
		out << f << L"\r\n";
	out << L"\r\n";

	out << L"[OpenFiles]\r\n";
	for (const auto& f : _openFiles)
		out << f << L"\r\n";

	std::string utf8 = WinFileIO::withUtf8Bom(StrUtil::wideToUtf8(out.str()));

	if (!WinFileIO::writeFile(_filePath, utf8))
		return false;

	_dirty = false;
	return true;
}

bool Project::saveAs(const std::wstring& path)
{
	_filePath = path;
	return save();
}
