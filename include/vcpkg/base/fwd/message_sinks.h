#pragma once

namespace vcpkg
{
    struct MessageSink;

    extern MessageSink& null_sink;
    extern MessageSink& out_sink;
    extern MessageSink& stdout_sink;
    extern MessageSink& stderr_sink;

    struct FileSink;
    struct CombiningSink;
}
