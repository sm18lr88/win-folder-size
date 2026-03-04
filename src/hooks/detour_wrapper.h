#pragma once
#include <windows.h>
#include <detours/detours.h>
#include <utility>

namespace fs::hooks {

// RAII wrapper for DetourTransactionBegin/Commit/Abort
class DetourTransaction {
public:
    DetourTransaction();
    ~DetourTransaction();
    
    // Non-copyable, non-movable
    DetourTransaction(const DetourTransaction&) = delete;
    DetourTransaction& operator=(const DetourTransaction&) = delete;
    
    bool begin();
    bool commit();
    void abort();
    
    bool is_active() const { return m_active; }
    LONG last_error() const { return m_lastError; }

private:
    bool m_active = false;
    bool m_committed = false;
    LONG m_lastError = NO_ERROR;
};

// Type-erased hook that stores original and hook function pointers
class DetourHook {
public:
    DetourHook() = default;
    ~DetourHook();
    
    // Non-copyable but movable
    DetourHook(const DetourHook&) = delete;
    DetourHook& operator=(const DetourHook&) = delete;
    DetourHook(DetourHook&& other) noexcept;
    DetourHook& operator=(DetourHook&& other) noexcept;
    
    // Attach a hook: ppOriginal is pointer to the original function pointer,
    // pHook is the replacement function
    bool attach(void** ppOriginal, void* pHook);
    
    // Detach the hook (must be called within a DetourTransaction)
    bool detach();
    
    bool is_attached() const { return m_attached; }

private:
    void** m_ppOriginal = nullptr;
    void* m_pHook = nullptr;
    bool m_attached = false;
};

} // namespace fs::hooks
