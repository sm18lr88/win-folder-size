#pragma once
#include <unknwn.h>
#include <atomic>

class ClassFactory : public IClassFactory {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override;
    ULONG STDMETHODCALLTYPE AddRef() override;
    ULONG STDMETHODCALLTYPE Release() override;
    HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown* pUnkOuter, REFIID riid, void** ppv) override;
    HRESULT STDMETHODCALLTYPE LockServer(BOOL fLock) override;
private:
    std::atomic<LONG> m_refCount{1};
};

extern std::atomic<LONG> g_lockCount;
extern std::atomic<LONG> g_objectCount;
