/*
 * ntobj.exe - dynamic ntdll.dll layer (implementation).
 */
#include "ntapi.h"
#include <cwchar>
#include <cstdio>
#include <iostream>

typedef NTSTATUS (NTAPI* PFN_NtOpenDirectoryObject)(PHANDLE, ACCESS_MASK, NtObjectAttributes*);
typedef NTSTATUS (NTAPI* PFN_NtCreateDirectoryObject)(PHANDLE, ACCESS_MASK, NtObjectAttributes*);
typedef NTSTATUS (NTAPI* PFN_NtQueryDirectoryObject)(HANDLE, void*, ULONG, BOOLEAN, BOOLEAN, ULONG*, ULONG*);
typedef NTSTATUS (NTAPI* PFN_NtOpenSymbolicLinkObject)(PHANDLE, ACCESS_MASK, NtObjectAttributes*);
typedef NTSTATUS (NTAPI* PFN_NtCreateSymbolicLinkObject)(PHANDLE, ACCESS_MASK, NtObjectAttributes*, NtUnicodeString*);
typedef NTSTATUS (NTAPI* PFN_NtQuerySymbolicLinkObject)(HANDLE, NtUnicodeString*, ULONG*);
typedef NTSTATUS (NTAPI* PFN_NtMakeTemporaryObject)(HANDLE);
typedef NTSTATUS (NTAPI* PFN_NtClose)(HANDLE);
typedef ULONG    (NTAPI* PFN_RtlNtStatusToDosError)(NTSTATUS);

/* Extended synchronization function pointers */
typedef NTSTATUS (NTAPI* PFN_NtOpenEvent)(PHANDLE, ACCESS_MASK, NtObjectAttributes*);
typedef NTSTATUS (NTAPI* PFN_NtCreateEvent)(PHANDLE, ACCESS_MASK, NtObjectAttributes*, ULONG, BOOLEAN);
typedef NTSTATUS (NTAPI* PFN_NtSetEvent)(HANDLE, LONG*);
typedef NTSTATUS (NTAPI* PFN_NtClearEvent)(HANDLE);
typedef NTSTATUS (NTAPI* PFN_NtResetEvent)(HANDLE, LONG*);

typedef NTSTATUS (NTAPI* PFN_NtOpenMutant)(PHANDLE, ACCESS_MASK, NtObjectAttributes*);
typedef NTSTATUS (NTAPI* PFN_NtCreateMutant)(PHANDLE, ACCESS_MASK, NtObjectAttributes*, BOOLEAN);
typedef NTSTATUS (NTAPI* PFN_NtReleaseMutant)(HANDLE, LONG*);

typedef NTSTATUS (NTAPI* PFN_NtOpenSemaphore)(PHANDLE, ACCESS_MASK, NtObjectAttributes*);
typedef NTSTATUS (NTAPI* PFN_NtCreateSemaphore)(PHANDLE, ACCESS_MASK, NtObjectAttributes*, LONG, LONG);
typedef NTSTATUS (NTAPI* PFN_NtReleaseSemaphore)(HANDLE, LONG, LONG*);

typedef NTSTATUS (NTAPI* PFN_NtOpenTimer)(PHANDLE, ACCESS_MASK, NtObjectAttributes*);
typedef NTSTATUS (NTAPI* PFN_NtCreateTimer)(PHANDLE, ACCESS_MASK, NtObjectAttributes*, ULONG);
typedef NTSTATUS (NTAPI* PFN_NtSetTimer)(HANDLE, PLARGE_INTEGER, PVOID, PVOID, BOOLEAN, LONG, BOOLEAN*);
typedef NTSTATUS (NTAPI* PFN_NtCancelTimer)(HANDLE, BOOLEAN*);

typedef NTSTATUS (NTAPI* PFN_NtOpenSection)(PHANDLE, ACCESS_MASK, NtObjectAttributes*);
typedef NTSTATUS (NTAPI* PFN_NtCreateSection)(PHANDLE, ACCESS_MASK, NtObjectAttributes*, PLARGE_INTEGER, ULONG, ULONG, HANDLE);

typedef NTSTATUS (NTAPI* PFN_NtOpenJobObject)(PHANDLE, ACCESS_MASK, NtObjectAttributes*);
typedef NTSTATUS (NTAPI* PFN_NtCreateJobObject)(PHANDLE, ACCESS_MASK, NtObjectAttributes*);

