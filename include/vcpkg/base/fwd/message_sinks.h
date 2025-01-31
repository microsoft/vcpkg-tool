#pragma once

namespace vcpkg
{
    struct MessageLineSegment;
    struct MessageLine;
    struct MessageSink;

    extern MessageSink& null_sink;
    extern MessageSink& out_sink;
    extern MessageSink& stdout_sink;
    extern MessageSink& stderr_sink;

    struct FileSink;
    struct TeeSink;
    struct BGMessageSink;
}
