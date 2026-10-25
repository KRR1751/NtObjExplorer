/*
 * NtObjDrv - WDM control driver for NT Object Manager namespace operations.
 *
 * Design notes:
 *  - METHOD_BUFFERED IOCTLs; the input is validated strictly (exact size,
 *    NUL-terminated path starting with '\') before any object manager call.
 *  - Symbolic links are opened with OBJ_OPENLINK so that deleting or
 *    querying a link never touches its target.
 *  - Objects are created with OBJ_PERMANENT so they survive handle close;
 *    deletion flips them back to temporary (ZwMakeTemporaryObject), which
 *    removes the name when the last reference is released.
 *  - No pool allocations at all: every buffer is fixed size on the stack
 *    or inside the IRP, which keeps the failure paths trivial.
 */
#include "driver.h"

#define NTOBJ_TAG 'bOtN'   /* pool tag reserved for diagnostics */

#ifdef _PREFAST_
#pragma warning(disable: 28196)
#endif

/* Query buffer for one OBJECT_DIRECTORY_INFORMATION entry plus its strings. */
union NTOBJ_QUERY_BUFFER {
    UCHAR Bytes[2048];
    PVOID Alignment;
};

/* ---------------------------------------------------------------------------
 * Path validation
 * ------------------------------------------------------------------------- */

static BOOLEAN NtObjpValidPath(const WCHAR* path)
{
    SIZE_T i;
    if (path == NULL || path[0] != L'\\') {
        return FALSE;
    }
    for (i = 0; i < NTOBJ_MAX_PATH; ++i) {
        if (path[i] == L'\0') {
            return TRUE;    /* NUL-terminated within bounds */
        }
    }
    return FALSE;
}

/* ---------------------------------------------------------------------------
 * Open helpers
 * ------------------------------------------------------------------------- */

static NTSTATUS NtObjpOpenDirectory(PCWSTR path, ACCESS_MASK access, HANDLE* handle)
{
    UNICODE_STRING uni;
    OBJECT_ATTRIBUTES oa;
    RtlInitUnicodeString(&uni, path);
    InitializeObjectAttributes(&oa, &uni, OBJ_CASE_INSENSITIVE, NULL, NULL);
    return ZwOpenDirectoryObject(handle, access, &oa);
}

/* OBJ_OPENLINK: open the symbolic link itself, never its target. */
static NTSTATUS NtObjpOpenSymlink(PCWSTR path, ACCESS_MASK access, HANDLE* handle)
{
    UNICODE_STRING uni;
    OBJECT_ATTRIBUTES oa;
    RtlInitUnicodeString(&uni, path);
    InitializeObjectAttributes(&oa, &uni, OBJ_CASE_INSENSITIVE | OBJ_OPENLINK, NULL, NULL);
    return ZwOpenSymbolicLinkObject(handle, access, &oa);
}

/* ---------------------------------------------------------------------------
 * IOCTL handlers
 * ------------------------------------------------------------------------- */

static NTSTATUS NtObjpPing(ULONG outLen, PVOID out, ULONG_PTR* information)
{
    NTOBJ_PING_REPLY* reply = (NTOBJ_PING_REPLY*)out;
    if (outLen < sizeof(NTOBJ_PING_REPLY)) {
        return STATUS_BUFFER_TOO_SMALL;
    }
    reply->Status   = STATUS_SUCCESS;
    reply->Version  = NTOBJ_VERSION;
    reply->Reserved = 0;
    *information    = sizeof(NTOBJ_PING_REPLY);
    return STATUS_SUCCESS;
}

