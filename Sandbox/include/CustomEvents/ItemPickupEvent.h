// ==============================================================================
//  ItemPickupEvent — fired when the player picks up a world item.
// ------------------------------------------------------------------------------
//  Part of the X-Macro event registry (see CustomEvents.h). Included twice
//  by EventBase.h with different macros defined, so this file must NOT have
//  an include guard / #pragma once.
// ==============================================================================

#ifdef DEFINE_CATEGORIES

DEFINE_CATEGORY(ClientCategory)

#else

DEFINE_EVENT(ItemPickupEvent, ItemPickupType, ClientCategory,
    uint64_t itemInstanceId;
int quantity;

ItemPickupEvent(uint64_t id, int qty)
    : itemInstanceId(id), quantity(qty) {
}
)

#endif