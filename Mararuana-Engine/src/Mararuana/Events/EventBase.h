#pragma once

#include "Mararuana/Core.h"

#include <utility>

namespace Mar {
    enum class EventType : uint16_t {
        None = 0,
        KeyPressed,
        KeyReleased,

        MouseButtonPressed,
        MouseButtonReleased,
        WindowResize,
        WindowClose,
        WindowMoved,
        EndTypeValue
    };

    constexpr size_t TotalEventTypes =
        static_cast<size_t>(EventType::EndTypeValue) - 1u;

    // Event categories.

    // Renamed from None to EventCategoryNone to avoid colliding with EventType::None
    // inside the same namespace. Both would otherwise resolve to 0.
    enum EventCategory : uint32_t {
        EventCategoryNone = 0,
        KeyEvent = BIT(0),
        MouseButtonEvent = BIT(1),
        WindowEvent = BIT(2),
    };

    // Base event type.

    // SubscribeHandled requires TEvent to derive from Event<TEvent>.
    // That gives two guarantees:
    //   - TEvent has a bool Handled member.
    //   - Handled stays at offset 0, which is required by the dispatch code.
    template<typename Derived>
    struct Event {
        bool Handled = false;

        [[nodiscard]] static constexpr EventType GetStaticType()                noexcept { return Derived::GetStaticType(); }
        [[nodiscard]] constexpr EventType        GetType()                const noexcept { return Derived::GetStaticType(); }
        [[nodiscard]] static constexpr uint32_t  GetStaticCategoryFlags()       noexcept { return Derived::GetStaticCategoryFlags(); }
        [[nodiscard]] constexpr uint32_t         GetCategoryFlags()       const noexcept { return Derived::GetStaticCategoryFlags(); }
        [[nodiscard]] constexpr bool             IsInCategory(uint32_t c) const noexcept { return (GetCategoryFlags() & c) != 0u; }
    };

    // Type traits.

    template<typename T, typename = void>
    struct has_event_class_type : std::false_type {};
    template<typename T>
    struct has_event_class_type<T, std::void_t<typename T::_mar_event_type_tag>>
        : std::true_type {
    };
    template<typename T>
    inline constexpr bool has_event_class_type_v = has_event_class_type<T>::value;

    // Detects whether TEvent provides GetStaticCategoryFlags().
    // This gives a clean error at the call site when EVENT_CLASS_CATEGORY is missing.
    template<typename T, typename = void>
    struct has_event_class_category : std::false_type {};
    template<typename T>
    struct has_event_class_category<T,
        std::void_t<decltype(T::GetStaticCategoryFlags())>>
        : std::true_type {};
    template<typename T>
    inline constexpr bool has_event_class_category_v =
        has_event_class_category<T>::value;

    // Event macros.

#define EVENT_CLASS_TYPE(type)                                              \
        using _mar_event_type_tag = void;                                   \
        [[nodiscard]] static constexpr ::Mar::EventType                     \
        GetStaticType() noexcept { return ::Mar::EventType::type; }

#define EVENT_CLASS_CATEGORY(category)                                      \
        [[nodiscard]] static constexpr uint32_t                             \
        GetStaticCategoryFlags() noexcept { return (category); }
}