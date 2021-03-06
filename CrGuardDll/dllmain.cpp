// dllmain.cpp : Defines the entry point for the DLL application.
#include "stdafx.h"
#include "MinHook.h"
#include "tlhelp32.h"
#include "winternl.h"
#include <stdio.h>
#include <intrin.h>

#ifndef _DEBUG
#define printf()
#define wprintf()
#endif // !DEBUG


void CreateHooks();

PVOID ChromeExeBase = 0;
PVOID ChromeDllBase = 0;

WCHAR DllPath[MAX_PATH];

BOOL APIENTRY DllMain( HMODULE hModule,
                       DWORD  ul_reason_for_call,
                       LPVOID lpReserved
                     )
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
#ifdef _DEBUG
		MessageBoxW(NULL, L"Inject Success", L"CrGuard", 0);
#endif
		ChromeExeBase = GetModuleHandleW(NULL);
		GetModuleFileNameW(hModule, DllPath, MAX_PATH);
		DebugSetProcessKillOnExit(FALSE);
		CreateHooks();
    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
    case DLL_PROCESS_DETACH:
        break;
    }
    return TRUE;
}

BOOL UtilCheckReturnAddressInChrome(PVOID Addr) {	
	if (!ChromeDllBase) {
		ChromeDllBase = GetModuleHandleW(L"chrome.dll");
	}
	if (Addr) {
		PVOID Base = 0;
		RtlPcToFileHeader(Addr, &Base);
		if (!Base) {
			printf("WARNING: Return address is not backed by image: %p", Addr);
		}
		if (Base == ChromeDllBase || Base == ChromeExeBase) {
			return TRUE;
		}
		else {
			WCHAR str[MAX_PATH];
			GetModuleFileNameW((HMODULE)Base, str, MAX_PATH);
			//wprintf(L"ImageBase: %p ModuleName: %s\n", Base, str);
		}
	}
	return FALSE;
}

#define NT_ACCESS_DENIED 0xC0000022

typedef LSTATUS(NTAPI *t_RegOpenKeyExW)
(
	HKEY    hKey,
	LPCWSTR lpSubKey,
	DWORD   ulOptions,
	REGSAM  samDesired,
	PHKEY   phkResult
	);


typedef BOOL(NTAPI *t_CreateProcessInternalW)
(
	HANDLE hToken,
	LPCWSTR lpApplicationName,
	LPWSTR lpCommandLine,
	LPSECURITY_ATTRIBUTES lpProcessAttributes,
	LPSECURITY_ATTRIBUTES lpThreadAttributes,
	BOOL bInheritHandles,
	DWORD dwCreationFlags,
	LPVOID lpEnvironment,
	LPCWSTR lpCurrentDirectory,
	LPSTARTUPINFOW lpStartupInfo,
	LPPROCESS_INFORMATION lpProcessInformation,
	PHANDLE hNewToken
	);

typedef NTSTATUS (NTAPI *t_NtCreateFile)
(
	OUT PHANDLE                      FileHandle,
	IN ACCESS_MASK                   DesiredAccess,
	IN POBJECT_ATTRIBUTES            ObjectAttributes,
	OUT PVOID                        IoStatusBlock,
	IN PLARGE_INTEGER AllocationSize OPTIONAL,
	IN ULONG                         FileAttributes,
	IN ULONG                         ShareAccess,
	IN ULONG                         CreateDisposition,
	IN ULONG                         CreateOptions,
	IN PVOID EaBuffer                OPTIONAL,
	IN ULONG                         EaLength
	);

typedef NTSTATUS(NTAPI *t_LdrRegisterDllNotification)
(
	_In_     ULONG                          Flags,
	_In_     PVOID							NotificationFunction,
	_In_opt_ PVOID                          Context,
	_Out_    PVOID                          *Cookie
	);

typedef HANDLE(WINAPI *t_CreateToolhelp32Snapshot)
(
	DWORD dwFlags,
	DWORD th32ProcessID
	);


t_CreateProcessInternalW CreateProcessInternalWOld;

