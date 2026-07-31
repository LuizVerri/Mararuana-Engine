// EventScopedToken.cpp
// RAII wrapper around a SubscriptionToken. Guarantees that a subscription is
// automatically cancelled when the owning object goes out of scope, preventing
// the event system from calling into dangling listener pointers.
//
// Ownership rules:
//   - A ScopedToken is tied to a specific EventManager instance (m_Manager).
//     The manager pointer must outlive the token; the token does not extend
//     the manager's lifetime.
//   - Move semantics transfer both the token and the manager reference.
//     The moved-from object is left in a null/invalid state so its destructor
//     is a no-op.
//   - operator=(SubscriptionToken) is provided for rebinding an existing
//     ScopedToken to a new subscription. It requires m_Manager to already be
//     set; use the two-argument constructor when first binding.

#include "Mararuana/EventSystem/EventScopedToken.h"

namespace Mar {

    ScopedToken::ScopedToken(EventManager& mgr, SubscriptionToken token) noexcept
        : m_Manager(&mgr), m_Token(token)
    {
    }

    ScopedToken::~ScopedToken()
    {
        Release();
    }

    ScopedToken::ScopedToken(ScopedToken&& o) noexcept
        : m_Manager(o.m_Manager), m_Token(o.m_Token)
    {
        o.m_Manager = nullptr;
        o.m_Token = {};
    }

    ScopedToken& ScopedToken::operator=(ScopedToken&& o) noexcept
    {
        if (this != &o) {
            Release();
            m_Manager = o.m_Manager;
            m_Token = o.m_Token;
            o.m_Manager = nullptr;
            o.m_Token = {};
        }
        return *this;
    }

    // Replaces the current subscription with a new one.
    // The previous subscription is unsubscribed before the new token is stored.
    // m_Manager must already be bound -- use the constructor to set it.
    ScopedToken& ScopedToken::operator=(SubscriptionToken token) noexcept
    {
        MAR_CORE_ASSERT(m_Manager != nullptr,
            "ScopedToken::operator=(SubscriptionToken) called without a bound manager. "
            "Use ScopedToken(mgr, token) instead.");
        Release();
        m_Token = token;
        return *this;
    }

    // Cancels the subscription if both the manager pointer and the token are
    // valid. Clears the token afterward so repeated calls are safe.
    void ScopedToken::Release() noexcept
    {
        if (m_Manager && m_Token.Valid())
            m_Manager->Unsubscribe(m_Token);
        m_Token = {};
    }

    // Associates a manager with a default-constructed ScopedToken before
    // assigning a token via operator=(SubscriptionToken).
    void ScopedToken::Bind(EventManager& mgr) noexcept
    {
        m_Manager = &mgr;
    }

    bool ScopedToken::Valid() const noexcept
    {
        return m_Token.Valid();
    }

    EventManager* ScopedToken::Manager() const noexcept
    {
        return m_Manager;
    }

} // namespace Mar