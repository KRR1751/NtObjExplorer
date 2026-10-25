/*
 * ntobj.exe - entry point (interactive REPL or one-shot command).
 */
#include "ntobj_app.h"
#include "ntapi.h"
#include "objects.h"
#include <iostream>
#include <io.h>
#include <fcntl.h>

/* ---------------------------------------------------------------------------
 * Environment helpers
 * ------------------------------------------------------------------------- */

bool IsElevated()
{
    HANDLE token = NULL;
    DWORD elevation = 0;
    DWORD len = 0;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        return false;
    }
    BOOL ok = GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &len);
    CloseHandle(token);
    return ok && elevation != 0;
}

static bool SetSinglePrivilege(const wchar_t* privilegeName, bool enable)
{
    HANDLE token = NULL;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token)) {
        return false;
    }
    TOKEN_PRIVILEGES tp;
    ZeroMemory(&tp, sizeof(tp));
    tp.PrivilegeCount = 1;
    tp.Privileges[0].Attributes = enable ? SE_PRIVILEGE_ENABLED : 0;
    if (!LookupPrivilegeValueW(NULL, privilegeName, &tp.Privileges[0].Luid)) {
        CloseHandle(token);
        return false;
    }
    AdjustTokenPrivileges(token, FALSE, &tp, 0, NULL, NULL);
    DWORD err = GetLastError();
    CloseHandle(token);
    return err == ERROR_SUCCESS;
}

void EnableStartupPrivileges()
{
    /* Needed by the direct (ntdll) backend for OBJ_PERMANENT creations. */
    SetSinglePrivilege(SE_CREATE_PERMANENT_NAME, true);
    /* Harmless if missing; helps browsing protected directories. */
    SetSinglePrivilege(SE_BACKUP_NAME, true);
    SetSinglePrivilege(SE_RESTORE_NAME, true);
}

static void PrintBanner()
{
    std::wcout << L"ntobj 1.0 - NT Object Manager namespace explorer (WinObj-like console tool)\n";
    std::wcout << L"Driver: ";
    if (DriverWasDetected()) {
        std::wcout << L"connected (backend 'driver' available)";
    } else {
        std::wcout << L"not connected (direct ntdll mode, limited to what your token allows)";
    }
    std::wcout << L"\n";
    if (!IsElevated()) {
        std::wcout << L"WARNING: process is not elevated - many directories will deny access.\n";
    }
    std::wcout << L"Type 'help' for commands, 'exit' to quit.\n\n";
}

int wmain(int argc, wchar_t* argv[])
{
    _setmode(_fileno(stdout), _O_U16TEXT);
    _setmode(_fileno(stdin), _O_U16TEXT);
    _setmode(_fileno(stderr), _O_U16TEXT);

    if (!InitNtApi()) {
        std::wcout << L"ERROR: ntdll.dll entry points could not be resolved.\n";
        return 2;
    }

    EnableStartupPrivileges();
    DetectDriverAvailable();
    PrintBanner();

    /* One-shot mode: ntobj ls \Device */
    if (argc > 1) {
        std::vector<std::wstring> args(argv + 1, argv + argc);
        return ExecuteCommand(args);
    }

    /* Interactive mode. */
    for (;;) {
        std::wcout << L"ntobj> ";
        std::wstring line;
        if (!std::getline(std::wcin, line)) {
            std::wcout << L"\n";
            break;
        }
        std::vector<std::wstring> args = Tokenize(line);
        if (args.empty()) {
            continue;
        }
        if (CommandIsExit(args)) {
            break;
        }
        ExecuteCommand(args);
    }

    return 0;
}

// [v1.1 resync 2026-10-23] current build-fix version