static NTSTATUS NtObjpList(const NTOBJ_PATH_REQ* req, ULONG inLen,
                           PVOID out, ULONG outLen, ULONG_PTR* information)
{
    NTOBJ_LIST_REPLY* reply;
    UNICODE_STRING uni;
    OBJECT_ATTRIBUTES oa;
    HANDLE dir = NULL;
    NTSTATUS st;
    NTOBJ_QUERY_BUFFER query;
    ULONG context;
    BOOLEAN restart;

    UNREFERENCED_PARAMETER(inLen);

    if (!NtObjpValidPath(req->Path)) {
        return STATUS_OBJECT_NAME_INVALID;
    }
    if (outLen < sizeof(NTOBJ_LIST_REPLY)) {
        return STATUS_BUFFER_TOO_SMALL;
    }

    reply = (NTOBJ_LIST_REPLY*)out;
    reply->Status    = STATUS_SUCCESS;
    reply->Count     = 0;
    reply->Truncated = 0;

    RtlInitUnicodeString(&uni, req->Path);
    InitializeObjectAttributes(&oa, &uni, OBJ_CASE_INSENSITIVE, NULL, NULL);

    st = ZwOpenDirectoryObject(&dir, DIRECTORY_QUERY, &oa);
    if (!NT_SUCCESS(st)) {
        return st;
    }

    context = 0;
    restart = TRUE;
    for (;;) {
        ULONG returned = 0;
        POBJECT_DIRECTORY_INFORMATION info;

        RtlZeroMemory(query.Bytes, sizeof(query.Bytes));
        st = ZwQueryDirectoryObject(dir, query.Bytes, sizeof(query.Bytes),
                                    TRUE /* single entry */, restart, &context, &returned);
        restart = FALSE;

        if (st == STATUS_NO_MORE_ENTRIES) {
            st = STATUS_SUCCESS;
            break;
        }
        if (!NT_SUCCESS(st)) {
            break;
        }

        info = (POBJECT_DIRECTORY_INFORMATION)query.Bytes;
        if (info->Name.Length == 0 || info->Name.Buffer == NULL) {
            st = STATUS_SUCCESS;    /* terminator entry */
            break;
        }
        if (reply->Count >= NTOBJ_MAX_ENTRIES) {
            reply->Truncated = 1;
            break;
        }

        {
            WCHAR* row = reply->Entries[reply->Count];
            ULONG nameChars = info->Name.Length / sizeof(WCHAR);
            ULONG typeChars = info->TypeName.Length / sizeof(WCHAR);

            RtlZeroMemory(row, (NTOBJ_MAX_NAME + NTOBJ_MAX_TYPE) * sizeof(WCHAR));
            if (nameChars > NTOBJ_MAX_NAME - 1) {
                nameChars = NTOBJ_MAX_NAME - 1;
            }
            if (typeChars > NTOBJ_MAX_TYPE - 1) {
                typeChars = NTOBJ_MAX_TYPE - 1;
            }
            RtlCopyMemory(row, info->Name.Buffer, nameChars * sizeof(WCHAR));
            RtlCopyMemory(row + NTOBJ_MAX_NAME, info->TypeName.Buffer, typeChars * sizeof(WCHAR));
            row[nameChars] = L'\0';
            row[NTOBJ_MAX_NAME + typeChars] = L'\0';
        }

        reply->Count++;
    }

    ZwClose(dir);
    reply->Status = st;
    *information  = sizeof(NTOBJ_LIST_REPLY);
    return st;
}

static NTSTATUS NtObjpCreateDirectory(const NTOBJ_PATH_REQ* req, ULONG inLen)
{
    UNICODE_STRING uni;
    OBJECT_ATTRIBUTES oa;
    HANDLE handle = NULL;
    NTSTATUS st;

    UNREFERENCED_PARAMETER(inLen);

    if (!NtObjpValidPath(req->Path)) {
        return STATUS_OBJECT_NAME_INVALID;
    }

    RtlInitUnicodeString(&uni, req->Path);
    InitializeObjectAttributes(&oa, &uni,
                               OBJ_CASE_INSENSITIVE | OBJ_PERMANENT, NULL, NULL);
    st = ZwCreateDirectoryObject(&handle, DIRECTORY_ALL_ACCESS, &oa);
    if (NT_SUCCESS(st)) {
        ZwClose(handle);
    }
    return st;
}

