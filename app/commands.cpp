/*
 * ntobj.exe - command engine.
 */
#include "ntobj_app.h"
#include "ntapi.h"
#include "drvio.h"
#include "objects.h"
#include <iostream>
#include <algorithm>
#include <cwctype>
#include <cwchar>
#include <cstdlib>

/* ---------------------------------------------------------------------------
 * Helpers
 * ------------------------------------------------------------------------- */

std::vector<std::wstring> Tokenize(const std::wstring& line)
{
    std::vector<std::wstring> out;
    std::wstring current;
    bool inQuotes = false;
    for (size_t i = 0; i < line.size(); ++i) {
        wchar_t c = line[i];
        if (c == L'"') {
            inQuotes = !inQuotes;
            continue;
        }
        if (!inQuotes && (c == L' ' || c == L'\t')) {
            if (!current.empty()) {
                out.push_back(current);
                current.clear();
            }
        } else {
            current += c;
        }
    }
    if (!current.empty()) {
        out.push_back(current);
    }
    return out;
}

std::wstring NormalizePath(const std::wstring& input)
{
    std::wstring p = input;
    /* trim surrounding whitespace */
    size_t b = p.find_first_not_of(L" \t");
    size_t e = p.find_last_not_of(L" \t");
    if (b == std::wstring::npos) {
        return std::wstring();
    }
    p = p.substr(b, e - b + 1);

    /* convenience: if user wrote "C:" or "D:" */
    if (p.size() >= 2 && iswalpha(p[0]) && p[1] == L':') {
        p = L"\\GLOBAL??\\" + p;
    }
    /* convenience: auto-prepend backslash if user typed "GLOBAL??\..." or "??\..." or "Device\..." */
    else if (!p.empty() && p[0] != L'\\') {
        p = L"\\" + p;
    }

    if (p.empty() || p[0] != L'\\') {
        return std::wstring();   /* NT namespace paths are absolute */
    }
    if (p.size() > NTOBJ_MAX_PATH - 2) {
        return std::wstring();   /* too long for the wire protocol */
    }
    /* strip trailing backslashes, keep the root "\" alone */
    while (p.size() > 1 && p[p.size() - 1] == L'\\') {
        p.erase(p.size() - 1);
    }
    return p;
}

static bool SameText(const std::wstring& a, const wchar_t* b)
{
    size_t la = a.size();
    size_t lb = wcslen(b);
    if (la != lb) {
        return false;
    }
    for (size_t i = 0; i < la; ++i) {
        if (towlower(a[i]) != towlower(b[i])) {
            return false;
        }
    }
    return true;
}

static bool EntryNameLess(const NtObjEntry& a, const NtObjEntry& b)
{
    size_t n = a.Name.size() < b.Name.size() ? a.Name.size() : b.Name.size();
    for (size_t i = 0; i < n; ++i) {
        wchar_t ca = towlower(a.Name[i]);
        wchar_t cb = towlower(b.Name[i]);
        if (ca != cb) {
            return ca < cb;
        }
    }
    return a.Name.size() < b.Name.size();
}

static std::wstring PadRight(const std::wstring& value, size_t width)
{
    if (value.size() >= width) {
        return value;
    }
    return value + std::wstring(width - value.size(), L' ');
}

static std::wstring JoinPath(const std::wstring& dir, const std::wstring& name)
{
    if (dir == L"\\") {
        return L"\\" + name;
    }
    return dir + L"\\" + name;
}

/* ---------------------------------------------------------------------------
 * Printing
 * ------------------------------------------------------------------------- */

static const size_t NAME_WIDTH = 44;
static const size_t TYPE_WIDTH = 20;
static const size_t LINK_RESOLVE_LIMIT = 200;   /* resolve "-> target" only for small dirs */

static void PrintEntries(const std::wstring& dir, const std::vector<NtObjEntry>& entries,
                         bool resolveLinks)
{
    for (size_t i = 0; i < entries.size(); ++i) {
        const NtObjEntry& e = entries[i];
        std::wstring line = L"  ";
        line += PadRight(e.Name, NAME_WIDTH);
        line += PadRight(e.Type, TYPE_WIDTH);
        if (resolveLinks && e.Type == L"SymbolicLink" && entries.size() <= LINK_RESOLVE_LIMIT) {
            std::wstring full = JoinPath(dir, e.Name);
            std::wstring target;
            if (NT_SUCCESS(ObjQuerySymlink(full, target))) {
                line += L" -> ";
                line += target;
            }
        }
        std::wcout << line << L"\n";
    }
}

