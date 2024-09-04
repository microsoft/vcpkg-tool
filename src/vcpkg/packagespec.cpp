#include <vcpkg/base/contractual-constants.h>
#include <vcpkg/base/fmt.h>
#include <vcpkg/base/messages.h>
#include <vcpkg/base/parse.h>
#include <vcpkg/base/strings.h>

#include <vcpkg/documentation.h>
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

    bool parse_features(char32_t& ch, ParsedQualifiedSpecifier& ret, ParserBase& parser)
    {
        auto& features = ret.features.emplace();
        for (;;)
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
                    return false;
            }
            auto skipped_space = parser.skip_tabs_spaces();
            ch = parser.cur();
            if (ch == ']')
            {
                ch = parser.next();
                return true;
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

                return false;
            }
        }
    }
} // unnamed namespace

namespace vcpkg
{
    std::string FeatureSpec::to_string() const { return adapt_to_string(*this); }
    void FeatureSpec::to_string(std::string& out) const
    {
        if (feature().empty()) return spec().to_string(out);
        fmt::format_to(std::back_inserter(out), "{}[{}]:{}", port(), feature(), triplet());
    }

    std::string format_name_only_feature_spec(StringView package_name, StringView feature_name)
    {
        return fmt::format("{}[{}]", package_name, feature_name);
    }

    bool InternalFeatureSet::empty_or_only_core() const
    {
        return empty() || (size() == 1 && *begin() == FeatureNameCore);
    }

    InternalFeatureSet internalize_feature_list(View<std::string> fs, ImplicitDefault id)
    {
        InternalFeatureSet ret;
        bool core = false;
        for (auto&& f : fs)
        {
            if (f == FeatureNameCore)
            {
                core = true;
            }
            ret.emplace_back(f);
        }

        if (!core)
        {
            ret.emplace_back(FeatureNameCore);
            if (id == ImplicitDefault::Yes)
            {
                ret.emplace_back(FeatureNameDefault);
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

    std::string PackageSpec::dir() const { return fmt::format("{}_{}", this->m_name, this->m_triplet); }

    std::string PackageSpec::to_string() const { return adapt_to_string(*this); }
    void PackageSpec::to_string(std::string& s) const
    {
        fmt::format_to(std::back_inserter(s), "{}:{}", this->name(), this->triplet());
    }

    bool operator==(const PackageSpec& left, const PackageSpec& right)
    {
        return left.name() == right.name() && left.triplet() == right.triplet();
    }

    FullPackageSpec ParsedQualifiedSpecifier::to_full_spec(Triplet default_triplet, ImplicitDefault id) const
    {
        if (platform)
        {
            Checks::unreachable(
                VCPKG_LINE_INFO,
                "AllowPlatformSpec must be No when calling parse_qualified_specifier and using to_full_spec");
        }

        View<std::string> fs{};
        if (auto pfeatures = features.get())
        {
            fs = *pfeatures;
        }

        return FullPackageSpec{{name, resolve_triplet(triplet, default_triplet)}, internalize_feature_list(fs, id)};
    }

    PackageSpec ParsedQualifiedSpecifier::to_package_spec(Triplet default_triplet) const
    {
        if (platform || features)
        {
            Checks::unreachable(VCPKG_LINE_INFO,
                                "AllowFeatures and AllowPlatformSpec must be No when calling "
                                "parse_qualified_specifier and using to_package_spec");
        }

        return PackageSpec{name, resolve_triplet(triplet, default_triplet)};
    }

    ExpectedL<ParsedQualifiedSpecifier> parse_qualified_specifier(StringView input,
                                                                  AllowFeatures allow_features,
                                                                  ParseExplicitTriplet parse_explicit_triplet,
                                                                  AllowPlatformSpec allow_platform_spec)
    {
        // there is no origin because this function is used for user inputs
        auto parser = ParserBase(input, nullopt);
        auto maybe_pqs = parse_qualified_specifier(parser, allow_features, parse_explicit_triplet, allow_platform_spec);
        if (!parser.at_eof())
        {
            if (allow_features == AllowFeatures::No && parse_explicit_triplet == ParseExplicitTriplet::Forbid &&
                allow_platform_spec == AllowPlatformSpec::No)
            {
                parser.add_error(msg::format(msgParsePackageNameNotEof, msg::url = docs::package_name_url));
            }
            else
            {
                // check if the user said zlib:x64-windows[core] instead of zlib[core]:x64-windows
                auto pqs = maybe_pqs.get();
                auto triplet = pqs ? pqs->triplet.get() : nullptr;
                if (pqs && triplet && !pqs->platform.has_value() && parser.cur() == '[')
                {
                    auto speculative_parser_copy = parser;
                    char32_t ch = '[';
                    if (parse_features(ch, *pqs, speculative_parser_copy) && speculative_parser_copy.at_eof())
                    {
                        auto presumed_spec =
                            fmt::format("{}[{}]:{}",
                                        pqs->name,
                                        Strings::join(",", pqs->features.value_or_exit(VCPKG_LINE_INFO)),
                                        *triplet);
                        parser.add_error(msg::format(msgParseQualifiedSpecifierNotEofSquareBracket,
                                                     msg::version_spec = presumed_spec));
                    }
                    else
                    {
                        parser.add_error(msg::format(msgParseQualifiedSpecifierNotEof));
                    }
                }
                else
                {
                    parser.add_error(msg::format(msgParseQualifiedSpecifierNotEof));
                }
            }
        }

        if (auto e = parser.get_error())
        {
            return LocalizedString::from_raw(e->to_string());
        }

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

        if (ret == FeatureNameDefault)
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
            parser.add_error(msg::format(msgInvalidCharacterInPortName));
            return nullopt;
        }
        if (ret.empty())
        {
            parser.add_error(msg::format(msgExpectedPortName));
            return nullopt;
        }
        return ret;
    }

    Optional<ParsedQualifiedSpecifier> parse_qualified_specifier(ParserBase& parser,
                                                                 AllowFeatures allow_features,
                                                                 ParseExplicitTriplet allow_triplet,
                                                                 AllowPlatformSpec allow_platform_spec)
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
            if (allow_features == AllowFeatures::No)
            {
                parser.add_error(msg::format(msgIllegalFeatures));
                return nullopt;
            }

            if (!parse_features(ch, ret, parser))
            {
                return nullopt;
            }
        }
        if (ch == ':')
        {
            auto triplet_start = parser.cur_loc();
            parser.next();
            auto triplet_parsed = parser.match_while(ParserBase::is_package_name_char);
            if (allow_triplet == ParseExplicitTriplet::Forbid)
            {
                parser.add_error(msg::format(msgAddTripletExpressionNotAllowed,
                                             msg::package_name = ret.name,
                                             msg::triplet = triplet_parsed),
                                 triplet_start);
                return nullopt;
            }

            if (triplet_parsed.empty())
            {
                parser.add_error(msg::format(msgExpectedTripletName));
                return nullopt;
            }

            ret.triplet.emplace(triplet_parsed.to_string());
        }
        else if (allow_triplet == ParseExplicitTriplet::Require)
        {
            parser.add_error(msg::format(msgExpectedExplicitTriplet));
            return nullopt;
        }

        parser.skip_tabs_spaces();
        ch = parser.cur();
        if (ch == '(')
        {
            if (allow_platform_spec == AllowPlatformSpec::No)
            {
                parser.add_error(msg::format(msgIllegalPlatformSpec));
                return nullopt;
            }

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
