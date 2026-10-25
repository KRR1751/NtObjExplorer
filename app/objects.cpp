/*
 * ntobj.exe - unified namespace operations (implementation).
 */
#include "objects.h"
#include "drvio.h"
#include "ntapi.h"
#include <cstring>

static BackendMode g_mode = BackendMode::Auto;
static bool g_driverDetected = false;

void SetBackendMode(BackendMode mode)
{
    g_mode = mode;
}

BackendMode GetBackendMode()
{
    return g_mode;
}

const wchar_t* GetBackendModeName()
{
    switch (g_mode) {
    case BackendMode::Driver: return L"driver";
    case BackendMode::Direct: return L"direct";
    default:                  return L"auto";
    }
}

bool DriverActive()
{
    if (g_mode == BackendMode::Direct) {
        return false;
    }
    if (g_mode == BackendMode::Driver) {
        return OpenDriver();
    }
    /* Auto: use the driver whenever it is reachable. */
    return g_driverDetected && OpenDriver();
}

bool DetectDriverAvailable()
{
    ULONG version = 0;
    g_driverDetected = DetectDriver(&version);
    return g_driverDetected;
}

bool DriverWasDetected()
{
    return g_driverDetected;
}

/* ---------------------------------------------------------------------------
 * Direct (user-mode) implementations
 * ------------------------------------------------------------------------- */

/* Buffer for one ZwQueryDirectoryObject entry. The union forces 8-byte
 * alignment for the pointer fields of the embedded UNICODE_STRINGs. */
union NtQueryBuffer {
    unsigned char bytes[2048];
    void*         alignment;
};

static NTSTATUS DirectList(const std::wstring& path, std::vector<NtObjEntry>& entries, bool* truncated)
{
    entries.clear();
    if (truncated != NULL) {
        *truncated = false;
    }

    NtUnicodeString uni = MakeUniString(path);
    NtObjectAttributes oa = MakeObjectAttributes(&uni, NTOBJ_OBJ_CASE_INSENSITIVE);

    HANDLE dir = NULL;
    NTSTATUS st = NtOpenDirectoryObject(&dir, NTOBJ_DIR_QUERY, &oa);
    if (!NT_SUCCESS(st)) {
        return st;
    }

    ULONG context = 0;
    BOOLEAN restart = TRUE;
    for (;;) {
        NtQueryBuffer qb;
        memset(qb.bytes, 0, sizeof(qb.bytes));
        ULONG returned = 0;
        st = NtQueryDirectoryObject(dir, qb.bytes, sizeof(qb.bytes), TRUE, restart, &context, &returned);
        restart = FALSE;
        if (st == NTOBJ_ST_NO_MORE_ENTRIES) {
            st = NTOBJ_ST_SUCCESS;
            break;
        }
        if (!NT_SUCCESS(st)) {
            break;
        }

        const NtDirectoryInfo* info = reinterpret_cast<const NtDirectoryInfo*>(qb.bytes);
        if (info->ObjectName.Buffer == NULL || info->ObjectName.Length == 0) {
            st = NTOBJ_ST_SUCCESS;
            break;
        }

        NtObjEntry entry;
        entry.Name.assign(info->ObjectName.Buffer, info->ObjectName.Length / sizeof(WCHAR));
        entry.Type.assign(info->ObjectTypeName.Buffer, info->ObjectTypeName.Length / sizeof(WCHAR));
        entries.push_back(entry);
    }

    NtCloseSafe(dir);
    return st;
}

static NTSTATUS DirectMakeDirectory(const std::wstring& path)
{
    NtUnicodeString uni = MakeUniString(path);
    NtObjectAttributes oa = MakeObjectAttributes(&uni,
                                                 NTOBJ_OBJ_CASE_INSENSITIVE | NTOBJ_OBJ_PERMANENT);
    HANDLE handle = NULL;
    NTSTATUS st = NtCreateDirectoryObject(&handle, NTOBJ_DIR_ALL_ACCESS, &oa);
    if (NT_SUCCESS(st)) {
        NtCloseSafe(handle);
    }
    return st;
}

