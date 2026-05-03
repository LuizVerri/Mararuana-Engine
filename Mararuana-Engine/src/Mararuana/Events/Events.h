#pragma once

#include "Mararuana/Core.h"
#include "Mararuana/Events/EventBase.h"

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

        EVENT_CLASS_TYPE(KeyReleased)
        EVENT_CLASS_CATEGORY(KeyEvent)
    };


    struct MouseButtonPressedEvent : public Event<MouseButtonPressedEvent> {
        int button;
        float x, y;

        EVENT_CLASS_TYPE(MouseButtonPressed)
        EVENT_CLASS_CATEGORY(MouseButtonEvent)
    };

    struct MouseButtonReleasedEvent : public Event<MouseButtonReleasedEvent> {
        int button;
        float x, y;

        EVENT_CLASS_TYPE(MouseButtonReleased)
        EVENT_CLASS_CATEGORY(MouseButtonEvent)
    };

    struct WindowResizeEvent : public Event<WindowResizeEvent> {
        int width;
        int height;

        EVENT_CLASS_TYPE(WindowResize)
        EVENT_CLASS_CATEGORY(WindowEvent)
    };

    struct WindowCloseEvent : public Event<WindowCloseEvent> {
        EVENT_CLASS_TYPE(WindowClose)
        EVENT_CLASS_CATEGORY(WindowEvent)
    };

    struct WindowMovedEvent : public Event<WindowMovedEvent> {
        int x;
        int y;

        EVENT_CLASS_TYPE(WindowMoved)
        EVENT_CLASS_CATEGORY(WindowEvent)
    };

}