static PFN_NtOpenDirectoryObject        s_NtOpenDirectoryObject;
static PFN_NtCreateDirectoryObject      s_NtCreateDirectoryObject;
static PFN_NtQueryDirectoryObject       s_NtQueryDirectoryObject;
static PFN_NtOpenSymbolicLinkObject     s_NtOpenSymbolicLinkObject;
static PFN_NtCreateSymbolicLinkObject   s_NtCreateSymbolicLinkObject;
static PFN_NtQuerySymbolicLinkObject    s_NtQuerySymbolicLinkObject;
static PFN_NtMakeTemporaryObject        s_NtMakeTemporaryObject;
static PFN_NtClose                      s_NtClose;
static PFN_RtlNtStatusToDosError        s_RtlNtStatusToDosError;

static PFN_NtOpenEvent                  s_NtOpenEvent;
static PFN_NtCreateEvent                s_NtCreateEvent;
static PFN_NtSetEvent                   s_NtSetEvent;
static PFN_NtClearEvent                 s_NtClearEvent;
static PFN_NtResetEvent                 s_NtResetEvent;

static PFN_NtOpenMutant                 s_NtOpenMutant;
static PFN_NtCreateMutant               s_NtCreateMutant;
static PFN_NtReleaseMutant              s_NtReleaseMutant;

static PFN_NtOpenSemaphore              s_NtOpenSemaphore;
static PFN_NtCreateSemaphore            s_NtCreateSemaphore;
static PFN_NtReleaseSemaphore           s_NtReleaseSemaphore;

static PFN_NtOpenTimer                  s_NtOpenTimer;
static PFN_NtCreateTimer                s_NtCreateTimer;
static PFN_NtSetTimer                   s_NtSetTimer;
static PFN_NtCancelTimer                s_NtCancelTimer;

static PFN_NtOpenSection                s_NtOpenSection;
static PFN_NtCreateSection              s_NtCreateSection;

static PFN_NtOpenJobObject              s_NtOpenJobObject;
static PFN_NtCreateJobObject            s_NtCreateJobObject;

static bool s_ntApiReady = false;

template <typename T>
static T GetNtProc(HMODULE ntdll, const char* name)
{
    FARPROC proc = GetProcAddress(ntdll, name);
    return reinterpret_cast<T>(proc);
}

