/*
 * ntobj.exe - console explorer for the NT Object Manager namespace.
 * Shared application definitions.
 *
 * IMPORTANT (this fixes the C2061 'NTSTATUS' cascade from the previous
 * release): the Windows SDK's <windows.h> does NOT define NTSTATUS,
 * UNICODE_STRING or OBJECT_ATTRIBUTES in user mode. winternl.h defines them,
 * but including it together with our own mirrors causes redefinition
 * conflicts. Therefore everything NT-related is defined here exactly once,
 * with layout-identical mirror types.
 */
#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winioctl.h>              /* CTL_CODE for ntobj_common.h */
#include <string>
#include <vector>
#include <utility>

#include "ntobj_common.h"

/* ---------------------------------------------------------------------------
 * NTSTATUS
 *
 * Repeated identical typedefs are legal C++ (dcl.typedef), so this stays
 * correct even if some other header in the build also typedefs LONG NTSTATUS.
 * ------------------------------------------------------------------------- */
typedef LONG NTSTATUS;

#ifndef NT_SUCCESS
#define NT_SUCCESS(status) (((NTSTATUS)(status)) >= 0)
#endif

/* NTSTATUS values used by this tool (values from ntstatus.h, distinct macro
 * names so they cannot collide with <winnt.h>). */
#define NTOBJ_ST_SUCCESS                 ((NTSTATUS)0x00000000L)
#define NTOBJ_ST_UNSUCCESSFUL            ((NTSTATUS)0xC0000001L)
#define NTOBJ_ST_INVALID_HANDLE          ((NTSTATUS)0xC0000008L)
#define NTOBJ_ST_INVALID_PARAMETER       ((NTSTATUS)0xC000000DL)
#define NTOBJ_ST_ACCESS_DENIED           ((NTSTATUS)0xC0000022L)
#define NTOBJ_ST_OBJECT_TYPE_MISMATCH    ((NTSTATUS)0xC0000024L)
#define NTOBJ_ST_BUFFER_TOO_SMALL        ((NTSTATUS)0xC0000023L)
#define NTOBJ_ST_OBJECT_NAME_NOT_FOUND   ((NTSTATUS)0xC0000034L)
#define NTOBJ_ST_OBJECT_NAME_COLLISION   ((NTSTATUS)0xC0000035L)
#define NTOBJ_ST_OBJECT_PATH_NOT_FOUND   ((NTSTATUS)0xC000003AL)
#define NTOBJ_ST_PRIVILEGE_NOT_HELD      ((NTSTATUS)0xC0000061L)
#define NTOBJ_ST_INSUFFICIENT_RESOURCES  ((NTSTATUS)0xC000009AL)
#define NTOBJ_ST_DELETE_PENDING          ((NTSTATUS)0xC0000056L)
#define NTOBJ_ST_NO_MORE_ENTRIES         ((NTSTATUS)0x8000001AL)

/* Synthetic status used by the driver transport: low 16 bits carry a Win32
 * error code when DeviceIoControl itself fails (e.g. driver not loaded). */
#define NTOBJ_ST_WIN32_BASE  ((NTSTATUS)0xE0000000L)
#define NTOBJ_IS_WIN32_ERR(status)  ((((ULONG)(status)) & 0xE0000000UL) == 0xE0000000UL)
#define NTOBJ_WIN32_CODE(status)    ((DWORD)(((ULONG)(status)) & 0x0000FFFFUL))
#define NTOBJ_MAKE_WIN32(err)       ((NTSTATUS)(NTOBJ_ST_WIN32_BASE | (ULONG)(err)))

/* ---------------------------------------------------------------------------
 * NT object types (layout-identical mirrors, unique names)
 * ------------------------------------------------------------------------- */

/* Mirror of UNICODE_STRING. */
struct NtUnicodeString {
    USHORT Length;         /* bytes, excluding the NUL terminator */
    USHORT MaximumLength;  /* bytes, buffer capacity              */
    PWSTR  Buffer;
};

/* Mirror of OBJECT_ATTRIBUTES. */
struct NtObjectAttributes {
    ULONG Length;
    HANDLE RootDirectory;
    NtUnicodeString* ObjectName;
    ULONG Attributes;
    PVOID SecurityDescriptor;
    PVOID SecurityQualityOfService;
};

/* Mirror of DIRECTORY_BASIC_INFORMATION (ZwQueryDirectoryObject output). */
struct NtDirectoryInfo {
    NtUnicodeString ObjectName;
    NtUnicodeString ObjectTypeName;
};

/* OBJECT_ATTRIBUTES flags (values from ntdef.h). */
#define NTOBJ_OBJ_CASE_INSENSITIVE  0x00000040UL
#define NTOBJ_OBJ_PERMANENT         0x00000010UL
#define NTOBJ_OBJ_OPENLINK          0x00000100UL  /* open the link, not its target */

/* Directory object access masks. */
#define NTOBJ_DIR_QUERY       0x00000001UL
#define NTOBJ_DIR_ALL_ACCESS  0x000F000FUL  /* STANDARD_RIGHTS_REQUIRED | 0xF */

/* Symbolic link object access masks. */
#define NTOBJ_LINK_QUERY      0x00000001UL
#define NTOBJ_LINK_ALL_ACCESS 0x000F0001UL  /* STANDARD_RIGHTS_REQUIRED | QUERY */

/* One listed entry (name + type name). */
struct NtObjEntry {
    std::wstring Name;
    std::wstring Type;
};

/* ---------------------------------------------------------------------------
 * Diagnostics (implemented in ntapi.cpp / main.cpp)
 * ------------------------------------------------------------------------- */
void PrintStatus(const std::wstring& operation, NTSTATUS status);
void PrintWin32Error(const std::wstring& operation, DWORD error);
const wchar_t* NtStatusName(NTSTATUS status);

/* Environment (implemented in main.cpp). */
bool IsElevated();
void EnableStartupPrivileges();

/* ---------------------------------------------------------------------------
 * Backend selection
 * ------------------------------------------------------------------------- */
enum class BackendMode { Auto, Driver, Direct };

/* ---------------------------------------------------------------------------
 * Command engine (commands.cpp). Returns process exit code for the command
 * (0 = success, non-zero = failure). Returns false only for "exit".
 * ------------------------------------------------------------------------- */
int  ExecuteCommand(const std::vector<std::wstring>& args);      /* 0 ok / 1 fail / 2 usage */
bool CommandIsExit(const std::vector<std::wstring>& args);
std::vector<std::wstring> Tokenize(const std::wstring& line);
std::wstring NormalizePath(const std::wstring& input);           /* empty = invalid */

// [v1.1 resync 2026-10-23] current build-fix version
