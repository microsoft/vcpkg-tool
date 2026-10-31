extern "C" __declspec(dllimport) int qt_core();

extern "C" __declspec(dllexport) int qt_network() { return qt_core(); }