static NTSTATUS NtObjpDelete(const NTOBJ_DELETE_REQ* req, ULONG inLen)
{
    HANDLE handle = NULL;
    NTSTATUS st;

    UNREFERENCED_PARAMETER(inLen);

    if (!NtObjpValidPath(req->Path)) {
        return STATUS_OBJECT_NAME_INVALID;
    }
    if (req->Flags != 0) {
        return STATUS_INVALID_PARAMETER;
    }

    UNICODE_STRING uni;
    OBJECT_ATTRIBUTES oa;
    RtlInitUnicodeString(&uni, req->Path);
    InitializeObjectAttributes(&oa, &uni, OBJ_CASE_INSENSITIVE | OBJ_OPENLINK, NULL, NULL);

    /* 1) Generic open: works for Event, Mutant, Semaphore, Timer, Section, Job, etc.
     *    and also SymbolicLink without following target because of OBJ_OPENLINK. */
    st = ObOpenObjectByName(&oa, NULL, KernelMode, NULL, DELETE, NULL, &handle);
    if (NT_SUCCESS(st)) {
        st = ZwMakeTemporaryObject(handle);
        ZwClose(handle);
        return st;
    }

    /* 2) Specific symbolic link open fallback */
    st = NtObjpOpenSymlink(req->Path, DELETE, &handle);
    if (NT_SUCCESS(st)) {
        st = ZwMakeTemporaryObject(handle);
        ZwClose(handle);
        return st;
    }

    /* 3) Directory open fallback */
    st = NtObjpOpenDirectory(req->Path, DELETE, &handle);
    if (NT_SUCCESS(st)) {
        st = ZwMakeTemporaryObject(handle);
        ZwClose(handle);
        return st;
    }
    return st;
}

static NTSTATUS NtObjpCreateSymlink(const NTOBJ_SYMLINK_REQ* req, ULONG inLen)
{
    UNICODE_STRING uni;
    UNICODE_STRING target;
    OBJECT_ATTRIBUTES oa;
    HANDLE handle = NULL;
    NTSTATUS st;

    UNREFERENCED_PARAMETER(inLen);

    if (!NtObjpValidPath(req->LinkPath) ||
        req->Target[0] != L'\\' ||
        req->Target[NTOBJ_MAX_TARGET - 1] != L'\0') {
        return STATUS_OBJECT_NAME_INVALID;
    }

    RtlInitUnicodeString(&uni, req->LinkPath);
    RtlInitUnicodeString(&target, req->Target);
    InitializeObjectAttributes(&oa, &uni,
                               OBJ_CASE_INSENSITIVE | OBJ_PERMANENT, NULL, NULL);
    st = ZwCreateSymbolicLinkObject(&handle, SYMBOLIC_LINK_ALL_ACCESS, &oa, &target);
    if (NT_SUCCESS(st)) {
        ZwClose(handle);
    }
    return st;
}

static NTSTATUS NtObjpQuerySymlink(const NTOBJ_PATH_REQ* req, ULONG inLen,
                                   PVOID out, ULONG outLen, ULONG_PTR* information)
{
    NTOBJ_TARGET_REPLY* reply;
    HANDLE handle = NULL;
    NTSTATUS st;

    UNREFERENCED_PARAMETER(inLen);

    if (!NtObjpValidPath(req->Path)) {
        return STATUS_OBJECT_NAME_INVALID;
    }
    if (outLen < sizeof(NTOBJ_TARGET_REPLY)) {
        return STATUS_BUFFER_TOO_SMALL;
    }

    reply = (NTOBJ_TARGET_REPLY*)out;
    RtlZeroMemory(reply, sizeof(NTOBJ_TARGET_REPLY));

    st = NtObjpOpenSymlink(req->Path, SYMBOLIC_LINK_QUERY, &handle);
    if (!NT_SUCCESS(st)) {
        return st;
    }

    {
        UNICODE_STRING tgt;
        tgt.Buffer        = reply->Target;
        tgt.Length        = 0;
        tgt.MaximumLength = (USHORT)(NTOBJ_MAX_TARGET * sizeof(WCHAR));

        st = ZwQuerySymbolicLinkObject(handle, &tgt, NULL);
        ZwClose(handle);
        if (NT_SUCCESS(st)) {
            ULONG chars = tgt.Length / sizeof(WCHAR);
            if (chars > NTOBJ_MAX_TARGET - 1) {
                chars = NTOBJ_MAX_TARGET - 1;
            }
            reply->Target[chars] = L'\0';
        }
    }

    reply->Status = st;
    *information  = sizeof(NTOBJ_TARGET_REPLY);
    return st;
}

