#pragma once

#include "Mararuana/Core/Core.h"
#include "Mararuana/EventSystem/EventBase.h"

namespace Mar {

    struct WindowResizeEvent : public Event<WindowResizeEvent> {
        int width;
        int height;

        WindowResizeEvent(int w, int h)
            : width(w), height(h)
        {

        }

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