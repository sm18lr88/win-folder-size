#include <windows.h>
#include <detours/detours.h>
#include "hooks/detour_wrapper.h"
#include "logging.h"

namespace fs::hooks {

// ============================================================================
// DetourTransaction Implementation
// ============================================================================

DetourTransaction::DetourTransaction() = default;

DetourTransaction::~DetourTransaction() {
    if (m_active && !m_committed) {
        abort();
    }
}

bool DetourTransaction::begin() {
    m_lastError = DetourTransactionBegin();
    if (m_lastError != NO_ERROR) {
        FS_ERROR(FS_MOD_HOOK, "DetourTransactionBegin failed: 0x%lx", m_lastError);
        return false;
    }
    
    m_lastError = DetourUpdateThread(GetCurrentThread());
    if (m_lastError != NO_ERROR) {
        FS_ERROR(FS_MOD_HOOK, "DetourUpdateThread failed: 0x%lx", m_lastError);
        DetourTransactionAbort();
        return false;
    }
    
    m_active = true;
    FS_TRACE(FS_MOD_HOOK, "DetourTransaction begun");
    return true;
}

bool DetourTransaction::commit() {
    if (!m_active) {
        FS_WARN(FS_MOD_HOOK, "DetourTransaction::commit called but transaction not active");
        return false;
    }
    
    m_lastError = DetourTransactionCommit();
    if (m_lastError != NO_ERROR) {
        FS_ERROR(FS_MOD_HOOK, "DetourTransactionCommit failed: 0x%lx", m_lastError);
        m_active = false;
        return false;
    }
    
    m_active = false;
    m_committed = true;
    FS_TRACE(FS_MOD_HOOK, "DetourTransaction committed");
    return true;
}

void DetourTransaction::abort() {
    if (!m_active) {
        return;
    }
    
    m_lastError = DetourTransactionAbort();
    m_active = false;
    FS_TRACE(FS_MOD_HOOK, "DetourTransaction aborted");
}

// ============================================================================
// DetourHook Implementation
// ============================================================================

DetourHook::DetourHook(DetourHook&& other) noexcept
    : m_ppOriginal(other.m_ppOriginal),
      m_pHook(other.m_pHook),
      m_attached(other.m_attached) {
    other.m_ppOriginal = nullptr;
    other.m_pHook = nullptr;
    other.m_attached = false;
}

DetourHook& DetourHook::operator=(DetourHook&& other) noexcept {
    if (this != &other) {
        m_ppOriginal = other.m_ppOriginal;
        m_pHook = other.m_pHook;
        m_attached = other.m_attached;
        
        other.m_ppOriginal = nullptr;
        other.m_pHook = nullptr;
        other.m_attached = false;
    }
    return *this;
}

DetourHook::~DetourHook() {
    if (m_attached) {
        FS_WARN(FS_MOD_HOOK, "DetourHook destroyed while still attached: original=%p, hook=%p",
                m_ppOriginal ? *m_ppOriginal : nullptr, m_pHook);
    }
}

bool DetourHook::attach(void** ppOriginal, void* pHook) {
    if (!ppOriginal || !pHook) {
        FS_ERROR(FS_MOD_HOOK, "DetourHook::attach called with null pointers");
        return false;
    }
    
    m_ppOriginal = ppOriginal;
    m_pHook = pHook;
    
    LONG result = DetourAttach(ppOriginal, pHook);
    if (result != NO_ERROR) {
        FS_ERROR(FS_MOD_HOOK, "DetourAttach failed: 0x%lx, original=%p, hook=%p",
                 result, *ppOriginal, pHook);
        m_ppOriginal = nullptr;
        m_pHook = nullptr;
        return false;
    }
    
    m_attached = true;
    FS_DEBUG(FS_MOD_HOOK, "Attaching hook: original=%p, hook=%p", *ppOriginal, pHook);
    return true;
}

bool DetourHook::detach() {
    if (!m_attached || !m_ppOriginal || !m_pHook) {
        FS_WARN(FS_MOD_HOOK, "DetourHook::detach called but hook not attached");
        return false;
    }
    
    LONG result = DetourDetach(m_ppOriginal, m_pHook);
    if (result != NO_ERROR) {
        FS_ERROR(FS_MOD_HOOK, "DetourDetach failed: 0x%lx, original=%p, hook=%p",
                 result, *m_ppOriginal, m_pHook);
        return false;
    }
    
    FS_DEBUG(FS_MOD_HOOK, "Detaching hook: original=%p, hook=%p", *m_ppOriginal, m_pHook);
    m_attached = false;
    m_ppOriginal = nullptr;
    m_pHook = nullptr;
    return true;
}

} // namespace fs::hooks