static const int TREE_MAX_DEPTH = 10;
static const int TREE_MAX_NODES = 5000;

static void PrintTree(const std::wstring& dir, int depth, int* nodes)
{
    std::vector<NtObjEntry> entries;
    bool truncated = false;
    NTSTATUS st = ObjList(dir, entries, &truncated);
    if (!NT_SUCCESS(st)) {
        std::wstring op = L"  ";
        for (int i = 0; i < depth; ++i) {
            op += L"  ";
        }
        op += L"<cannot list ";
        op += dir;
        op += L": ";
        op += NtStatusName(st);
        op += L">";
        std::wcout << op << L"\n";
        return;
    }

    std::sort(entries.begin(), entries.end(), EntryNameLess);
    PrintEntries(dir, entries, false);

    if (truncated) {
        std::wcout << L"  (listing truncated)" << L"\n";
    }

    for (size_t i = 0; i < entries.size(); ++i) {
        if (*nodes >= TREE_MAX_NODES || depth + 1 >= TREE_MAX_DEPTH) {
            return;
        }
        if (entries[i].Type != L"Directory") {
            continue;   /* never descend through symbolic links */
        }
        ++(*nodes);
        std::wstring indent;
        for (int k = 0; k <= depth; ++k) {
            indent += L"  ";
        }
        std::wcout << indent << L"[" << entries[i].Name << L"]" << L"\n";
        PrintTree(JoinPath(dir, entries[i].Name), depth + 1, nodes);
    }
}

/* ---------------------------------------------------------------------------
 * Commands
 * ------------------------------------------------------------------------- */

static int CmdLs(const std::vector<std::wstring>& args)
{
    std::wstring dir = L"\\";
    if (args.size() > 1) {
        dir = NormalizePath(args[1]);
        if (dir.empty()) {
            std::wcout << L"Invalid path (absolute NT path starting with '\\' required)" << L"\n";
            return 2;
        }
    }
    std::vector<NtObjEntry> entries;
    bool truncated = false;
    NTSTATUS st = ObjList(dir, entries, &truncated);
    PrintStatus(L"Listing " + dir, st);
    if (!NT_SUCCESS(st)) {
        return 1;
    }
    std::sort(entries.begin(), entries.end(), EntryNameLess);
    std::wcout << PadRight(L"  Name", NAME_WIDTH) << PadRight(L"Type", TYPE_WIDTH) << L"\n";
    std::wstring rule(NAME_WIDTH + TYPE_WIDTH, L'-');
    std::wcout << L"  " << rule << L"\n";
    PrintEntries(dir, entries, true);
    if (truncated) {
        std::wcout << L"  (listing truncated - more entries exist)" << L"\n";
    }
    std::wcout << L"  " << entries.size() << L" object(s)" << L"\n";
    return 0;
}

static int CmdTree(const std::vector<std::wstring>& args)
{
    std::wstring dir = L"\\";
    if (args.size() > 1) {
        dir = NormalizePath(args[1]);
        if (dir.empty()) {
            std::wcout << L"Invalid path" << L"\n";
            return 2;
        }
    }
    int nodes = 0;
    std::wcout << dir << L"\n";
    PrintTree(dir, 0, &nodes);
    return 0;
}

static int CmdMkdir(const std::vector<std::wstring>& args)
{
    if (args.size() < 2) {
        std::wcout << L"Usage: mkdir <\\path\\to\\directory>" << L"\n";
        return 2;
    }
    std::wstring path = NormalizePath(args[1]);
    if (path.empty()) {
        std::wcout << L"Invalid path" << L"\n";
        return 2;
    }
    NTSTATUS st = ObjMakeDirectory(path);
    PrintStatus(L"Creating directory " + path, st);
    if (!NT_SUCCESS(st) && st == NTOBJ_ST_PRIVILEGE_NOT_HELD) {
        std::wcout << L"Hint: permanent objects need SeCreatePermanentPrivilege - "
                      L"run as Administrator or switch to driver mode (mode driver)." << L"\n";
    }
    return NT_SUCCESS(st) ? 0 : 1;
}

