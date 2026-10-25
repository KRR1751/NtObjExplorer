/*
 * ntobj.exe - transport layer to the NtObjDrv kernel driver (implementation).
 *
 * Every request struct is copied into a fixed-size buffer and sent with
 * DeviceIoControl (METHOD_BUFFERED). The driver fills the reply and returns
 * its NTSTATUS inside the reply; DeviceIoControl-level failures are mapped
 * to synthetic statuses (NTOBJ_ST_WIN32_BASE | win32 error).
 */
#include "drvio.h"
#include <cstring>

static HANDLE g_device = NULL;

bool OpenDriver()
{
    if (g_device != NULL) {
        return true;
    }
    HANDLE h = CreateFileW(NTOBJ_DEVICE_NAME_DOS,
                           GENERIC_READ | GENERIC_WRITE,
                           0, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        return false;
    }
    g_device = h;
    return true;
}

void CloseDriver()
{
    if (g_device != NULL) {
        CloseHandle(g_device);
        g_device = NULL;
    }
}

bool DriverIsOpen()
{
    return g_device != NULL;
}

bool DetectDriver(ULONG* versionOut)
{
    if (!OpenDriver()) {
        return false;
    }
    NTOBJ_PING_REPLY reply;
    ZeroMemory(&reply, sizeof(reply));
    DWORD returned = 0;
    if (!DeviceIoControl(g_device, IOCTL_NTOBJ_PING, NULL, 0,
                         &reply, sizeof(reply), &returned, NULL)) {
        CloseDriver();
        return false;
    }
    if (returned < sizeof(reply) || !NT_SUCCESS(reply.Status)) {
        CloseDriver();
        return false;
    }
    if (versionOut != NULL) {
        *versionOut = reply.Version;
    }
    return true;
}

/* Copy a std::wstring into a fixed WCHAR buffer, enforcing NUL termination.
 * Returns false when the value is too long or does not start with '\'. */
static bool FillPathBuffer(WCHAR* dst, size_t maxChars, const std::wstring& value)
{
    if (value.empty() || value[0] != L'\\' || value.size() + 1 > maxChars) {
        return false;
    }
    memcpy(dst, value.c_str(), (value.size() + 1) * sizeof(WCHAR));
    return true;
}

static NTSTATUS CallIoctl(ULONG code, void* in, ULONG inSize, void* out, ULONG outSize)
{
    if (!OpenDriver()) {
        return NTOBJ_MAKE_WIN32(ERROR_FILE_NOT_FOUND);
    }
    DWORD returned = 0;
    if (!DeviceIoControl(g_device, code, in, inSize, out, outSize, &returned, NULL)) {
        return NTOBJ_MAKE_WIN32(GetLastError());
    }
    return NTOBJ_ST_SUCCESS;
}

NTSTATUS DrvList(const std::wstring& path, std::vector<NtObjEntry>& entries, bool* truncated)
{
    entries.clear();
    if (truncated != NULL) {
        *truncated = false;
    }

    NTOBJ_PATH_REQ req;
    ZeroMemory(&req, sizeof(req));
    if (!FillPathBuffer(req.Path, NTOBJ_MAX_PATH, path)) {
        return NTOBJ_ST_INVALID_PARAMETER;
    }

    std::vector<WCHAR> raw(sizeof(NTOBJ_LIST_REPLY) / sizeof(WCHAR), 0);
    NTSTATUS st = CallIoctl(IOCTL_NTOBJ_LIST, &req, sizeof(req), raw.data(),
                            static_cast<ULONG>(raw.size() * sizeof(WCHAR)));
    if (!NT_SUCCESS(st)) {
        return st;
    }

    const NTOBJ_LIST_REPLY* reply = reinterpret_cast<const NTOBJ_LIST_REPLY*>(raw.data());
    if (!NT_SUCCESS(reply->Status)) {
        return reply->Status;
    }

    ULONG count = reply->Count;
    if (count > NTOBJ_MAX_ENTRIES) {
        count = NTOBJ_MAX_ENTRIES;
    }
    entries.reserve(count);
    for (ULONG i = 0; i < count; ++i) {
        const WCHAR* row = reply->Entries[i];
        NtObjEntry entry;
        entry.Name = std::wstring(row);                  /* NUL-terminated by the driver */
        entry.Type = std::wstring(row + NTOBJ_MAX_NAME); /* NUL-terminated by the driver */
        if (!entry.Name.empty()) {
            entries.push_back(entry);
        }
    }
    if (truncated != NULL) {
        *truncated = reply->Truncated != 0;
    }
    return reply->Status;
}

