#include <windows.h>
#include <shlobj.h>
#include <olectl.h>
#include <initguid.h>
#include "guids.h"
#include "logging.h"
#include "com/class_factory.h"

extern HINSTANCE GetDllInstance() noexcept;

HRESULT WINAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, LPVOID* ppv) {
    if (!ppv) {
        return E_INVALIDARG;
    }

    FS_DEBUG(FS_MOD_COM, "DllGetClassObject called");

    if (rclsid != CLSID_FolderSizeExtension) {
        FS_DEBUG(FS_MOD_COM, "DllGetClassObject - CLSID not recognized");
        return CLASS_E_CLASSNOTAVAILABLE;
    }

    ClassFactory* pFactory = new ClassFactory();
    if (!pFactory) {
        FS_ERROR(FS_MOD_COM, "DllGetClassObject - failed to allocate ClassFactory");
        return E_OUTOFMEMORY;
    }

    HRESULT hr = pFactory->QueryInterface(riid, ppv);
    pFactory->Release();

    if (FAILED(hr)) {
        FS_ERROR(FS_MOD_COM, "DllGetClassObject - QueryInterface failed: 0x%08lx", hr);
    } else {
        FS_DEBUG(FS_MOD_COM, "DllGetClassObject - success");
    }

    return hr;
}

HRESULT WINAPI DllCanUnloadNow() {
    BOOL canUnload = (g_lockCount == 0 && g_objectCount == 0);
    HRESULT hr = canUnload ? S_OK : S_FALSE;
    FS_DEBUG(FS_MOD_COM, "DllCanUnloadNow - locks=%ld, objects=%ld, result=%s",
             g_lockCount.load(), g_objectCount.load(), canUnload ? "S_OK" : "S_FALSE");
    return hr;
}

HRESULT WINAPI DllRegisterServer() {
    FS_INFO(FS_MOD_COM, "DllRegisterServer - starting registration");

    HINSTANCE hInstance = GetDllInstance();
    wchar_t dllPath[MAX_PATH] = {0};
    DWORD pathLen = GetModuleFileNameW(hInstance, dllPath, MAX_PATH);
    if (pathLen == 0 || pathLen >= MAX_PATH) {
        FS_ERROR(FS_MOD_COM, "DllRegisterServer - failed to get DLL path");
        return SELFREG_E_CLASS;
    }

    FS_INFO(FS_MOD_COM, "DllRegisterServer - DLL path: %ls", dllPath);

    HKEY hKey = nullptr;
    DWORD disposition = 0;

    // Create HKCR\CLSID\{GUID}
    wchar_t clsidPath[256] = {0};
    swprintf_s(clsidPath, sizeof(clsidPath) / sizeof(wchar_t),
               L"CLSID\\{A3F5C8E2-7B4D-4E9A-B6D1-8F2C3A5E7D90}");

    LONG result = RegCreateKeyExW(HKEY_CLASSES_ROOT, clsidPath, 0, nullptr,
                                   REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKey, &disposition);
    if (result != ERROR_SUCCESS) {
        FS_ERROR(FS_MOD_COM, "DllRegisterServer - failed to create CLSID key: %ld", result);
        return SELFREG_E_CLASS;
    }

    FS_INFO(FS_MOD_COM, "DllRegisterServer - created CLSID key");

    // Set default value to "FolderSize Shell Extension"
    const wchar_t* description = L"FolderSize Shell Extension";
    result = RegSetValueExW(hKey, nullptr, 0, REG_SZ,
                           (const BYTE*)description,
                           static_cast<DWORD>((wcslen(description) + 1) * sizeof(wchar_t)));
    if (result != ERROR_SUCCESS) {
        FS_ERROR(FS_MOD_COM, "DllRegisterServer - failed to set CLSID description: %ld", result);
        RegCloseKey(hKey);
        return SELFREG_E_CLASS;
    }

    FS_INFO(FS_MOD_COM, "DllRegisterServer - set CLSID description");

    // Create InProcServer32 subkey
    HKEY hInProcKey = nullptr;
    result = RegCreateKeyExW(hKey, L"InProcServer32", 0, nullptr,
                            REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hInProcKey, &disposition);
    if (result != ERROR_SUCCESS) {
        FS_ERROR(FS_MOD_COM, "DllRegisterServer - failed to create InProcServer32 key: %ld", result);
        RegCloseKey(hKey);
        return SELFREG_E_CLASS;
    }

    FS_INFO(FS_MOD_COM, "DllRegisterServer - created InProcServer32 key");

    // Set DLL path
    result = RegSetValueExW(hInProcKey, nullptr, 0, REG_SZ,
                           (const BYTE*)dllPath,
                           static_cast<DWORD>((wcslen(dllPath) + 1) * sizeof(wchar_t)));
    if (result != ERROR_SUCCESS) {
        FS_ERROR(FS_MOD_COM, "DllRegisterServer - failed to set DLL path: %ld", result);
        RegCloseKey(hInProcKey);
        RegCloseKey(hKey);
        return SELFREG_E_CLASS;
    }

    FS_INFO(FS_MOD_COM, "DllRegisterServer - set DLL path");

    // Set ThreadingModel to "Apartment"
    const wchar_t* threadingModel = L"Apartment";
    result = RegSetValueExW(hInProcKey, L"ThreadingModel", 0, REG_SZ,
                           (const BYTE*)threadingModel,
                           static_cast<DWORD>((wcslen(threadingModel) + 1) * sizeof(wchar_t)));
    if (result != ERROR_SUCCESS) {
        FS_ERROR(FS_MOD_COM, "DllRegisterServer - failed to set ThreadingModel: %ld", result);
        RegCloseKey(hInProcKey);
        RegCloseKey(hKey);
        return SELFREG_E_CLASS;
    }

    FS_INFO(FS_MOD_COM, "DllRegisterServer - set ThreadingModel");

    RegCloseKey(hInProcKey);
    RegCloseKey(hKey);

    // Register shell icon overlay identifier
    HKEY hOverlayKey = nullptr;
    result = RegCreateKeyExW(HKEY_LOCAL_MACHINE,
                            L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Explorer\\ShellIconOverlayIdentifiers\\ FolderSize",
                            0, nullptr, REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hOverlayKey, &disposition);
    if (result != ERROR_SUCCESS) {
        FS_ERROR(FS_MOD_COM, "DllRegisterServer - failed to create overlay identifier key: %ld", result);
        return SELFREG_E_CLASS;
    }

    FS_INFO(FS_MOD_COM, "DllRegisterServer - created overlay identifier key");

    // Set overlay identifier to CLSID
    const wchar_t* clsidString = L"{A3F5C8E2-7B4D-4E9A-B6D1-8F2C3A5E7D90}";
    result = RegSetValueExW(hOverlayKey, nullptr, 0, REG_SZ,
                           (const BYTE*)clsidString,
                           static_cast<DWORD>((wcslen(clsidString) + 1) * sizeof(wchar_t)));
    if (result != ERROR_SUCCESS) {
        FS_ERROR(FS_MOD_COM, "DllRegisterServer - failed to set overlay identifier: %ld", result);
        RegCloseKey(hOverlayKey);
        return SELFREG_E_CLASS;
    }

    FS_INFO(FS_MOD_COM, "DllRegisterServer - set overlay identifier");

    RegCloseKey(hOverlayKey);

    // Notify shell of changes
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    FS_INFO(FS_MOD_COM, "DllRegisterServer - notified shell of changes");

    FS_INFO(FS_MOD_COM, "DllRegisterServer - registration complete");
    return S_OK;
}

