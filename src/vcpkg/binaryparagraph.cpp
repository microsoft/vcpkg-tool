#include <vcpkg/base/checks.h>
#include <vcpkg/base/contractual-constants.h>
#include <vcpkg/base/strings.h>
#include <vcpkg/base/util.h>

#include <vcpkg/binaryparagraph.h>
#include <vcpkg/paragraphparser.h>
#include <vcpkg/paragraphs.h>

using namespace vcpkg::Paragraphs;

namespace vcpkg
{
    BinaryParagraph::BinaryParagraph(StringView origin, Paragraph&& fields)
        : spec(), version(), description(), maintainers(), feature(), default_features(), dependencies(), abi()
    {
        ParagraphParser parser(origin, std::move(fields));
        this->spec = PackageSpec(parser.required_field(ParagraphIdPackage),
                                 Triplet::from_canonical_name(parser.required_field(ParagraphIdArchitecture)));

        // one or the other
        this->version.text = parser.optional_field_or_empty(ParagraphIdVersion);
        auto maybe_port_version = parser.optional_field(ParagraphIdPortVersion);
        auto port_version = maybe_port_version.get();
        this->version.port_version = 0;
        if (port_version)
        {
            auto pv_opt = Strings::strto<int>(port_version->first);
            if (auto pv = pv_opt.get())
            {
                this->version.port_version = *pv;
            }
            else
            {
                parser.add_error(port_version->second, msgPortVersionControlMustBeANonNegativeInteger);
            }
        }

        this->feature = parser.optional_field_or_empty(ParagraphIdFeature);
        this->description = Strings::split(parser.optional_field_or_empty(ParagraphIdDescription), '\n');
        this->maintainers = Strings::split(parser.optional_field_or_empty(ParagraphIdMaintainer), '\n');

        this->abi = parser.optional_field_or_empty(ParagraphIdAbi);

        std::string multi_arch = parser.required_field(ParagraphIdMultiArch);

        Triplet my_triplet = this->spec.triplet();
        auto maybe_depends_field = parser.optional_field(ParagraphIdDepends);
        if (auto depends_field = maybe_depends_field.get())
        {
            this->dependencies = Util::fmap(
                parse_qualified_specifier_list(std::move(depends_field->first), origin, depends_field->second)
                    .value_or_exit(VCPKG_LINE_INFO),
                [my_triplet](const ParsedQualifiedSpecifier& dep) {
                    // for compatibility with previous vcpkg versions, we discard all irrelevant information
                    return PackageSpec{
                        dep.name.value,
                        dep.triplet
                            .map([](const Located<std::string>& s) { return Triplet::from_canonical_name(s.value); })
                            .value_or(my_triplet),
                    };
                });
        }
        if (!this->is_feature())
        {
            auto maybe_default_features_field = parser.optional_field(ParagraphIdDefaultFeatures);
            if (auto default_features_field = maybe_default_features_field.get())
            {
                this->default_features = parse_default_features_list(std::move(default_features_field->first),
                                                                     origin,
                                                                     default_features_field->second)
                                             .value_or_exit(VCPKG_LINE_INFO);
            }
        }

        // This is leftover from a previous attempt to add "alias ports", not currently used.
        (void)parser.optional_field("Type");
        const auto maybe_error = parser.error();
        if (auto error = maybe_error.get())
        {
            msg::println_error(msgErrorParsingBinaryParagraph, msg::spec = this->spec);
            print_error_message(*error);
            Checks::exit_fail(VCPKG_LINE_INFO);
        }

        // prefer failing above when possible because it gives better information
        Checks::msg_check_exit(VCPKG_LINE_INFO, multi_arch == "same", msgMultiArch, msg::option = multi_arch);

        canonicalize();
    }

    BinaryParagraph::BinaryParagraph(const SourceParagraph& spgh,
                                     const std::vector<std::string>& default_features,
                                     Triplet triplet,
                                     const std::string& abi_tag,
                                     std::vector<PackageSpec> deps)
        : spec(spgh.name, triplet)
        , version(spgh.version)
        , description(spgh.description)
        , maintainers(spgh.maintainers)
        , feature()
        , default_features(default_features)
        , dependencies(std::move(deps))
        , abi(abi_tag)
    {
        canonicalize();
    }

    BinaryParagraph::BinaryParagraph(const PackageSpec& spec,
                                     const FeatureParagraph& fpgh,
                                     std::vector<PackageSpec> deps)
        : spec(spec)
        , version()
        , description(fpgh.description)
        , maintainers()
        , feature(fpgh.name)
        , default_features()
        , dependencies(std::move(deps))
        , abi()
    {
        canonicalize();
    }

    void BinaryParagraph::canonicalize()
    {
        constexpr auto all_empty = [](const std::vector<std::string>& range) {
            return std::all_of(range.begin(), range.end(), [](const std::string& el) { return el.empty(); });
        };

        Util::sort_unique_erase(this->dependencies);

        for (auto& maintainer : this->maintainers)
        {
            Strings::inplace_trim(maintainer);
        }
        if (all_empty(this->maintainers))
        {
            this->maintainers.clear();
        }

        for (auto& desc : this->description)
        {
            Strings::inplace_trim(desc);
        }
        if (all_empty(this->description))
        {
            this->description.clear();
        }
    }