bool InitNtApi()
{
    if (s_ntApiReady) {
        return true;
    }
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (!ntdll) {
        return false;
    }

    s_NtOpenDirectoryObject      = GetNtProc<PFN_NtOpenDirectoryObject>(ntdll, "NtOpenDirectoryObject");
    s_NtCreateDirectoryObject    = GetNtProc<PFN_NtCreateDirectoryObject>(ntdll, "NtCreateDirectoryObject");
    s_NtQueryDirectoryObject     = GetNtProc<PFN_NtQueryDirectoryObject>(ntdll, "NtQueryDirectoryObject");
    s_NtOpenSymbolicLinkObject   = GetNtProc<PFN_NtOpenSymbolicLinkObject>(ntdll, "NtOpenSymbolicLinkObject");
    s_NtCreateSymbolicLinkObject = GetNtProc<PFN_NtCreateSymbolicLinkObject>(ntdll, "NtCreateSymbolicLinkObject");
    s_NtQuerySymbolicLinkObject  = GetNtProc<PFN_NtQuerySymbolicLinkObject>(ntdll, "NtQuerySymbolicLinkObject");
    s_NtMakeTemporaryObject      = GetNtProc<PFN_NtMakeTemporaryObject>(ntdll, "NtMakeTemporaryObject");
    s_NtClose                    = GetNtProc<PFN_NtClose>(ntdll, "NtClose");
    s_RtlNtStatusToDosError      = GetNtProc<PFN_RtlNtStatusToDosError>(ntdll, "RtlNtStatusToDosError");

    s_NtOpenEvent                = GetNtProc<PFN_NtOpenEvent>(ntdll, "NtOpenEvent");
    s_NtCreateEvent              = GetNtProc<PFN_NtCreateEvent>(ntdll, "NtCreateEvent");
    s_NtSetEvent                 = GetNtProc<PFN_NtSetEvent>(ntdll, "NtSetEvent");
    s_NtClearEvent               = GetNtProc<PFN_NtClearEvent>(ntdll, "NtClearEvent");
    s_NtResetEvent               = GetNtProc<PFN_NtResetEvent>(ntdll, "NtResetEvent");

    s_NtOpenMutant               = GetNtProc<PFN_NtOpenMutant>(ntdll, "NtOpenMutant");
    s_NtCreateMutant             = GetNtProc<PFN_NtCreateMutant>(ntdll, "NtCreateMutant");
    s_NtReleaseMutant            = GetNtProc<PFN_NtReleaseMutant>(ntdll, "NtReleaseMutant");

    s_NtOpenSemaphore            = GetNtProc<PFN_NtOpenSemaphore>(ntdll, "NtOpenSemaphore");
    s_NtCreateSemaphore          = GetNtProc<PFN_NtCreateSemaphore>(ntdll, "NtCreateSemaphore");
    s_NtReleaseSemaphore         = GetNtProc<PFN_NtReleaseSemaphore>(ntdll, "NtReleaseSemaphore");

    s_NtOpenTimer                = GetNtProc<PFN_NtOpenTimer>(ntdll, "NtOpenTimer");
    s_NtCreateTimer              = GetNtProc<PFN_NtCreateTimer>(ntdll, "NtCreateTimer");
    s_NtSetTimer                 = GetNtProc<PFN_NtSetTimer>(ntdll, "NtSetTimer");
    s_NtCancelTimer              = GetNtProc<PFN_NtCancelTimer>(ntdll, "NtCancelTimer");

    s_NtOpenSection              = GetNtProc<PFN_NtOpenSection>(ntdll, "NtOpenSection");
    s_NtCreateSection            = GetNtProc<PFN_NtCreateSection>(ntdll, "NtCreateSection");

    s_NtOpenJobObject            = GetNtProc<PFN_NtOpenJobObject>(ntdll, "NtOpenJobObject");
    s_NtCreateJobObject          = GetNtProc<PFN_NtCreateJobObject>(ntdll, "NtCreateJobObject");

    s_ntApiReady =
        s_NtOpenDirectoryObject      != NULL &&
        s_NtCreateDirectoryObject    != NULL &&
        s_NtQueryDirectoryObject     != NULL &&
        s_NtOpenSymbolicLinkObject   != NULL &&
        s_NtCreateSymbolicLinkObject != NULL &&
        s_NtQuerySymbolicLinkObject  != NULL &&
        s_NtMakeTemporaryObject      != NULL &&
        s_NtClose                    != NULL &&
        s_RtlNtStatusToDosError      != NULL;

    return s_ntApiReady;
}

NTSTATUS NtOpenDirectoryObject(HANDLE* handle, ACCESS_MASK access, NtObjectAttributes* attrs)
{
    return s_NtOpenDirectoryObject(handle, access, attrs);
}

NTSTATUS NtCreateDirectoryObject(HANDLE* handle, ACCESS_MASK access, NtObjectAttributes* attrs)
{
    return s_NtCreateDirectoryObject(handle, access, attrs);
}

NTSTATUS NtQueryDirectoryObject(HANDLE dir, void* buffer, ULONG length,
                                BOOLEAN single, BOOLEAN restart,
                                ULONG* context, ULONG* returned)
{
    return s_NtQueryDirectoryObject(dir, buffer, length, single, restart, context, returned);
}

NTSTATUS NtOpenSymbolicLinkObject(HANDLE* handle, ACCESS_MASK access, NtObjectAttributes* attrs)
{
    return s_NtOpenSymbolicLinkObject(handle, access, attrs);
}

NTSTATUS NtCreateSymbolicLinkObject(HANDLE* handle, ACCESS_MASK access,
                                    NtObjectAttributes* attrs, NtUnicodeString* target)
{
    return s_NtCreateSymbolicLinkObject(handle, access, attrs, target);
}

NTSTATUS NtQuerySymbolicLinkObject(HANDLE link, NtUnicodeString* target, ULONG* returned)
{
    return s_NtQuerySymbolicLinkObject(link, target, returned);
}