static int CmdDelete(const std::vector<std::wstring>& args)
{
    if (args.size() < 2) {
        std::wcout << L"Usage: del <\\path\\to\\object>  [aliases: delete, rm, rmdir, unlink]\n";
        return 2;
    }
    std::wstring path = NormalizePath(args[1]);
    if (path.empty() || path == L"\\") {
        std::wcout << L"Invalid path (refusing to touch the root)\n";
        return 2;
    }
    NTSTATUS st = ObjDeleteObject(path);
    PrintStatus(L"Deleting " + path, st);
    if (NT_SUCCESS(st)) {
        std::wcout << L"The object was marked temporary (unlinked from namespace); the name disappears "
                      L"once references are released.\n";
    }
    return NT_SUCCESS(st) ? 0 : 1;
}

static int CmdMklink(const std::vector<std::wstring>& args)
{
    if (args.size() < 3) {
        std::wcout << L"Usage: mklink <\\path\\link> <\\target\\path>" << L"\n";
        return 2;
    }
    std::wstring link = NormalizePath(args[1]);
    std::wstring target = NormalizePath(args[2]);
    if (link.empty() || target.empty()) {
        std::wcout << L"Invalid path" << L"\n";
        return 2;
    }
    NTSTATUS st = ObjMakeSymlink(link, target);
    PrintStatus(L"Creating link " + link + L" -> " + target, st);
    return NT_SUCCESS(st) ? 0 : 1;
}

static int CmdLinkInfo(const std::vector<std::wstring>& args)
{
    if (args.size() < 2) {
        std::wcout << L"Usage: ln <\\path\\link>" << L"\n";
        return 2;
    }
    std::wstring path = NormalizePath(args[1]);
    if (path.empty()) {
        std::wcout << L"Invalid path" << L"\n";
        return 2;
    }
    std::wstring target;
    NTSTATUS st = ObjQuerySymlink(path, target);
    if (NT_SUCCESS(st)) {
        std::wcout << path << L" -> " << target << L"\n";
        return 0;
    }
    PrintStatus(L"Reading link " + path, st);
    return 1;
}

static int CmdType(const std::vector<std::wstring>& args)
{
    if (args.size() < 2) {
        std::wcout << L"Usage: type <\\path\\object>\n";
        return 2;
    }
    std::wstring path = NormalizePath(args[1]);
    if (path.empty()) {
        std::wcout << L"Invalid path\n";
        return 2;
    }
    if (path == L"\\") {
        std::wcout << L"\\ is a Directory (root)\n";
        return 0;
    }
    ULONG kind = 0xFFFFFFFFUL;
    std::wstring typeName;
    NTSTATUS st = ObjDetectKind(path, &kind, &typeName);
    if (NT_SUCCESS(st) && !typeName.empty()) {
        std::wcout << path << L" is a " << typeName;
        if (typeName == L"SymbolicLink") {
            std::wstring target;
            if (NT_SUCCESS(ObjQuerySymlink(path, target))) {
                std::wcout << L" -> " << target;
            }
        }
        std::wcout << L"\n";
        return 0;
    }
    std::wcout << L"Object not found or cannot query type: " << path << L"\n";
    return 1;
}

static bool ParseKind(const std::wstring& text, ULONG* kind, const wchar_t** help)
{
    if (SameText(text, L"event"))          { *kind = NTOBJ_KIND_EVENT;     *help = L"0=notification 1=synchronization"; return true; }
    if (SameText(text, L"mutant") ||
        SameText(text, L"mutex"))          { *kind = NTOBJ_KIND_MUTANT;    *help = L""; return true; }
    if (SameText(text, L"semaphore"))      { *kind = NTOBJ_KIND_SEMAPHORE; *help = L"max count (default 1)"; return true; }
    if (SameText(text, L"timer"))          { *kind = NTOBJ_KIND_TIMER;     *help = L"0=notification 1=synchronization"; return true; }
    if (SameText(text, L"section"))        { *kind = NTOBJ_KIND_SECTION;   *help = L"size in bytes (default 65536)"; return true; }
    if (SameText(text, L"job"))            { *kind = NTOBJ_KIND_JOB;       *help = L""; return true; }
    return false;
}

