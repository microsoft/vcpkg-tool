#include <vcpkg/base/contractual-constants.h>
#include <vcpkg/base/util.h>

#include <vcpkg/statusparagraph.h>

namespace vcpkg
{
    StatusParagraph::StatusParagraph() noexcept : want(Want::ERROR_STATE), state(InstallState::ERROR_STATE) { }

    void serialize(const StatusParagraph& pgh, std::string& out_str)
    {
        auto want_literal = to_string_literal(pgh.want);
        auto state_literal = to_string_literal(pgh.state);
        serialize(pgh.package, out_str);
        out_str.append("Status: ")
            .append(want_literal.data(), want_literal.size())
            .append(" ok ")
            .append(state_literal.data(), state_literal.size())
            .push_back('\n');
    }

    StatusParagraph::StatusParagraph(StringView origin, Paragraph&& fields)
        : want(Want::ERROR_STATE), state(InstallState::ERROR_STATE)
    {
        auto status_it = fields.find(ParagraphIdStatus);
        Checks::msg_check_maybe_upgrade(VCPKG_LINE_INFO, status_it != fields.end(), msgExpectedStatusField);
        std::string status_field = std::move(status_it->second.first);
        fields.erase(status_it);

        this->package = BinaryParagraph(origin, std::move(fields));

        auto b = status_field.begin();
        const auto mark = b;
        const auto e = status_field.end();

        // Todo: improve error handling
        while (b != e && *b != ' ')
            ++b;

        want = [](const std::string& text) {
            if (text == StatusUnknown) return Want::UNKNOWN;
            if (text == StatusInstall) return Want::INSTALL;
            if (text == StatusHold) return Want::HOLD;
            if (text == StatusDeinstall) return Want::DEINSTALL;
            if (text == StatusPurge) return Want::PURGE;
            return Want::ERROR_STATE;
        }(std::string(mark, b));

        if (std::distance(b, e) < 4) return;
        b += 4;

        state = [](const std::string& text) {
            if (text == StatusNotInstalled) return InstallState::NOT_INSTALLED;
            if (text == StatusInstalled) return InstallState::INSTALLED;
            if (text == StatusHalfInstalled) return InstallState::HALF_INSTALLED;
            return InstallState::ERROR_STATE;
        }(std::string(b, e));
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
            case Want::UNKNOWN: return StatusUnknown;
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
            for (auto&& dep : feature->package.dependencies)
            {
                deps.push_back(dep);
            }
        }

        // Add the core paragraph dependencies to the list
        for (auto&& dep : core->package.dependencies)
        {
            deps.push_back(dep);
        }

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