static NTSTATUS DirectMakeSymlink(const std::wstring& link, const std::wstring& target)
{
    NtUnicodeString uni = MakeUniString(link);
    NtUnicodeString tgt = MakeUniString(target);
    NtObjectAttributes oa = MakeObjectAttributes(&uni,
                                                 NTOBJ_OBJ_CASE_INSENSITIVE | NTOBJ_OBJ_PERMANENT);
    HANDLE handle = NULL;
    NTSTATUS st = NtCreateSymbolicLinkObject(&handle, NTOBJ_LINK_ALL_ACCESS, &oa, &tgt);
    if (NT_SUCCESS(st)) {
        NtCloseSafe(handle);
    }
    return st;
}

static NTSTATUS DirectQuerySymlink(const std::wstring& path, std::wstring& target)
{
    target.clear();
    NtUnicodeString uni = MakeUniString(path);
    NtObjectAttributes oa = MakeObjectAttributes(&uni,
                                                 NTOBJ_OBJ_CASE_INSENSITIVE | NTOBJ_OBJ_OPENLINK);
    HANDLE handle = NULL;
    NTSTATUS st = NtOpenSymbolicLinkObject(&handle, NTOBJ_LINK_QUERY, &oa);
    if (!NT_SUCCESS(st)) {
        return st;
    }

    WCHAR buf[NTOBJ_MAX_TARGET];
    memset(buf, 0, sizeof(buf));
    NtUnicodeString tgt;
    tgt.Length        = 0;
    tgt.MaximumLength = sizeof(buf);
    tgt.Buffer        = buf;

    st = NtQuerySymbolicLinkObject(handle, &tgt, NULL);
    NtCloseSafe(handle);
    if (NT_SUCCESS(st)) {
        ULONG chars = tgt.Length / sizeof(WCHAR);
        if (chars > NTOBJ_MAX_TARGET - 1) {
            chars = NTOBJ_MAX_TARGET - 1;
        }
        target.assign(buf, chars);
    }
    return st;
}

/* Open-and-make-temporary. Symbolic links are opened with OBJ_OPENLINK so a
 * link delete does not touch its target; directories and other objects are opened normally. */
static NTSTATUS DirectDeleteObject(const std::wstring& path)
{
    HANDLE handle = NULL;
    NTSTATUS st;

    NtUnicodeString uni = MakeUniString(path);
    NtObjectAttributes oaLink = MakeObjectAttributes(&uni, NTOBJ_OBJ_CASE_INSENSITIVE | NTOBJ_OBJ_OPENLINK);
    NtObjectAttributes oa = MakeObjectAttributes(&uni, NTOBJ_OBJ_CASE_INSENSITIVE);

    /* 1) Symbolic link */
    st = NtOpenSymbolicLinkObject(&handle, DELETE, &oaLink);
    if (NT_SUCCESS(st)) {
        NTSTATUS tmp = NtMakeTemporaryObject(handle);
        NtCloseSafe(handle);
        return tmp;
    }

    /* 2) Directory */
    st = NtOpenDirectoryObject(&handle, DELETE, &oa);
    if (NT_SUCCESS(st)) {
        NTSTATUS tmp = NtMakeTemporaryObject(handle);
        NtCloseSafe(handle);
        return tmp;
    }

    /* 3) Event */
    st = NtOpenEvent(&handle, DELETE, &oa);
    if (NT_SUCCESS(st)) {
        NTSTATUS tmp = NtMakeTemporaryObject(handle);
        NtCloseSafe(handle);
        return tmp;
    }

    /* 4) Mutant */
    st = NtOpenMutant(&handle, DELETE, &oa);
    if (NT_SUCCESS(st)) {
        NTSTATUS tmp = NtMakeTemporaryObject(handle);
        NtCloseSafe(handle);
        return tmp;
    }

    /* 5) Semaphore */
    st = NtOpenSemaphore(&handle, DELETE, &oa);
    if (NT_SUCCESS(st)) {
        NTSTATUS tmp = NtMakeTemporaryObject(handle);
        NtCloseSafe(handle);
        return tmp;
    }

    /* 6) Timer */
    st = NtOpenTimer(&handle, DELETE, &oa);
    if (NT_SUCCESS(st)) {
        NTSTATUS tmp = NtMakeTemporaryObject(handle);
        NtCloseSafe(handle);
        return tmp;
    }

    /* 7) Section */
    st = NtOpenSection(&handle, DELETE, &oa);
    if (NT_SUCCESS(st)) {
        NTSTATUS tmp = NtMakeTemporaryObject(handle);
        NtCloseSafe(handle);
        return tmp;
    }

    /* 8) Job */
    st = NtOpenJobObject(&handle, DELETE, &oa);
    if (NT_SUCCESS(st)) {
        NTSTATUS tmp = NtMakeTemporaryObject(handle);
        NtCloseSafe(handle);
        return tmp;
    }

    return st;
}

