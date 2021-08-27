# FBX2GLTF

This is an interm fbx2gltf release.

Change skinning-weights to 4 if your engine does not support that feature.

Change the default import of the engine to be different from 30 fps if needed.

There are artifacts in the Github Actions for Windows, MacOS and Linux.

This is a build used with values according to the company's internal model values.

## Build Instructions

Install conan in your python environment.

Using Visual Studio 2017.

Install FBX SDK.

In FBX2glTF directory, create 'build' directory, and run these commands:

```
> conan remote update bincrafters https://bincrafters.jfrog.io/artifactory/api/conan/public-conan 

> conan config set general.revisions_enabled=1 

> conan.exe  install . -i build -s build_type=Release -e FBXSDK_SDKS="C:/Program Files/Autodesk/FBX/FBX SDK" -s compiler.version=15  --build=missing 

> cd build

> cmake -DCMAKE_BUILD_TYPE=Release ..

> cmake --build  . --target ALL_BUILD --config Release

```
