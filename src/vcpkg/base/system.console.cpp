#include <vcpkg/base/stringview.h>
#include <vcpkg/base/system.console.h>
#include <vcpkg/base/span.h>

namespace vcpkg
{
    void Console::print_lines(View<std::pair<Color, StringView>> lines)
    {
        std::lock_guard<std::mutex> lck{mtx};

        for (auto&& line : lines)
        {
            print_unlocked(line.first, line.second);
        }
    }

    void Console::print_unlocked(Color c, StringView sv) noexcept
    {
        static constexpr char reset_color_sequence[] = {'\033', '[', '0', 'm'};

        if (sv.empty()) return;

        std::lock_guard<std::mutex> lck{mtx};
        bool reset_color = false;
        // Only write color sequence if file descriptor is a terminal
        if (is_terminal && c != Color::none)
        {
            reset_color = true;

            const char set_color_sequence[] = {'\033', '[', '9', static_cast<char>(c), 'm'};
            write(set_color_sequence, sizeof(set_color_sequence));
        }

        write(sv.data(), sv.size());

        if (reset_color)
        {
            write(reset_color_sequence, sizeof(reset_color_sequence));
        }
    }

    void Console::write(const char* text, size_t count) noexcept
    {
        while (count != 0)
        {
            auto written = ::write(fd, text, count);
            if (written == -1)
            {
                ::fprintf(stderr, "[DEBUG] Failed to print to stdout: %d\n", errno);
                std::abort();
            }
            text += written;
            count -= written;
        }
    }

    Console std_out_instance(STDOUT_FILENO);
    Console std_error_instance(STDERR_FILENO);

    Console& std_out = std_out_instance;
    Console& std_error = std_out_instance;
} // namespace vcpkg