static int CmdMkobj(const std::vector<std::wstring>& args)
{
    if (args.size() < 3) {
        std::wcout << L"Usage: mkobj <type> <\\path\\name> [param]\n"
                   << L"Supported types:\n"
                   << L"  event <path> [0|1]           notification (0) or synchronization (1)\n"
                   << L"  mutant <path>                mutex/mutant\n"
                   << L"  semaphore <path> [limit]     semaphore with max count\n"
                   << L"  timer <path> [0|1]           notification (0) or synchronization (1)\n"
                   << L"  section <path> [size_bytes]  memory section (default 64KB)\n"
                   << L"  job <path>                   job object\n"
                   << L"  dir <path>                   directory object\n"
                   << L"  link <path> <target>         symbolic link\n"
                   << L"  winstation <name>            window station (user32)\n"
                   << L"  desktop <name>               desktop object (user32)\n";
        return 2;
    }
    std::wstring type = args[1];

    if (SameText(type, L"dir") || SameText(type, L"directory")) {
        std::vector<std::wstring> sub = { L"mkdir", args[2] };
        return CmdMkdir(sub);
    }
    if (SameText(type, L"link") || SameText(type, L"symlink")) {
        if (args.size() < 4) {
            std::wcout << L"Usage: mkobj link <\\path\\link> <\\target\\path>\n";
            return 2;
        }
        std::vector<std::wstring> sub = { L"mklink", args[2], args[3] };
        return CmdMklink(sub);
    }
    if (SameText(type, L"winstation") || SameText(type, L"windowstation")) {
        NTSTATUS st = ObjCreateWindowStation(args[2]);
        PrintStatus(L"Creating WindowStation " + args[2], st);
        return NT_SUCCESS(st) ? 0 : 1;
    }
    if (SameText(type, L"desktop")) {
        NTSTATUS st = ObjCreateDesktop(args[2]);
        PrintStatus(L"Creating Desktop " + args[2], st);
        return NT_SUCCESS(st) ? 0 : 1;
    }

    ULONG kind = 0;
    const wchar_t* help = L"";
    if (!ParseKind(type, &kind, &help)) {
        std::wcout << L"Unknown object kind: " << type << L" (type 'types' to see all Object Manager types)\n";
        return 2;
    }
    std::wstring path = NormalizePath(args[2]);
    if (path.empty()) {
        std::wcout << L"Invalid path\n";
        return 2;
    }
    ULONG param1 = 0;
    if (args.size() > 3) {
        param1 = static_cast<ULONG>(wcstoul(args[3].c_str(), NULL, 0));
    }
    NTSTATUS st = ObjCreateObject(path, kind, param1);
    PrintStatus(L"Creating object " + path, st);
    return NT_SUCCESS(st) ? 0 : 1;
}