static NTSTATUS DirectCreateObject(const std::wstring& path, ULONG kind, ULONG param1)
{
    NtUnicodeString uni = MakeUniString(path);
    NtObjectAttributes oa = MakeObjectAttributes(&uni, NTOBJ_OBJ_CASE_INSENSITIVE | NTOBJ_OBJ_PERMANENT);
    HANDLE handle = NULL;
    NTSTATUS st = NTOBJ_ST_INVALID_PARAMETER;

    switch (kind) {
    case NTOBJ_KIND_EVENT:
    {
        ULONG type = (param1 == 0) ? 0 /* NotificationEvent */ : 1 /* SynchronizationEvent */;
        st = NtCreateEvent(&handle, EVENT_ALL_ACCESS, &oa, type, FALSE);
        break;
    }
    case NTOBJ_KIND_MUTANT:
        st = NtCreateMutant(&handle, MUTANT_ALL_ACCESS, &oa, FALSE);
        break;
    case NTOBJ_KIND_SEMAPHORE:
    {
        LONG limit = (param1 != 0) ? static_cast<LONG>(param1) : 1;
        st = NtCreateSemaphore(&handle, SEMAPHORE_ALL_ACCESS, &oa, 0, limit);
        break;
    }
    case NTOBJ_KIND_TIMER:
        st = NtCreateTimer(&handle, TIMER_ALL_ACCESS, &oa, 0 /* NotificationTimer */);
        break;
    case NTOBJ_KIND_SECTION:
    {
        LARGE_INTEGER size;
        size.QuadPart = (param1 != 0) ? static_cast<LONGLONG>(param1) : 65536LL;
        st = NtCreateSection(&handle, SECTION_ALL_ACCESS, &oa, &size, PAGE_READWRITE, SEC_COMMIT, NULL);
        break;
    }
    case NTOBJ_KIND_JOB:
        st = NtCreateJobObject(&handle, JOB_OBJECT_ALL_ACCESS, &oa);
        break;
    default:
        st = NTOBJ_ST_INVALID_PARAMETER;
        break;
    }

    if (NT_SUCCESS(st)) {
        NtCloseSafe(handle);
    }
    return st;
}

