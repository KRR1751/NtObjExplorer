/*
 * ntobj.exe - dynamic ntdll.dll layer.
 *
 * All Nt* entry points are resolved with GetProcAddress so the tool builds
 * and runs without the WDK and without ntdll.lib. Direct user-mode access is
 * enough for listing/creating/deleting namespace objects (given admin rights
 * and the SeCreatePermanentPrivilege for permanent objects); the driver path
 * additionally removes the privilege requirement.
 */
#pragma once
#include "ntobj_app.h"

/* Resolve ntdll.dll entry points; false if anything is missing. */
bool InitNtApi();

/* Raw wrappers (thin, no error translation). */
NTSTATUS NtOpenDirectoryObject(HANDLE* handle, ACCESS_MASK access, NtObjectAttributes* attrs);
NTSTATUS NtCreateDirectoryObject(HANDLE* handle, ACCESS_MASK access, NtObjectAttributes* attrs);
NTSTATUS NtQueryDirectoryObject(HANDLE dir, void* buffer, ULONG length,
                                BOOLEAN single, BOOLEAN restart,
                                ULONG* context, ULONG* returned);
NTSTATUS NtOpenSymbolicLinkObject(HANDLE* handle, ACCESS_MASK access, NtObjectAttributes* attrs);
NTSTATUS NtCreateSymbolicLinkObject(HANDLE* handle, ACCESS_MASK access,
                                    NtObjectAttributes* attrs, NtUnicodeString* target);
NTSTATUS NtQuerySymbolicLinkObject(HANDLE link, NtUnicodeString* target, ULONG* returned);
NTSTATUS NtMakeTemporaryObject(HANDLE handle);
void     NtCloseSafe(HANDLE handle);
ULONG    NtStatusToDosError(NTSTATUS status);

/* Extended synchronization and object wrappers */
NTSTATUS NtOpenEvent(HANDLE* handle, ACCESS_MASK access, NtObjectAttributes* attrs);
NTSTATUS NtCreateEvent(HANDLE* handle, ACCESS_MASK access, NtObjectAttributes* attrs, ULONG eventType, BOOLEAN initialState);
NTSTATUS NtSetEvent(HANDLE handle, LONG* previousState);
NTSTATUS NtClearEvent(HANDLE handle);
NTSTATUS NtResetEvent(HANDLE handle, LONG* previousState);

NTSTATUS NtOpenMutant(HANDLE* handle, ACCESS_MASK access, NtObjectAttributes* attrs);
NTSTATUS NtCreateMutant(HANDLE* handle, ACCESS_MASK access, NtObjectAttributes* attrs, BOOLEAN initialOwner);
NTSTATUS NtReleaseMutant(HANDLE handle, LONG* previousCount);

NTSTATUS NtOpenSemaphore(HANDLE* handle, ACCESS_MASK access, NtObjectAttributes* attrs);
NTSTATUS NtCreateSemaphore(HANDLE* handle, ACCESS_MASK access, NtObjectAttributes* attrs, LONG initialCount, LONG maximumCount);
NTSTATUS NtReleaseSemaphore(HANDLE handle, LONG releaseCount, LONG* previousCount);

NTSTATUS NtOpenTimer(HANDLE* handle, ACCESS_MASK access, NtObjectAttributes* attrs);
NTSTATUS NtCreateTimer(HANDLE* handle, ACCESS_MASK access, NtObjectAttributes* attrs, ULONG timerType);
NTSTATUS NtSetTimer(HANDLE handle, PLARGE_INTEGER dueTime, PVOID timerApcRoutine, PVOID timerContext, BOOLEAN resumeTimer, LONG period, BOOLEAN* previousState);
NTSTATUS NtCancelTimer(HANDLE handle, BOOLEAN* currentState);

NTSTATUS NtOpenSection(HANDLE* handle, ACCESS_MASK access, NtObjectAttributes* attrs);
NTSTATUS NtCreateSection(HANDLE* handle, ACCESS_MASK access, NtObjectAttributes* attrs, PLARGE_INTEGER maximumSize, ULONG sectionPageProtection, ULONG allocationAttributes, HANDLE fileHandle);

NTSTATUS NtOpenJobObject(HANDLE* handle, ACCESS_MASK access, NtObjectAttributes* attrs);
NTSTATUS NtCreateJobObject(HANDLE* handle, ACCESS_MASK access, NtObjectAttributes* attrs);

/* Attribute helpers shared by the direct-mode operations. */
NtUnicodeString    MakeUniString(const std::wstring& value);
NtObjectAttributes MakeObjectAttributes(NtUnicodeString* name, ULONG attributes);

// [v1.1 resync 2026-10-23] current build-fix version