static NTSTATUS NtObjpCreateObject(const NTOBJ_MKOBJ_REQ* req, ULONG inLen)
{
    UNICODE_STRING uni;
    OBJECT_ATTRIBUTES oa;
    HANDLE handle = NULL;
    NTSTATUS st = STATUS_INVALID_PARAMETER;

    UNREFERENCED_PARAMETER(inLen);

    if (!NtObjpValidPath(req->Path)) {
        return STATUS_OBJECT_NAME_INVALID;
    }

    RtlInitUnicodeString(&uni, req->Path);
    InitializeObjectAttributes(&oa, &uni,
                               OBJ_CASE_INSENSITIVE | OBJ_PERMANENT, NULL, NULL);

    switch (req->Kind) {
    case NTOBJ_KIND_EVENT:
    {
        EVENT_TYPE type = (req->Param1 == 0) ? NotificationEvent : SynchronizationEvent;
        st = ZwCreateEvent(&handle, EVENT_ALL_ACCESS, &oa, type, FALSE);
        break;
    }
    case NTOBJ_KIND_MUTANT:
        st = STATUS_NOT_SUPPORTED;
        break;
    case NTOBJ_KIND_SEMAPHORE:
    {
        ULONG limit = (req->Param1 != 0) ? req->Param1 : 1;
        st = ZwCreateSemaphore(&handle, SEMAPHORE_ALL_ACCESS, &oa, 0, limit);
        break;
    }
    case NTOBJ_KIND_TIMER:
        st = ZwCreateTimer(&handle, TIMER_MODIFY_STATE, &oa, NotificationTimer);
        break;
    case NTOBJ_KIND_SECTION:
    {
        LARGE_INTEGER size;
        size.QuadPart = (req->Param1 != 0) ? (LONGLONG)req->Param1 : 65536;
        st = ZwCreateSection(&handle, SECTION_ALL_ACCESS, &oa, &size, PAGE_READWRITE, SEC_COMMIT, NULL);
        break;
    }
    case NTOBJ_KIND_JOB:
        st = ZwCreateJobObject(&handle, JOB_OBJECT_ALL_ACCESS, &oa);
        break;
    default:
        st = STATUS_INVALID_PARAMETER;
        break;
    }

    if (NT_SUCCESS(st)) {
        ZwClose(handle);
    }
    return st;
}

