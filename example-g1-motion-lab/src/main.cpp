#include "ofApp.h"

#include <iostream>
#include <stdexcept>

namespace {
int parseFrames(const std::string& value) {
    std::size_t consumed = 0;
    const int result = std::stoi(value, &consumed);
    if (consumed != value.size() || result < 2 || result > 21600) {
        throw std::invalid_argument("Capture frame must be an integer from 2 to 21600.");
    }
    return result;
}
}

int main(int argc, char** argv) {
    G1Options options;
    options.run.frames = 180;
    try {
        for (int i = 1; i < argc; ++i) {
            const std::string argument(argv[i]);
            if (argument == "--smoke-test") options.run.smokeTest = true;
            else if (argument.starts_with("--capture=")) options.run.capturePath = argument.substr(10);
            else if (argument == "--capture") {
                if (++i == argc) throw std::invalid_argument("--capture requires an absolute PNG path.");
                options.run.capturePath = argv[i];
            } else if (argument.starts_with("--capture-frame=")) options.run.frames = parseFrames(argument.substr(16));
            else if (argument.starts_with("--frames=")) options.run.frames = parseFrames(argument.substr(9));
            else if (argument == "--capture-frame" || argument == "--frames") {
                if (++i == argc) throw std::invalid_argument("Capture frame is missing.");
                options.run.frames = parseFrames(argv[i]);
            } else if (argument == "--source-overlay") options.sourceOverlay = true;
            else if (argument.starts_with("--clip=")) {
                const auto value = argument.substr(7);
                if (value == "all") options.selectedClip = -1;
                else if (value == "A" || value == "B" || value == "C") options.selectedClip = value[0] - 'A';
                else throw std::invalid_argument("--clip must be A, B, C, or all.");
            } else throw std::invalid_argument("Unknown argument: " + argument);
            if ((argument == "--capture" || argument.starts_with("--capture=")) && options.run.capturePath.empty()) {
                throw std::invalid_argument("Capture path is empty.");
            }
        }
        if (!options.run.capturePath.empty()) {
            const of::filesystem::path path(options.run.capturePath);
            if (!path.is_absolute() || ofToLower(path.extension().string()) != ".png") {
                throw std::invalid_argument("Capture must be an absolute .png path.");
            }
            options.run.smokeTest = true;
        }
    } catch (const std::exception& error) {
        std::cerr << "G1 Motion Lab: " << error.what() << '\n';
        return 1;
    }

    ofGLFWWindowSettings settings;
    settings.setSize(1440, 900);
    settings.setGLVersion(3, 2);
    settings.numSamples = 4;
    settings.title = "G1 Motion Lab | Kinematic Reference";
    // Automated captures are rendered without showing or focusing a window.
    settings.visible = !options.run.smokeTest;
    const auto window = ofCreateWindow(settings);
    ofRunApp(window, std::make_shared<ofApp>(options));
    return ofRunMainLoop();
}
