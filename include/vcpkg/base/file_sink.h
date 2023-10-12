#pragma once

#include <vcpkg/base/files.h>
#include <vcpkg/base/message_sinks.h>

namespace vcpkg
{
    struct FileSink : MessageSink
    {
        Path m_log_file;
        WriteFilePointer m_out_file;

        FileSink(const Filesystem& fs, StringView log_file, Append append_to_file)
            : m_log_file(log_file), m_out_file(fs.open_for_write(m_log_file, append_to_file, VCPKG_LINE_INFO))
        {
        }
        void print(Color c, StringView sv) override;
    };
}
