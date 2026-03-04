# Hook Target Analysis: Windows 11 Explorer Size Column

## Summary

Explorer's Size column is populated by internal functions in `windows.storage.dll`, resolved via PDB symbol names at runtime. For folders, the primary function returns `VT_EMPTY` (blank). We intercept this and inject the folder size from Everything.

**Total hooks: 6 (3 required, 3 recommended)**

---

## Hook 1: `CFSFolder::_GetSize` (REQUIRED — Primary Hook)

**Module**: `windows.storage.dll`  
**Resolution**: PDB symbol (not exported)  
**Symbol name (x64)**:
```
protected: static long __cdecl CFSFolder::_GetSize(class CFSFolder *,struct _ITEMID_CHILD const __unaligned *,struct IDFOLDER const __unaligned *,struct tagPROPVARIANT *)
```

### Signature
```cpp
HRESULT WINAPI CFSFolder_GetSize(
    void*                pCFSFolder,   // CFSFolder* — can QI to IShellFolder2
    const ITEMID_CHILD*  itemidChild,   // Child PIDL identifying the item
    const void*          idFolder,      // IDFOLDER* — internal opaque struct
    PROPVARIANT*         propVariant    // Output: VT_UI8 for files, VT_EMPTY for folders
);
```

### Behavior
- Called by Explorer for **every item** in the details view when the Size column is visible.
- For **files**: returns `S_OK`, sets `propVariant->vt = VT_UI8`, `propVariant->uhVal.QuadPart = file_size`.
- For **folders**: returns `S_OK`, sets `propVariant->vt = VT_EMPTY` (displayed as blank).
- Called lazily — only for **visible items** (virtualized list). When scrolling, new items trigger calls.
- Called **multiple times per item** during sort-by-size operations.

### Hook Strategy
1. Call original function first.
2. Check: if `ret == S_OK && propVariant->vt == VT_EMPTY` → this is a folder.
3. If folder:
   a. QI `pCFSFolder` to `IShellFolder2` via `((IUnknown*)pCFSFolder)->QueryInterface(IID_IShellFolder2, ...)`.
   b. Use `IShellFolder2::BindToObject(itemidChild, nullptr, IID_IShellFolder2, &childFolder)` to get the child folder's shell interface.
   c. Extract the folder path via `IPersistFolder2` → `GetCurFolder()` → `SHGetPathFromIDListEx()`.
   d. Query our `SizeCache` first. On cache miss, query `EverythingClient`.
   e. Set `propVariant->vt = VT_UI8` and `propVariant->uhVal.QuadPart = folder_size`.
4. If anything fails, return the original result (folder shows blank — fail-silent).

### Path Extraction
```
IShellFolder2::BindToObject(itemidChild) → IShellFolder2 childFolder
childFolder→QueryInterface(IID_IPersistFolder2) → IPersistFolder2
IPersistFolder2::GetCurFolder() → PIDL (absolute)
SHGetPathFromIDListEx(pidl) → wchar_t path[]
Strip "\\\\?\\" prefix if present (long path support)
```

### Why This Function
- It's the ONLY function that receives both the **item identity** (PIDL) and the **size output** (PROPVARIANT*).
- `PSFormatForDisplayAlloc` receives the PROPVARIANT but NOT the item path — it can only format, not decide WHAT to format.
- Hooking at this level means Explorer's entire property pipeline (formatting, sorting, caching) works naturally with our injected value.

---

## Hook 2: `CRecursiveFolderOperation::Prepare` + `Do` (REQUIRED — Guard Hooks)

**Module**: `windows.storage.dll`  
**Resolution**: PDB symbol (not exported)

### Symbol names (x64)
```
public: long __cdecl CRecursiveFolderOperation::Prepare(void)
public: virtual long __cdecl CRecursiveFolderOperation::Do(void)
```

### Signatures
```cpp
HRESULT __thiscall CRecursiveFolderOperation_Prepare(void* pThis);
HRESULT __thiscall CRecursiveFolderOperation_Do(void* pThis);
```