NTSTATUS NtMakeTemporaryObject(HANDLE handle)
{
    return s_NtMakeTemporaryObject(handle);
}

NTSTATUS NtOpenEvent(HANDLE* handle, ACCESS_MASK access, NtObjectAttributes* attrs)
{
    if (!s_NtOpenEvent) return NTOBJ_ST_UNSUCCESSFUL;
    return s_NtOpenEvent(handle, access, attrs);
}

NTSTATUS NtCreateEvent(HANDLE* handle, ACCESS_MASK access, NtObjectAttributes* attrs, ULONG eventType, BOOLEAN initialState)
{
    if (!s_NtCreateEvent) return NTOBJ_ST_UNSUCCESSFUL;
    return s_NtCreateEvent(handle, access, attrs, eventType, initialState);
}

NTSTATUS NtSetEvent(HANDLE handle, LONG* previousState)
{
    if (!s_NtSetEvent) return NTOBJ_ST_UNSUCCESSFUL;
    return s_NtSetEvent(handle, previousState);
}

NTSTATUS NtClearEvent(HANDLE handle)
{
    if (!s_NtClearEvent) return NTOBJ_ST_UNSUCCESSFUL;
    return s_NtClearEvent(handle);
}

NTSTATUS NtResetEvent(HANDLE handle, LONG* previousState)
{
    if (!s_NtResetEvent) return NTOBJ_ST_UNSUCCESSFUL;
    return s_NtResetEvent(handle, previousState);
}

NTSTATUS NtOpenMutant(HANDLE* handle, ACCESS_MASK access, NtObjectAttributes* attrs)
{
    if (!s_NtOpenMutant) return NTOBJ_ST_UNSUCCESSFUL;
    return s_NtOpenMutant(handle, access, attrs);
}

NTSTATUS NtCreateMutant(HANDLE* handle, ACCESS_MASK access, NtObjectAttributes* attrs, BOOLEAN initialOwner)
{
    if (!s_NtCreateMutant) return NTOBJ_ST_UNSUCCESSFUL;
    return s_NtCreateMutant(handle, access, attrs, initialOwner);
}

NTSTATUS NtReleaseMutant(HANDLE handle, LONG* previousCount)
{
    if (!s_NtReleaseMutant) return NTOBJ_ST_UNSUCCESSFUL;
    return s_NtReleaseMutant(handle, previousCount);
}

NTSTATUS NtOpenSemaphore(HANDLE* handle, ACCESS_MASK access, NtObjectAttributes* attrs)
{
    if (!s_NtOpenSemaphore) return NTOBJ_ST_UNSUCCESSFUL;
    return s_NtOpenSemaphore(handle, access, attrs);
}

NTSTATUS NtCreateSemaphore(HANDLE* handle, ACCESS_MASK access, NtObjectAttributes* attrs, LONG initialCount, LONG maximumCount)
{
    if (!s_NtCreateSemaphore) return NTOBJ_ST_UNSUCCESSFUL;
    return s_NtCreateSemaphore(handle, access, attrs, initialCount, maximumCount);
}

NTSTATUS NtReleaseSemaphore(HANDLE handle, LONG releaseCount, LONG* previousCount)
{
    if (!s_NtReleaseSemaphore) return NTOBJ_ST_UNSUCCESSFUL;
    return s_NtReleaseSemaphore(handle, releaseCount, previousCount);
}

NTSTATUS NtOpenTimer(HANDLE* handle, ACCESS_MASK access, NtObjectAttributes* attrs)
{
    if (!s_NtOpenTimer) return NTOBJ_ST_UNSUCCESSFUL;
    return s_NtOpenTimer(handle, access, attrs);
}

NTSTATUS NtCreateTimer(HANDLE* handle, ACCESS_MASK access, NtObjectAttributes* attrs, ULONG timerType)
{
    if (!s_NtCreateTimer) return NTOBJ_ST_UNSUCCESSFUL;
    return s_NtCreateTimer(handle, access, attrs, timerType);
}

NTSTATUS NtSetTimer(HANDLE handle, PLARGE_INTEGER dueTime, PVOID timerApcRoutine, PVOID timerContext, BOOLEAN resumeTimer, LONG period, BOOLEAN* previousState)
{
    if (!s_NtSetTimer) return NTOBJ_ST_UNSUCCESSFUL;
    return s_NtSetTimer(handle, dueTime, timerApcRoutine, timerContext, resumeTimer, period, previousState);
}

