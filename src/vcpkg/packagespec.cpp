#include <vcpkg/base/checks.h>
#include <vcpkg/base/messages.h>
#include <vcpkg/base/parse.h>
#include <vcpkg/base/util.h>

#include <vcpkg/packagespec.h>
#include <vcpkg/paragraphparser.h>

namespace vcpkg
{
    std::string FeatureSpec::to_string() const
    {
        std::string ret;
        this->to_string(ret);
        return ret;
    }
    void FeatureSpec::to_string(std::string& out) const
    {
        if (feature().empty()) return spec().to_string(out);
        Strings::append(out, port(), '[', feature(), "]:", triplet());
    }

    void FullPackageSpec::expand_fspecs_to(std::vector<FeatureSpec>& out) const
    {
        for (auto&& feature : features)
        {
            out.emplace_back(package_spec, feature);
        }
    }

    const std::string& PackageSpec::name() const { return this->m_name; }

    Triplet PackageSpec::triplet() const { return this->m_triplet; }

    std::string PackageSpec::dir() const { return Strings::format("%s_%s", this->m_name, this->m_triplet); }

    std::string PackageSpec::to_string() const { return Strings::format("%s:%s", this->name(), this->triplet()); }
    void PackageSpec::to_string(std::string& s) const { Strings::append(s, this->name(), ':', this->triplet()); }

    bool operator==(const PackageSpec& left, const PackageSpec& right)
    {
        return left.name() == right.name() && left.triplet() == right.triplet();
    }

    DECLARE_AND_REGISTER_MESSAGE(IllegalPlatformSpec,
                                 (),
                                 "",
                                 "error: Platform qualifier is not allowed in this context");
    DECLARE_AND_REGISTER_MESSAGE(IllegalFeatures, (), "", "error: List of features is not allowed in this contect");

    static InternalFeatureSet normalize_feature_list(View<std::string> fs, ImplicitDefault id)
    {
        InternalFeatureSet ret;
        bool core = false;
        for (auto&& f : fs)
        {
            if (f == "core")
            {
                core = true;
            }
            ret.emplace_back(f);
        }

        if (!core)
        {
            ret.emplace_back("core");
            if (id == ImplicitDefault::YES)
            {
                ret.emplace_back("default");
            }
        }
        return ret;
    }

    ExpectedS<FullPackageSpec> ParsedQualifiedSpecifier::to_full_spec(Triplet default_triplet, ImplicitDefault id) const
    {
        if (platform)
        {
            return {msg::format(msgIllegalPlatformSpec).data(), expected_right_tag};
        }

        const Triplet t = triplet ? Triplet::from_canonical_name(*triplet.get()) : default_triplet;
        const View<std::string> fs = !features.get() ? View<std::string>{} : *features.get();
        return FullPackageSpec{{name, t}, normalize_feature_list(fs, id)};
    }

    ExpectedS<PackageSpec> ParsedQualifiedSpecifier::to_package_spec(Triplet default_triplet) const
    {
        if (platform)
        {
            return {msg::format(msgIllegalPlatformSpec).data(), expected_right_tag};
        }
        if (features)
        {
            return {msg::format(msgIllegalFeatures).data(), expected_right_tag};
        }

        const Triplet t = triplet ? Triplet::from_canonical_name(*triplet.get()) : default_triplet;
        return PackageSpec{name, t};
    }

    ExpectedS<ParsedQualifiedSpecifier> parse_qualified_specifier(StringView input)
    {
        auto parser = ParserBase(input, "<unknown>");
        auto maybe_pqs = parse_qualified_specifier(parser);
        if (!parser.at_eof()) parser.add_error("expected eof");
        if (auto e = parser.get_error()) return e->format();
        return std::move(maybe_pqs).value_or_exit(VCPKG_LINE_INFO);
    }

    Optional<std::string> parse_feature_name(ParserBase& parser)
    {
        auto ret = parser.match_zero_or_more(ParserBase::is_package_name_char).to_string();
        auto ch = parser.cur();

        // ignores the feature name vwebp_sdl as a back-compat thing
        const bool has_underscore = std::find(ret.begin(), ret.end(), '_') != ret.end() && ret != "vwebp_sdl";
        if (has_underscore || ParserBase::is_upper_alpha(ch))
        {
            parser.add_error("invalid character in feature name (must be lowercase, digits, '-')");
            return nullopt;
        }

        if (ret == "default")
        {
            parser.add_error("'default' is a reserved feature name");
            return nullopt;
        }

        if (ret.empty())
        {
            parser.add_error("expected feature name (must be lowercase, digits, '-')");
            return nullopt;
        }
        return ret;
    }
    Optional<std::string> parse_package_name(ParserBase& parser)
    {
        auto ret = parser.match_zero_or_more(ParserBase::is_package_name_char).to_string();
        auto ch = parser.cur();
        if (ParserBase::is_upper_alpha(ch) || ch == '_')
        {
            parser.add_error("invalid character in package name (must be lowercase, digits, '-')");
            return nullopt;
        }
        if (ret.empty())
        {
            parser.add_error("expected package name (must be lowercase, digits, '-')");
            return nullopt;
        }
        return ret;
    }

