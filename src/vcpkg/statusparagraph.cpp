#include <vcpkg/base/contractual-constants.h>
#include <vcpkg/base/util.h>

#include <vcpkg/paragraphs.h>
#include <vcpkg/statusparagraph.h>

using namespace vcpkg::Paragraphs;

namespace vcpkg
{
    void StatusLine::to_string(std::string& out) const
    {
        fmt::format_to(std::back_inserter(out), "{} ok {}", want, state);
    }

    std::string StatusLine::to_string() const { return adapt_to_string(*this); }

    ExpectedL<StatusLine> parse_status_line(StringView text, Optional<StringView> origin, TextRowCol init_rowcol)
    {
        ParserBase parser{text, origin, init_rowcol};
        StatusLine result;
        const auto want_start = parser.cur_loc();
        auto want_text = parser.match_until(ParserBase::is_whitespace);
        if (want_text == StatusInstall)
        {
            result.want = Want::INSTALL;
        }
        else if (want_text == StatusHold)
        {
            result.want = Want::HOLD;
        }
        else if (want_text == StatusDeinstall)
        {
            result.want = Want::DEINSTALL;
        }
        else if (want_text == StatusPurge)
        {
            result.want = Want::PURGE;
        }
        else
        {
            parser.add_error(msg::format(msgExpectedWantField), want_start);
            return parser.messages().join();
        }

        if (parser.require_text(" ok "))
        {
            auto state_start = parser.cur_loc();
            auto state_text = parser.match_until(ParserBase::is_whitespace);
            if (state_text == StatusNotInstalled)
            {
                result.state = InstallState::NOT_INSTALLED;
            }
            else if (state_text == StatusInstalled)
            {
                result.state = InstallState::INSTALLED;
            }
            else if (state_text == StatusHalfInstalled)
            {
                result.state = InstallState::HALF_INSTALLED;
            }
            else
            {
                parser.add_error(msg::format(msgExpectedInstallStateField), state_start);
                return parser.messages().join();
            }

            if (parser.messages().good())
            {
                return result;
            }
        }

        return parser.messages().join();
    }

    void serialize(const StatusParagraph& pgh, std::string& out)
    {
        serialize(pgh.package, out);
        append_paragraph_field(ParagraphIdStatus, pgh.status.to_string(), out);
    }

    StatusParagraph::StatusParagraph(StringView origin, Paragraph&& fields)
    {
        auto status_it = fields.find(ParagraphIdStatus);
        Checks::msg_check_maybe_upgrade(VCPKG_LINE_INFO, status_it != fields.end(), msgExpectedStatusField);
        auto status_field = std::move(status_it->second);
        fields.erase(status_it);
        this->package = BinaryParagraph(origin, std::move(fields));
        this->status =
            parse_status_line(status_field.first, origin, status_field.second).value_or_exit(VCPKG_LINE_INFO);
    }

    StringLiteral to_string_literal(InstallState f)
    {
        switch (f)
        {
            case InstallState::HALF_INSTALLED: return StatusHalfInstalled;
            case InstallState::INSTALLED: return StatusInstalled;
            case InstallState::NOT_INSTALLED: return StatusNotInstalled;
            default: Checks::unreachable(VCPKG_LINE_INFO);
        }
    }

    StringLiteral to_string_literal(Want f)
    {
        switch (f)
        {
            case Want::DEINSTALL: return StatusDeinstall;
            case Want::HOLD: return StatusHold;
            case Want::INSTALL: return StatusInstall;
            case Want::PURGE: return StatusPurge;
            default: Checks::unreachable(VCPKG_LINE_INFO);
        }
    }

    std::map<std::string, std::vector<FeatureSpec>> InstalledPackageView::feature_dependencies() const
    {
        auto extract_deps = [](const PackageSpec& spec) { return FeatureSpec{spec, FeatureNameCore}; };

        std::map<std::string, std::vector<FeatureSpec>> deps;
        deps.emplace(FeatureNameCore, Util::fmap(core->package.dependencies, extract_deps));
        for (const StatusParagraph* feature : features)
        {
            deps.emplace(feature->package.feature, Util::fmap(feature->package.dependencies, extract_deps));
        }

        return deps;
    }

    InternalFeatureSet InstalledPackageView::feature_list() const
    {
        InternalFeatureSet ret;
        ret.emplace_back(FeatureNameCore);
        for (const StatusParagraph* f : features)
        {
            ret.emplace_back(f->package.feature);
        }

        return ret;
    }

    const Version& InstalledPackageView::version() const { return core->package.version; }

    std::vector<PackageSpec> InstalledPackageView::dependencies() const
    {
        // accumulate all features in installed dependencies
        // Todo: make this unneeded by collapsing all package dependencies into the core package
        std::vector<PackageSpec> deps;
        for (const StatusParagraph* feature : features)
        {
            Util::Vectors::append(deps, feature->package.dependencies);
        }

        // Add the core paragraph dependencies to the list
        Util::Vectors::append(deps, core->package.dependencies);

        const auto& this_spec = this->spec();
        Util::erase_remove_if(deps, [this_spec](const PackageSpec& pspec) { return pspec == this_spec; });
        Util::sort_unique_erase(deps);
        return deps;
    }

    std::vector<StatusParagraph> InstalledPackageView::all_status_paragraphs() const
    {
        std::vector<StatusParagraph> result;
        result.emplace_back(*core);
        for (const StatusParagraph* feature : features)
        {
            result.emplace_back(*feature);
        }

        return result;
    }
}