static NTSTATUS NtObjpSetState(const NTOBJ_SETSTATE_REQ* req, ULONG inLen)
{
    UNICODE_STRING uni;
    OBJECT_ATTRIBUTES oa;
    HANDLE handle = NULL;
    NTSTATUS st;

    UNREFERENCED_PARAMETER(inLen);

    if (!NtObjpValidPath(req->Path)) {
        return STATUS_OBJECT_NAME_INVALID;
    }

    RtlInitUnicodeString(&uni, req->Path);
    InitializeObjectAttributes(&oa, &uni, OBJ_CASE_INSENSITIVE, NULL, NULL);

    switch (req->Kind) {
    case NTOBJ_KIND_EVENT:
        st = ZwOpenEvent(&handle, EVENT_MODIFY_STATE, &oa);
        if (!NT_SUCCESS(st)) {
            return st;
        }
        if (req->Action == NTOBJ_ACTION_SIGNAL) {
            st = ZwSetEvent(handle, NULL);
        } else if (req->Action == NTOBJ_ACTION_CLEAR) {
            st = ZwClearEvent(handle);
        } else {
            st = STATUS_INVALID_PARAMETER;
        }
        ZwClose(handle);
        return st;

    case NTOBJ_KIND_MUTANT:
        st = ObOpenObjectByName(&oa, NULL, KernelMode, NULL, MUTANT_ALL_ACCESS, NULL, &handle);
        if (!NT_SUCCESS(st)) {
            return st;
        }
        if (req->Action == NTOBJ_ACTION_RELEASE) {
            PKMUTANT mutant = NULL;
            st = ObReferenceObjectByHandle(handle, MUTANT_ALL_ACCESS, NULL, KernelMode, (PVOID*)&mutant, NULL);
            if (NT_SUCCESS(st)) {
                KeReleaseMutant(mutant, 0, FALSE, FALSE);
                ObDereferenceObject(mutant);
            }
        } else {
            st = STATUS_INVALID_PARAMETER;
        }
        ZwClose(handle);
        return st;

    case NTOBJ_KIND_SEMAPHORE:
        st = ObOpenObjectByName(&oa, *ExSemaphoreObjectType, KernelMode, NULL, SEMAPHORE_MODIFY_STATE, NULL, &handle);
        if (!NT_SUCCESS(st)) {
            return st;
        }
        if (req->Action == NTOBJ_ACTION_RELEASE) {
            LONG previous = 0;
            st = ZwReleaseSemaphore(handle, req->Value ? req->Value : 1, &previous);
        } else {
            st = STATUS_INVALID_PARAMETER;
        }
        ZwClose(handle);
        return st;

    case NTOBJ_KIND_TIMER:
        st = ZwOpenTimer(&handle, TIMER_MODIFY_STATE, &oa);
        if (!NT_SUCCESS(st)) {
            return st;
        }
        if (req->Action == NTOBJ_ACTION_SIGNAL) {
            LARGE_INTEGER dueTime;
            dueTime.QuadPart = (req->Value != 0) ? -10000LL * (LONGLONG)req->Value : -1LL;
            st = ZwSetTimer(handle, &dueTime, NULL, NULL, FALSE, 0, NULL);
        } else if (req->Action == NTOBJ_ACTION_CLEAR) {
            BOOLEAN current = FALSE;
            st = ZwCancelTimer(handle, &current);
        } else {
            st = STATUS_INVALID_PARAMETER;
        }
        ZwClose(handle);
        return st;

    default:
        return STATUS_INVALID_PARAMETER;
    }
}

/* ---------------------------------------------------------------------------
 * IRP dispatch
 * ------------------------------------------------------------------------- */

