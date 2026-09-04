#include "ofMain.h"
#include "testApp.h"

//========================================================================
int main() {
	ofGLFWWindowSettings settings;
	settings.setSize(1280, 720);
	settings.setGLVersion(2, 1);
	settings.windowMode = OF_WINDOW;
	settings.numSamples = 4;

	auto window = ofCreateWindow(settings);
	ofRunApp(window, std::make_shared<testApp>());
	ofRunMainLoop();
}
