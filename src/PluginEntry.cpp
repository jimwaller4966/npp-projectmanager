// PluginEntry.cpp — the DLL's actual entry point. Every Notepad++ plugin is
// a plain Win32 DLL; this is the one function Windows itself calls.
#include <windows.h>
#include "PluginDefinition.h"

BOOL APIENTRY DllMain(HANDLE hModule, DWORD reasonForCall, LPVOID)
{
	switch (reasonForCall)
	{
		case DLL_PROCESS_ATTACH:
			::DisableThreadLibraryCalls(static_cast<HMODULE>(hModule));
			pluginInit(hModule);
			break;

		case DLL_PROCESS_DETACH:
			pluginCleanUp();
			break;

		case DLL_THREAD_ATTACH:
		case DLL_THREAD_DETACH:
			break;
	}
	return TRUE;
}
