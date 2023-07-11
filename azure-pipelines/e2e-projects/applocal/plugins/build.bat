cd %~dp0
pushd installed\tools\azure-kinect-sensor-sdk
cl /LD depthengine_2_0.cpp
popd
pushd installed\bin
cl /LD k4a.cpp
popd
cl /EHsc main.cpp installed/bin/k4a.lib
