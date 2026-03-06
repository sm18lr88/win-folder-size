#include <windows.h>
#include <psapi.h>
#include <dbghelp.h>
#include <propsys.h>
#include <propvarutil.h>
#include <shlobj.h>
#include <propkey.h>
#include <detours/detours.h>
#include <cstring>
#include <mutex>
#include <atomic>

#include "hooks/hook_manager.h"
#include "hooks/detour_wrapper.h"
#include "hooks/reg_query_hook.h"
#include "logging.h"
#include "core/init.h"
#include "core/size_formatter.h"
#include "core/size_resolver.h"

// ============================================================================
// DbgHelp Dynamic Loading
// ============================================================================
// Explorer uses the system dbghelp.dll which lacks symsrv.dll support.
// We explicitly load the Debuggers version which can download PDBs from
// Microsoft's symbol server.

using pSymSetOptions_t = DWORD(WINAPI*)(DWORD);
using pSymInitialize_t = BOOL(WINAPI*)(HANDLE, PCSTR, BOOL);
using pSymCleanup_t = BOOL(WINAPI*)(HANDLE);
using pSymLoadModuleEx_t = DWORD64(WINAPI*)(HANDLE, HANDLE, PCSTR, PCSTR, DWORD64, DWORD, PMODLOAD_DATA, DWORD);
using pSymFromName_t = BOOL(WINAPI*)(HANDLE, PCSTR, PSYMBOL_INFO);
using pSymEnumSymbols_t = BOOL(WINAPI*)(HANDLE, ULONG64, PCSTR, PSYM_ENUMERATESYMBOLS_CALLBACK, PVOID);

static HMODULE s_hDbgHelp = nullptr;
static pSymSetOptions_t    pSymSetOptions    = nullptr;
static pSymInitialize_t    pSymInitialize    = nullptr;
static pSymCleanup_t       pSymCleanup       = nullptr;
static pSymLoadModuleEx_t  pSymLoadModuleEx  = nullptr;
static pSymFromName_t      pSymFromName      = nullptr;
static pSymEnumSymbols_t   pSymEnumSymbols   = nullptr;