    Optional<ParsedQualifiedSpecifier> parse_qualified_specifier(ParserBase& parser)
    {
        ParsedQualifiedSpecifier ret;
        auto name = parse_package_name(parser);
        if (auto n = name.get())
            ret.name = std::move(*n);
        else
            return nullopt;
        auto ch = parser.cur();
        if (ch == '[')
        {
            std::vector<std::string> features;
            do
            {
                parser.next();
                parser.skip_tabs_spaces();
                if (parser.cur() == '*')
                {
                    features.push_back("*");
                    parser.next();
                }
                else
                {
                    auto feature = parse_feature_name(parser);
                    if (auto f = feature.get())
                        features.push_back(std::move(*f));
                    else
                        return nullopt;
                }
                auto skipped_space = parser.skip_tabs_spaces();
                ch = parser.cur();
                if (ch == ']')
                {
                    ch = parser.next();
                    break;
                }
                else if (ch == ',')
                {
                    continue;
                }
                else
                {
                    if (skipped_space.size() > 0 || ParserBase::is_lineend(parser.cur()))
                        parser.add_error("expected ',' or ']' in feature list");
                    else
                        parser.add_error("invalid character in feature name (must be lowercase, digits, '-', or '*')");
                    return nullopt;
                }
            } while (true);
            ret.features = std::move(features);
        }
        if (ch == ':')
        {
            parser.next();
            ret.triplet = parser.match_zero_or_more(ParserBase::is_package_name_char).to_string();
            if (ret.triplet.get()->empty())
            {
                parser.add_error("expected triplet name (must be lowercase, digits, '-')");
                return nullopt;
            }
        }
        parser.skip_tabs_spaces();
        ch = parser.cur();
        if (ch == '(')
        {
            auto loc = parser.cur_loc();
            std::string platform_string;
            int depth = 1;
            while (depth > 0 && (ch = parser.next()) != 0)
            {
                if (ch == '(') ++depth;
                if (ch == ')') --depth;
            }
            if (depth > 0)
            {
                parser.add_error("unmatched open braces in platform specifier", loc);
                return nullopt;
            }
            platform_string.append((++loc.it).pointer_to_current(), parser.it().pointer_to_current());
            auto platform_opt = PlatformExpression::parse_platform_expression(
                platform_string, PlatformExpression::MultipleBinaryOperators::Allow);
            if (auto platform = platform_opt.get())
            {
                ret.platform = std::move(*platform);
            }
            else
            {
                parser.add_error(platform_opt.error(), loc);
            }
            parser.next();
        }
        // This makes the behavior of the parser more consistent -- otherwise, it will skip tabs and spaces only if
        // there isn't a qualifier.
        parser.skip_tabs_spaces();
        return ret;
    }

    bool operator==(const DependencyConstraint& lhs, const DependencyConstraint& rhs)
    {
        if (lhs.type != rhs.type) return false;
        if (lhs.value != rhs.value) return false;
        return lhs.port_version == rhs.port_version;
    }

    Optional<Version> DependencyConstraint::try_get_minimum_version() const
    {
        if (type == VersionConstraintKind::None)
        {
            return nullopt;
        }

        return Version{
            value,
            port_version,
        };
    }

    FullPackageSpec Dependency::to_full_spec(Triplet target, Triplet host_triplet, ImplicitDefault id) const
    {
        return FullPackageSpec{{name, host ? host_triplet : target}, normalize_feature_list(features, id)};
    }

    bool operator==(const Dependency& lhs, const Dependency& rhs)
    {
        if (lhs.name != rhs.name) return false;
        if (lhs.features != rhs.features) return false;
        if (!structurally_equal(lhs.platform, rhs.platform)) return false;
        if (lhs.extra_info != rhs.extra_info) return false;
        if (lhs.constraint != rhs.constraint) return false;
        if (lhs.host != rhs.host) return false;

        return true;
    }
    bool operator!=(const Dependency& lhs, const Dependency& rhs);

    bool operator==(const DependencyOverride& lhs, const DependencyOverride& rhs)
    {
        if (lhs.version_scheme != rhs.version_scheme) return false;
        if (lhs.port_version != rhs.port_version) return false;
        if (lhs.name != rhs.name) return false;
        if (lhs.version != rhs.version) return false;
        return lhs.extra_info == rhs.extra_info;
    }
    bool operator!=(const DependencyOverride& lhs, const DependencyOverride& rhs);
}
