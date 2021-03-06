// CrGuard.cpp : Defines the entry point for the application.
//

#include "stdafx.h"
#include "CrGuard.h"
#include <shellapi.h>

LPCWSTR RegKey = L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Image File Execution Options\\chrome.exe";
LPCWSTR RegValue = L"Debugger";

int InstallCrGuard(HKEY hKey, LPCWSTR Path) {
	WCHAR PathQuoted[MAX_PATH + 2] = { 0 };
	wcscat_s(PathQuoted, MAX_PATH + 2, L"\"");
	wcscat_s(PathQuoted, MAX_PATH + 2, Path);
	wcscat_s(PathQuoted, MAX_PATH + 2, L"\"");

	LSTATUS status = RegSetValueExW(hKey, RegValue, 0, REG_SZ, (LPBYTE)PathQuoted, wcslen(PathQuoted) * sizeof(WCHAR) + 1);
	if (status == ERROR_SUCCESS) {
		MessageBoxW(NULL, L"CrGuard Installed!", L"CrGuard", 0);
		return 0;
	}
	else {
		MessageBoxW(NULL, L"CrGuard Installed Failed!", L"CrGuard", 0);
		return -1;
	}
}

int UninstallCrGuard(HKEY hKey) {
	LSTATUS status = RegDeleteValueW(hKey, RegValue);
	if (status == ERROR_SUCCESS) {
		MessageBoxW(NULL, L"CrGuard Removed!", L"CrGuard", 0);
		return 0;
	}
	else {
		MessageBoxW(NULL, L"CrGuard Remove Failed!", L"CrGuard", 0);
		return -1;
	}
}

BOOL IsElevated() {
	BOOL fRet = FALSE;
	HANDLE hToken = NULL;
	if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken)) {
		TOKEN_ELEVATION Elevation;
		DWORD cbSize = sizeof(TOKEN_ELEVATION);
		if (GetTokenInformation(hToken, TokenElevation, &Elevation, sizeof(Elevation), &cbSize)) {
			fRet = Elevation.TokenIsElevated;
		}
	}
	if (hToken) {
		CloseHandle(hToken);
	}
	return fRet;
}

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
                     _In_opt_ HINSTANCE hPrevInstance,
                     _In_ LPWSTR    lpCmdLine,
                     _In_ int       nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
#ifdef _DEBUG
	MessageBoxW(NULL, lpCmdLine, L"CrGuard Start", 0);
#endif

	int ArgCount;
	LPWSTR *ArgList = CommandLineToArgvW(lpCmdLine, &ArgCount);
	if (ArgCount < 1) return -1;
	LPWSTR TargetProgram = ArgList[0];

	WCHAR MyProgram[MAX_PATH];
	GetModuleFileNameW(NULL, MyProgram, MAX_PATH);

	if (wcsstr(TargetProgram, L"[open]") != 0 && ArgCount >= 2) {
		ShellExecute(0, 0, ArgList[1], 0, 0, SW_SHOW);
		ExitProcess(0);
	}else if (wcsstr(TargetProgram,L"chrome.exe") == 0) {

		if (!IsElevated()) {
			MessageBoxW(NULL, L"Admin Required For Install/Uninstall CrGuard", L"CrGuard", 0);
			ExitProcess(-1);
		}

		DWORD RegType = REG_SZ;
		HKEY hKey = 0;
		WCHAR Value = 0;
		LSTATUS status = RegOpenKeyW(HKEY_LOCAL_MACHINE, RegKey, &hKey);
		if (status == ERROR_SUCCESS) {
			status = RegQueryValueExW(hKey, RegValue, NULL, &RegType, NULL, NULL);
			if (status == ERROR_SUCCESS) {
				return UninstallCrGuard(hKey);
			}
		}
		else {
			RegCreateKeyW(HKEY_LOCAL_MACHINE, RegKey, &hKey);
		}
		return InstallCrGuard(hKey, MyProgram);
	}
	LocalFree(ArgList);

	*wcsrchr(MyProgram, '\\') = '\0';
	wcscat_s(MyProgram, MAX_PATH, L"\\CrGuard.dll");

	PROCESS_INFORMATION pi;
	STARTUPINFO si = { sizeof(si) };

	BOOL bCreated = CreateProcessW(nullptr, lpCmdLine, nullptr, nullptr, FALSE, DEBUG_PROCESS, nullptr, nullptr, &si, &pi);
	if (!bCreated) {
		MessageBoxW(NULL, L"CreateProcessW Failed", NULL, 0);
		return -1;
	}
	SIZE_T BufferSize = wcslen(MyProgram) * sizeof(WCHAR);
	LPVOID RemoteBuffer = VirtualAllocEx(pi.hProcess, nullptr, BufferSize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
	if (!RemoteBuffer) {
		MessageBoxW(NULL, L"VirtualAllocEx Failed", NULL, 0);
		return -1;
	}
	SIZE_T Written;
	WriteProcessMemory(pi.hProcess, RemoteBuffer, MyProgram, BufferSize , &Written);
	if (Written != BufferSize) {
		MessageBoxW(NULL, L"WriteProcessMemory Failed", NULL, 0);
		return -1;
	}
	HANDLE RemoteThread = CreateRemoteThread(pi.hProcess, nullptr, 0, (PTHREAD_START_ROUTINE)GetProcAddress(GetModuleHandle(L"kernel32.dll"), "LoadLibraryW"), RemoteBuffer, 0, nullptr);
	if (!RemoteThread) {
		MessageBoxW(NULL, L"CreateRemoteThread", NULL, 0);
		return -1;
	}
	DebugSetProcessKillOnExit(FALSE);
	ExitProcess(0);
    return 0;
}