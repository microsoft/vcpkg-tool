cd %1
cl /LD k4a.cpp
cl /LD depthengine_2_0.cpp
cl /EHsc main.cpp k4a.lib depthengine_2_0.lib