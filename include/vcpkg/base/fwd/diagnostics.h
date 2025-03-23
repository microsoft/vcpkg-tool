#pragma once

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

    struct TextRowCol;
    struct DiagnosticLine;
    struct DiagnosticContext;
    struct PrintingDiagnosticContext;
    struct BufferedDiagnosticContext;
    struct FullyBufferedDiagnosticContext;
    struct AttemptDiagnosticContext;
    struct WarningDiagnosticContext;

    extern DiagnosticContext& console_diagnostic_context;
    extern DiagnosticContext& status_only_diagnostic_context;
    extern DiagnosticContext& null_diagnostic_context;
}
