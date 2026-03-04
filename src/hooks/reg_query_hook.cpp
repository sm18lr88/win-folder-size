#include "hooks/reg_query_hook.h"
#include "hooks/detour_wrapper.h"
#include "logging.h"

#include <windows.h>
#include <detours/detours.h>
#include <algorithm>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// NtQueryKey — used to get the full registry path for an HKEY so we can
// confirm it belongs to HKCR\Folder or HKCR\Directory before modifying the
// returned value.  We load it dynamically to avoid a hard ntdll import.
// ---------------------------------------------------------------------------

// NtQueryKey info class we need (value = 3 = KeyNameInformation)
static constexpr int kKeyNameInformation = 3;

// Layout of the kernel structure for KeyNameInformation
struct KeyNameInfo {
    ULONG  NameLength; // byte count, NOT including null terminator
    WCHAR  Name[2048]; // plenty for any registry path
};

using NtQueryKey_t = LONG(NTAPI*)(HANDLE, int, void*, ULONG, ULONG*);
static NtQueryKey_t s_NtQueryKey = nullptr;

namespace {

// Return the NT registry path for hKey, e.g.
//   \REGISTRY\MACHINE\SOFTWARE\Classes\Folder
// Returns empty string on failure.
static std::wstring get_key_path(HKEY hKey) {
    if (!s_NtQueryKey) return {};

    KeyNameInfo info = {};
    ULONG returned = 0;
    LONG status = s_NtQueryKey(hKey, kKeyNameInformation,
                               &info, sizeof(info), &returned);
    // STATUS_SUCCESS = 0
    if (status != 0) return {};

    return std::wstring(info.Name, info.NameLength / sizeof(WCHAR));
}

// Case-insensitive suffix check: does path end with suffix?
static bool ends_with_icase(const std::wstring& s, const wchar_t* suffix) {
    size_t slen = wcslen(suffix);
    if (s.size() < slen) return false;
    return _wcsicmp(s.c_str() + s.size() - slen, suffix) == 0;
}

// True if hKey resolves to HKCR\<subkey> (either HKLM or HKCU classes hive).
// subkey example: L"Folder" or L"Directory"
static bool matches_class_subkey(HKEY hKey, const wchar_t* subkey) {
    auto path = get_key_path(hKey);
    if (path.empty()) return false;

    // Build the suffix we need to match: \SOFTWARE\Classes\<subkey>
    std::wstring suffix = L"\\SOFTWARE\\Classes\\";
    suffix += subkey;
    return ends_with_icase(path, suffix.c_str());
}

// True if the property string already contains System.Size in any decorated
// form (System.Size, ~System.Size, *System.Size).
static bool has_system_size(const std::wstring& val) {
    // case-insensitive search for "System.Size"
    auto it = std::search(val.cbegin(), val.cend(),
                          L"System.Size", L"System.Size" + 11,
                          [](wchar_t a, wchar_t b) {
                              return towlower(a) == towlower(b);
                          });
    return it != val.cend();
}

// Append ";toAdd" at end of val if System.Size is not already present.
static void append_system_size(std::wstring& val, const wchar_t* toAdd) {
    if (has_system_size(val)) return;
    val += L';';
    val += toAdd;
}

// Insert ";toInsert" immediately after the first occurrence of markerKey
// (case-insensitive search for the substring).  If markerKey not found,
// falls back to appending at the end.
static void insert_after_key(std::wstring& val,
                              const wchar_t* markerKey,
                              const wchar_t* toInsert) {
    if (has_system_size(val)) return;

    // Find markerKey substring (case-insensitive)
    size_t mklen = wcslen(markerKey);
    auto it = std::search(val.cbegin(), val.cend(),
                          markerKey, markerKey + mklen,
                          [](wchar_t a, wchar_t b) {
                              return towlower(a) == towlower(b);
                          });
    if (it == val.cend()) {
        // Marker not found — append at end
        val += L';';
        val += toInsert;
        return;
    }
    // Advance past the marker to the end of this token (next ';' or end)
    size_t pos = static_cast<size_t>(it - val.cbegin()) + mklen;
    size_t nextSemi = val.find(L';', pos);
    std::wstring insertion = L";";
    insertion += toInsert;
    if (nextSemi == std::wstring::npos) {
        val += insertion;
    } else {
        val.insert(nextSemi, insertion);
    }
}

// Insert "toInsert;" right after the first ';' (i.e., as the 2nd item in the
// semicolon-separated list).  If there is no ';', appends at end.
static void insert_at_position2(std::wstring& val, const wchar_t* toInsert) {
    if (has_system_size(val)) return;

    size_t firstSemi = val.find(L';');
    std::wstring insertion = toInsert;
    insertion += L';';
    if (firstSemi == std::wstring::npos) {
        val += L';';
        val += toInsert;
    } else {
        val.insert(firstSemi + 1, insertion);
    }
}

// ============================================================================
// Target descriptor table
// ============================================================================
enum class InjectMode {
    Append,           // append ;System.Size at end
    InsertAfterKey,   // insert ;System.Size after a named key substring
    InsertAtPos2,     // insert ~System.Size; as 2nd list item
};

struct RegTarget {
    const wchar_t* valueName;
    const wchar_t* classSubkey; // "Folder" or "Directory"
    InjectMode      mode;
    const wchar_t* markerKey;   // used for InsertAfterKey only
    const wchar_t* toInject;    // the property string fragment
    const wchar_t* synthDefault;// synthetic value when key absent (nullptr = skip)
};

static const RegTarget k_targets[] = {
    {
        L"TileInfo",
        L"Folder",
        InjectMode::Append,
        nullptr,
        L"System.Size",
        L"prop:*System.ItemType;System.Size"
    },
    {
        L"ContentViewModeForBrowse",
        L"Folder",
        InjectMode::Append,
        nullptr,
        L"System.Size",
        L"prop:Name;DateModified;System.Size"
    },
    {
        L"ContentViewModeForSearch",
        L"Folder",
        InjectMode::Append,
        nullptr,
        L"System.Size",
        L"prop:Name;DateModified;System.Size"
    },
    {
        L"PreviewDetails",
        L"Directory",
        InjectMode::InsertAfterKey,
        L"DateModified",
        L"System.Size",
        L"prop:*System.DateModified;System.Size;System.ItemFolderNameDisplay"
    },
    {
        L"StatusBar",
        L"Directory",
        InjectMode::InsertAtPos2,
        nullptr,
        L"~System.Size",
        L"prop:*System.DateModified;~System.Size"
    },
};
static constexpr int k_num_targets = static_cast<int>(sizeof(k_targets) / sizeof(k_targets[0]));

static const RegTarget* find_target(LPCWSTR valueName) {
    if (!valueName) return nullptr;
    for (int i = 0; i < k_num_targets; ++i) {
        if (_wcsicmp(valueName, k_targets[i].valueName) == 0)
            return &k_targets[i];
    }
    return nullptr;
}

// Apply the injection strategy to a mutable value string.
static void apply_injection(std::wstring& val, const RegTarget& t) {
    switch (t.mode) {
    case InjectMode::Append:
        append_system_size(val, t.toInject);
        break;
    case InjectMode::InsertAfterKey:
        insert_after_key(val, t.markerKey, t.toInject);
        break;
    case InjectMode::InsertAtPos2:
        insert_at_position2(val, t.toInject);
        break;
    }
}

// Copy a wstring value into the caller's RegQueryValueEx output buffers.
// Returns ERROR_SUCCESS, ERROR_MORE_DATA, or leaves *lpcbData set for
// size-query calls (lpData == nullptr).
static LSTATUS return_string_value(const std::wstring& val,
                                   LPDWORD lpType,
                                   LPBYTE  lpData,
                                   LPDWORD lpcbData) {
    DWORD cbNeeded = static_cast<DWORD>((val.size() + 1) * sizeof(wchar_t));
    if (lpType)    *lpType = REG_SZ;
    if (!lpcbData) return ERROR_SUCCESS;

    if (!lpData) {
        // Size-query call — report how much space we need
        *lpcbData = cbNeeded;
        return ERROR_SUCCESS;
    }
    if (*lpcbData < cbNeeded) {
        *lpcbData = cbNeeded;
        return ERROR_MORE_DATA;
    }
    memcpy(lpData, val.c_str(), cbNeeded);
    *lpcbData = cbNeeded;
    return ERROR_SUCCESS;
}

} // anonymous namespace

