/*
 * ntobj.exe - unified namespace operations with two backends:
 *   - driver: everything goes through NtObjDrv via IOCTLs (no privileges needed)
 *   - direct: user-mode ntdll calls (needs admin + SeCreatePermanentPrivilege
 *     for permanent object creation)
 */
#pragma once
#include "ntobj_app.h"

void        SetBackendMode(BackendMode mode);
BackendMode GetBackendMode();
const wchar_t* GetBackendModeName();
bool        DriverActive();         /* true if operations currently go through the driver */
bool        DetectDriverAvailable();/* probe \\.\NtObjDrv once (call at startup) */
bool        DriverWasDetected();

NTSTATUS ObjList(const std::wstring& path, std::vector<NtObjEntry>& entries, bool* truncated);
NTSTATUS ObjMakeDirectory(const std::wstring& path);
NTSTATUS ObjDeleteObject(const std::wstring& path);
NTSTATUS ObjMakeSymlink(const std::wstring& link, const std::wstring& target);
NTSTATUS ObjQuerySymlink(const std::wstring& path, std::wstring& target);
NTSTATUS ObjSetSymlinkTarget(const std::wstring& link, const std::wstring& newTarget);
NTSTATUS ObjCreateObject(const std::wstring& path, ULONG kind, ULONG param1);
NTSTATUS ObjSetState(const std::wstring& path, ULONG kind, ULONG action, ULONG value);
NTSTATUS ObjDetectKind(const std::wstring& path, ULONG* outKind, std::wstring* outTypeName);
NTSTATUS ObjListTypes(std::vector<NtObjEntry>& types);
NTSTATUS ObjCreateWindowStation(const std::wstring& name);
NTSTATUS ObjCreateDesktop(const std::wstring& name);

// [v1.1 resync 2026-10-23] current build-fix version
