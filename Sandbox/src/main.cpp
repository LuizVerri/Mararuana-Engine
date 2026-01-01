#include <Mararuana.h>

class Sandbox : public Mar::Application
{

public:
	Sandbox()
	{

	}

	~Sandbox()
	{

	}

};

Mar::Application* Mar::CreateApplication()
{
	return new Sandbox();
}