    std::string BinaryParagraph::display_name() const
    {
        if (!this->is_feature() || this->feature == FeatureNameCore)
        {
            return fmt::format("{}:{}", this->spec.name(), this->spec.triplet());
        }

        return fmt::format("{}[{}]:{}", this->spec.name(), this->feature, this->spec.triplet());
    }

    std::string BinaryParagraph::fullstem() const
    {
        return fmt::format("{}_{}_{}", this->spec.name(), this->version.text, this->spec.triplet());
    }

    bool operator==(const BinaryParagraph& lhs, const BinaryParagraph& rhs)
    {
        if (lhs.spec != rhs.spec) return false;
        if (lhs.version != rhs.version) return false;
        if (lhs.description != rhs.description) return false;
        if (lhs.maintainers != rhs.maintainers) return false;
        if (lhs.feature != rhs.feature) return false;
        if (lhs.default_features != rhs.default_features) return false;
        if (lhs.dependencies != rhs.dependencies) return false;
        if (lhs.abi != rhs.abi) return false;

        return true;
    }

    bool operator!=(const BinaryParagraph& lhs, const BinaryParagraph& rhs) { return !(lhs == rhs); }

    static void serialize_array(StringView name,
                                const std::vector<std::string>& array,
                                std::string& out_str,
                                StringLiteral joiner = ", ")
    {
        if (array.empty())
        {
            return;
        }

        out_str.append(name.data(), name.size()).append(": ");
        out_str.append(Strings::join(joiner, array));
        out_str.push_back('\n');
    }
    static void serialize_paragraph(StringView name, const std::vector<std::string>& array, std::string& out_str)
    {
        serialize_array(name, array, out_str, "\n    ");
    }

    static std::string serialize_deps_list(View<PackageSpec> deps, Triplet target)
    {
        return Strings::join(", ", deps, [target](const PackageSpec& pspec) {
            if (pspec.triplet() == target)
            {
                return pspec.name();
            }
            else
            {
                return pspec.to_string();
            }
        });
    }

    void serialize(const BinaryParagraph& pgh, std::string& out_str)
    {
        const size_t initial_end = out_str.size();

        append_paragraph_field(ParagraphIdPackage, pgh.spec.name(), out_str);
        append_paragraph_field(ParagraphIdVersion, pgh.version.text, out_str);
        if (pgh.version.port_version != 0)
        {
            fmt::format_to(std::back_inserter(out_str), "{}: {}\n", ParagraphIdPortVersion, pgh.version.port_version);
        }

        if (pgh.is_feature())
        {
            append_paragraph_field(ParagraphIdFeature, pgh.feature, out_str);
        }

        if (!pgh.dependencies.empty())
        {
            append_paragraph_field(
                ParagraphIdDepends, serialize_deps_list(pgh.dependencies, pgh.spec.triplet()), out_str);
        }

        append_paragraph_field(ParagraphIdArchitecture, pgh.spec.triplet().to_string(), out_str);
        append_paragraph_field(ParagraphIdMultiArch, "same", out_str);
        serialize_paragraph(ParagraphIdMaintainer, pgh.maintainers, out_str);
        append_paragraph_field(ParagraphIdAbi, pgh.abi, out_str);
        serialize_paragraph(ParagraphIdDescription, pgh.description, out_str);
        serialize_array(ParagraphIdDefaultFeatures, pgh.default_features, out_str);

        // sanity check the serialized data
        auto my_paragraph = StringView{out_str}.substr(initial_end);
        static constexpr StringLiteral sanity_parse_origin = "vcpkg::serialize(const BinaryParagraph&, std::string&)";
        auto parsed_paragraph =
            Paragraphs::parse_single_paragraph(StringView{out_str}.substr(initial_end), sanity_parse_origin);
        if (!parsed_paragraph)
        {
            Checks::msg_exit_maybe_upgrade(
                VCPKG_LINE_INFO,
                msg::format(msgFailedToParseSerializedBinParagraph, msg::error_msg = parsed_paragraph.error())
                    .append_raw('\n')
                    .append_raw(my_paragraph));
        }

        auto binary_paragraph = BinaryParagraph(sanity_parse_origin, std::move(*parsed_paragraph.get()));
        if (binary_paragraph != pgh)
        {
            Checks::msg_exit_maybe_upgrade(VCPKG_LINE_INFO,
                                           msg::format(msgMismatchedBinParagraphs)
                                               .append(msgOriginalBinParagraphHeader)
                                               .append_raw(format_binary_paragraph(pgh))
                                               .append(msgSerializedBinParagraphHeader)
                                               .append_raw(format_binary_paragraph(binary_paragraph)));
        }
    }

    std::string format_binary_paragraph(const BinaryParagraph& paragraph)
    {
        static constexpr StringLiteral join_str = R"(", ")";
        return fmt::format(
            "\nspec: \"{}\"\nversion: \"{}\"\nport_version: {}\ndescription: [\"{}\"]\nmaintainers: [\"{}\"]\nfeature: "
            "\"{}\"\ndefault_features: [\"{}\"]\ndependencies: [\"{}\"]\nabi: \"{}\"",
            paragraph.spec,
            paragraph.version.text,
            paragraph.version.port_version,
            Strings::join(join_str, paragraph.description),
            Strings::join(join_str, paragraph.maintainers),
            paragraph.feature,
            Strings::join(join_str, paragraph.default_features),
            Strings::join(join_str, paragraph.dependencies),
            paragraph.abi);
    }
}
