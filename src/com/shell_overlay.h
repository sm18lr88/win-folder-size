#pragma once
#include <shlobj.h>
#include <atomic>

class ShellOverlay : public IShellIconOverlayIdentifier {
public:
    ShellOverlay();
    ~ShellOverlay();

    // IUnknown
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override;
    ULONG STDMETHODCALLTYPE AddRef() override;
    ULONG STDMETHODCALLTYPE Release() override;

    // IShellIconOverlayIdentifier
    HRESULT STDMETHODCALLTYPE IsMemberOf(LPCWSTR pwszPath, DWORD dwAttrib) override;
    HRESULT STDMETHODCALLTYPE GetOverlayInfo(LPWSTR pwszIconFile, int cchMax, int* pIndex, DWORD* pdwFlags) override;
    HRESULT STDMETHODCALLTYPE GetPriority(int* pPriority) override;

private:
    std::atomic<LONG> m_refCount{1};
};
