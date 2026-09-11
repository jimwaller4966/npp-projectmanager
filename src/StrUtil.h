// StrUtil.h — small UTF-8 <-> UTF-16 (wchar_t) helpers used throughout the plugin.
// Notepad++'s plugin interface is wchar_t-based (Unicode-only builds), but the
// .nppproj project file is stored as plain UTF-8 text so it's easy to read/diff.
#pragma once

#include <windows.h>
#include <string>
#include <cwchar>

namespace StrUtil
{
	inline std::wstring utf8ToWide(const std::string& utf8)
	{
		if (utf8.empty())
			return std::wstring();

		int wlen = ::MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), nullptr, 0);
		if (wlen <= 0)
			return std::wstring();

		std::wstring result(static_cast<size_t>(wlen), L'\0');
		::MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), &result[0], wlen);
		return result;
	}

	inline std::string wideToUtf8(const std::wstring& wide)
	{
		if (wide.empty())
			return std::string();

		int len = ::WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
		if (len <= 0)
			return std::string();

		std::string result(static_cast<size_t>(len), '\0');
		::WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()), &result[0], len, nullptr, nullptr);
		return result;
	}

	// Trim ASCII whitespace from both ends.
	inline std::wstring trim(const std::wstring& s)
	{
		size_t start = s.find_first_not_of(L" \t\r\n");
		if (start == std::wstring::npos)
			return std::wstring();
		size_t end = s.find_last_not_of(L" \t\r\n");
		return s.substr(start, end - start + 1);
	}

	// Case-insensitive compare (good enough for Windows paths).
	inline bool iequals(const std::wstring& a, const std::wstring& b)
	{
		if (a.size() != b.size())
			return false;
		return ::_wcsicmp(a.c_str(), b.c_str()) == 0;
	}

	// Returns the last path component (file or folder name) of a full path.
	inline std::wstring fileNameFromPath(const std::wstring& path)
	{
		size_t pos = path.find_last_of(L"\\/");
		if (pos == std::wstring::npos)
			return path;
		return path.substr(pos + 1);
	}

	// Strips a trailing backslash/slash, if any (but keeps "C:\").
	inline std::wstring stripTrailingSlash(const std::wstring& path)
	{
		if (path.size() > 3 && (path.back() == L'\\' || path.back() == L'/'))
			return path.substr(0, path.size() - 1);
		return path;
	}
}
