#include <windows.h>
#include <windows.h>
#include "com/class_factory.h"
#include "com/shell_overlay.h"
#include "logging.h"
#include "logging.h"

std::atomic<LONG> g_lockCount{0};
std::atomic<LONG> g_objectCount{0};

HRESULT STDMETHODCALLTYPE ClassFactory::QueryInterface(REFIID riid, void** ppv) {
    if (!ppv) {
        return E_INVALIDARG;
    }

    if (riid == IID_IUnknown || riid == IID_IClassFactory) {
        *ppv = static_cast<IClassFactory*>(this);
        AddRef();
        FS_TRACE(FS_MOD_COM, "ClassFactory::QueryInterface succeeded for IID");
        return S_OK;
    }

    *ppv = nullptr;
    FS_TRACE(FS_MOD_COM, "ClassFactory::QueryInterface failed - unsupported IID");
    return E_NOINTERFACE;
}

ULONG STDMETHODCALLTYPE ClassFactory::AddRef() {
    LONG count = ++m_refCount;
    FS_TRACE(FS_MOD_COM, "ClassFactory::AddRef - refcount now %ld", count);
    return count;
}

ULONG STDMETHODCALLTYPE ClassFactory::Release() {
    LONG count = --m_refCount;
    FS_TRACE(FS_MOD_COM, "ClassFactory::Release - refcount now %ld", count);
    if (count == 0) {
        FS_TRACE(FS_MOD_COM, "ClassFactory::Release - deleting this");
        delete this;
    }
    return count;
}

HRESULT STDMETHODCALLTYPE ClassFactory::CreateInstance(IUnknown* pUnkOuter, REFIID riid, void** ppv) {
    if (!ppv) {
        return E_INVALIDARG;
    }

    if (pUnkOuter) {
        FS_TRACE(FS_MOD_COM, "ClassFactory::CreateInstance - aggregation not supported");
        return CLASS_E_NOAGGREGATION;
    }

    FS_DEBUG(FS_MOD_COM, "ClassFactory::CreateInstance - creating ShellOverlay");
    ShellOverlay* pOverlay = new ShellOverlay();
    if (!pOverlay) {
        FS_ERROR(FS_MOD_COM, "ClassFactory::CreateInstance - allocation failed");
        return E_OUTOFMEMORY;
    }

    HRESULT hr = pOverlay->QueryInterface(riid, ppv);
    pOverlay->Release();

    if (FAILED(hr)) {
        FS_ERROR(FS_MOD_COM, "ClassFactory::CreateInstance - QI failed: 0x%08lx", hr);
    } else {
        FS_DEBUG(FS_MOD_COM, "ClassFactory::CreateInstance - success");
    }
    return hr;
}

HRESULT STDMETHODCALLTYPE ClassFactory::LockServer(BOOL fLock) {
    if (fLock) {
        LONG count = ++g_lockCount;
        FS_TRACE(FS_MOD_COM, "ClassFactory::LockServer(TRUE) - lock count now %ld", count);
    } else {
        LONG count = --g_lockCount;
        FS_TRACE(FS_MOD_COM, "ClassFactory::LockServer(FALSE) - lock count now %ld", count);
    }
    return S_OK;
}
