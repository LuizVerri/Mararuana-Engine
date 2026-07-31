// main.cpp
// Client application using CRTP-based Application framework.
// All lifecycle hooks (OnUpdate, OnShutdown) are resolved at compile time.
//
// App configuration (window subsystem, SIMD level, custom events header)
// lives entirely in CMakeLists.txt now, under "PAINEL DE CONFIGURAÇÃO".
// There's no MyAppConfig.h to include anymore.

#include <Mararuana/Mararuana.h>
SET_APP_VERSION(1, 0, 0)

#include <array>
#include <string_view>
#include <optional>

// ---------------------------------------------------------------------------
// Key bindings
// ---------------------------------------------------------------------------
namespace Key {
    constexpr int Quit = 81;  // Q
    constexpr int Pickup = 69;  // E
}

// ---------------------------------------------------------------------------
// Items
// ---------------------------------------------------------------------------
struct Item {
    int                instanceId;
    std::string_view   name;
};

static constexpr std::array<Item, 3> kItems{ {
    { 1, "Shovel"  },
    { 2, "Lantern" },
    { 3, "Map"     },
} };

static std::optional<std::string_view> FindItemName(int instanceId) {
    for (const auto& item : kItems) {
        if (item.instanceId == instanceId)
            return item.name;
    }
    return std::nullopt;
}

// ---------------------------------------------------------------------------
// Sandbox
// Inherits via CRTP: Application<Sandbox>.
// The 'final' keyword enables devirtualization guarantees and prevents
// accidental further derivation (which would break the CRTP contract).
// ---------------------------------------------------------------------------
class Sandbox final : public Mar::Application<Sandbox> {
public:
    Sandbox()
        : Mar::Application<Sandbox>(Mar::ApplicationSpecification{
              L"Sandbox", 1280, 720
            })
    {
        BindKeyboard();
        BindMouse();
        BindItems();
    }

    // -----------------------------------------------------------------------
    // CRTP Hooks — compile-time resolved, fully inlineable
    // -----------------------------------------------------------------------
    void OnUpdate() noexcept {
        // Per-frame game logic. Called between PollEvents() and Dispatch().
        // Zero overhead: the compiler inlines this directly into the main loop.
    }

    void OnShutdown() noexcept {
        MAR_CORE_INFO("Sandbox shutdown. Total items collected: {}", m_ItemsCollected);
    }

private:
    // -----------------------------------------------------------------------
    // Event Bindings
    // -----------------------------------------------------------------------
    void BindKeyboard() {
        MAR_SUBSCRIBE(Mar::KeyPressedEvent, {
            MAR_CORE_INFO("Key pressed: {}", e.key);
            switch (e.key) {
                case Key::Quit:
                    
                    MAR_CORE_TRACE("Quit requested — stopping main loop");
                    // Direct call to inherited Stop(). No event round-trip.
                    // Sets m_Running = false, loop exits on next iteration.
                    Stop();
                    break;

                case Key::Pickup:
                    (void)Mar::EventManager::GetInstance()->Push(
                        Mar::ItemPickupEvent(m_NextPickupId, 1));
                    m_NextPickupId =
                        (m_NextPickupId % static_cast<int>(kItems.size())) + 1;
                    break;

                default:
                    break;
            }
            });
    }

    void BindMouse() {
        MAR_SUBSCRIBE(Mar::MouseButtonPressedEvent, {
            MAR_CORE_INFO("Mouse button {} pressed at ({:.1f}, {:.1f})",
                          e.button, e.x, e.y);
            });

        MAR_SUBSCRIBE(Mar::MouseMotionEvent, {
            MAR_CORE_TRACE("Cursor ({:.1f}, {:.1f})", e.x, e.y);
            MAR_CORE_TRACE("Delta ({:.1f}, {:.1f})", e.deltaX, e.deltaY);
            });
    }

    void BindItems() {
        MAR_SUBSCRIBE(Mar::ItemPickupEvent, {
            if (const auto name = FindItemName(e.itemInstanceId)) {
                MAR_CORE_INFO("Picked up '{}' (id: {})", *name, e.itemInstanceId);
                ++m_ItemsCollected;
                MAR_CORE_TRACE("Total items collected: {}", m_ItemsCollected);
            }
 else {
  MAR_CORE_WARN("Unknown item (id: {})", e.itemInstanceId);
}
            });
    }

    int m_NextPickupId = 1;
    int m_ItemsCollected = 0;
};

// ---------------------------------------------------------------------------
// Entry Point — generates main(), stack-allocates Sandbox, calls Run().
// No heap allocation. No virtual dispatch. No delete.
// ---------------------------------------------------------------------------
MAR_MAIN(Sandbox)