static int CmdSig(const std::vector<std::wstring>& args)
{
    if (args.size() < 2) {
        std::wcout << L"Usage: sig <\\path\\object> [signal|clear|release|set|reset] [value]\n";
        return 2;
    }
    std::wstring path = NormalizePath(args[1]);
    if (path.empty()) {
        std::wcout << L"Invalid path\n";
        return 2;
    }

    ULONG kind = 0xFFFFFFFFUL;
    std::wstring typeName;
    ObjDetectKind(path, &kind, &typeName);

    if (typeName == L"SymbolicLink") {
        std::wstring target;
        ObjQuerySymlink(path, target);
        std::wcout << path << L" is a SymbolicLink (target: " << target << L")\n"
                   << L"Hint: to change link destination, use: set " << path << L" <new_target>\n";
        if (args.size() >= 3) {
            std::wstring newTarget = NormalizePath(args[2]);
            if (newTarget.empty()) {
                newTarget = args[2];
            }
            std::wcout << L"Changing SymbolicLink target to " << newTarget << L"...\n";
            NTSTATUS st = ObjSetSymlinkTarget(path, newTarget);
            PrintStatus(L"Updating link target", st);
            return NT_SUCCESS(st) ? 0 : 1;
        }
        return 0;
    }

    if (kind == 0xFFFFFFFFUL) {
        std::wcout << L"Cannot find or open signalable object: " << path << L"\n";
        if (!typeName.empty()) {
            std::wcout << L"Object type is '" << typeName << L"' (not an Event/Mutant/Semaphore/Timer).\n";
        }
        return 1;
    }

    std::wstring actionStr = (args.size() >= 3) ? args[2] : L"signal";
    ULONG action = NTOBJ_ACTION_SIGNAL;
    if (SameText(actionStr, L"signal") || SameText(actionStr, L"set") || SameText(actionStr, L"1")) {
        action = NTOBJ_ACTION_SIGNAL;
    } else if (SameText(actionStr, L"clear") || SameText(actionStr, L"reset") || SameText(actionStr, L"0")) {
        action = NTOBJ_ACTION_CLEAR;
    } else if (SameText(actionStr, L"release")) {
        action = NTOBJ_ACTION_RELEASE;
    } else {
        std::wcout << L"Unknown action: " << actionStr << L" (use signal, clear, or release)\n";
        return 2;
    }

    ULONG value = 1;
    if (args.size() > 3) {
        value = static_cast<ULONG>(wcstoul(args[3].c_str(), NULL, 0));
    }
    NTSTATUS st = ObjSetState(path, kind, action, value);
    PrintStatus(L"Signaling " + path + L" (" + typeName + L")", st);
    return NT_SUCCESS(st) ? 0 : 1;
}

static int CmdSet(const std::vector<std::wstring>& args)
{
    if (args.size() < 3) {
        std::wcout << L"Usage: set <\\path\\object> <value>\n"
                   << L"  For SymbolicLink: changes the destination target\n"
                   << L"  For Event:        1/signal/set (activate) or 0/clear/reset (deactivate)\n"
                   << L"  For Semaphore:    releases count (e.g. set \\... 5)\n"
                   << L"  For Mutant:       releases ownership\n"
                   << L"  For Timer:        0 (cancel) or delay in ms\n";
        return 2;
    }
    std::wstring path = NormalizePath(args[1]);
    if (path.empty()) {
        std::wcout << L"Invalid path\n";
        return 2;
    }
    std::wstring val = args[2];

    ULONG kind = 0xFFFFFFFFUL;
    std::wstring typeName;
    ObjDetectKind(path, &kind, &typeName);

    if (typeName == L"SymbolicLink") {
        std::wstring target = NormalizePath(val);
        if (target.empty()) {
            target = val;
        }
        NTSTATUS st = ObjSetSymlinkTarget(path, target);
        PrintStatus(L"Setting link " + path + L" -> " + target, st);
        return NT_SUCCESS(st) ? 0 : 1;
    }

    if (kind == NTOBJ_KIND_EVENT) {
        ULONG action = (SameText(val, L"0") || SameText(val, L"clear") || SameText(val, L"reset") || SameText(val, L"false"))
                       ? NTOBJ_ACTION_CLEAR : NTOBJ_ACTION_SIGNAL;
        NTSTATUS st = ObjSetState(path, kind, action, 0);
        PrintStatus(L"Setting Event " + path, st);
        return NT_SUCCESS(st) ? 0 : 1;
    }

    if (kind == NTOBJ_KIND_SEMAPHORE) {
        ULONG count = static_cast<ULONG>(wcstoul(val.c_str(), NULL, 0));
        NTSTATUS st = ObjSetState(path, kind, NTOBJ_ACTION_RELEASE, count ? count : 1);
        PrintStatus(L"Releasing Semaphore " + path, st);
        return NT_SUCCESS(st) ? 0 : 1;
    }

    if (kind == NTOBJ_KIND_MUTANT) {
        NTSTATUS st = ObjSetState(path, kind, NTOBJ_ACTION_RELEASE, 1);
        PrintStatus(L"Releasing Mutant " + path, st);
        return NT_SUCCESS(st) ? 0 : 1;
    }

    if (kind == NTOBJ_KIND_TIMER) {
        if (SameText(val, L"0") || SameText(val, L"cancel") || SameText(val, L"clear")) {
            NTSTATUS st = ObjSetState(path, kind, NTOBJ_ACTION_CLEAR, 0);
            PrintStatus(L"Canceling Timer " + path, st);
            return NT_SUCCESS(st) ? 0 : 1;
        } else {
            ULONG ms = static_cast<ULONG>(wcstoul(val.c_str(), NULL, 0));
            NTSTATUS st = ObjSetState(path, kind, NTOBJ_ACTION_SIGNAL, ms);
            PrintStatus(L"Setting Timer " + path, st);
            return NT_SUCCESS(st) ? 0 : 1;
        }
    }

    std::wcout << L"Unknown or unsupported object type for 'set': " << (typeName.empty() ? L"(unknown)" : typeName) << L"\n";
    return 1;
}