NTSTATUS NtCancelTimer(HANDLE handle, BOOLEAN* currentState)
{
    if (!s_NtCancelTimer) return NTOBJ_ST_UNSUCCESSFUL;
    return s_NtCancelTimer(handle, currentState);
}

NTSTATUS NtOpenSection(HANDLE* handle, ACCESS_MASK access, NtObjectAttributes* attrs)
{
    if (!s_NtOpenSection) return NTOBJ_ST_UNSUCCESSFUL;
    return s_NtOpenSection(handle, access, attrs);
}

NTSTATUS NtCreateSection(HANDLE* handle, ACCESS_MASK access, NtObjectAttributes* attrs, PLARGE_INTEGER maximumSize, ULONG sectionPageProtection, ULONG allocationAttributes, HANDLE fileHandle)
{
    if (!s_NtCreateSection) return NTOBJ_ST_UNSUCCESSFUL;
    return s_NtCreateSection(handle, access, attrs, maximumSize, sectionPageProtection, allocationAttributes, fileHandle);
}

NTSTATUS NtOpenJobObject(HANDLE* handle, ACCESS_MASK access, NtObjectAttributes* attrs)
{
    if (!s_NtOpenJobObject) return NTOBJ_ST_UNSUCCESSFUL;
    return s_NtOpenJobObject(handle, access, attrs);
}

NTSTATUS NtCreateJobObject(HANDLE* handle, ACCESS_MASK access, NtObjectAttributes* attrs)
{
    if (!s_NtCreateJobObject) return NTOBJ_ST_UNSUCCESSFUL;
    return s_NtCreateJobObject(handle, access, attrs);
}

void NtCloseSafe(HANDLE handle)
{
    if (handle != NULL && handle != INVALID_HANDLE_VALUE) {
        s_NtClose(handle);
    }
}

ULONG NtStatusToDosError(NTSTATUS status)
{
    return s_RtlNtStatusToDosError(status);
}

NtUnicodeString MakeUniString(const std::wstring& value)
{
    NtUnicodeString uni;
    uni.Length        = static_cast<USHORT>(value.size() * sizeof(WCHAR));
    uni.MaximumLength = static_cast<USHORT>((value.size() + 1) * sizeof(WCHAR));
    uni.Buffer        = const_cast<PWSTR>(value.c_str());
    return uni;
}

NtObjectAttributes MakeObjectAttributes(NtUnicodeString* name, ULONG attributes)
{
    NtObjectAttributes oa;
    oa.Length                 = sizeof(NtObjectAttributes);
    oa.RootDirectory          = NULL;
    oa.ObjectName             = name;
    oa.Attributes             = attributes;
    oa.SecurityDescriptor     = NULL;
    oa.SecurityQualityOfService = NULL;
    return oa;
}

/* ---------------------------------------------------------------------------
 * Status name table
 * ------------------------------------------------------------------------- */
struct NtStatusEntry {
    NTSTATUS      Status;
    const wchar_t* Name;
};