static NTSTATUS DirectSetState(const std::wstring& path, ULONG kind, ULONG action, ULONG value)
{
    NtUnicodeString uni = MakeUniString(path);
    NtObjectAttributes oa = MakeObjectAttributes(&uni, NTOBJ_OBJ_CASE_INSENSITIVE);
    HANDLE handle = NULL;
    NTSTATUS st;

    switch (kind) {
    case NTOBJ_KIND_EVENT:
        st = NtOpenEvent(&handle, EVENT_MODIFY_STATE, &oa);
        if (!NT_SUCCESS(st)) {
            return st;
        }
        if (action == NTOBJ_ACTION_SIGNAL) {
            st = NtSetEvent(handle, NULL);
        } else if (action == NTOBJ_ACTION_CLEAR) {
            st = NtClearEvent(handle);
        } else {
            st = NTOBJ_ST_INVALID_PARAMETER;
        }
        NtCloseSafe(handle);
        return st;

    case NTOBJ_KIND_MUTANT:
        st = NtOpenMutant(&handle, MUTANT_ALL_ACCESS, &oa);
        if (!NT_SUCCESS(st)) {
            return st;
        }
        if (action == NTOBJ_ACTION_RELEASE) {
            LONG prev = 0;
            st = NtReleaseMutant(handle, &prev);
        } else {
            st = NTOBJ_ST_INVALID_PARAMETER;
        }
        NtCloseSafe(handle);
        return st;

    case NTOBJ_KIND_SEMAPHORE:
        st = NtOpenSemaphore(&handle, SEMAPHORE_MODIFY_STATE, &oa);
        if (!NT_SUCCESS(st)) {
            return st;
        }
        if (action == NTOBJ_ACTION_RELEASE) {
            LONG prev = 0;
            st = NtReleaseSemaphore(handle, value ? static_cast<LONG>(value) : 1, &prev);
        } else {
            st = NTOBJ_ST_INVALID_PARAMETER;
        }
        NtCloseSafe(handle);
        return st;

    case NTOBJ_KIND_TIMER:
        st = NtOpenTimer(&handle, TIMER_MODIFY_STATE, &oa);
        if (!NT_SUCCESS(st)) {
            return st;
        }
        if (action == NTOBJ_ACTION_SIGNAL) {
            LARGE_INTEGER dueTime;
            dueTime.QuadPart = (value != 0) ? -10000LL * static_cast<LONGLONG>(value) : -1LL;
            st = NtSetTimer(handle, &dueTime, NULL, NULL, FALSE, 0, NULL);
        } else if (action == NTOBJ_ACTION_CLEAR) {
            BOOLEAN cur = FALSE;
            st = NtCancelTimer(handle, &cur);
        } else {
            st = NTOBJ_ST_INVALID_PARAMETER;
        }
        NtCloseSafe(handle);
        return st;

    default:
        return NTOBJ_ST_INVALID_PARAMETER;
    }
}

/* ---------------------------------------------------------------------------
 * Unified operations
 * ------------------------------------------------------------------------- */

NTSTATUS ObjList(const std::wstring& path, std::vector<NtObjEntry>& entries, bool* truncated)
{
    if (DriverActive()) {
        return DrvList(path, entries, truncated);
    }
    return DirectList(path, entries, truncated);
}

NTSTATUS ObjMakeDirectory(const std::wstring& path)
{
    if (DriverActive()) {
        return DrvMakeDirectory(path);
    }
    return DirectMakeDirectory(path);
}

NTSTATUS ObjDeleteObject(const std::wstring& path)
{
    if (DriverActive()) {
        return DrvDeleteObject(path);
    }
    return DirectDeleteObject(path);
}

NTSTATUS ObjMakeSymlink(const std::wstring& link, const std::wstring& target)
{
    if (DriverActive()) {
        return DrvMakeSymlink(link, target);
    }
    return DirectMakeSymlink(link, target);
}

NTSTATUS ObjQuerySymlink(const std::wstring& path, std::wstring& target)
{
    if (DriverActive()) {
        return DrvQuerySymlink(path, target);
    }
    return DirectQuerySymlink(path, target);
}

NTSTATUS ObjCreateObject(const std::wstring& path, ULONG kind, ULONG param1)
{
    if (DriverActive()) {
        return DrvCreateObject(path, kind, param1);
    }
    return DirectCreateObject(path, kind, param1);
}

NTSTATUS ObjSetState(const std::wstring& path, ULONG kind, ULONG action, ULONG value)
{
    if (DriverActive()) {
        return DrvSetState(path, kind, action, value);
    }
    return DirectSetState(path, kind, action, value);
}