static int CmdTypes(const std::vector<std::wstring>& args)
{
    (void)args;
    std::vector<NtObjEntry> types;
    NTSTATUS st = ObjListTypes(types);
    if (!NT_SUCCESS(st)) {
        PrintStatus(L"Listing \\ObjectTypes", st);
        return 1;
    }
    std::sort(types.begin(), types.end(), EntryNameLess);

    std::wcout << L"Object Types registered in \\ObjectTypes (" << types.size() << L" total):\n";
    std::wcout << L"  " << PadRight(L"Type Name", 36) << L"Capabilities / Creation\n";
    std::wcout << L"  " << std::wstring(72, L'-') << L"\n";

    for (size_t i = 0; i < types.size(); ++i) {
        const std::wstring& t = types[i].Name;
        std::wstring desc;
        if (SameText(t, L"Directory"))            desc = L"[creatable: mkdir / mkobj dir]";
        else if (SameText(t, L"SymbolicLink"))    desc = L"[creatable: mklink / mkobj link; settable: set]";
        else if (SameText(t, L"Event"))           desc = L"[creatable: mkobj event; signalable: sig / set]";
        else if (SameText(t, L"Mutant"))          desc = L"[creatable: mkobj mutant; releasable: sig / set]";
        else if (SameText(t, L"Semaphore"))       desc = L"[creatable: mkobj semaphore; releasable: sig / set]";
        else if (SameText(t, L"Timer"))           desc = L"[creatable: mkobj timer; settable: sig / set]";
        else if (SameText(t, L"Section"))         desc = L"[creatable: mkobj section]";
        else if (SameText(t, L"Job"))             desc = L"[creatable: mkobj job]";
        else if (SameText(t, L"WindowStation"))   desc = L"[creatable: mkobj winstation]";
        else if (SameText(t, L"Desktop"))         desc = L"[creatable: mkobj desktop]";
        else if (SameText(t, L"Session"))         desc = L"[managed by kernel / sessions]";

        std::wcout << L"  " << PadRight(t, 36) << desc << L"\n";
    }
    return 0;
}

static int CmdDrv(const std::vector<std::wstring>& args)
{
    (void)args;
    ULONG version = 0;
    if (DetectDriver(&version)) {
        std::wcout << L"Driver: CONNECTED (device \\\\.\\NtObjDrv, protocol 0x"
                   << std::hex << version << std::dec << L")" << L"\n";
    } else {
        std::wcout << L"Driver: NOT CONNECTED - install it with scripts\\install-driver.bat "
                      L"(run as Administrator)" << L"\n";
    }
    std::wcout << L"Backend mode: " << GetBackendModeName();
    std::wcout << L" (effective: " << (DriverActive() ? L"driver" : L"direct ntdll") << L")" << L"\n";
    return 0;
}

static int CmdMode(const std::vector<std::wstring>& args)
{
    if (args.size() < 2) {
        std::wcout << L"Backend mode: " << GetBackendModeName() << L"\n";
        std::wcout << L"Usage: mode <auto|driver|direct>" << L"\n";
        return 0;
    }
    if (SameText(args[1], L"auto")) {
        SetBackendMode(BackendMode::Auto);
    } else if (SameText(args[1], L"driver")) {
        SetBackendMode(BackendMode::Driver);
    } else if (SameText(args[1], L"direct")) {
        SetBackendMode(BackendMode::Direct);
    } else {
        std::wcout << L"Unknown mode: " << args[1] << L"\n";
        return 2;
    }
    std::wcout << L"Backend mode set to " << GetBackendModeName() << L"\n";
    return 0;
}