BOOL WINAPI CreateProcessInternalWNew(
	HANDLE hToken,
	LPCWSTR lpApplicationName,
	LPWSTR lpCommandLine,
	LPSECURITY_ATTRIBUTES lpProcessAttributes,
	LPSECURITY_ATTRIBUTES lpThreadAttributes,
	BOOL bInheritHandles,
	DWORD dwCreationFlags,
	LPVOID lpEnvironment,
	LPCWSTR lpCurrentDirectory,
	LPSTARTUPINFOW lpStartupInfo,
	LPPROCESS_INFORMATION lpProcessInformation,
	PHANDLE hNewToken
)
{
	BOOL CreatedAsDebug = FALSE;
	BOOL InjectNeeded = TRUE;
	if (lpApplicationName) {
		if (wcsstr(lpApplicationName, L"chrome.exe")) {
			dwCreationFlags |= DEBUG_PROCESS;
			CreatedAsDebug = TRUE;
		}
	}
	if (lpCommandLine) {
		if (wcsstr(lpCommandLine, L"chrome.exe")) {
			dwCreationFlags |= DEBUG_PROCESS;
			CreatedAsDebug = TRUE;
		}
	}
	BOOL succ = CreateProcessInternalWOld(hToken, lpApplicationName, lpCommandLine, lpProcessAttributes, lpThreadAttributes, bInheritHandles, dwCreationFlags, lpEnvironment, lpCurrentDirectory, lpStartupInfo, lpProcessInformation, hNewToken);
	if (succ) {
		wprintf(L"CreateProcess %s\n",lpCommandLine);
		static LPCWSTR WhitelistProcessFlags[] = {
			L"--type=renderer",
			L"--type=gpu-process",
			L"--type=utility",
			L"--type=plugin",
			L"--type=ppapi",
			L"--type=sandbox",
			L"--type=nacl",
			L"field-trial-handle",
			L"service-sandbox-type",
			L"service-request-channel",
			NULL
		};
		size_t i = 0;
		while (LPCWSTR key = WhitelistProcessFlags[i]) {
			if (wcsstr(lpCommandLine, key)) {
				if (CreatedAsDebug) {
					InjectNeeded = FALSE;
				}
			}
			i++;
		}

		if (InjectNeeded) {
			SIZE_T BufferSize = wcslen(DllPath) * sizeof(WCHAR);
			LPVOID RemoteBuffer = VirtualAllocEx(lpProcessInformation->hProcess, nullptr, BufferSize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
			if (!RemoteBuffer) {
				MessageBoxW(NULL, L"VirtualAllocEx Failed", NULL, 0);
				return -1;
			}
			SIZE_T Written;
			WriteProcessMemory(lpProcessInformation->hProcess, RemoteBuffer, DllPath, BufferSize, &Written);
			if (Written != BufferSize) {
				MessageBoxW(NULL, L"WriteProcessMemory Failed", NULL, 0);
				return -1;
			}
			HANDLE RemoteThread = CreateRemoteThread(lpProcessInformation->hProcess, nullptr, 0, (PTHREAD_START_ROUTINE)GetProcAddress(GetModuleHandle(L"kernel32.dll"), "LoadLibraryW"), RemoteBuffer, 0, nullptr);
			if (!RemoteThread) {
				MessageBoxW(NULL, L"CreateRemoteThread", NULL, 0);
				return -1;
			}
		}

		if (CreatedAsDebug) {
			DebugActiveProcessStop(lpProcessInformation->dwProcessId);
		}
	}
	return succ;
}


t_NtCreateFile NtCreateFileOld;

NTSTATUS WINAPI NtCreateFileNew(
	OUT PHANDLE                      FileHandle,
	IN ACCESS_MASK                   DesiredAccess,
	IN POBJECT_ATTRIBUTES            ObjectAttributes,
	OUT PVOID                        IoStatusBlock,
	IN PLARGE_INTEGER AllocationSize OPTIONAL,
	IN ULONG                         FileAttributes,
	IN ULONG                         ShareAccess,
	IN ULONG                         CreateDisposition,
	IN ULONG                         CreateOptions,
	IN PVOID EaBuffer                OPTIONAL,
	IN ULONG                         EaLength
)
{
	if (ObjectAttributes && ObjectAttributes->ObjectName && ObjectAttributes->ObjectName->Length) {
		static LPCWSTR BlacklistFileNames[] = {
			L"SwReporter",
			L"software_reporter_tool",
			L"debug.log",
			NULL
		};
		size_t i = 0;
		while (LPCWSTR key = BlacklistFileNames[i]) {
			if (wcsstr(ObjectAttributes->ObjectName->Buffer, key)) {
				return NT_ACCESS_DENIED;
			}
			i++;
		}
	}
	return NtCreateFileOld(FileHandle, DesiredAccess, ObjectAttributes, IoStatusBlock, AllocationSize, FileAttributes, ShareAccess, CreateDisposition, CreateOptions, EaBuffer, EaLength);
}

t_LdrRegisterDllNotification LdrRegisterDllNotificationOld;

NTSTATUS WINAPI LdrRegisterDllNotificationNew(
	_In_     ULONG                          Flags,
	_In_     PVOID							NotificationFunction,
	_In_opt_ PVOID                          Context,
	_Out_    PVOID                          *Cookie
)
{
	return NT_ACCESS_DENIED;
}

t_CreateToolhelp32Snapshot CreateToolhelp32SnapshotOld;

HANDLE WINAPI CreateToolhelp32SnapshotNew(
	DWORD dwFlags,
	DWORD th32ProcessID
)
{
	if (UtilCheckReturnAddressInChrome(_ReturnAddress())) {
		if (dwFlags == (TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32) && th32ProcessID == GetCurrentProcessId()) {
			wprintf(L"Block chrome get module list\n");
			return INVALID_HANDLE_VALUE;
		}
	}
	return CreateToolhelp32SnapshotOld(dwFlags, th32ProcessID);
}


t_RegOpenKeyExW RegOpenKeyExWOld;

LSTATUS WINAPI RegOpenKeyExWNew(
	HKEY    hKey,
	LPCWSTR lpSubKey,
	DWORD   ulOptions,
	REGSAM  samDesired,
	PHKEY   phkResult
)
{
	if (lpSubKey) {
		static LPCWSTR BlacklistRegKeys[] = {
			L"Microsoft\\Windows\\CurrentVersion\\Uninstall",
			L"Microsoft\\Windows\\CurrentVersion\\Shell Extensions",
			L"Microsoft\\CTF\\TIP",
			L"\\shellex\\",
			NULL
		};
		if (UtilCheckReturnAddressInChrome(_ReturnAddress())) {
			size_t i = 0;
			while (LPCWSTR key = BlacklistRegKeys[i]) {
				if (wcsstr(lpSubKey, key)) {
					wprintf(L"Block chrome read regkey: %s\n", key);
					return NT_ACCESS_DENIED;
				}
				i++;
			}
		}
	}

	return RegOpenKeyExWOld(hKey, lpSubKey, ulOptions, samDesired, phkResult);
}

void CreateHooks() {
#ifdef _DEBUG
	AllocConsole();
	freopen("CONOUT$", "w", stdout);
	freopen("CONIN$", "r", stdin);
#endif
	MH_Initialize();

	HMODULE NtDll = GetModuleHandleW(L"ntdll.dll");
	HMODULE KernelBase = GetModuleHandleW(L"kernelbase.dll");
	HMODULE Kernel32 = GetModuleHandleW(L"kernel32.dll");

	// Apply fixes for CreateProcess (because of Image Execute Options)
	MH_STATUS st = MH_CreateHook(GetProcAddress(KernelBase, "CreateProcessInternalW"), CreateProcessInternalWNew, (PVOID *)&CreateProcessInternalWOld);
	if (st != MH_OK) MessageBoxW(NULL, L"Hook CreateProcessInternalW Failed", L"CrGuard", 0);

	// Block chrome get installed program list, shell ext list , and IME list
	st = MH_CreateHook(GetProcAddress(KernelBase, "RegOpenKeyExW"), RegOpenKeyExWNew, (PVOID *)&RegOpenKeyExWOld);
	if (st != MH_OK) MessageBoxW(NULL, L"Hook RegOpenKeyExW Failed", L"CrGuard", 0);

	// Block chrome download SwReporter
	st = MH_CreateHook(GetProcAddress(NtDll, "NtCreateFile"), NtCreateFileNew, (PVOID *)&NtCreateFileOld);
	if (st != MH_OK) MessageBoxW(NULL, L"Hook NtCreateFile Failed", L"CrGuard", 0);

	// Block chrome register dll load notification
	st = MH_CreateHook(GetProcAddress(NtDll, "LdrRegisterDllNotification"), LdrRegisterDllNotificationNew, (PVOID *)&LdrRegisterDllNotificationOld);
	if (st != MH_OK) MessageBoxW(NULL, L"Hook LdrRegisterDllNotification Failed", L"CrGuard", 0);

	// Block chrome get module list
	st = MH_CreateHook(GetProcAddress(Kernel32, "CreateToolhelp32Snapshot"), CreateToolhelp32SnapshotNew, (PVOID *)&CreateToolhelp32SnapshotOld);
	if (st != MH_OK) MessageBoxW(NULL, L"Hook CreateToolhelp32SnapshotFailed", L"CrGuard", 0);
	
	st = MH_EnableHook(MH_ALL_HOOKS);
	if (st != MH_OK) MessageBoxW(NULL, L"Enable Hook Failed", L"CrGuard", 0);
}