/*
 * ntobj.exe - transport layer to the NtObjDrv kernel driver.
 */
#pragma once
#include "ntobj_app.h"

/* Try to open \\.\NtObjDrv. True if the driver device is usable. */
bool OpenDriver();
void CloseDriver();
bool DriverIsOpen();

/* Probe the driver once (PING + version check). */
bool DetectDriver(ULONG* versionOut);

NTSTATUS DrvList(const std::wstring& path, std::vector<NtObjEntry>& entries, bool* truncated);
NTSTATUS DrvMakeDirectory(const std::wstring& path);
NTSTATUS DrvDeleteObject(const std::wstring& path);
NTSTATUS DrvMakeSymlink(const std::wstring& link, const std::wstring& target);
NTSTATUS DrvQuerySymlink(const std::wstring& path, std::wstring& target);
NTSTATUS DrvCreateObject(const std::wstring& path, ULONG kind, ULONG param1);
NTSTATUS DrvSetState(const std::wstring& path, ULONG kind, ULONG action, ULONG value);

// [v1.1 resync 2026-10-23] current build-fix version
