extern "C" __declspec(dllimport) int qt_gui();
extern "C" __declspec(dllimport) int qt_network();
extern "C" __declspec(dllimport) int qt_printsupport();
extern "C" __declspec(dllimport) int qt_sql();
extern "C" __declspec(dllimport) int qt_widgets();

int main() { return qt_gui() + qt_network() + qt_printsupport() + qt_sql() + qt_widgets(); }
