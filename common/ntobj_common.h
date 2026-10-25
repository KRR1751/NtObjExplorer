/*
 * NtObjExplorer - definitions shared between the NtObjDrv kernel driver and
 * the ntobj.exe user-mode tool.
 *
 * Plain C, no pointer-sized members: the structures below are exchanged
 * verbatim over METHOD_BUFFERED IOCTLs. Fixed-size buffers keep validation
 * trivial on the kernel side.
 *
 * Include AFTER <windows.h> + <winioctl.h> (user mode) or <ntddk.h> (kernel),
 * so that CTL_CODE / FILE_DEVICE_UNKNOWN / METHOD_BUFFERED are available.
 */
#ifndef NTOBJ_COMMON_H
#define NTOBJ_COMMON_H

/* --- Identity ------------------------------------------------------------- */
#define NTOBJ_DEVICE_NAME_NT    L"\\Device\\NtObjDrv"
#define NTOBJ_SYMLINK_NT_NAME   L"\\??\\NtObjDrv"    /* kernel-side link to the device */
#define NTOBJ_DEVICE_NAME_DOS   L"\\\\.\\NtObjDrv"   /* opens as \\.\NtObjDrv  */
#define NTOBJ_SERVICE_NAME_A    "NtObjDrv"
#define NTOBJ_VERSION           0x00010000UL         /* protocol version 1.0   */

/* --- Limits (in WCHARs, including the terminating NUL) --------------------- */
#define NTOBJ_MAX_PATH    512
#define NTOBJ_MAX_TARGET  512
#define NTOBJ_MAX_NAME    64
#define NTOBJ_MAX_TYPE    24
#define NTOBJ_MAX_ENTRIES 512

/* --- Object kinds (CREATE_OBJECT / SET_STATE) ------------------------------ */
#define NTOBJ_KIND_EVENT      0
#define NTOBJ_KIND_MUTANT     1
#define NTOBJ_KIND_SEMAPHORE  2
#define NTOBJ_KIND_TIMER      3
#define NTOBJ_KIND_SECTION    4
#define NTOBJ_KIND_JOB        5

/* --- SET_STATE actions ------------------------------------------------------ */
#define NTOBJ_ACTION_SIGNAL   0
#define NTOBJ_ACTION_CLEAR    1
#define NTOBJ_ACTION_RELEASE  2

/* --- IOCTL codes -------------------------------------------------------------
 * All METHOD_BUFFERED / FILE_ANY_ACCESS. The driver validates that the input
 * buffer length equals exactly sizeof(request) and that the output buffer is
 * at least sizeof(reply).
 */
#define IOCTL_NTOBJ_PING           CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_NTOBJ_LIST           CTL_CODE(FILE_DEVICE_UNKNOWN, 0x801, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_NTOBJ_CREATE_DIR     CTL_CODE(FILE_DEVICE_UNKNOWN, 0x802, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_NTOBJ_DELETE         CTL_CODE(FILE_DEVICE_UNKNOWN, 0x803, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_NTOBJ_CREATE_SYMLINK CTL_CODE(FILE_DEVICE_UNKNOWN, 0x804, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_NTOBJ_QUERY_SYMLINK  CTL_CODE(FILE_DEVICE_UNKNOWN, 0x805, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_NTOBJ_CREATE_OBJECT  CTL_CODE(FILE_DEVICE_UNKNOWN, 0x806, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_NTOBJ_SET_STATE      CTL_CODE(FILE_DEVICE_UNKNOWN, 0x807, METHOD_BUFFERED, FILE_ANY_ACCESS)

#pragma pack(push, 4)

/* --- Requests --------------------------------------------------------------- */

typedef struct _NTOBJ_PATH_REQ {
    WCHAR Path[NTOBJ_MAX_PATH];          /* NUL-terminated, must start with '\' */
} NTOBJ_PATH_REQ, *PNTOBJ_PATH_REQ;

typedef struct _NTOBJ_DELETE_REQ {
    ULONG Flags;                         /* reserved, must be 0 */
    WCHAR Path[NTOBJ_MAX_PATH];          /* NUL-terminated, must start with '\' */
} NTOBJ_DELETE_REQ, *PNTOBJ_DELETE_REQ;

typedef struct _NTOBJ_SYMLINK_REQ {
    WCHAR LinkPath[NTOBJ_MAX_PATH];      /* NUL-terminated, must start with '\' */
    WCHAR Target[NTOBJ_MAX_TARGET];      /* NUL-terminated NT path              */
} NTOBJ_SYMLINK_REQ, *PNTOBJ_SYMLINK_REQ;

typedef struct _NTOBJ_MKOBJ_REQ {
    ULONG Kind;                          /* NTOBJ_KIND_*                        */
    ULONG Param1;                        /* Event: 0=Notification 1=Synchronization
                                            Semaphore: max count (default 1)    */
    WCHAR Path[NTOBJ_MAX_PATH];          /* NUL-terminated, must start with '\' */
} NTOBJ_MKOBJ_REQ, *PNTOBJ_MKOBJ_REQ;

typedef struct _NTOBJ_SETSTATE_REQ {
    ULONG Kind;                          /* NTOBJ_KIND_*                        */
    ULONG Action;                        /* NTOBJ_ACTION_*                      */
    ULONG Value;                         /* release count for semaphores        */
    WCHAR Path[NTOBJ_MAX_PATH];          /* NUL-terminated, must start with '\' */
} NTOBJ_SETSTATE_REQ, *PNTOBJ_SETSTATE_REQ;

/* --- Replies ----------------------------------------------------------------- */

typedef struct _NTOBJ_PING_REPLY {
    LONG  Status;                        /* NTSTATUS of the operation           */
    ULONG Version;                       /* NTOBJ_VERSION                       */
    ULONG Reserved;
} NTOBJ_PING_REPLY, *PNTOBJ_PING_REPLY;

/*
 * Directory listing reply. Entries[i] is one flat row:
 *   row[0 .. NTOBJ_MAX_NAME-1]                 = object name, NUL-terminated
 *   row[NTOBJ_MAX_NAME .. ]                    = object type, NUL-terminated
 * Valid rows: 0 .. Count-1.
 */
typedef struct _NTOBJ_LIST_REPLY {
    LONG  Status;
    ULONG Count;
    ULONG Truncated;                     /* 1 = more entries exist in the dir   */
    WCHAR Entries[NTOBJ_MAX_ENTRIES][NTOBJ_MAX_NAME + NTOBJ_MAX_TYPE];
} NTOBJ_LIST_REPLY, *PNTOBJ_LIST_REPLY;

typedef struct _NTOBJ_TARGET_REPLY {
    LONG  Status;
    WCHAR Target[NTOBJ_MAX_TARGET];      /* NUL-terminated target of a symlink  */
} NTOBJ_TARGET_REPLY, *PNTOBJ_TARGET_REPLY;

#pragma pack(pop)

#endif /* NTOBJ_COMMON_H */

// [v1.1 resync 2026-10-23] current build-fix version
