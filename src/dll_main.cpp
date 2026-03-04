#include <windows.h>
#include "logging.h"
#include "core/init.h"

static HINSTANCE g_hInstance = nullptr;

HINSTANCE GetDllInstance() noexcept { return g_hInstance; }



BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID lpReserved) {
    switch (reason) {
    case DLL_PROCESS_ATTACH:
        g_hInstance = hModule;
        DisableThreadLibraryCalls(hModule);
        fs::log::init_logging();
        fs::log::diagnostic_logf("DLL loaded pid=%lu", GetCurrentProcessId());
        FS_INFO(FS_MOD_LIFECYCLE, "FolderSize DLL loaded into process %lu",
                GetCurrentProcessId());
        fs::core::initialize_foldersize();
        break;
    case DLL_PROCESS_DETACH:
        FS_INFO(FS_MOD_LIFECYCLE, "FolderSize DLL unloading (lpReserved=%p)", lpReserved);
        if (!lpReserved) {
            // Dynamic unload - safe to do full cleanup
            fs::core::shutdown_foldersize();
        }
        fs::log::shutdown_logging();
        break;
    }
    return TRUE;
}
