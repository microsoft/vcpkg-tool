extern "C" __declspec(dllimport) void magnum_audio_func();
extern "C" __declspec(dllimport) void openni2_func();

int main()
{
    magnum_audio_func();
    openni2_func();
    return 0;
}
