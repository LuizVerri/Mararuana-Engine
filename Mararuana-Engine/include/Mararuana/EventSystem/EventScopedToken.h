#pragma once

#include "Mararuana/Core/Core.h"
#include "Mararuana/EventSystem/EventManager.h"

namespace Mar {

    // RAII wrapper for SubscriptionToken.
    // It unsubscribes automatically when the token goes out of scope.
    // This helps keep listener lifetime tied to object lifetime.
    class ScopedToken {
    public:
        ScopedToken() = default;

        ScopedToken(EventManager& mgr, SubscriptionToken token) noexcept;

        ~ScopedToken();

        ScopedToken(const ScopedToken&) = delete;
        ScopedToken& operator=(const ScopedToken&) = delete;

        ScopedToken(ScopedToken&& o) noexcept;
        ScopedToken& operator=(ScopedToken&& o) noexcept;

        // Assign from an existing subscription token.
        // The manager must already be bound before using this overload.
        ScopedToken& operator=(SubscriptionToken token) noexcept;

        // Unsubscribe now and clear the stored token.
        void Release() noexcept;

        void Bind(EventManager& mgr) noexcept;

        [[nodiscard]] bool          Valid()   const noexcept;
        [[nodiscard]] EventManager* Manager() const noexcept;

    private:
        EventManager* m_Manager = nullptr;
        SubscriptionToken m_Token{};
    };

} // namespace Mar