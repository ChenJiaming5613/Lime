#include "Engine/Engine.h"

#include "HelloTriangleApp.h"

int main(int ArgumentCount, char** Arguments)
{
	Lime::FEngineConfig Config;
	Config.WindowTitle = "LimeEngine - HelloTriangle";
	Config.ParseCommandLine(ArgumentCount, Arguments);

	Lime::FHelloTriangleApp Application;
	Lime::FEngine Engine;
	return Engine.Run(Config, Application);
}
