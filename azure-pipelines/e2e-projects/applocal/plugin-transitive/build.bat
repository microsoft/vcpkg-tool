cd %~dp0
pushd installed\bin
cl /LD OpenNI2.cpp
cl /LD MagnumAudio.cpp
popd
pushd installed\bin\magnum\audioimporters
cl /LD importer.cpp ..\..\OpenNI2.lib
popd
cl /EHsc main.cpp installed\bin\MagnumAudio.lib installed\bin\OpenNI2.lib
