#include <Mararuana.h>

class Sandbox final : public Mar::Application
{
public:
    Sandbox()
        : Mar::Application(Mar::ApplicationSpecification{})
    {
        MAR_SUBSCRIBE(GetEventManager(), Mar::KeyPressedEvent,
            {
                MAR_CORE_INFO("Key pressed: {}", e.key);

                if (e.key == 42)
                {
                    MAR_CORE_TRACE("[EASTER EGG] 42 detectado - encerrando aplicacao");
                    Mar::WindowCloseEvent closeEvent{};
                    (void)GetEventManager().Push(closeEvent);
                }
            });
    }
};

Mar::Application* Mar::CreateApplication()
{
    return new Sandbox();
}