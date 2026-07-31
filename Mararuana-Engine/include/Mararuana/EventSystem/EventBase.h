#pragma once

#include "Mararuana/Core/Core.h"
#include <cstdint>
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
        MouseMotion,
        WindowMoved,

#define DEFINE_EVENT(name, type, category, ...) type,
#include MAR_CUSTOM_EVENTS_H
#undef DEFINE_EVENT

        EndTypeValue
    };

    // How many real event types exist, not counting None.
    constexpr size_t TotalEventTypes =
        static_cast<size_t>(EventType::EndTypeValue) - 1u;


    // Event categories as bitmask flags.
    // Renamed from None to EventCategoryNone to avoid clashing with EventType::None
    // since both live in the same namespace and would otherwise both be 0.
    enum EventCategory : uint32_t {
        None = 0,

        KeyShift = 0,
        MouseButtonShift = 1,
        WindowShift = 2,
        MouseShift = 3,

#define DEFINE_CATEGORIES

#define DEFINE_CATEGORY(category) category##Shift,
#include MAR_CUSTOM_EVENTS_H
#undef DEFINE_CATEGORY

#undef DEFINE_CATEGORIES

        KeyEvent = 1 << KeyShift,                 // BIT(0)
        MouseButtonEvent = 1 << MouseButtonShift, // BIT(1)
        WindowEvent = 1 << WindowShift,           // BIT(2)
        MouseEvent = 1 << MouseShift,             // BIT(3)

#define DEFINE_CATEGORIES

#define DEFINE_CATEGORY(category) category = 1 << category##Shift,
#include MAR_CUSTOM_EVENTS_H
#undef DEFINE_CATEGORY

#undef DEFINE_CATEGORIES
    };

    // Bitwise operator overloads for EventCategory
    // Allows combining flags safely, e.g., EventCategory::MouseEvent | EventCategory::InputEvent
    constexpr uint32_t operator|(EventCategory lhs, EventCategory rhs) noexcept {
        return static_cast<uint32_t>(lhs) | static_cast<uint32_t>(rhs);
    }

    constexpr uint32_t operator|(uint32_t lhs, EventCategory rhs) noexcept {
        return lhs | static_cast<uint32_t>(rhs);
    }

    constexpr uint32_t operator|(EventCategory lhs, uint32_t rhs) noexcept {
        return static_cast<uint32_t>(lhs) | rhs;
    }

    // Base template every concrete event inherits from.
    // Gives each event a Handled flag and a set of static query methods.
    // SubscribeHandled requires Handled to sit at offset 0, so keep it first.
    template<typename Derived>
    struct Event {
        bool Handled = false;

        [[nodiscard]] static constexpr EventType   GetStaticType()                noexcept { return Derived::GetStaticType(); }
        [[nodiscard]] constexpr EventType          GetType()                const noexcept { return Derived::GetStaticType(); }
        [[nodiscard]] static constexpr uint32_t    GetStaticCategoryFlags()       noexcept { return Derived::GetStaticCategoryFlags(); }
        [[nodiscard]] constexpr uint32_t           GetCategoryFlags()       const noexcept { return Derived::GetStaticCategoryFlags(); }
        [[nodiscard]] constexpr bool               IsInCategory(uint32_t c) const noexcept { return (GetCategoryFlags() & c) != 0u; }
    };

    // Type traits used to catch missing macros at compile time.

    // True when EVENT_CLASS_TYPE was used on the type.
    template<typename T, typename = void>
    struct has_event_class_type : std::false_type {};
    template<typename T>
    struct has_event_class_type<T, std::void_t<typename T::_mar_event_type_tag>>
        : std::true_type {
    };
    template<typename T>
    inline constexpr bool has_event_class_type_v = has_event_class_type<T>::value;

    // True when EVENT_CLASS_CATEGORY was used on the type.
    template<typename T, typename = void>
    struct has_event_class_category : std::false_type {};
    template<typename T>
    struct has_event_class_category<T,
        std::void_t<decltype(T::GetStaticCategoryFlags())>>
        : std::true_type {};
    template<typename T>
    inline constexpr bool has_event_class_category_v =
        has_event_class_category<T>::value;

    // Required macros for concrete event types.
    // EVENT_CLASS_TYPE  injects GetStaticType() and the tag the trait checks.
    // EVENT_CLASS_CATEGORY injects GetStaticCategoryFlags().
#define EVENT_CLASS_TYPE(type)                                              \
        using _mar_event_type_tag = void;                                       \
        [[nodiscard]] static constexpr ::Mar::EventType                         \
        GetStaticType() noexcept { return ::Mar::EventType::type; }

#define EVENT_CLASS_CATEGORY(category)                                      \
        [[nodiscard]] static constexpr uint32_t                                 \
        GetStaticCategoryFlags() noexcept {                                     \
            return static_cast<uint32_t>(category);                             \
        }

}