NTSTATUS ObjSetSymlinkTarget(const std::wstring& link, const std::wstring& newTarget)
{
    ObjDeleteObject(link);
    return ObjMakeSymlink(link, newTarget);
}

NTSTATUS ObjDetectKind(const std::wstring& path, ULONG* outKind, std::wstring* outTypeName)
{
    if (outKind) *outKind = 0xFFFFFFFFUL;
    if (outTypeName) outTypeName->clear();

    if (path == L"\\") {
        if (outTypeName) *outTypeName = L"Directory";
        return NTOBJ_ST_SUCCESS;
    }

    NtUnicodeString uni = MakeUniString(path);
    HANDLE handle = NULL;
    NTSTATUS st;

    /* 1) Try SymbolicLink with OBJ_OPENLINK */
    NtObjectAttributes oaLink = MakeObjectAttributes(&uni, NTOBJ_OBJ_CASE_INSENSITIVE | NTOBJ_OBJ_OPENLINK);
    st = NtOpenSymbolicLinkObject(&handle, NTOBJ_LINK_QUERY, &oaLink);
    if (NT_SUCCESS(st)) {
        NtCloseSafe(handle);
        if (outTypeName) *outTypeName = L"SymbolicLink";
        return NTOBJ_ST_SUCCESS;
    }

    /* 2) Try Directory */
    NtObjectAttributes oa = MakeObjectAttributes(&uni, NTOBJ_OBJ_CASE_INSENSITIVE);
    st = NtOpenDirectoryObject(&handle, NTOBJ_DIR_QUERY, &oa);
    if (NT_SUCCESS(st)) {
        NtCloseSafe(handle);
        if (outTypeName) *outTypeName = L"Directory";
        return NTOBJ_ST_SUCCESS;
    }

    /* 3) Try Event */
    st = NtOpenEvent(&handle, EVENT_MODIFY_STATE, &oa);
    if (NT_SUCCESS(st)) {
        NtCloseSafe(handle);
        if (outKind) *outKind = NTOBJ_KIND_EVENT;
        if (outTypeName) *outTypeName = L"Event";
        return NTOBJ_ST_SUCCESS;
    }

    /* 4) Try Semaphore */
    st = NtOpenSemaphore(&handle, SEMAPHORE_MODIFY_STATE, &oa);
    if (NT_SUCCESS(st)) {
        NtCloseSafe(handle);
        if (outKind) *outKind = NTOBJ_KIND_SEMAPHORE;
        if (outTypeName) *outTypeName = L"Semaphore";
        return NTOBJ_ST_SUCCESS;
    }

    /* 5) Try Mutant */
    st = NtOpenMutant(&handle, MUTANT_ALL_ACCESS, &oa);
    if (NT_SUCCESS(st)) {
        NtCloseSafe(handle);
        if (outKind) *outKind = NTOBJ_KIND_MUTANT;
        if (outTypeName) *outTypeName = L"Mutant";
        return NTOBJ_ST_SUCCESS;
    }

    /* 6) Try Timer */
    st = NtOpenTimer(&handle, TIMER_MODIFY_STATE, &oa);
    if (NT_SUCCESS(st)) {
        NtCloseSafe(handle);
        if (outKind) *outKind = NTOBJ_KIND_TIMER;
        if (outTypeName) *outTypeName = L"Timer";
        return NTOBJ_ST_SUCCESS;
    }

    /* 7) Try Section */
    st = NtOpenSection(&handle, SECTION_MAP_READ, &oa);
    if (NT_SUCCESS(st)) {
        NtCloseSafe(handle);
        if (outKind) *outKind = NTOBJ_KIND_SECTION;
        if (outTypeName) *outTypeName = L"Section";
        return NTOBJ_ST_SUCCESS;
    }

    /* 8) Try Job */
    st = NtOpenJobObject(&handle, JOB_OBJECT_QUERY, &oa);
    if (NT_SUCCESS(st)) {
        NtCloseSafe(handle);
        if (outKind) *outKind = NTOBJ_KIND_JOB;
        if (outTypeName) *outTypeName = L"Job";
        return NTOBJ_ST_SUCCESS;
    }

    /* Fallback: ask parent directory listing */
    size_t cut = path.find_last_of(L'\\');
    std::wstring parent = (cut == 0) ? L"\\" : path.substr(0, cut);
    std::wstring name = path.substr(cut + 1);
    if (!name.empty()) {
        std::vector<NtObjEntry> entries;
        bool truncated = false;
        if (NT_SUCCESS(ObjList(parent, entries, &truncated))) {
            for (size_t i = 0; i < entries.size(); ++i) {
                if (_wcsicmp(entries[i].Name.c_str(), name.c_str()) == 0) {
                    if (outTypeName) *outTypeName = entries[i].Type;
                    if (entries[i].Type == L"Event" && outKind) *outKind = NTOBJ_KIND_EVENT;
                    else if (entries[i].Type == L"Mutant" && outKind) *outKind = NTOBJ_KIND_MUTANT;
                    else if (entries[i].Type == L"Semaphore" && outKind) *outKind = NTOBJ_KIND_SEMAPHORE;
                    else if (entries[i].Type == L"Timer" && outKind) *outKind = NTOBJ_KIND_TIMER;
                    else if (entries[i].Type == L"Section" && outKind) *outKind = NTOBJ_KIND_SECTION;
                    else if (entries[i].Type == L"Job" && outKind) *outKind = NTOBJ_KIND_JOB;
                    return NTOBJ_ST_SUCCESS;
                }
            }
        }
    }

    return NTOBJ_ST_OBJECT_NAME_NOT_FOUND;
}

