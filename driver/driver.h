/*
 * NtObjDrv - WDM control driver for NT Object Manager namespace operations.
 *
 * Exposes a \\.\NtObjDrv control device. User-mode requests arrive as
 * METHOD_BUFFERED IOCTLs (see ../common/ntobj_common.h) and are translated
 * to documented Zw* object manager calls, executed in kernel mode so that
 * no user privileges (SeCreatePermanentPrivilege, directory DACLs) limit
 * the operations.
 */
#pragma once

/* ntifs.h includes ntddk.h and adds ZwOpenDirectoryObject, ZwCreateEvent,
   ZwSetEvent, etc. */
#include <ntifs.h>
#include "ntobj_common.h"

/* -----------------------------------------------------------------------
 * Undocumented / missing Zw* prototypes and types
 *
 * These are stable NT kernel APIs present in ntoskrnl.lib exports but
 * whose prototypes are not (or are no longer) shipped in the public WDK
 * headers.  The signatures match the ntoskrnl exports since Windows NT 4.
 * ----------------------------------------------------------------------- */

#ifdef __cplusplus
extern "C" {
#endif

/* --- OBJECT_DIRECTORY_INFORMATION -------------------------------------- */
#ifndef _OBJECT_DIRECTORY_INFORMATION_DEFINED
#define _OBJECT_DIRECTORY_INFORMATION_DEFINED
typedef struct _OBJECT_DIRECTORY_INFORMATION {
    UNICODE_STRING Name;
    UNICODE_STRING TypeName;
} OBJECT_DIRECTORY_INFORMATION, *POBJECT_DIRECTORY_INFORMATION;
#endif

/* --- ZwQueryDirectoryObject -------------------------------------------- */
NTSYSAPI NTSTATUS NTAPI
ZwQueryDirectoryObject(
    _In_  HANDLE  DirectoryHandle,
    _Out_ PVOID   Buffer,
    _In_  ULONG   Length,
    _In_  BOOLEAN ReturnSingleEntry,
    _In_  BOOLEAN RestartScan,
    _Inout_ PULONG Context,
    _Out_opt_ PULONG ReturnLength);

/* --- ZwCreateSymbolicLinkObject ---------------------------------------- */
NTSYSAPI NTSTATUS NTAPI
ZwCreateSymbolicLinkObject(
    _Out_ PHANDLE            LinkHandle,
    _In_  ACCESS_MASK        DesiredAccess,
    _In_  POBJECT_ATTRIBUTES ObjectAttributes,
    _In_  PUNICODE_STRING    LinkTarget);

/* --- ZwCreateSemaphore ------------------------------------------------- */
NTSYSAPI NTSTATUS NTAPI
ZwCreateSemaphore(
    _Out_ PHANDLE            SemaphoreHandle,
    _In_  ACCESS_MASK        DesiredAccess,
    _In_opt_ POBJECT_ATTRIBUTES ObjectAttributes,
    _In_  LONG               InitialCount,
    _In_  LONG               MaximumCount);

/* --- ZwReleaseSemaphore ------------------------------------------------ */
NTSYSAPI NTSTATUS NTAPI
ZwReleaseSemaphore(
    _In_      HANDLE SemaphoreHandle,
    _In_      LONG   ReleaseCount,
    _Out_opt_ PLONG  PreviousCount);

/* --- ObOpenObjectByName ------------------------------------------------ */
NTSYSAPI NTSTATUS NTAPI
ObOpenObjectByName(
    _In_ POBJECT_ATTRIBUTES ObjectAttributes,
    _In_opt_ POBJECT_TYPE ObjectType,
    _In_ KPROCESSOR_MODE AccessMode,
    _Inout_opt_ PACCESS_STATE AccessState,
    _In_ ACCESS_MASK DesiredAccess,
    _Inout_opt_ PVOID ParseContext,
    _Out_ PHANDLE Handle);

extern POBJECT_TYPE *ExSemaphoreObjectType;

/* --- ZwClearEvent ------------------------------------------------------ */
NTSYSAPI NTSTATUS NTAPI
ZwClearEvent(
    _In_ HANDLE EventHandle);

/* --- ZwCreateJobObject ------------------------------------------------- */
NTSYSAPI NTSTATUS NTAPI
ZwCreateJobObject(
    _Out_ PHANDLE            JobHandle,
    _In_  ACCESS_MASK        DesiredAccess,
    _In_opt_ POBJECT_ATTRIBUTES ObjectAttributes);

/* --- ZwOpenJobObject --------------------------------------------------- */
NTSYSAPI NTSTATUS NTAPI
ZwOpenJobObject(
    _Out_ PHANDLE            JobHandle,
    _In_  ACCESS_MASK        DesiredAccess,
    _In_  POBJECT_ATTRIBUTES ObjectAttributes);

/* --- ZwOpenSection ----------------------------------------------------- */
NTSYSAPI NTSTATUS NTAPI
ZwOpenSection(
    _Out_ PHANDLE            SectionHandle,
    _In_  ACCESS_MASK        DesiredAccess,
    _In_  POBJECT_ATTRIBUTES ObjectAttributes);

#ifdef __cplusplus
}
#endif

/* Fallbacks for access masks that are not present in every WDK version. */
#ifndef JOB_OBJECT_ALL_ACCESS
#define JOB_OBJECT_ALL_ACCESS (STANDARD_RIGHTS_REQUIRED | SYNCHRONIZE | 0x1F)
#endif
#ifndef SECTION_ALL_ACCESS
#define SECTION_ALL_ACCESS   (STANDARD_RIGHTS_REQUIRED | 0xF)
#endif

/* Fallbacks for access masks that are not present in every WDK version. */
#ifndef EVENT_ALL_ACCESS
#define EVENT_ALL_ACCESS     (STANDARD_RIGHTS_REQUIRED | SYNCHRONIZE | EVENT_MODIFY_STATE)
#endif
#ifndef MUTANT_QUERY_STATE
#define MUTANT_QUERY_STATE   0x0001
#endif
#ifndef MUTANT_ALL_ACCESS
#define MUTANT_ALL_ACCESS    (STANDARD_RIGHTS_REQUIRED | SYNCHRONIZE | MUTANT_QUERY_STATE)
#endif
#ifndef SEMAPHORE_MODIFY_STATE
#define SEMAPHORE_MODIFY_STATE 0x0002
#endif
#ifndef SEMAPHORE_ALL_ACCESS
#define SEMAPHORE_ALL_ACCESS (STANDARD_RIGHTS_REQUIRED | SYNCHRONIZE | SEMAPHORE_MODIFY_STATE)
#endif
#ifndef TIMER_QUERY_STATE
#define TIMER_QUERY_STATE    0x0001
#endif
#ifndef TIMER_SET_STATE
#define TIMER_SET_STATE      0x0002
#endif
#ifndef TIMER_MODIFY_STATE
#define TIMER_MODIFY_STATE   TIMER_SET_STATE
#endif
#ifndef TIMER_ALL_ACCESS
#define TIMER_ALL_ACCESS     (STANDARD_RIGHTS_REQUIRED | SYNCHRONIZE | TIMER_QUERY_STATE | TIMER_SET_STATE)
#endif

extern "C" NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath);

// [v1.3 standalone-km 2026-10-23] current build-fix version