// ============================================================================
// Hook implementation
// ============================================================================
using RegQueryValueExW_t = LSTATUS(WINAPI*)(HKEY, LPCWSTR, LPDWORD, LPDWORD,
                                             LPBYTE, LPDWORD);
static RegQueryValueExW_t s_original_RegQueryValueEx = nullptr;

static LSTATUS hooked_RegQueryValueEx_inner(HKEY hKey, LPCWSTR lpValueName,
                                             LPDWORD lpReserved, LPDWORD lpType,
                                             LPBYTE lpData, LPDWORD lpcbData) {
    // Fast pre-filter: skip unknown value names immediately
    const RegTarget* target = find_target(lpValueName);
    if (!target) {
        return s_original_RegQueryValueEx(hKey, lpValueName, lpReserved,
                                          lpType, lpData, lpcbData);
    }

    // Confirm the key belongs to the right class hive
    if (!matches_class_subkey(hKey, target->classSubkey)) {
        return s_original_RegQueryValueEx(hKey, lpValueName, lpReserved,
                                          lpType, lpData, lpcbData);
    }

    // Read the original value into a local buffer (property strings are small)
    static constexpr DWORD kBufCch = 2048;
    wchar_t buf[kBufCch];
    DWORD   cbBuf  = kBufCch * sizeof(wchar_t);
    DWORD   dwType = 0;
    LSTATUS ret    = s_original_RegQueryValueEx(hKey, lpValueName, nullptr,
                                                &dwType, reinterpret_cast<LPBYTE>(buf),
                                                &cbBuf);

    if (ret == ERROR_FILE_NOT_FOUND) {
        // Value absent — provide a synthetic default that already has System.Size
        if (!target->synthDefault) {
            return ret; // no default defined, pass through
        }
        FS_DEBUG("REGHOOK", "Synthesizing '%ls' for key '%ls'",
                 lpValueName, target->classSubkey);
        return return_string_value(target->synthDefault, lpType, lpData, lpcbData);
    }

    if (ret == ERROR_MORE_DATA) {
        // Our stack buffer was too small (very unusual for these property strings).
        // Fall through with enlarged size hint.
        if (lpcbData) *lpcbData = cbBuf + 128;
        return ret;
    }

    if (ret != ERROR_SUCCESS || dwType != REG_SZ) {
        // Unexpected — let caller handle it normally
        return s_original_RegQueryValueEx(hKey, lpValueName, lpReserved,
                                          lpType, lpData, lpcbData);
    }

    // Build an std::wstring from the raw buffer (cbBuf is bytes actually written,
    // including the null terminator)
    size_t cchRead = cbBuf / sizeof(wchar_t);
    std::wstring value(buf, cchRead);
    // Strip embedded nulls at the end (RegQueryValueEx includes the null)
    while (!value.empty() && value.back() == L'\0') value.pop_back();

    // Inject System.Size if not already present
    apply_injection(value, *target);

    FS_TRACE("REGHOOK", "Returning modified '%ls': %ls",
             lpValueName, value.c_str());
    return return_string_value(value, lpType, lpData, lpcbData);
}

