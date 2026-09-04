#include "ofMain.h"
#include "testApp.h"

//========================================================================
int main() {
	ofGLWindowSettings settings;
	settings.setSize(1280, 768);
	settings.setGLVersion(2, 1);
	settings.windowMode = OF_WINDOW;

	auto window = ofCreateWindow(settings);
	ofRunApp(window, std::make_shared<testApp>());
	ofRunMainLoop();
}
