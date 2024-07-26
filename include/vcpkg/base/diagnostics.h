#pragma once

#include <vcpkg/base/expected.h>
#include <vcpkg/base/messages.h>
#include <vcpkg/base/optional.h>

#include <memory>
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

    struct TextRowCol
    {
        // '0' indicates uninitialized; '1' is the first row/column
        int row = 0;
        int column = 0;
    };

    struct DiagnosticLine
    {
        template<class MessageLike, std::enable_if_t<std::is_convertible_v<MessageLike, LocalizedString>, int> = 0>
        DiagnosticLine(DiagKind kind, MessageLike&& message)
            : m_kind(kind), m_origin(), m_position(), m_message(std::forward<MessageLike>(message))
        {
        }

        template<class MessageLike, std::enable_if_t<std::is_convertible_v<MessageLike, LocalizedString>, int> = 0>
        DiagnosticLine(DiagKind kind, StringView origin, MessageLike&& message)
            : m_kind(kind), m_origin(origin.to_string()), m_position(), m_message(std::forward<MessageLike>(message))
        {
            if (origin.empty())
            {
                Checks::unreachable(VCPKG_LINE_INFO, "origin must not be empty");
            }
        }

        template<class MessageLike, std::enable_if_t<std::is_convertible_v<MessageLike, LocalizedString>, int> = 0>
        DiagnosticLine(DiagKind kind, StringView origin, TextRowCol position, MessageLike&& message)
            : m_kind(kind)
            , m_origin(origin.to_string())
            , m_position(position)
            , m_message(std::forward<MessageLike>(message))
        {
            if (origin.empty())
            {
                Checks::unreachable(VCPKG_LINE_INFO, "origin must not be empty");
            }
        }

        // Prints this diagnostic to the terminal.
        // Not thread safe: The console DiagnosticContext must apply its own synchronization.
        void print(MessageSink& sink) const;
        // Converts this message into a string
        // Prefer print() if possible because it applies color
        std::string to_string() const;
        void to_string(std::string& target) const;

        LocalizedString to_json_reader_string(const std::string& path, const LocalizedString& type) const;

        DiagKind kind() const noexcept { return m_kind; }

    private:
        DiagKind m_kind;
        Optional<std::string> m_origin;
        TextRowCol m_position;
        LocalizedString m_message;
    };

    struct DiagnosticContext
    {
        virtual void report(const DiagnosticLine& line) = 0;
        virtual void report(DiagnosticLine&& line) { report(line); }

        void report_error(const LocalizedString& message) { report(DiagnosticLine{DiagKind::Error, message}); }
        void report_error(LocalizedString&& message) { report(DiagnosticLine{DiagKind::Error, std::move(message)}); }
        template<VCPKG_DECL_MSG_TEMPLATE>
        void report_error(VCPKG_DECL_MSG_ARGS)
        {
            LocalizedString message;
            msg::format_to(message, VCPKG_EXPAND_MSG_ARGS);
            this->report_error(std::move(message));
        }

    protected:
        ~DiagnosticContext() = default;
    };

    struct BufferedDiagnosticContext final : DiagnosticContext
    {
        virtual void report(const DiagnosticLine& line) override;
        virtual void report(DiagnosticLine&& line) override;

        std::vector<DiagnosticLine> lines;

        // Prints all diagnostics to the terminal.
        void print(MessageSink& sink) const;
        // Converts this message into a string
        // Prefer print() if possible because it applies color
        std::string to_string() const;
        void to_string(std::string& target) const;

        bool any_errors() const noexcept;
    };

    extern DiagnosticContext& console_diagnostic_context;
    extern DiagnosticContext& null_diagnostic_context;

    // The following overloads are implementing
    // adapt_context_to_expected(Fn functor, Args&&... args)
    //
    // Given:
    // Optional<T> functor(DiagnosticContext&, args...), adapts functor to return ExpectedL<T>
    // Optional<T>& functor(DiagnosticContext&, args...), adapts functor to return ExpectedL<T&>
    // Optional<T>&& functor(DiagnosticContext&, args...), adapts functor to return ExpectedL<T>
    // std::unique_ptr<T> functor(DiagnosticContext&, args...), adapts functor to return ExpectedL<std::unique_ptr<T>>

    // If Ty is an Optional<U>, typename AdaptContextUnwrapOptional<Ty>::type is the type necessary to return U, and fwd
    // is the type necessary to forward U. Otherwise, there are no members ::type or ::fwd
    template<class Ty>
    struct AdaptContextUnwrapOptional
    {
        // no member ::type, SFINAEs out when the input type is:
        // * not Optional
        // * volatile
    };

    template<class Wrapped>
    struct AdaptContextUnwrapOptional<Optional<Wrapped>>
    {
        // prvalue, move from the optional into the expected
        using type = Wrapped;
        using fwd = Wrapped&&;
    };

    template<class Wrapped>
    struct AdaptContextUnwrapOptional<const Optional<Wrapped>>
    {
        // const prvalue
        using type = Wrapped;
        using fwd = const Wrapped&&;
    };

    template<class Wrapped>
    struct AdaptContextUnwrapOptional<Optional<Wrapped>&>
    {
        // lvalue, return an expected referencing the Wrapped inside the optional
        using type = Wrapped&;
        using fwd = Wrapped&;
    };

    template<class Wrapped>
    struct AdaptContextUnwrapOptional<const Optional<Wrapped>&>
    {
        // const lvalue
        using type = const Wrapped&;
        using fwd = const Wrapped&;
    };

    template<class Wrapped>
    struct AdaptContextUnwrapOptional<Optional<Wrapped>&&>
    {
        // xvalue, move from the optional into the expected
        using type = Wrapped;
        using fwd = Wrapped&&;
    };

    template<class Wrapped>
    struct AdaptContextUnwrapOptional<const Optional<Wrapped>&&>
    {
        // const xvalue
        using type = Wrapped;
        using fwd = const Wrapped&&;
    };

    // The overload for functors that return Optional<T>
    template<class Fn, class... Args>
    auto adapt_context_to_expected(Fn functor, Args&&... args)
        -> ExpectedL<
            typename AdaptContextUnwrapOptional<std::invoke_result_t<Fn, BufferedDiagnosticContext&, Args...>>::type>
    {
        using Unwrapper = AdaptContextUnwrapOptional<std::invoke_result_t<Fn, BufferedDiagnosticContext&, Args...>>;
        using ReturnType = ExpectedL<typename Unwrapper::type>;
        BufferedDiagnosticContext bdc;
        decltype(auto) maybe_result = functor(bdc, std::forward<Args>(args)...);
        if (auto result = maybe_result.get())
        {
            // N.B.: This may be a move
            return ReturnType{static_cast<typename Unwrapper::fwd>(*result), expected_left_tag};
        }

        return ReturnType{LocalizedString::from_raw(bdc.to_string()), expected_right_tag};
    }

    // If Ty is a std::unique_ptr<U>, typename AdaptContextDetectUniquePtr<Ty>::type is the type necessary to return
    template<class Ty>
    struct AdaptContextDetectUniquePtr
    {
        // no member ::type, SFINAEs out when the input type is:
        // * not unique_ptr
        // * volatile
    };

    template<class Wrapped, class Deleter>
    struct AdaptContextDetectUniquePtr<std::unique_ptr<Wrapped, Deleter>>
    {
        // prvalue, move into the Expected
        using type = std::unique_ptr<Wrapped, Deleter>;
    };

    template<class Wrapped, class Deleter>
    struct AdaptContextDetectUniquePtr<const std::unique_ptr<Wrapped, Deleter>>
    {
        // const prvalue (not valid, can't be moved from)
        // no members
    };

    template<class Wrapped, class Deleter>
    struct AdaptContextDetectUniquePtr<std::unique_ptr<Wrapped, Deleter>&>
    {
        // lvalue, reference the unique_ptr itself
        using type = std::unique_ptr<Wrapped, Deleter>&;
    };

    template<class Wrapped, class Deleter>
    struct AdaptContextDetectUniquePtr<const std::unique_ptr<Wrapped, Deleter>&>
    {
        // const lvalue
        using type = const std::unique_ptr<Wrapped, Deleter>&;
    };

    template<class Wrapped, class Deleter>
    struct AdaptContextDetectUniquePtr<std::unique_ptr<Wrapped, Deleter>&&>
    {
        // xvalue, move into the Expected
        using type = std::unique_ptr<Wrapped, Deleter>;
    };

    template<class Wrapped, class Deleter>
    struct AdaptContextDetectUniquePtr<const std::unique_ptr<Wrapped, Deleter>&&>
    {
        // const xvalue (not valid, can't be moved from)
        // no members
    };

    // The overload for functors that return std::unique_ptr<T>
    template<class Fn, class... Args>
    auto adapt_context_to_expected(Fn functor, Args&&... args)
        -> ExpectedL<
            typename AdaptContextDetectUniquePtr<std::invoke_result_t<Fn, BufferedDiagnosticContext&, Args...>>::type>
    {
        using ReturnType = ExpectedL<
            typename AdaptContextDetectUniquePtr<std::invoke_result_t<Fn, BufferedDiagnosticContext&, Args...>>::type>;
        BufferedDiagnosticContext bdc;
        decltype(auto) maybe_result = functor(bdc, std::forward<Args>(args)...);
        if (maybe_result)
        {
            return ReturnType{static_cast<decltype(maybe_result)&&>(maybe_result), expected_left_tag};
        }

        return ReturnType{LocalizedString::from_raw(bdc.to_string()), expected_right_tag};
    }
}