HRESULT WINAPI DllUnregisterServer() {
    FS_INFO(FS_MOD_COM, "DllUnregisterServer - starting unregistration");

    LONG result = ERROR_SUCCESS;

    // Delete overlay identifier
    result = RegDeleteKeyW(HKEY_LOCAL_MACHINE,
                          L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Explorer\\ShellIconOverlayIdentifiers\\ FolderSize");
    if (result != ERROR_SUCCESS && result != ERROR_FILE_NOT_FOUND) {
        FS_ERROR(FS_MOD_COM, "DllUnregisterServer - failed to delete overlay identifier: %ld", result);
        return SELFREG_E_CLASS;
    }

    FS_INFO(FS_MOD_COM, "DllUnregisterServer - deleted overlay identifier");

    // Delete InProcServer32
    wchar_t inProcPath[256] = {0};
    swprintf_s(inProcPath, sizeof(inProcPath) / sizeof(wchar_t),
               L"CLSID\\{A3F5C8E2-7B4D-4E9A-B6D1-8F2C3A5E7D90}\\InProcServer32");
    result = RegDeleteKeyW(HKEY_CLASSES_ROOT, inProcPath);
    if (result != ERROR_SUCCESS && result != ERROR_FILE_NOT_FOUND) {
        FS_ERROR(FS_MOD_COM, "DllUnregisterServer - failed to delete InProcServer32: %ld", result);
        return SELFREG_E_CLASS;
    }

    FS_INFO(FS_MOD_COM, "DllUnregisterServer - deleted InProcServer32");

    // Delete CLSID
    wchar_t clsidPath[256] = {0};
    swprintf_s(clsidPath, sizeof(clsidPath) / sizeof(wchar_t),
               L"CLSID\\{A3F5C8E2-7B4D-4E9A-B6D1-8F2C3A5E7D90}");
    result = RegDeleteKeyW(HKEY_CLASSES_ROOT, clsidPath);
    if (result != ERROR_SUCCESS && result != ERROR_FILE_NOT_FOUND) {
        FS_ERROR(FS_MOD_COM, "DllUnregisterServer - failed to delete CLSID: %ld", result);
        return SELFREG_E_CLASS;
    }

    FS_INFO(FS_MOD_COM, "DllUnregisterServer - deleted CLSID");

    // Notify shell of changes
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    FS_INFO(FS_MOD_COM, "DllUnregisterServer - notified shell of changes");

    FS_INFO(FS_MOD_COM, "DllUnregisterServer - unregistration complete");
    return S_OK;
}
