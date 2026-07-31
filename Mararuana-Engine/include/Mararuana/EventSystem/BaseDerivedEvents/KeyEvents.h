#pragma once

#include "Mararuana/Core/Core.h"
#include "Mararuana/EventSystem/EventBase.h"

namespace Mar {

    struct KeyPressedEvent : public Event<KeyPressedEvent> {
        int key;

        KeyPressedEvent(int k)
            : key(k) {
        }

        EVENT_CLASS_TYPE(KeyPressed)
        EVENT_CLASS_CATEGORY(KeyEvent)
    };

    struct KeyReleasedEvent : public Event<KeyReleasedEvent> {
        int key;

        KeyReleasedEvent(int k)
            : key(k) {
        }

        EVENT_CLASS_TYPE(KeyReleased)
        EVENT_CLASS_CATEGORY(KeyEvent)
    };
}
