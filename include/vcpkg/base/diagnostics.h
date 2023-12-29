#pragma once

#include <vcpkg/base/expected.h>
#include <vcpkg/base/messages.h>
#include <vcpkg/base/optional.h>

#include <mutex>
#include <string>
#include <type_traits>
#include <vector>

namespace vcpkg
{
    enum class DiagKind
    {
        None,    // foo.h: localized
        Message, // foo.h: message: localized
        Error,   // foo.h: error: localized
        Warning, // foo.h: warning: localized
        Note,    // foo.h: note: localized
        COUNT
    };

    struct TextPosition
    {
        // '0' indicates uninitialized; '1' is the first row/column
        int row = 0;
        int column = 0;
    };

    struct DiagnosticLine
    {
        DiagnosticLine(DiagKind kind, LocalizedString&& message)
            : m_kind(kind), m_origin(), m_position(), m_message(std::move(message))
        {
        }

        DiagnosticLine(DiagKind kind, StringView origin, LocalizedString&& message)
            : m_kind(kind), m_origin(origin.to_string()), m_position(), m_message(std::move(message))
        {
        }

        DiagnosticLine(DiagKind kind, StringView origin, TextPosition position, LocalizedString&& message)
            : m_kind(kind), m_origin(origin.to_string()), m_position(position), m_message(std::move(message))
        {
        }

        // Prints this diagnostic to the terminal.
        // Not thread safe: The console DiagnosticContext must apply its own synchronization.
        void print(MessageSink& sink) const;
        // Converts this message into a string
        // Prefer print() if possible because it applies color
        std::string to_string() const;
        void to_string(std::string& target) const;

    private:
        DiagKind m_kind;
        Optional<std::string> m_origin;
        TextPosition m_position;
        LocalizedString m_message;
    };

    struct DiagnosticContext
    {
        // Records a diagnostic. Implementations must make simultaneous calls of report() safe from multiple threads
        // and print entire DiagnosticLines as atomic units. Implementations are not required to synchronize with
        // other machinery like msg::print and friends.
        //
        // This serves to make multithreaded code that reports only via this mechanism safe.
        virtual void report(const DiagnosticLine& line) = 0;
        virtual void report(DiagnosticLine&& line) { report(line); }

    protected:
        ~DiagnosticContext() = default;
    };

    struct BufferedDiagnosticContext final : DiagnosticContext
    {
        virtual void report(const DiagnosticLine& line) override;
        virtual void report(DiagnosticLine&& line) override;

        std::vector<DiagnosticLine> lines;

        // Prints all diagnostics to the terminal.
        // Not safe to use in the face of concurrent calls to report()
        void print(MessageSink& sink) const;
        // Converts this message into a string
        // Prefer print() if possible because it applies color
        // Not safe to use in the face of concurrent calls to report()
        std::string to_string() const;
        void to_string(std::string& target) const;

    private:
        std::mutex m_mtx;
    };

    extern DiagnosticContext& console_diagnostic_context;

    // If T Ty is an rvalue Optional<U>, typename UnwrapOptional<Ty>::type is the type necessary to forward U
    // Otherwise, there is no member UnwrapOptional<Ty>::type
    template<class Ty>
    struct UnwrapOptional
    {
        // no member ::type, SFINAEs out when the input type is:
        // * not Optional
        // * not an rvalue
        // * volatile
    };

    template<class Wrapped>
    struct UnwrapOptional<Optional<Wrapped>>
    {
        // prvalue
        using type = Wrapped;
        using fwd = Wrapped&&;
    };

    template<class Wrapped>
    struct UnwrapOptional<const Optional<Wrapped>>
    {
        // const prvalue
        using type = Wrapped;
        using fwd = const Wrapped&&;
    };

    template<class Wrapped>
    struct UnwrapOptional<Optional<Wrapped>&&>
    {
        // xvalue
        using type = Wrapped&&;
        using fwd = Wrapped&&;
    };

    template<class Wrapped>
    struct UnwrapOptional<const Optional<Wrapped>&&>
    {
        // const xvalue
        using type = const Wrapped&&;
        using fwd = Wrapped&&;
    };

    template<class Fn, class... Args>
    auto adapt_context_to_expected(Fn functor, Args&&... args)
        -> ExpectedL<typename UnwrapOptional<std::invoke_result_t<Fn, BufferedDiagnosticContext&, Args...>>::type>
    {
        using Contained = typename UnwrapOptional<std::invoke_result_t<Fn, BufferedDiagnosticContext&, Args...>>::type;
        BufferedDiagnosticContext bdc;
        auto maybe_result = functor(bdc, std::forward<Args>(args)...);
        if (auto result = maybe_result.get())
        {
            // N.B.: This may be a move
            return ExpectedL<Contained>{
                static_cast<
                    typename UnwrapOptional<std::invoke_result_t<Fn, BufferedDiagnosticContext&, Args...>>::fwd>(
                    *result),
                expected_left_tag};
        }

        return ExpectedL<Contained>{LocalizedString::from_raw(bdc.to_string()), expected_right_tag};
    }
}
