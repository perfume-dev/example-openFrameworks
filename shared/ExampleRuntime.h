#pragma once

#include "ofMain.h"
#include "ofAppGLFWWindow.h"
#include "ofxBvh.h"
#include <GLFW/glfw3.h>

#include <array>
#include <cmath>
#include <memory>

namespace example {

struct RunOptions {
	bool smokeTest = false;
	int frames = 90;
	std::string capturePath;

	static RunOptions parse(int argc, char** argv) {
		RunOptions options;
		for (int i = 1; i < argc; ++i) {
			const std::string argument(argv[i]);
			if (argument == "--smoke-test") options.smokeTest = true;
			else if (argument.rfind("--capture=", 0) == 0) options.capturePath = argument.substr(10);
			else if (argument.rfind("--frames=", 0) == 0) options.frames = std::max(2, ofToInt(argument.substr(9)));
		}
		if (!options.capturePath.empty()) options.smokeTest = true;
		return options;
	}
};

// A hidden GLFW window still renders every frame, without activating a preview.
template <typename App>
int launch(int argc, char** argv, const std::string& title) {
	const auto options = RunOptions::parse(argc, argv);
	ofGLFWWindowSettings settings;
	settings.setSize(1024, 768);
	settings.setGLVersion(3, 2);
	settings.windowMode = OF_WINDOW;
	settings.title = title;
	settings.visible = !options.smokeTest;
	auto window = ofCreateWindow(settings);
	ofRunApp(window, std::make_shared<App>(options));
	return ofRunMainLoop();
}

class Runtime {
public:
	explicit Runtime(const RunOptions& options) : options(options) {}

	void setup() const {
		ofSetFrameRate(options.smokeTest ? 0 : 60);
		ofSetVerticalSync(!options.smokeTest);
		if (options.smokeTest) ofSetRandomSeed(2012);
	}

	float deltaSeconds() const { return options.smokeTest ? 1.0f / 60.0f : ofGetLastFrameTime(); }
	float elapsedSeconds() const { return options.smokeTest ? ofGetFrameNum() / 60.0f : ofGetElapsedTimef(); }
	bool isSmokeTest() const { return options.smokeTest; }

	void finishFrame(bool ready, const std::string& name, size_t geometryVertices) const {
		if (!options.smokeTest) return;
		auto* window = dynamic_cast<ofAppGLFWWindow*>(ofGetWindowPtr());
		auto* nativeWindow = window ? window->getGLFWWindow() : nullptr;
		const bool passive = nativeWindow && !glfwGetWindowAttrib(nativeWindow, GLFW_VISIBLE)
			&& !glfwGetWindowAttrib(nativeWindow, GLFW_FOCUSED);
		const GLenum error = glGetError();
		if (!ready || !passive || error != GL_NO_ERROR) {
			ofLogError(name) << "SMOKE_TEST_FAILED ready=" << ready << " passive=" << passive << " glError=" << error;
			ofExit(1);
			return;
		}
		const bool lastFrame = ofGetFrameNum() + 1 >= options.frames;
		if (ofGetFrameNum() != 0 && !lastFrame) return;

		ofImage capture;
		capture.grabScreen(0, 0, ofGetWidth(), ofGetHeight());
		const auto& pixels = capture.getPixels();
		if (!pixels.isAllocated() || pixels.getWidth() < 16 || pixels.getHeight() < 128) {
			ofLogError(name) << "SMOKE_TEST_FAILED framebuffer capture unavailable";
			ofExit(1);
			return;
		}
		unsigned char minimum = 255;
		unsigned char maximum = 0;
		uint64_t hash = 1469598103934665603ull;
		size_t variedPixels = 0;
		const int left = pixels.getWidth() / 10;
		const int right = pixels.getWidth() * 9 / 10;
		const int top = std::max(100, int(pixels.getHeight() / 10));
		const int bottom = pixels.getHeight() * 9 / 10;
		const auto reference = pixels.getColor(left, top);
		for (int y = top; y < bottom; ++y) {
			for (int x = left; x < right; ++x) {
				const auto color = pixels.getColor(x, y);
				for (const auto channel : {color.r, color.g, color.b}) {
					minimum = std::min(minimum, channel);
					maximum = std::max(maximum, channel);
					hash = (hash ^ channel) * 1099511628211ull;
				}
				if (std::abs(int(color.r) - reference.r) + std::abs(int(color.g) - reference.g)
					+ std::abs(int(color.b) - reference.b) > 24) ++variedPixels;
			}
		}
		if (!lastFrame) {
			initialPixelsHash = hash;
			return;
		}
		if (!pixels.isAllocated() || geometryVertices == 0 || maximum - minimum < 8
			|| variedPixels < size_t((right - left) * (bottom - top)) / 500 || hash == initialPixelsHash) {
			ofLogError(name) << "SMOKE_TEST_FAILED empty, flat, or unchanged content region"
				<< " vertices=" << geometryVertices << " variedPixels=" << variedPixels;
			ofExit(1);
			return;
		}
		if (!options.capturePath.empty() && !capture.save(options.capturePath)) {
			ofLogError(name) << "SMOKE_TEST_FAILED unable to save capture";
			ofExit(1);
			return;
		}
		ofLogNotice(name) << "SMOKE_TEST_OK frames=" << options.frames
			<< " renderer=GL3 pixelRange=" << int(maximum - minimum) << " geometryVertices=" << geometryVertices
			<< " variedPixels=" << variedPixels << " motionChanged=true hidden=true focused=false";
		ofExit(0);
	}

private:
	RunOptions options;
	mutable uint64_t initialPixelsHash = 0;
};

class MotionPlayback {
public:
	std::array<ofxBvh, 3> motions;
	bool ready = false;
	bool audioReady = false;
	bool usingBundledMotion = false;