static void PrintHelp()
{
    std::wcout <<
        L"Commands:\n"
        L"  ls <path>                    list a directory (root = \\)       [alias: dir]\n"
        L"  tree [path]                  recursive listing (no symlink follow)\n"
        L"  mkdir <path>                 create a directory object         [alias: md]\n"
        L"  del <path>                   delete / unlink any single object [alias: rm, delete, rmdir]\n"
        L"  mklink <link> <target>       create a symbolic link\n"
        L"  ln <link>                    show symbolic link target         [alias: readlink]\n"
        L"  type <path>                  show the object type\n"
        L"  types                        list all types registered in \\ObjectTypes\n"
        L"  mkobj <type> <path> [param]  create object (event, mutant, semaphore, timer, section, job, winstation, desktop)\n"
        L"  sig <path> [action] [value]  signal/clear/release a sync object [actions: signal, clear, release]\n"
        L"  set <path> <value>           change object value or link target [alias: setval]\n"
        L"  drv                          show driver connection status\n"
        L"  mode [auto|driver|direct]    show or set the backend mode\n"
        L"  help                         this text\n"
        L"  exit                         quit\n"
        L"\n"
        L"Examples:\n"
        L"  ls \\Device\n"
        L"  types\n"
        L"  mkobj event \\BaseNamedObjects\\MyEvent 0\n"
        L"  sig \\BaseNamedObjects\\MyEvent signal\n"
        L"  set \\BaseNamedObjects\\MyEvent 0\n"
        L"  del \\BaseNamedObjects\\MyEvent\n"
        L"  mklink \\GLOBAL??\\X: \\Device\\HarddiskVolume1\n"
        L"  set \\GLOBAL??\\X: \\Device\\HarddiskVolume2\n"
        L"  del \\GLOBAL??\\X:\n";
}

bool CommandIsExit(const std::vector<std::wstring>& args)
{
    if (args.empty()) {
        return false;
    }
    return SameText(args[0], L"exit") || SameText(args[0], L"quit");
}

int ExecuteCommand(const std::vector<std::wstring>& args)
{
    if (args.empty()) {
        return 0;
    }
    const std::wstring& cmd = args[0];
    int result = 0;

    if (SameText(cmd, L"help") || SameText(cmd, L"?")) {
        PrintHelp();
    } else if (SameText(cmd, L"ls") || SameText(cmd, L"dir")) {
        result = CmdLs(args);
    } else if (SameText(cmd, L"tree")) {
        result = CmdTree(args);
    } else if (SameText(cmd, L"mkdir") || SameText(cmd, L"md")) {
        result = CmdMkdir(args);
    } else if (SameText(cmd, L"rmdir") || SameText(cmd, L"rm") ||
               SameText(cmd, L"del") || SameText(cmd, L"delete") ||
               SameText(cmd, L"unlink")) {
        result = CmdDelete(args);
    } else if (SameText(cmd, L"mklink")) {
        result = CmdMklink(args);
    } else if (SameText(cmd, L"ln") || SameText(cmd, L"readlink")) {
        result = CmdLinkInfo(args);
    } else if (SameText(cmd, L"type")) {
        result = CmdType(args);
    } else if (SameText(cmd, L"types")) {
        result = CmdTypes(args);
    } else if (SameText(cmd, L"mkobj")) {
        result = CmdMkobj(args);
    } else if (SameText(cmd, L"sig")) {
        result = CmdSig(args);
    } else if (SameText(cmd, L"set") || SameText(cmd, L"setval")) {
        result = CmdSet(args);
    } else if (SameText(cmd, L"drv")) {
        result = CmdDrv(args);
    } else if (SameText(cmd, L"mode")) {
        result = CmdMode(args);
    } else {
        std::wcout << L"Unknown command: " << cmd << L" (type 'help')" << L"\n";
        result = 2;
    }
    return result;
}

// [v1.1 resync 2026-10-23] current build-fix version
