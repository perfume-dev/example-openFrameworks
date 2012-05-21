# How to Compile

## Requirements
- Mac OS X 10.6+ (I use 10.7.3)
- Xcode 4+

## Instructions

1. **Checkout [openFrameworks](https://github.com/openframeworks/openFrameworks)**

        git clone https://github.com/openframeworks/openFrameworks.git

	Be patient. It may take a while.

2. **Build openFrameworks**

    2.1. Open `openFrameworks/apps/devApps/_DeployExamples/deployExamples.xcodeproj`

    2.2. Build and Run. 

    2.3. Make sure `openFrameworks/libs/openFrameworksCompiled/lib/osx/openFrameworksDebug.a` was generated.

3. **Checkout the [Perfume openFrameworks example](https://github.com/perfume-dev/example-openFrameworks)**

        git clone https://github.com/perfume-dev/example-openFrameworks.git

    You need to put it under `openFrameworks` or change the xcode config file manually. I suggest that you put it under `openFrameworks`. The path should look like this: `openFrameworks/example-openFrameworks/ofxBvh/...`

4. **Download the Perfume bvh and BGM from [Perfume Global Site](http://www.perfume-global.com)**

    In case you couldn't find it, the download section is located at the top left corner.

    Extract files to `openFrameworks/example-openFrameworks/ofxBvh/example-sync-sound/bin/data`. The `data` folder should contains one folder called `bvhfiles` and a wav file `Perfume_globalsite_sound.wav`.

5. **Run the `example-sync-sound`**

	Open `example-sync-sound.xcodeproj` and run. If GLUT.framework wasn't linked correctly, you need to add it manually.

# Have fun!