static NTSTATUS NtObjpCreateClose(PDEVICE_OBJECT deviceObject, PIRP irp)
{
    UNREFERENCED_PARAMETER(deviceObject);
    irp->IoStatus.Status      = STATUS_SUCCESS;
    irp->IoStatus.Information = 0;
    IoCompleteRequest(irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}

static NTSTATUS NtObjpDeviceControl(PDEVICE_OBJECT deviceObject, PIRP irp)
{
    PIO_STACK_LOCATION io;
    ULONG code;
    ULONG inLen;
    ULONG outLen;
    PVOID buffer;
    NTSTATUS st = STATUS_INVALID_DEVICE_REQUEST;
    ULONG_PTR information = 0;

    UNREFERENCED_PARAMETER(deviceObject);

    io = IoGetCurrentIrpStackLocation(irp);
    code  = io->Parameters.DeviceIoControl.IoControlCode;
    inLen = io->Parameters.DeviceIoControl.InputBufferLength;
    outLen = io->Parameters.DeviceIoControl.OutputBufferLength;
    buffer = irp->AssociatedIrp.SystemBuffer;

    switch (code) {
    case IOCTL_NTOBJ_PING:
        st = NtObjpPing(outLen, buffer, &information);
        break;

    case IOCTL_NTOBJ_LIST:
        if (inLen == sizeof(NTOBJ_PATH_REQ)) {
            st = NtObjpList((const NTOBJ_PATH_REQ*)buffer, inLen, buffer, outLen, &information);
        } else {
            st = STATUS_INVALID_PARAMETER;
        }
        break;

    case IOCTL_NTOBJ_CREATE_DIR:
        if (inLen == sizeof(NTOBJ_PATH_REQ)) {
            st = NtObjpCreateDirectory((const NTOBJ_PATH_REQ*)buffer, inLen);
        } else {
            st = STATUS_INVALID_PARAMETER;
        }
        break;

    case IOCTL_NTOBJ_DELETE:
        if (inLen == sizeof(NTOBJ_DELETE_REQ)) {
            st = NtObjpDelete((const NTOBJ_DELETE_REQ*)buffer, inLen);
        } else {
            st = STATUS_INVALID_PARAMETER;
        }
        break;

    case IOCTL_NTOBJ_CREATE_SYMLINK:
        if (inLen == sizeof(NTOBJ_SYMLINK_REQ)) {
            st = NtObjpCreateSymlink((const NTOBJ_SYMLINK_REQ*)buffer, inLen);
        } else {
            st = STATUS_INVALID_PARAMETER;
        }
        break;

    case IOCTL_NTOBJ_QUERY_SYMLINK:
        if (inLen == sizeof(NTOBJ_PATH_REQ)) {
            st = NtObjpQuerySymlink((const NTOBJ_PATH_REQ*)buffer, inLen, buffer, outLen, &information);
        } else {
            st = STATUS_INVALID_PARAMETER;
        }
        break;

    case IOCTL_NTOBJ_CREATE_OBJECT:
        if (inLen == sizeof(NTOBJ_MKOBJ_REQ)) {
            st = NtObjpCreateObject((const NTOBJ_MKOBJ_REQ*)buffer, inLen);
        } else {
            st = STATUS_INVALID_PARAMETER;
        }
        break;

    case IOCTL_NTOBJ_SET_STATE:
        if (inLen == sizeof(NTOBJ_SETSTATE_REQ)) {
            st = NtObjpSetState((const NTOBJ_SETSTATE_REQ*)buffer, inLen);
        } else {
            st = STATUS_INVALID_PARAMETER;
        }
        break;

    default:
        st = STATUS_INVALID_DEVICE_REQUEST;
        break;
    }

    irp->IoStatus.Status      = st;
    irp->IoStatus.Information = information;
    IoCompleteRequest(irp, IO_NO_INCREMENT);
    return st;
}

/* ---------------------------------------------------------------------------
 * Unload / entry
 * ------------------------------------------------------------------------- */

extern "C" VOID NtObjUnload(PDRIVER_OBJECT driverObject)
{
    UNICODE_STRING symName;
    RtlInitUnicodeString(&symName, NTOBJ_SYMLINK_NT_NAME);
    IoDeleteSymbolicLink(&symName);
    if (driverObject->DeviceObject != NULL) {
        IoDeleteDevice(driverObject->DeviceObject);
    }
}

extern "C" NTSTATUS DriverEntry(PDRIVER_OBJECT driverObject, PUNICODE_STRING registryPath)
{
    UNICODE_STRING deviceName;
    UNICODE_STRING symName;
    PDEVICE_OBJECT device = NULL;
    NTSTATUS st;

    UNREFERENCED_PARAMETER(registryPath);

    RtlInitUnicodeString(&deviceName, NTOBJ_DEVICE_NAME_NT);
    RtlInitUnicodeString(&symName, NTOBJ_SYMLINK_NT_NAME);

    st = IoCreateDevice(driverObject, 0, &deviceName, FILE_DEVICE_UNKNOWN, 0, FALSE, &device);
    if (!NT_SUCCESS(st)) {
        return st;
    }

    driverObject->MajorFunction[IRP_MJ_CREATE]       = NtObjpCreateClose;
    driverObject->MajorFunction[IRP_MJ_CLOSE]        = NtObjpCreateClose;
    driverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = NtObjpDeviceControl;
    driverObject->DriverUnload = NtObjUnload;

    device->Flags |= DO_BUFFERED_IO;
    device->Flags &= ~DO_DEVICE_INITIALIZING;

    st = IoCreateSymbolicLink(&symName, &deviceName);
    if (!NT_SUCCESS(st)) {
        IoDeleteDevice(device);
        return st;
    }

    return STATUS_SUCCESS;
}

// [v1.1 resync 2026-10-23] current build-fix version