NTSTATUS DrvMakeDirectory(const std::wstring& path)
{
    NTOBJ_PATH_REQ req;
    ZeroMemory(&req, sizeof(req));
    if (!FillPathBuffer(req.Path, NTOBJ_MAX_PATH, path)) {
        return NTOBJ_ST_INVALID_PARAMETER;
    }
    return CallIoctl(IOCTL_NTOBJ_CREATE_DIR, &req, sizeof(req), NULL, 0);
}

NTSTATUS DrvDeleteObject(const std::wstring& path)
{
    NTOBJ_DELETE_REQ req;
    ZeroMemory(&req, sizeof(req));
    if (!FillPathBuffer(req.Path, NTOBJ_MAX_PATH, path)) {
        return NTOBJ_ST_INVALID_PARAMETER;
    }
    NTOBJ_PING_REPLY reply;
    ZeroMemory(&reply, sizeof(reply));
    NTSTATUS st = CallIoctl(IOCTL_NTOBJ_DELETE, &req, sizeof(req), &reply, sizeof(reply));
    if (!NT_SUCCESS(st)) {
        return st;
    }
    return reply.Status;
}

NTSTATUS DrvMakeSymlink(const std::wstring& link, const std::wstring& target)
{
    NTOBJ_SYMLINK_REQ req;
    ZeroMemory(&req, sizeof(req));
    if (!FillPathBuffer(req.LinkPath, NTOBJ_MAX_PATH, link)) {
        return NTOBJ_ST_INVALID_PARAMETER;
    }
    if (target.empty() || target.size() + 1 > NTOBJ_MAX_TARGET) {
        return NTOBJ_ST_INVALID_PARAMETER;
    }
    memcpy(req.Target, target.c_str(), (target.size() + 1) * sizeof(WCHAR));

    NTOBJ_PING_REPLY reply;
    ZeroMemory(&reply, sizeof(reply));
    NTSTATUS st = CallIoctl(IOCTL_NTOBJ_CREATE_SYMLINK, &req, sizeof(req), &reply, sizeof(reply));
    if (!NT_SUCCESS(st)) {
        return st;
    }
    return reply.Status;
}

NTSTATUS DrvQuerySymlink(const std::wstring& path, std::wstring& target)
{
    target.clear();
    NTOBJ_PATH_REQ req;
    ZeroMemory(&req, sizeof(req));
    if (!FillPathBuffer(req.Path, NTOBJ_MAX_PATH, path)) {
        return NTOBJ_ST_INVALID_PARAMETER;
    }

    NTOBJ_TARGET_REPLY reply;
    ZeroMemory(&reply, sizeof(reply));
    NTSTATUS st = CallIoctl(IOCTL_NTOBJ_QUERY_SYMLINK, &req, sizeof(req), &reply, sizeof(reply));
    if (!NT_SUCCESS(st)) {
        return st;
    }
    if (!NT_SUCCESS(reply.Status)) {
        return reply.Status;
    }
    reply.Target[NTOBJ_MAX_TARGET - 1] = 0;
    target = reply.Target;
    return reply.Status;
}

NTSTATUS DrvCreateObject(const std::wstring& path, ULONG kind, ULONG param1)
{
    NTOBJ_MKOBJ_REQ req;
    ZeroMemory(&req, sizeof(req));
    req.Kind   = kind;
    req.Param1 = param1;
    if (!FillPathBuffer(req.Path, NTOBJ_MAX_PATH, path)) {
        return NTOBJ_ST_INVALID_PARAMETER;
    }
    NTOBJ_PING_REPLY reply;
    ZeroMemory(&reply, sizeof(reply));
    NTSTATUS st = CallIoctl(IOCTL_NTOBJ_CREATE_OBJECT, &req, sizeof(req), &reply, sizeof(reply));
    if (!NT_SUCCESS(st)) {
        return st;
    }
    return reply.Status;
}

NTSTATUS DrvSetState(const std::wstring& path, ULONG kind, ULONG action, ULONG value)
{
    NTOBJ_SETSTATE_REQ req;
    ZeroMemory(&req, sizeof(req));
    req.Kind   = kind;
    req.Action = action;
    req.Value  = value;
    if (!FillPathBuffer(req.Path, NTOBJ_MAX_PATH, path)) {
        return NTOBJ_ST_INVALID_PARAMETER;
    }
    NTOBJ_PING_REPLY reply;
    ZeroMemory(&reply, sizeof(reply));
    NTSTATUS st = CallIoctl(IOCTL_NTOBJ_SET_STATE, &req, sizeof(req), &reply, sizeof(reply));
    if (!NT_SUCCESS(st)) {
        return st;
    }
    return reply.Status;
}

// [v1.1 resync 2026-10-23] current build-fix version