NTSTATUS ObjListTypes(std::vector<NtObjEntry>& types)
{
    bool truncated = false;
    return ObjList(L"\\ObjectTypes", types, &truncated);
}

typedef HWINSTA (WINAPI* PFN_CreateWindowStationW)(LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES);
typedef HDESK   (WINAPI* PFN_CreateDesktopW)(LPCWSTR, LPCWSTR, LPDEVMODEW, DWORD, DWORD, LPSECURITY_ATTRIBUTES);

NTSTATUS ObjCreateWindowStation(const std::wstring& name)
{
    HMODULE u32 = LoadLibraryW(L"user32.dll");
    if (!u32) {
        return NTOBJ_MAKE_WIN32(GetLastError());
    }
    PFN_CreateWindowStationW pfn = reinterpret_cast<PFN_CreateWindowStationW>(GetProcAddress(u32, "CreateWindowStationW"));
    if (!pfn) {
        FreeLibrary(u32);
        return NTOBJ_ST_UNSUCCESSFUL;
    }
    HWINSTA hw = pfn(name.c_str(), 0, WINSTA_ALL_ACCESS, NULL);
    if (!hw) {
        DWORD err = GetLastError();
        FreeLibrary(u32);
        return NTOBJ_MAKE_WIN32(err);
    }
    FreeLibrary(u32);
    return NTOBJ_ST_SUCCESS;
}

NTSTATUS ObjCreateDesktop(const std::wstring& name)
{
    HMODULE u32 = LoadLibraryW(L"user32.dll");
    if (!u32) {
        return NTOBJ_MAKE_WIN32(GetLastError());
    }
    PFN_CreateDesktopW pfn = reinterpret_cast<PFN_CreateDesktopW>(GetProcAddress(u32, "CreateDesktopW"));
    if (!pfn) {
        FreeLibrary(u32);
        return NTOBJ_ST_UNSUCCESSFUL;
    }
    HDESK hd = pfn(name.c_str(), NULL, NULL, 0, GENERIC_ALL, NULL);
    if (!hd) {
        DWORD err = GetLastError();
        FreeLibrary(u32);
        return NTOBJ_MAKE_WIN32(err);
    }
    FreeLibrary(u32);
    return NTOBJ_ST_SUCCESS;
}

// [v1.1 resync 2026-10-23] current build-fix version
