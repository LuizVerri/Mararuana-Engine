#pragma once

#include "Mararuana/Core.h"
#include "Mararuana/Events/EventManager.h"

namespace Mar {

    // RAII wrapper for SubscriptionToken.
    // It unsubscribes automatically when the token goes out of scope.
    // This helps keep listener lifetime tied to object lifetime.
    class ScopedToken {
    public:
        ScopedToken() = default;

        ScopedToken(EventManager& mgr, SubscriptionToken token) noexcept
            : m_Manager(&mgr), m_Token(token) {
        }

        ~ScopedToken() {
            Release();
        }

        ScopedToken(const ScopedToken&) = delete;
        ScopedToken& operator=(const ScopedToken&) = delete;

        ScopedToken(ScopedToken&& o) noexcept
            : m_Manager(o.m_Manager), m_Token(o.m_Token)
        {
            o.m_Manager = nullptr;
            o.m_Token = {};
        }

        ScopedToken& operator=(ScopedToken&& o) noexcept {
            if (this != &o) {
                Release();
                m_Manager = o.m_Manager;
                m_Token = o.m_Token;
                o.m_Manager = nullptr;
                o.m_Token = {};
            }
            return *this;
        }

        // Assign from an existing subscription token.
        // The manager must already be bound before using this overload.
        ScopedToken& operator=(SubscriptionToken token) noexcept {
            MAR_CORE_ASSERT(m_Manager != nullptr,
                "ScopedToken::operator=(SubscriptionToken) called without a bound manager. "
                "Use ScopedToken(mgr, token) instead.");
            Release();
            m_Token = token;
            return *this;
        }

        // Unsubscribe now and clear the stored token.
        void Release() noexcept {
            if (m_Manager && m_Token.Valid())
                m_Manager->Unsubscribe(m_Token);
            m_Token = {};
        }

        void Bind(EventManager& mgr) noexcept { m_Manager = &mgr; }

        [[nodiscard]] bool          Valid()   const noexcept { return m_Token.Valid(); }
        [[nodiscard]] EventManager* Manager() const noexcept { return m_Manager; }

    private:
        EventManager* m_Manager = nullptr;
        SubscriptionToken m_Token{};
    };
}