static LSTATUS WINAPI hooked_RegQueryValueEx(HKEY hKey, LPCWSTR lpValueName,
                                              LPDWORD lpReserved, LPDWORD lpType,
                                              LPBYTE lpData, LPDWORD lpcbData) {
    __try {
        return hooked_RegQueryValueEx_inner(hKey, lpValueName, lpReserved,
                                            lpType, lpData, lpcbData);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        FS_ERROR("REGHOOK", "SEH exception in hooked_RegQueryValueEx");
        return s_original_RegQueryValueEx(hKey, lpValueName, lpReserved,
                                           lpType, lpData, lpcbData);
    }
}

// ============================================================================
// Install / Remove
// ============================================================================
namespace fs::hooks {

void install_reg_query_hook() {
    // Load NtQueryKey from ntdll (already in process for any Win32 process)
    if (!s_NtQueryKey) {
        HMODULE hNtdll = GetModuleHandleW(L"ntdll.dll");
        if (hNtdll) {
            s_NtQueryKey = reinterpret_cast<NtQueryKey_t>(
                GetProcAddress(hNtdll, "NtQueryKey"));
        }
        if (!s_NtQueryKey) {
            fs::log::diagnostic_log("RegQueryHook: NtQueryKey not found, hook disabled");
            return;
        }
    }

    // Get RegQueryValueExW from kernelbase (the canonical implementation)
    HMODULE hKernelBase = GetModuleHandleW(L"kernelbase.dll");
    if (!hKernelBase) {
        fs::log::diagnostic_log("RegQueryHook: kernelbase.dll not loaded");
        return;
    }

    auto* pFunc = reinterpret_cast<RegQueryValueExW_t>(
        GetProcAddress(hKernelBase, "RegQueryValueExW"));
    if (!pFunc) {
        fs::log::diagnostic_log("RegQueryHook: RegQueryValueExW not found");
        return;
    }

    s_original_RegQueryValueEx = pFunc;
    LONG err = DetourAttach(reinterpret_cast<PVOID*>(&s_original_RegQueryValueEx),
                            hooked_RegQueryValueEx);
    if (err != NO_ERROR) {
        fs::log::diagnostic_logf("RegQueryHook: DetourAttach failed err=%ld", err);
        s_original_RegQueryValueEx = nullptr;
        return;
    }
    fs::log::diagnostic_log("RegQueryHook: installed RegQueryValueExW hook");
}

void remove_reg_query_hook() {
    if (!s_original_RegQueryValueEx) return;
    LONG err = DetourDetach(reinterpret_cast<PVOID*>(&s_original_RegQueryValueEx),
                            hooked_RegQueryValueEx);
    if (err != NO_ERROR) {
        fs::log::diagnostic_logf("RegQueryHook: DetourDetach failed err=%ld", err);
    } else {
        fs::log::diagnostic_log("RegQueryHook: queued RegQueryValueExW detach");
        // Do NOT null s_original_RegQueryValueEx here — the DetourDetach is only
        // queued, not committed yet.  If the outer transaction is aborted, the hook
        // remains installed and hooked_RegQueryValueEx still needs the pointer to
        // call through to the original.  The caller (remove_hooks) will set
        // m_active=false after a successful commit, preventing further hook calls.
    }
}

} // namespace fs::hooks
