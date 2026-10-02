extern "C" __declspec(dllimport) int qt_gui();

extern "C" __declspec(dllexport) int qt_widgets() { return qt_gui(); }
