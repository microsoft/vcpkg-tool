#include <vcpkg/base/fmt.h>
#include <vcpkg/base/messages.h>
#include <vcpkg/base/parse.h>

#include <vcpkg/packagespec.h>

namespace
{
    using namespace vcpkg;
    Triplet resolve_triplet(const Optional<std::string>& specified_triplet, Triplet default_triplet)
    {
        if (auto pspecified = specified_triplet.get())
        {
            return Triplet::from_canonical_name(*pspecified);
        }

        return default_triplet;
    }
} // unnamed namespace

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
        fmt::format_to(std::back_inserter(out), "{}[{}]:{}", port(), feature(), triplet());
    }

    std::string format_name_only_feature_spec(StringView package_name, StringView feature_name)
    {
        return fmt::format("{}[{}]", package_name, feature_name);
    }

    bool InternalFeatureSet::empty_or_only_core() const { return empty() || (size() == 1 && *begin() == "core"); }

    InternalFeatureSet internalize_feature_list(View<std::string> fs, ImplicitDefault id)
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

    void FullPackageSpec::expand_fspecs_to(std::vector<FeatureSpec>& out) const
    {
        for (auto&& feature : features)
        {
            out.emplace_back(package_spec, feature);
        }
    }

    const std::string& PackageSpec::name() const { return this->m_name; }

    Triplet PackageSpec::triplet() const { return this->m_triplet; }

    std::string PackageSpec::dir() const { return fmt::format("{}_{}", this->m_name, this->m_triplet); }

    std::string PackageSpec::to_string() const { return fmt::format("{}:{}", this->name(), this->triplet()); }
    void PackageSpec::to_string(std::string& s) const
    {
        fmt::format_to(std::back_inserter(s), "{}:{}", this->name(), this->triplet());
    }

    bool operator==(const PackageSpec& left, const PackageSpec& right)
    {
        return left.name() == right.name() && left.triplet() == right.triplet();
    }

    ExpectedL<FullPackageSpec> ParsedQualifiedSpecifier::to_full_spec(Triplet default_triplet, ImplicitDefault id) const
    {
        if (platform)
        {
            return msg::format_error(msgIllegalPlatformSpec);
        }

        View<std::string> fs{};
        if (auto pfeatures = features.get())
        {
            fs = *pfeatures;
        }

        return FullPackageSpec{{name, resolve_triplet(triplet, default_triplet)}, internalize_feature_list(fs, id)};
    }

    ExpectedL<PackageSpec> ParsedQualifiedSpecifier::to_package_spec(Triplet default_triplet) const
    {
        if (platform)
        {
            return msg::format_error(msgIllegalPlatformSpec);
        }

        if (features)
        {
            return msg::format_error(msgIllegalFeatures);
        }

        return PackageSpec{name, resolve_triplet(triplet, default_triplet)};
    }

    ExpectedL<ParsedQualifiedSpecifier> parse_qualified_specifier(StringView input)
    {
        auto parser = ParserBase(input, "<unknown>");
        auto maybe_pqs = parse_qualified_specifier(parser);
        if (!parser.at_eof()) parser.add_error(msg::format(msgExpectedEof));
        if (auto e = parser.get_error()) return LocalizedString::from_raw(e->to_string());
        return std::move(maybe_pqs).value_or_exit(VCPKG_LINE_INFO);
    }

    Optional<std::string> parse_feature_name(ParserBase& parser)
    {
        auto ret = parser.match_while(ParserBase::is_package_name_char).to_string();
        auto ch = parser.cur();

        // ignores the feature name vwebp_sdl as a back-compat thing
        const bool has_underscore = std::find(ret.begin(), ret.end(), '_') != ret.end() && ret != "vwebp_sdl";
        if (has_underscore || ParserBase::is_upper_alpha(ch))
        {
            parser.add_error(msg::format(msgInvalidCharacterInFeatureName));
            return nullopt;
        }

        if (ret == "default")
        {
            parser.add_error(msg::format(msgInvalidDefaultFeatureName));
            return nullopt;
        }

        if (ret.empty())
        {
            parser.add_error(msg::format(msgExpectedFeatureName));
            return nullopt;
        }
        return ret;
    }
    Optional<std::string> parse_package_name(ParserBase& parser)
    {
        auto ret = parser.match_while(ParserBase::is_package_name_char).to_string();
        auto ch = parser.cur();
        if (ParserBase::is_upper_alpha(ch) || ch == '_')
        {
            parser.add_error(msg::format(msgInvalidCharacterInPackageName));
            return nullopt;
        }
        if (ret.empty())
        {
            parser.add_error(msg::format(msgExpectedPortName));
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
                    features.emplace_back("*");
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
                    {
                        parser.add_error(msg::format(msgExpectedFeatureListTerminal));
                    }
                    else
                    {
                        parser.add_error(msg::format(msgInvalidCharacterInFeatureList));
                    }

                    return nullopt;
                }
            } while (true);
            ret.features = std::move(features);
        }
        if (ch == ':')
        {
            parser.next();
            ret.triplet = parser.match_while(ParserBase::is_package_name_char).to_string();
            if (ret.triplet.get()->empty())
            {
                parser.add_error(msg::format(msgExpectedTripletName));
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
                parser.add_error(msg::format(msgMissingClosingParen), loc);
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
                parser.add_error(std::move(platform_opt).error(), loc);
            }

            parser.next();
        }
        // This makes the behavior of the parser more consistent -- otherwise, it will skip tabs and spaces only if
        // there isn't a qualifier.
        parser.skip_tabs_spaces();
        return ret;
    }
}
