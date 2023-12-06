cd %~dp0
pushd installed\tools\azure-kinect-sensor-sdk
cl /LD depthengine_2_0.cpp
popd
pushd installed\debug\bin
cl /LD k4a.cpp
popd
cl /EHsc main.cpp installed/debug/bin/k4a.lib