static bool load_dbghelp() {
    // Try the Windows Kits Debuggers version first (has symsrv.dll support)
    s_hDbgHelp = LoadLibraryExW(
        L"C:\\Program Files (x86)\\Windows Kits\\10\\Debuggers\\x64\\dbghelp.dll",
        nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);

    if (s_hDbgHelp) {
        fs::log::diagnostic_log("Loaded Debuggers dbghelp.dll (symsrv support)");
    } else {
        // Fall back to system dbghelp.dll (no symsrv, but works with cached PDBs)
        fs::log::diagnostic_logf("Debuggers dbghelp.dll not found (err=0x%lx), using system", GetLastError());
        s_hDbgHelp = LoadLibraryW(L"dbghelp.dll");
        if (!s_hDbgHelp) {
            fs::log::diagnostic_logf("System dbghelp.dll load FAILED err=0x%lx", GetLastError());
            return false;
        }
        fs::log::diagnostic_log("Loaded system dbghelp.dll (no symsrv)");
    }

    pSymSetOptions   = (pSymSetOptions_t)  GetProcAddress(s_hDbgHelp, "SymSetOptions");
    pSymInitialize   = (pSymInitialize_t)  GetProcAddress(s_hDbgHelp, "SymInitialize");
    pSymCleanup      = (pSymCleanup_t)     GetProcAddress(s_hDbgHelp, "SymCleanup");
    pSymLoadModuleEx = (pSymLoadModuleEx_t)GetProcAddress(s_hDbgHelp, "SymLoadModuleExW");
    pSymFromName     = (pSymFromName_t)    GetProcAddress(s_hDbgHelp, "SymFromName");
    pSymEnumSymbols  = (pSymEnumSymbols_t) GetProcAddress(s_hDbgHelp, "SymEnumSymbols");

    // SymLoadModuleExW might not exist in older versions, try SymLoadModuleEx
    if (!pSymLoadModuleEx) {
        pSymLoadModuleEx = (pSymLoadModuleEx_t)GetProcAddress(s_hDbgHelp, "SymLoadModuleEx");
    }

    if (!pSymSetOptions || !pSymInitialize || !pSymCleanup || !pSymLoadModuleEx || !pSymFromName) {
        fs::log::diagnostic_log("Failed to resolve DbgHelp function pointers");
        return false;
    }

    fs::log::diagnostic_log("All DbgHelp function pointers resolved");
    return true;
}
namespace fs::hooks {

// ============================================================================
// RecursiveOpGuard Implementation
// ============================================================================
thread_local int RecursiveOpGuard::s_depth = 0;

// ============================================================================
// Hook Function Pointers (file-static)
// ============================================================================

// CFSFolder::_GetSize hook
using CFSFolder_GetSize_t = HRESULT(WINAPI*)(void*, const ITEMID_CHILD*, const void*, PROPVARIANT*);
static CFSFolder_GetSize_t s_original_GetSize = nullptr;

// Inner function — has all the C++ logic (try/catch, RAII objects, etc.)
static HRESULT hooked_GetSize_inner(void* pCFSFolder, const ITEMID_CHILD* pidlChild,
                                     const void* idFolder, PROPVARIANT* pv) {
    static std::atomic<int> s_getsize_calls{0};
    int call_num = s_getsize_calls.fetch_add(1) + 1;
    if (call_num <= 50) {
        fs::log::diagnostic_logf("GetSize hook call #%d tid=%lu vt=%d",
                                 call_num, (unsigned long)GetCurrentThreadId(),
                                 (int)pv->vt);
    }
    try {
        FS_TRACE(FS_MOD_HOOK, "hooked_GetSize called");
        HRESULT hr = s_original_GetSize(pCFSFolder, pidlChild, idFolder, pv);
        if (hr != S_OK || pv->vt != VT_EMPTY) {
            return hr;
        }
        if (RecursiveOpGuard::active()) {
            return hr;
        }
        if (!fs::core::is_initialized()) return hr;
        fs::core::SizeResolver::instance().resolve_and_inject(pCFSFolder, pidlChild, pv);
        return hr;
    } catch (...) {
        FS_ERROR(FS_MOD_HOOK, "C++ exception in hooked_GetSize");
        return s_original_GetSize(pCFSFolder, pidlChild, idFolder, pv);
    }
}

// Outer function — SEH wrapper, no C++ objects with destructors
static HRESULT WINAPI hooked_GetSize(void* pCFSFolder, const ITEMID_CHILD* pidlChild,
                                      const void* idFolder, PROPVARIANT* pv) {
    __try {
        return hooked_GetSize_inner(pCFSFolder, pidlChild, idFolder, pv);
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        FS_ERROR(FS_MOD_HOOK, "SEH exception in hooked_GetSize");
        return s_original_GetSize(pCFSFolder, pidlChild, idFolder, pv);
    }
}

// CRecursiveFolderOperation::Prepare hook
using RecursiveOp_Prepare_t = HRESULT(WINAPI*)(void*);
static RecursiveOp_Prepare_t s_original_Prepare = nullptr;

// Inner function — has all the C++ logic (try/catch, RAII objects, etc.)
static HRESULT hooked_Prepare_inner(void* pThis) {
    try {
        FS_TRACE(FS_MOD_HOOK, "hooked_Prepare called");
        RecursiveOpGuard guard;
        return s_original_Prepare(pThis);
    } catch (...) {
        FS_ERROR(FS_MOD_HOOK, "C++ exception in hooked_Prepare");
        return s_original_Prepare(pThis);
    }
}

// Outer function — SEH wrapper, no C++ objects with destructors
static HRESULT WINAPI hooked_Prepare(void* pThis) {
    __try {
        return hooked_Prepare_inner(pThis);
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        FS_ERROR(FS_MOD_HOOK, "SEH exception in hooked_Prepare");
        return s_original_Prepare(pThis);
    }
}

// CRecursiveFolderOperation::Do hook
using RecursiveOp_Do_t = HRESULT(WINAPI*)(void*);
static RecursiveOp_Do_t s_original_Do = nullptr;

// Inner function — has all the C++ logic (try/catch, RAII objects, etc.)
static HRESULT hooked_Do_inner(void* pThis) {
    try {
        FS_TRACE(FS_MOD_HOOK, "hooked_Do called");
        RecursiveOpGuard guard;
        return s_original_Do(pThis);
    } catch (...) {
        FS_ERROR(FS_MOD_HOOK, "C++ exception in hooked_Do");
        return s_original_Do(pThis);
    }
}

// Outer function — SEH wrapper, no C++ objects with destructors
static HRESULT WINAPI hooked_Do(void* pThis) {
    __try {
        return hooked_Do_inner(pThis);
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        FS_ERROR(FS_MOD_HOOK, "SEH exception in hooked_Do");
        return s_original_Do(pThis);
    }
}

// PSFormatForDisplayAlloc hook (from propsys.dll)
using PSFormatForDisplayAlloc_t = HRESULT(WINAPI*)(REFPROPERTYKEY, REFPROPVARIANT,
                                                     PROPDESC_FORMAT_FLAGS, PWSTR*);
static PSFormatForDisplayAlloc_t s_original_PSFormat = nullptr;

// Inner function — has all the C++ logic (try/catch, RAII objects, etc.)
static HRESULT hooked_PSFormat_inner(REFPROPERTYKEY key, REFPROPVARIANT propvar,
                                     PROPDESC_FORMAT_FLAGS pdff, PWSTR* ppszDisplay) {
    static std::atomic<int> s_psformat_calls{0};
    int call_num = s_psformat_calls.fetch_add(1) + 1;
    if (call_num <= 50) {
        fs::log::diagnostic_logf("PSFormat hook call #%d tid=%lu key_pid=%lu vt=%d",
                                 call_num, (unsigned long)GetCurrentThreadId(),
                                 (unsigned long)key.pid, (int)propvar.vt);
    }
    try {
        if (!fs::core::is_initialized()) {
            return s_original_PSFormat(key, propvar, pdff, ppszDisplay);
        }
        HRESULT fmt_hr = fs::core::SizeResolver::instance().format_size_display(
            key, propvar, pdff, ppszDisplay);
        if (fmt_hr == S_OK) return S_OK;
        return s_original_PSFormat(key, propvar, pdff, ppszDisplay);
    } catch (...) {
        FS_ERROR(FS_MOD_HOOK, "C++ exception in hooked_PSFormat");
        return s_original_PSFormat(key, propvar, pdff, ppszDisplay);
    }
}

// Outer function — SEH wrapper, no C++ objects with destructors
static HRESULT WINAPI hooked_PSFormat(REFPROPERTYKEY key, REFPROPVARIANT propvar,
                                       PROPDESC_FORMAT_FLAGS pdff, PWSTR* ppszDisplay) {
    __try {
        return hooked_PSFormat_inner(key, propvar, pdff, ppszDisplay);
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        FS_ERROR(FS_MOD_HOOK, "SEH exception in hooked_PSFormat");
        return s_original_PSFormat(key, propvar, pdff, ppszDisplay);
    }
}

// PSFormatForDisplay hook (non-Alloc variant, from propsys.dll)
// Used by older shell dialogs (Open/Save in legacy apps, regedit export).
// Identical logic to PSFormatForDisplayAlloc but writes into a caller buffer.
using PSFormatForDisplay_t = HRESULT(WINAPI*)(REFPROPERTYKEY, REFPROPVARIANT,
                                               PROPDESC_FORMAT_FLAGS, LPWSTR, DWORD);
static PSFormatForDisplay_t s_original_PSFormatNonAlloc = nullptr;

static HRESULT hooked_PSFormatNonAlloc_inner(REFPROPERTYKEY key, REFPROPVARIANT propvar,
                                              PROPDESC_FORMAT_FLAGS pdff,
                                              LPWSTR pwszText, DWORD cchText) {
    try {
        if (!fs::core::is_initialized() || cchText == 0) {
            return s_original_PSFormatNonAlloc(key, propvar, pdff, pwszText, cchText);
        }
        if (!IsEqualPropertyKey(key, PKEY_Size) || propvar.vt != VT_UI8) {
            return s_original_PSFormatNonAlloc(key, propvar, pdff, pwszText, cchText);
        }

        uint64_t bytes = propvar.uhVal.QuadPart;
        std::wstring formatted = fs::format_size_for_shell_column(bytes);

        // Copy into caller-supplied buffer; _TRUNCATE ensures null termination
        wcsncpy_s(pwszText, cchText, formatted.c_str(), _TRUNCATE);
        return S_OK;
    } catch (...) {
        FS_ERROR(FS_MOD_HOOK, "C++ exception in hooked_PSFormatNonAlloc");
        return s_original_PSFormatNonAlloc(key, propvar, pdff, pwszText, cchText);
    }
}

static HRESULT WINAPI hooked_PSFormatNonAlloc(REFPROPERTYKEY key, REFPROPVARIANT propvar,
                                               PROPDESC_FORMAT_FLAGS pdff,
                                               LPWSTR pwszText, DWORD cchText) {
    __try {
        return hooked_PSFormatNonAlloc_inner(key, propvar, pdff, pwszText, cchText);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        FS_ERROR(FS_MOD_HOOK, "SEH exception in hooked_PSFormatNonAlloc");
        return s_original_PSFormatNonAlloc(key, propvar, pdff, pwszText, cchText);
    }
}

// ============================================================================
// Symbol Resolution Helper
// ============================================================================

static bool resolve_symbol(const char* symbol_name, const char* fallback_decorated,
                           DWORD64 module_base, DWORD64* out_address) {
    (void)module_base;  // Unused parameter
    
    if (!out_address || !pSymFromName) {
        return false;
    }

    SYMBOL_INFO* sym_info = (SYMBOL_INFO*)malloc(sizeof(SYMBOL_INFO) + 256);
    if (!sym_info) {
        FS_ERROR(FS_MOD_HOOK, "Failed to allocate SYMBOL_INFO");
        return false;
    }

    sym_info->SizeOfStruct = sizeof(SYMBOL_INFO);
    sym_info->MaxNameLen = 256;

    // Try FULL undecorated signature first (to match exact overload)
    // Then fall back to short name only if full name fails
    BOOL found = FALSE;
    DWORD64 short_addr = 0;
    DWORD64 full_addr = 0;

    if (fallback_decorated) {
        found = pSymFromName(GetCurrentProcess(), fallback_decorated, sym_info);
        if (found) {
            full_addr = sym_info->Address;
            fs::log::diagnostic_logf("SymFromName FULL matched '%s' -> Name='%s' addr=0x%llx",
                                     fallback_decorated, sym_info->Name, sym_info->Address);
        }
    }

    // Also try short name to compare (diagnostic)
    SYMBOL_INFO* sym_info2 = (SYMBOL_INFO*)malloc(sizeof(SYMBOL_INFO) + 256);
    if (sym_info2) {
        sym_info2->SizeOfStruct = sizeof(SYMBOL_INFO);
        sym_info2->MaxNameLen = 256;
        BOOL found_short = pSymFromName(GetCurrentProcess(), symbol_name, sym_info2);
        if (found_short) {
            short_addr = sym_info2->Address;
            fs::log::diagnostic_logf("SymFromName SHORT matched '%s' -> Name='%s' addr=0x%llx",
                                     symbol_name, sym_info2->Name, sym_info2->Address);
            if (full_addr && short_addr != full_addr) {
                fs::log::diagnostic_logf("WARNING: SHORT addr 0x%llx != FULL addr 0x%llx — WRONG OVERLOAD!",
                                         short_addr, full_addr);
            }
            if (!found) {
                // Use short name result as fallback
                memcpy(sym_info, sym_info2, sizeof(SYMBOL_INFO) + 256);
                found = TRUE;
            }
        }
        free(sym_info2);
    }

    if (!found) {
        FS_WARN(FS_MOD_HOOK, "Failed to resolve symbol: %s (error: 0x%lx)", symbol_name,
                GetLastError());
        free(sym_info);
        return false;
    }

    *out_address = sym_info->Address;
    FS_DEBUG(FS_MOD_HOOK, "Resolved symbol '%s' to address 0x%llx", symbol_name, *out_address);
    free(sym_info);
    return true;
}

// ============================================================================
// HookManager Implementation
// ============================================================================

HookManager& HookManager::instance() {
    static HookManager s_instance;
    return s_instance;
}

bool HookManager::install_hooks() {
    std::call_once(m_initFlag, [this]() {
        FS_INFO(FS_MOD_HOOK, "Installing hooks...");

        // Load windows.storage.dll module
        HMODULE storage_dll = GetModuleHandleW(L"windows.storage.dll");
        if (!storage_dll) {
            fs::log::diagnostic_log("windows.storage.dll NOT LOADED");
            return;
        }
        DWORD64 storage_base = (DWORD64)storage_dll;
        fs::log::diagnostic_logf("windows.storage.dll base=0x%llx", (unsigned long long)storage_dll);

        // ================================================================
        // Resolve target function addresses using RVA offsets
        // RVAs verified via SymEnumSymbols on windows.storage.dll Build 26200.7840
        // Prologue byte validation catches DLL version mismatches
        // ================================================================
        DWORD64 addr_GetSize = 0;
        DWORD64 addr_Prepare = 0;
        DWORD64 addr_Do = 0;

        struct RvaTarget {
            const char* name;
            DWORD64* addr;
            DWORD64 rva;
            unsigned char expected_prologue[8];  // all-zero = skip validation
        };
        RvaTarget targets[] = {
            {"CFSFolder::_GetSize",               &addr_GetSize,  0x79100,  {0x40, 0x55, 0x53, 0x56, 0x57, 0x41, 0x54, 0x41}},
            {"CRecursiveFolderOperation::Prepare", &addr_Prepare,  0x6008C, {0}},
            {"CRecursiveFolderOperation::Do",      &addr_Do,       0x5F6D0, {0}},
        };

        bool all_resolved = true;
        for (auto& t : targets) {
            DWORD64 candidate = storage_base + t.rva;
            unsigned char actual[8];
            memcpy(actual, (void*)candidate, sizeof(actual));

            // Validate prologue bytes (catches DLL version changes)
            bool prologue_ok = (t.expected_prologue[0] == 0) ||
                               (memcmp(actual, t.expected_prologue, sizeof(actual)) == 0);

            if (prologue_ok) {
                *t.addr = candidate;
                fs::log::diagnostic_logf("%s -> 0x%llx (RVA 0x%llx, prologue OK)",
                                         t.name, candidate, t.rva);
            } else {
                fs::log::diagnostic_logf("%s RVA MISMATCH: expected %02x%02x%02x%02x got %02x%02x%02x%02x",
                                         t.name,
                                         t.expected_prologue[0], t.expected_prologue[1],
                                         t.expected_prologue[2], t.expected_prologue[3],
                                         actual[0], actual[1], actual[2], actual[3]);
                all_resolved = false;
            }
        }

        if (!all_resolved) {
            // Prologue mismatch — DLL version changed. Try DbgHelp as last resort.
            fs::log::diagnostic_log("Prologue mismatch detected, trying DbgHelp symbol resolution...");
            if (load_dbghelp()) {
                pSymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_NO_PROMPTS);
                if (pSymInitialize(GetCurrentProcess(), "srv*C:\\Symbols*https://msdl.microsoft.com/download/symbols", FALSE)) {
                    char storage_path[MAX_PATH] = {};
                    GetModuleFileNameA(storage_dll, storage_path, MAX_PATH);
                    MODULEINFO mod_info = {};
                    GetModuleInformation(GetCurrentProcess(), storage_dll, &mod_info, sizeof(mod_info));
                    pSymLoadModuleEx(GetCurrentProcess(), nullptr, storage_path,
                                    nullptr, storage_base, mod_info.SizeOfImage, nullptr, 0);
                    if (!addr_GetSize) resolve_symbol("CFSFolder::_GetSize", nullptr, storage_base, &addr_GetSize);
                    if (!addr_Prepare) resolve_symbol("CRecursiveFolderOperation::Prepare", nullptr, storage_base, &addr_Prepare);
                    if (!addr_Do) resolve_symbol("CRecursiveFolderOperation::Do", nullptr, storage_base, &addr_Do);
                    pSymCleanup(GetCurrentProcess());
                }
            }
        }

        fs::log::diagnostic_logf("Final: GetSize=0x%llx Prepare=0x%llx Do=0x%llx",
                                 addr_GetSize, addr_Prepare, addr_Do);

        if (!addr_GetSize || !addr_Prepare || !addr_Do) {
            FS_ERROR(FS_MOD_HOOK, "Failed to resolve critical function addresses");
            return;
        }

        // Get PSFormatForDisplayAlloc from propsys.dll
        HMODULE propsys_dll = GetModuleHandleW(L"propsys.dll");
        if (!propsys_dll) {
            FS_ERROR(FS_MOD_HOOK, "propsys.dll not loaded");
            // DbgHelp cleanup happens in fallback path only
            return;
        }

        PSFormatForDisplayAlloc_t pPSFormat =
            (PSFormatForDisplayAlloc_t)GetProcAddress(propsys_dll, "PSFormatForDisplayAlloc");
        if (!pPSFormat) {
            FS_ERROR(FS_MOD_HOOK, "Failed to get PSFormatForDisplayAlloc: 0x%lx",
                     GetLastError());
            // DbgHelp cleanup happens in fallback path only
            return;
        }

        PSFormatForDisplay_t pPSFormatNonAlloc =
            (PSFormatForDisplay_t)GetProcAddress(propsys_dll, "PSFormatForDisplay");
        // Non-alloc variant is optional — older propsys versions may lack it
        if (!pPSFormatNonAlloc) {
            fs::log::diagnostic_logf("PSFormatForDisplay not found (err=0x%lx), skipping",
                                     GetLastError());
        }

        // Set up original function pointers from resolved addresses
        s_original_GetSize = (CFSFolder_GetSize_t)addr_GetSize;
        s_original_Prepare = (RecursiveOp_Prepare_t)addr_Prepare;
        s_original_Do = (RecursiveOp_Do_t)addr_Do;
        s_original_PSFormat = pPSFormat;
        s_original_PSFormatNonAlloc = pPSFormatNonAlloc;

        // Begin Detours transaction
        DetourTransaction txn;
        if (!txn.begin()) {
            FS_ERROR(FS_MOD_HOOK, "Failed to begin Detours transaction");
            // DbgHelp cleanup happens in fallback path only
            return;
        }

        // Attach all hooks
        bool attach_ok = true;
        attach_ok &= (DetourAttach(&(PVOID&)s_original_GetSize, hooked_GetSize) == NO_ERROR);
        attach_ok &= (DetourAttach(&(PVOID&)s_original_Prepare, hooked_Prepare) == NO_ERROR);
        attach_ok &= (DetourAttach(&(PVOID&)s_original_Do, hooked_Do) == NO_ERROR);
        attach_ok &= (DetourAttach(&(PVOID&)s_original_PSFormat, hooked_PSFormat) == NO_ERROR);
        if (s_original_PSFormatNonAlloc) {
            attach_ok &= (DetourAttach(&(PVOID&)s_original_PSFormatNonAlloc,
                                       hooked_PSFormatNonAlloc) == NO_ERROR);
        }

        // Install RegQueryValueExW hook (kernelbase.dll — extends Tiles/Content/
        // Details-pane/Status-bar support) within the same transaction.
        install_reg_query_hook();

        if (!attach_ok) {
            FS_ERROR(FS_MOD_HOOK, "One or more DetourAttach calls failed");
            txn.abort();
            // DbgHelp cleanup happens in fallback path only
            return;
        }

        // Commit transaction
        if (!txn.commit()) {
            FS_ERROR(FS_MOD_HOOK, "Failed to commit Detours transaction");
            // DbgHelp cleanup happens in fallback path only
            return;
        }

        fs::log::diagnostic_log("Detours transaction committed");
        {
            char exe_path[MAX_PATH] = {};
            GetModuleFileNameA(nullptr, exe_path, MAX_PATH);
            fs::log::diagnostic_logf("Process: pid=%lu exe=%s",
                                     GetCurrentProcessId(), exe_path);
        }

        m_active = true;
        fs::log::diagnostic_log("Hooks installed successfully");
        FS_INFO(FS_MOD_HOOK, "All hooks installed successfully");
    });

