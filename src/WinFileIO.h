// WinFileIO.h — tiny whole-file read/write helpers built directly on the
// Win32 API rather than <fstream>.
//
// Why not std::ifstream/std::ofstream? MSVC's STL accepts a std::wstring /
// const wchar_t* path in the fstream constructors (a Microsoft extension),
// but MinGW-w64's libstdc++ does not - only narrow (char*) paths are
// portable there, which would silently mangle any project path containing
// characters outside the system codepage. Going straight to CreateFileW
// works identically with either compiler and handles arbitrary Unicode
// paths correctly.
#pragma once

#include <windows.h>
#include <string>

namespace WinFileIO
{
	inline bool readFile(const std::wstring& path, std::string& outBytes)
	{
		HANDLE hFile = ::CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
			OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (hFile == INVALID_HANDLE_VALUE)
			return false;

		LARGE_INTEGER size{};
		if (!::GetFileSizeEx(hFile, &size) || size.QuadPart < 0)
		{
			::CloseHandle(hFile);
			return false;
		}

		outBytes.assign(static_cast<size_t>(size.QuadPart), '\0');

		BOOL ok = TRUE;
		DWORD bytesRead = 0;
		if (!outBytes.empty())
			ok = ::ReadFile(hFile, &outBytes[0], static_cast<DWORD>(outBytes.size()), &bytesRead, nullptr);

		::CloseHandle(hFile);
		return ok && static_cast<size_t>(bytesRead) == outBytes.size();
	}

	inline bool writeFile(const std::wstring& path, const std::string& bytes)
	{
		HANDLE hFile = ::CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
			CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (hFile == INVALID_HANDLE_VALUE)
			return false;

		BOOL ok = TRUE;
		DWORD bytesWritten = 0;
		if (!bytes.empty())
			ok = ::WriteFile(hFile, bytes.data(), static_cast<DWORD>(bytes.size()), &bytesWritten, nullptr);

		::CloseHandle(hFile);
		return ok && static_cast<size_t>(bytesWritten) == bytes.size();
	}

	// Prepends a UTF-8 BOM (0xEF 0xBB 0xBF), so files this plugin writes are
	// unambiguous if opened directly in Notepad++ or another editor.
	inline std::string withUtf8Bom(const std::string& utf8)
	{
		std::string result;
		result.reserve(utf8.size() + 3);
		result += '\xEF';
		result += '\xBB';
		result += '\xBF';
		result += utf8;
		return result;
	}

	// Strips a leading UTF-8 BOM, if present.
	inline std::string stripUtf8Bom(std::string bytes)
	{
		if (bytes.size() >= 3 &&
			static_cast<unsigned char>(bytes[0]) == 0xEF &&
			static_cast<unsigned char>(bytes[1]) == 0xBB &&
			static_cast<unsigned char>(bytes[2]) == 0xBF)
		{
			bytes.erase(0, 3);
		}
		return bytes;
	}
}
