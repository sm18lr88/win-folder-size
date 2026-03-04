#include <windows.h>
#include <shlobj.h>
#include "com/shell_overlay.h"
#include "logging.h"
#include "com/class_factory.h"

ShellOverlay::ShellOverlay() {
    FS_TRACE(FS_MOD_COM, "ShellOverlay::ShellOverlay - creating instance");
    ++g_objectCount;
}

ShellOverlay::~ShellOverlay() {
    FS_TRACE(FS_MOD_COM, "ShellOverlay::~ShellOverlay - destroying instance");
    --g_objectCount;
}

HRESULT STDMETHODCALLTYPE ShellOverlay::QueryInterface(REFIID riid, void** ppv) {
    if (!ppv) {
        return E_INVALIDARG;
    }

    if (riid == IID_IUnknown || riid == IID_IShellIconOverlayIdentifier) {
        *ppv = static_cast<IShellIconOverlayIdentifier*>(this);
        AddRef();
        FS_TRACE(FS_MOD_COM, "ShellOverlay::QueryInterface succeeded for IID");
        return S_OK;
    }

    *ppv = nullptr;
    FS_TRACE(FS_MOD_COM, "ShellOverlay::QueryInterface failed - unsupported IID");
    return E_NOINTERFACE;
}

ULONG STDMETHODCALLTYPE ShellOverlay::AddRef() {
    LONG count = ++m_refCount;
    FS_TRACE(FS_MOD_COM, "ShellOverlay::AddRef - refcount now %ld", count);
    return count;
}

ULONG STDMETHODCALLTYPE ShellOverlay::Release() {
    LONG count = --m_refCount;
    FS_TRACE(FS_MOD_COM, "ShellOverlay::Release - refcount now %ld", count);
    if (count == 0) {
        FS_TRACE(FS_MOD_COM, "ShellOverlay::Release - deleting this");
        delete this;
    }
    return count;
}

HRESULT STDMETHODCALLTYPE ShellOverlay::IsMemberOf(LPCWSTR pwszPath, DWORD dwAttrib) {
    // NO-OP: We don't want to show any overlay icons
    // This is called for EVERY item in Explorer, so it must be FAST
    (void)pwszPath;  // Suppress unused parameter warning
    (void)dwAttrib;  // Suppress unused parameter warning
    FS_TRACE(FS_MOD_COM, "ShellOverlay::IsMemberOf - returning S_FALSE");
    return S_FALSE;
}

HRESULT STDMETHODCALLTYPE ShellOverlay::GetOverlayInfo(LPWSTR pwszIconFile, int cchMax, int* pIndex, DWORD* pdwFlags) {
    if (!pwszIconFile || !pIndex || !pdwFlags) {
        return E_INVALIDARG;
    }

    (void)cchMax;  // Suppress unused parameter warning
    pwszIconFile[0] = L'\0';
    *pIndex = 0;
    *pdwFlags = 0;

    FS_TRACE(FS_MOD_COM, "ShellOverlay::GetOverlayInfo - returning S_OK");
    return S_OK;
}

HRESULT STDMETHODCALLTYPE ShellOverlay::GetPriority(int* pPriority) {
    if (!pPriority) {
        return E_INVALIDARG;
    }

    *pPriority = 100;
    FS_TRACE(FS_MOD_COM, "ShellOverlay::GetPriority - returning S_OK");
    return S_OK;
}
