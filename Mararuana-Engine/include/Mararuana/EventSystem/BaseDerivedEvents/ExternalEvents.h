#pragma once

#include "Mararuana/Core/Core.h"
#include "Mararuana/EventSystem/EventBase.h"

namespace Mar {

#define DEFINE_EVENT(name, type, category, ...)             \
    struct name : public ::Mar::Event<name> {               \
        EVENT_CLASS_TYPE(type)                              \
        EVENT_CLASS_CATEGORY(category)                      \
        __VA_ARGS__                                        \
    };

#include MAR_CUSTOM_EVENTS_H

#undef DEFINE_EVENT

}