	void load(const std::string& name, bool bundledOnly, bool silent) {
		constexpr std::array<const char*, 3> original = {
			"bvhfiles/aachan.bvh", "bvhfiles/kashiyuka.bvh", "bvhfiles/nocchi.bvh"};
		constexpr std::array<const char*, 3> bundled = {"A_test.bvh", "B_test.bvh", "C_test.bvh"};
		ready = true;
		usingBundledMotion = false;
		for (size_t i = 0; i < motions.size(); ++i) {
			const bool originalExists = !bundledOnly && ofFile::doesFileExist(original[i]);
			usingBundledMotion = usingBundledMotion || !originalExists;
			const auto path = originalExists ? std::string(original[i])
				: (bundledOnly ? std::string(bundled[i]) : "../../../example-bvh/bin/data/" + std::string(bundled[i]));
			ready = motions[i].load(path) && ready;
			motions[i].setLoop(true);
		}
		audioReady = !bundledOnly && !silent && ofFile::doesFileExist("Perfume_globalsite_sound.wav")
			&& audio.load("Perfume_globalsite_sound.wav");
		if (audioReady) {
			audio.setLoop(true);
			audio.play();
		} else if (!bundledOnly) {
			ofLogNotice(name) << (usingBundledMotion ? "Bundled motion" : "Project motion") << "; using a silent internal clock.";
		}
		playhead = silent ? 4.0f : 0.0f;
	}

	void update(float deltaSeconds, float speed = 1.0f) {
		if (!ready) return;
		const float duration = motions.front().getDuration();
		if (audioReady) {
			audio.setSpeed(speed);
			playhead = audio.getPosition() * audio.getDuration();
		} else {
			playhead += deltaSeconds * speed;
		}
		playhead = std::fmod(playhead, duration);
		if (playhead < 0.0f) playhead += duration;
		position = playhead / duration;
		for (auto& motion : motions) {
			motion.setPosition(position);
			motion.update(0.0f);
		}
	}

	float normalizedPosition() const { return position; }

	void drawStatus(float x = 10.0f, float y = 20.0f) const {
		if (!ready) ofDrawBitmapString("Motion data is missing. See README.md.", x, y);
		else if (!audioReady) ofDrawBitmapString(usingBundledMotion ? "Bundled motion / silent clock" : "Project motion / silent clock", x, y);
		else if (usingBundledMotion) ofDrawBitmapString("Bundled motion / audio clock", x, y);
	}

private:
	ofSoundPlayer audio;
	float playhead = 0.0f;
	float position = 0.0f;
};

inline glm::vec3 safeNormalize(const glm::vec3& value, const glm::vec3& fallback = {0, 1, 0}) {
	const float lengthSquared = glm::dot(value, value);
	return lengthSquared > 1e-12f ? value / std::sqrt(lengthSquared) : fallback;
}

} // namespace example
