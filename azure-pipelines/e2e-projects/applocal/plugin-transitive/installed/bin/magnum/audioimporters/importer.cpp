extern "C" __declspec(dllimport) void openni2_func();

extern "C" __declspec(dllexport) void importer_func() { openni2_func(); }
