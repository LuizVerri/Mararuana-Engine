#pragma once

extern Mar::Application* Mar::CreateApplication();

int main(int argc, char** argv)
{
	printf("Hello Mararuana!\n");
	auto app = Mar::CreateApplication();
	app->Run();
	delete app;
}
