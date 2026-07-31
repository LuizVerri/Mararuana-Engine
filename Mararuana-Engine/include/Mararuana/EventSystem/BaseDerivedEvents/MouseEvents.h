#pragma once

#pragma once

#include "Mararuana/Core/Core.h"
#include "Mararuana/EventSystem/EventBase.h"

namespace Mar {

    struct MouseButtonPressedEvent : public Event<MouseButtonPressedEvent> {
        int button;
        float x, y;

        MouseButtonPressedEvent(int b, float xpos, float ypos)
            : button(b), x(xpos), y(ypos)
        {
        }

        EVENT_CLASS_TYPE(MouseButtonPressed)
        EVENT_CLASS_CATEGORY(MouseEvent)
    };

    struct MouseButtonReleasedEvent : public Event<MouseButtonReleasedEvent> {
        int button;
        float x, y;

        MouseButtonReleasedEvent(int b, float xpos, float ypos)
            : button(b), x(xpos), y(ypos)
        {
        }

        EVENT_CLASS_TYPE(MouseButtonReleased)
        EVENT_CLASS_CATEGORY(MouseEvent)
    };

    struct MouseMotionEvent : public Event<MouseMotionEvent> {
        float x, y;
        float deltaX, deltaY; // Raw input deltas

        MouseMotionEvent(float x_pos, float y_pos, float dx, float dy)
            : x(x_pos), y(y_pos), deltaX(dx), deltaY(dy) {
        }

        EVENT_CLASS_TYPE(MouseMotion)
        EVENT_CLASS_CATEGORY(MouseEvent)
    };

}