### Why These Are Needed
When Explorer performs file operations (copy, move, delete), it internally calls `CFSFolder::_GetSize` to compute total sizes for progress bars and confirmation dialogs. Without these guards:
- Our hook would inject Everything sizes during copy/move/delete operations.
- This would give **incorrect** results (Everything's index may not match actual files being operated on).
- It would **slow down** file operations with unnecessary IPC calls to Everything.

### Hook Strategy — Improvement Over Windhawk

**Windhawk's approach (BUGGY)**:
```cpp
// BAD: bare thread_local booleans, no RAII, can leak on exception
thread_local bool g_inCRecursiveFolderOperation_Prepare;
// Manually set true/false around original call
```

**Our approach (IMPROVED)**:
```cpp
// RAII scoped guard with thread_local depth counter
class RecursiveOpGuard {
    static thread_local int s_depth;  // per-thread nesting depth
public:
    RecursiveOpGuard() noexcept { ++s_depth; }
    ~RecursiveOpGuard() noexcept { --s_depth; }
    static bool active() noexcept { return s_depth > 0; }
    // Non-copyable, non-movable
    RecursiveOpGuard(const RecursiveOpGuard&) = delete;
    RecursiveOpGuard& operator=(const RecursiveOpGuard&) = delete;
};

// In hook callbacks:
HRESULT CRecursiveFolderOperation_Prepare_Hook(void* pThis) {
    RecursiveOpGuard guard;  // RAII: auto-increments, auto-decrements
    return Original(pThis);
}

// In CFSFolder_GetSize_Hook:
if (RecursiveOpGuard::active()) return original_result;  // skip our logic
```

**Improvements over Windhawk**:
1. **RAII**: Guard is exception-safe — counter always decremented even on exception/longjmp.
2. **Nesting support**: Depth counter instead of boolean handles nested recursive operations.
3. **No manual set/clear**: Impossible to forget the clear or have mismatched set/clear.

---

## Hook 3: `PSFormatForDisplayAlloc` (RECOMMENDED — Smart Formatting)

**Module**: `propsys.dll`  
**Resolution**: Exported function (GetProcAddress)

### Signature (from Microsoft docs)
```cpp
HRESULT WINAPI PSFormatForDisplayAlloc(
    REFPROPERTYKEY        key,          // Property being formatted
    REFPROPVARIANT        propvar,      // Value to format
    PROPDESC_FORMAT_FLAGS pdff,         // Format flags
    PWSTR*                ppszDisplay   // Output: allocated formatted string
);
```

### Why This Hook
- Explorer's default formatting shows ALL sizes in KB (e.g., "1,572,864 KB" for a 1.5 GB folder).
- Our requirement: "Smart size formatting (B, KB, MB, GB, TB)" — match our `format_size_for_column()` output.
- This hook intercepts the formatting step and substitutes our formatting for PKEY_Size values.

### Hook Strategy
1. Check if `key == PKEY_Size` (fmtid = `{B725F130-47EF-101A-A5F1-02608C9EEBAC}`, pid = 12).
2. If not PKEY_Size, call original and return.
3. If PKEY_Size and `propvar.vt == VT_UI8`:
   a. Extract the raw size: `propvar.uhVal.QuadPart`.
   b. Format using our `format_size_for_column()`.
   c. Allocate output string with `CoTaskMemAlloc` and copy our formatted string.
   d. Return `S_OK`.
4. If PKEY_Size but empty (`VT_EMPTY`), call original (returns blank for unsized items).

### PKEY_Size Definition
```cpp
// {B725F130-47EF-101A-A5F1-02608C9EEBAC}, 12
DEFINE_PROPERTYKEY(PKEY_Size, 0xB725F130, 0x47EF, 0x101A, 0xA5, 0xF1, 0x02, 0x60, 0x8C, 0x9E, 0xEB, 0xAC, 12);
```

---

## Hook 4: `RegQueryValueExW` (RECOMMENDED — Extended View Support)

**Module**: `kernelbase.dll`
**Resolution**: Exported function (GetProcAddress)

### Behavior
Explorer reads shell property format strings from `HKCR\Folder` and `HKCR\Directory`
to decide which properties to display in Tiles, Content, Details-pane, and Status-bar
views.  By default these strings omit `System.Size` for folders.

Our hook intercepts reads of five specific values and injects `System.Size`:

| Value Name | Key | Injection |
|---|---|---|
| `TileInfo` | `HKCR\Folder` | append `;System.Size` |
| `ContentViewModeForBrowse` | `HKCR\Folder` | append `;System.Size` |
| `ContentViewModeForSearch` | `HKCR\Folder` | append `;System.Size` |
| `PreviewDetails` | `HKCR\Directory` | insert `;System.Size` after `DateModified` |
| `StatusBar` | `HKCR\Directory` | insert `~System.Size;` as 2nd item |

Key identity is verified via `NtQueryKey(KeyNameInformation)` — only the HKCR
classes hive (`\REGISTRY\MACHINE\SOFTWARE\Classes\` or
`\REGISTRY\USER\..._Classes\`) is intercepted.

Combined with the existing `_GetSize` hook that provides the folder size value,
this enables folder sizes in all Explorer view modes.

---

## Hook 5: `PSFormatForDisplay` (RECOMMENDED — Legacy Dialog Support)

**Module**: `propsys.dll`
**Resolution**: Exported function (GetProcAddress)

Buffer-writing variant of `PSFormatForDisplayAlloc`.  Used by older shell
dialogs (legacy Open/Save dialogs, regedit export).  Identical logic to
Hook 3 but writes into the caller's `LPWSTR pwszText` buffer instead of
allocating a new string.

---

## Hook 6: Shell Change Notification — Cache Invalidation (UNIQUE)

**Not a Detours hook** — runs as a background thread with a hidden
message-only window registered via `SHChangeNotifyRegister`.

Monitors `SHCNE_UPDATEDIR | SHCNE_RMDIR | SHCNE_MKDIR | SHCNE_CREATE |
SHCNE_DELETE | SHCNE_RENAMEITEM | SHCNE_RENAMEFOLDER` events for the
entire shell namespace (desktop root, recursive).

On each event: the affected path **and all ancestor directories** are
invalidated from `SizeCache`, so re-navigating a parent folder shows
fresh sizes immediately.

**Windhawk has no cache invalidation** — sizes stay stale until the
5-minute TTL expires.

---

## Hook 7: `CFSFolder::CompareIDs` (OPTIONAL — Sort Mixing)

**Module**: `windows.storage.dll`  
**Resolution**: PDB symbol

### Symbol name (x64)
```
public: virtual long __cdecl CFSFolder::CompareIDs(__int64,struct _ITEMIDLIST_RELATIVE const __unaligned *,struct _ITEMIDLIST_RELATIVE const __unaligned *)
```

### Signature
```cpp
HRESULT WINAPI CFSFolder_CompareIDs(
    void*                          pCFSFolder,
    LPARAM                         lParam,      // LOWORD = column index
    const ITEMIDLIST_RELATIVE*     pidl1,
    const ITEMIDLIST_RELATIVE*     pidl2
);
```

### Purpose
By default, Explorer keeps files and folders separated when sorting. When sorting by size with folder sizes enabled, users expect folders and files to be interleaved by size. This hook can override the comparison to mix them.

### Deferral Decision
**Deferred to post-MVP**. The core value (displaying folder sizes) doesn't require this. Sorting works; it just keeps files and folders in separate groups.

---

## Symbol Resolution Strategy

All `windows.storage.dll` hooks require PDB symbol resolution (functions are not exported).

### Resolution Chain
1. **Primary**: Download PDB from Microsoft Symbol Server via `SymInitialize` + `SymLoadModuleEx` + `SymFromName`.
   - Symbol server: `srv*C:\\Symbols*https://msdl.microsoft.com/download/symbols`
   - Module: `windows.storage.dll` (loaded into Explorer's process space)
2. **Fallback**: If PDB download fails (no internet, symbol server down), DLL becomes a **silent no-op**.
3. **No hardcoded offsets** — offsets change with every Windows update.

### DbgHelp API Usage
```cpp
SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_DEBUG);
SymInitialize(GetCurrentProcess(), "srv*C:\\Symbols*https://msdl.microsoft.com/download/symbols", FALSE);
HMODULE hMod = GetModuleHandleW(L"windows.storage.dll");
SymLoadModuleEx(GetCurrentProcess(), nullptr, "windows.storage.dll", nullptr, (DWORD64)hMod, 0, nullptr, 0);

SYMBOL_INFO_PACKAGE sip = {};
sip.si.SizeOfStruct = sizeof(SYMBOL_INFO);
sip.si.MaxNameLen = MAX_SYM_NAME;
SymFromName(GetCurrentProcess(), "CFSFolder::_GetSize", &sip.si);
// sip.si.Address = function address to hook
```

### For propsys.dll
`PSFormatForDisplayAlloc` is an **exported** function — use `GetProcAddress(GetModuleHandleW(L"propsys.dll"), "PSFormatForDisplayAlloc")`. No PDB needed.

---

## Reversibility

All hooks are installed via Microsoft Detours, which:
1. Saves the original function bytes and restores them on `DetourDetach`.
2. Our DLL is loaded via COM registration — `regsvr32 /u` removes the registration.
3. After unregistration + Explorer restart, the DLL is no longer loaded and all hooks are gone.
4. **No permanent changes** to any system DLLs or Explorer binaries.

---

## Comparison with Windhawk — Our Improvements

| Aspect | Windhawk | Ours |
|--------|----------|------|
| Guard mechanism | `thread_local bool` (no RAII) | RAII `RecursiveOpGuard` scoped class |
| Global state | Many globals (`g_settings`, `g_cache*`, `g_gs*`) | Meyers singletons (SizeCache, EverythingClient, HookManager) |
| SEH protection | None — hooks can crash Explorer | Every hook callback wrapped in `__try/__except` |
| Error handling | `Wh_Log` only | Multi-output logging (OutputDebugString + file) with source location |
| Everything client | Embedded SDK3 source (~800 lines) | Clean C++ client class with RAII (EverythingClient singleton) |
| Cache | Ad-hoc `std::map` with tick-based expiry | Thread-safe LRU cache with TTL, bounded memory (50MB) |
| Hook framework | Windhawk API (DLL injection) | COM shell extension + Microsoft Detours (fully reversible) |
| Loading mechanism | Windhawk service (detected as virus) | Standard COM registration (`regsvr32`) — no third-party service |
| Uninstallation | Requires Windhawk uninstall | `regsvr32 /u foldersize.dll` — one command |
| Reparse/OneDrive | Known bugs (#1527) | resolve_real_path via GetFinalPathNameByHandle; query Everything with canonical path |
| Tiles / Content view sizes | ✓ (RegQueryValueEx hook) | ✓ (RegQueryValueExW hook, kernelbase.dll) |
| Details-pane / Status-bar sizes | ✓ | ✓ (PreviewDetails + StatusBar injection) |
| PSFormatForDisplay (legacy) | ✓ | ✓ (PSFormatForDisplay + PSFormatForDisplayAlloc both hooked) |
| Cache invalidation on FS changes | ✗ — stale forever | ✓ SHChangeNotifyRegister thread, ancestor cascade |
| Process exclusion | Manual exclude list | Only loads into Explorer (COM registration is Explorer-specific) |

---

## Risk Assessment

| Risk | Likelihood | Impact | Mitigation |
|------|-----------|--------|------------|
| PDB symbols unavailable | Low | DLL is no-op (no sizes, no crash) | Graceful fallback chain |
| Symbol names change in Windows update | Medium | DLL is no-op until updated | Log warning, user can check DebugView |
| Explorer internal structure changes | Low-Medium | Hook signature mismatch → SEH catches | SEH wrapper returns original result |
| Everything service not running | Low | Folders show blank (stock behavior) | Fail-silent, no error dialogs |
| Slow Everything IPC | Low | Sizes delayed but no hang | 2-second timeout per query, cache mitigates |