static const NtStatusEntry g_statusNames[] = {
    { NTOBJ_ST_SUCCESS,                 L"STATUS_SUCCESS" },
    { NTOBJ_ST_UNSUCCESSFUL,            L"STATUS_UNSUCCESSFUL" },
    { NTOBJ_ST_INVALID_HANDLE,          L"STATUS_INVALID_HANDLE" },
    { NTOBJ_ST_INVALID_PARAMETER,       L"STATUS_INVALID_PARAMETER" },
    { NTOBJ_ST_ACCESS_DENIED,           L"STATUS_ACCESS_DENIED" },
    { NTOBJ_ST_OBJECT_TYPE_MISMATCH,    L"STATUS_OBJECT_TYPE_MISMATCH" },
    { NTOBJ_ST_BUFFER_TOO_SMALL,        L"STATUS_BUFFER_TOO_SMALL" },
    { NTOBJ_ST_OBJECT_NAME_NOT_FOUND,   L"STATUS_OBJECT_NAME_NOT_FOUND" },
    { NTOBJ_ST_OBJECT_NAME_COLLISION,   L"STATUS_OBJECT_NAME_COLLISION" },
    { NTOBJ_ST_OBJECT_PATH_NOT_FOUND,   L"STATUS_OBJECT_PATH_NOT_FOUND" },
    { NTOBJ_ST_PRIVILEGE_NOT_HELD,      L"STATUS_PRIVILEGE_NOT_HELD" },
    { NTOBJ_ST_INSUFFICIENT_RESOURCES,  L"STATUS_INSUFFICIENT_RESOURCES" },
    { NTOBJ_ST_DELETE_PENDING,          L"STATUS_DELETE_PENDING" },
    { NTOBJ_ST_NO_MORE_ENTRIES,         L"STATUS_NO_MORE_ENTRIES" },
    { (NTSTATUS)0xC0000004L,            L"STATUS_INFO_LENGTH_MISMATCH" },
    { (NTSTATUS)0xC000000FL,            L"STATUS_INVALID_DEVICE_REQUEST" },
    { (NTSTATUS)0xC0000033L,            L"STATUS_OBJECT_NAME_INVALID" },
    { (NTSTATUS)0xC00000ACL,            L"STATUS_DIRECTORY_NOT_EMPTY" },
    { (NTSTATUS)0xC00000BBL,            L"STATUS_NOT_A_DIRECTORY" },
    { (NTSTATUS)0xC000010AL,            L"STATUS_CANNOT_DELETE" },
    { (NTSTATUS)0x80000005L,            L"STATUS_BUFFER_OVERFLOW" },
    { (NTSTATUS)0x00000103L,            L"STATUS_PENDING" },
};

const wchar_t* NtStatusName(NTSTATUS status)
{
    if (NTOBJ_IS_WIN32_ERR(status)) {
        return L"WIN32_ERROR";
    }
    const int count = sizeof(g_statusNames) / sizeof(g_statusNames[0]);
    for (int i = 0; i < count; ++i) {
        if (g_statusNames[i].Status == status) {
            return g_statusNames[i].Name;
        }
    }
    return L"UNKNOWN_STATUS";
}

/* ---------------------------------------------------------------------------
 * Human-readable diagnostics
 * ------------------------------------------------------------------------- */
static void AppendDosMessage(std::wstring& into, DWORD code, HMODULE fromModule)
{
    WCHAR msg[512];
    DWORD flags = FORMAT_MESSAGE_IGNORE_INSERTS;
    if (fromModule != NULL) {
        flags |= FORMAT_MESSAGE_FROM_HMODULE;
    } else {
        flags |= FORMAT_MESSAGE_FROM_SYSTEM;
    }
    DWORD len = FormatMessageW(flags, fromModule, code,
                               MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                               msg, 512, NULL);
    if (len > 0) {
        /* trim trailing CR/LF/spaces */
        while (len > 0 && (msg[len - 1] == L'\r' || msg[len - 1] == L'\n' ||
                           msg[len - 1] == L' ')) {
            msg[--len] = 0;
        }
        if (len > 0) {
            into += L" - ";
            into += msg;
        }
    }
}

void PrintWin32Error(const std::wstring& operation, DWORD error)
{
    std::wstring text = operation;
    text += L": FAILED (Win32 error ";
    text += std::to_wstring(static_cast<unsigned long>(error));
    text += L")";
    AppendDosMessage(text, error, NULL);
    std::wcout << text << L"\n";
}

void PrintStatus(const std::wstring& operation, NTSTATUS status)
{
    if (status >= 0) {
        std::wstring text = operation;
        text += L": OK";
        std::wcout << text << L"\n";
        return;
    }

    std::wstring text = operation;
    wchar_t hex[16];
    swprintf(hex, 16, L"%08X", static_cast<unsigned long>(status));
    text += L": FAILED [0x";
    text += hex;
    text += L" ";
    text += NtStatusName(status);
    text += L"]";

    if (NTOBJ_IS_WIN32_ERR(status)) {
        AppendDosMessage(text, NTOBJ_WIN32_CODE(status), NULL);
    } else {
        ULONG dos = NtStatusToDosError(status);
        if (dos != 0) {
            AppendDosMessage(text, dos, NULL);
        }
    }
    std::wcout << text << L"\n";
}

// [v1.1 resync 2026-10-23] current build-fix version
