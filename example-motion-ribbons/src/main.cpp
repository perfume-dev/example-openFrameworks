#include "ofMain.h"
#include "ofApp.h"

#include <iostream>
#include <stdexcept>
#include <string>

int main(int argc, char* argv[]) {
    AppOptions options;
    try {
        for (int i = 1; i < argc; ++i) {
            const std::string argument = argv[i];
            if (argument == "--smoke-test") options.smokeTest = true;
            else if (argument.starts_with("--capture=")) {
                options.capturePath = argument.substr(10);
                if (options.capturePath.empty()) throw std::invalid_argument("Capture path is empty.");
            } else if (argument.starts_with("--capture-frame=")) {
                const auto value = argument.substr(16);
                std::size_t consumed = 0;
                options.captureFrame = std::stoi(value, &consumed);
                if (consumed != value.size()) throw std::invalid_argument("Capture frame must be an integer.");
            }
            else {
                std::cerr << "Unknown option: " << argument << '\n';
                return 1;
            }
        }
        if (options.captureFrame < 2 || options.captureFrame > 3600 ||
            (!options.capturePath.empty() && !of::filesystem::path(options.capturePath).is_absolute())) {
            throw std::invalid_argument("Use an absolute capture path and a frame between 2 and 3600.");
        }
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    ofGLFWWindowSettings settings;
    settings.setSize(1440, 900);
    settings.setGLVersion(3, 2);
    settings.numSamples = 0; // The FBO owns the render target in interactive and automated runs.
    settings.visible = !options.automated();
    auto window = ofCreateWindow(settings);
    ofRunApp(window, std::make_shared<ofApp>(options));
    return ofRunMainLoop();
}