    return m_active;
}

void HookManager::remove_hooks() {
    if (!m_active) {
        FS_DEBUG(FS_MOD_HOOK, "Hooks not active, nothing to remove");
        return;
    }

    FS_INFO(FS_MOD_HOOK, "Removing hooks...");

    DetourTransaction txn;
    if (!txn.begin()) {
        FS_ERROR(FS_MOD_HOOK, "Failed to begin Detours transaction for hook removal");
        return;
    }

    bool detach_ok = true;

    if (s_original_GetSize) {
        if (DetourDetach(&(PVOID&)s_original_GetSize, hooked_GetSize) != NO_ERROR) {
            FS_ERROR(FS_MOD_HOOK, "DetourDetach failed for GetSize");
            detach_ok = false;
        } else {
            FS_DEBUG(FS_MOD_HOOK, "Detached hook for CFSFolder::_GetSize");
        }
    }

    if (s_original_Prepare) {
        if (DetourDetach(&(PVOID&)s_original_Prepare, hooked_Prepare) != NO_ERROR) {
            FS_ERROR(FS_MOD_HOOK, "DetourDetach failed for Prepare");
            detach_ok = false;
        } else {
            FS_DEBUG(FS_MOD_HOOK, "Detached hook for CRecursiveFolderOperation::Prepare");
        }
    }

    if (s_original_Do) {
        if (DetourDetach(&(PVOID&)s_original_Do, hooked_Do) != NO_ERROR) {
            FS_ERROR(FS_MOD_HOOK, "DetourDetach failed for Do");
            detach_ok = false;
        } else {
            FS_DEBUG(FS_MOD_HOOK, "Detached hook for CRecursiveFolderOperation::Do");
        }
    }

    if (s_original_PSFormat) {
        if (DetourDetach(&(PVOID&)s_original_PSFormat, hooked_PSFormat) != NO_ERROR) {
            FS_ERROR(FS_MOD_HOOK, "DetourDetach failed for PSFormat");
            detach_ok = false;
        } else {
            FS_DEBUG(FS_MOD_HOOK, "Detached hook for PSFormatForDisplayAlloc");
        }
    }

    if (s_original_PSFormatNonAlloc) {
        if (DetourDetach(&(PVOID&)s_original_PSFormatNonAlloc,
                         hooked_PSFormatNonAlloc) != NO_ERROR) {
            FS_ERROR(FS_MOD_HOOK, "DetourDetach failed for PSFormatNonAlloc");
            detach_ok = false;
        } else {
            FS_DEBUG(FS_MOD_HOOK, "Detached hook for PSFormatForDisplay");
        }
    }

    remove_reg_query_hook();

    if (!detach_ok) {
        FS_ERROR(FS_MOD_HOOK, "One or more hook detachments failed, aborting transaction");
        txn.abort();
        return;
    }

    if (!txn.commit()) {
        FS_ERROR(FS_MOD_HOOK, "Failed to commit Detours transaction for hook removal");
        return;
    }

    // Cleanup DbgHelp
    if (pSymCleanup) pSymCleanup(GetCurrentProcess());

    m_active = false;
    FS_INFO(FS_MOD_HOOK, "All hooks removed successfully");
}

bool HookManager::is_active() const {
    return m_active;
}

} // namespace fs::hooks
