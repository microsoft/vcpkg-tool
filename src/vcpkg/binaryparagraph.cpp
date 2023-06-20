#include <vcpkg/base/checks.h>
#include <vcpkg/base/strings.h>
#include <vcpkg/base/util.h>

#include <vcpkg/binaryparagraph.h>
#include <vcpkg/paragraphparser.h>
#include <vcpkg/paragraphs.h>

namespace vcpkg
{
    namespace Fields
    {
        static constexpr StringLiteral PACKAGE = "Package";
        static constexpr StringLiteral VERSION = "Version";
        static constexpr StringLiteral PORT_VERSION = "Port-Version";
        static constexpr StringLiteral ARCHITECTURE = "Architecture";
        static constexpr StringLiteral MULTI_ARCH = "Multi-Arch";
    }

    namespace Fields
    {
        static constexpr StringLiteral ABI = "Abi";
        static constexpr StringLiteral FEATURE = "Feature";
        static constexpr StringLiteral DESCRIPTION = "Description";
        static constexpr StringLiteral MAINTAINER = "Maintainer";
        static constexpr StringLiteral DEPENDS = "Depends";
        static constexpr StringLiteral DEFAULT_FEATURES = "Default-Features";
    }

    BinaryParagraph::BinaryParagraph() = default;

    BinaryParagraph::BinaryParagraph(Paragraph fields)
    {
        ParagraphParser parser(std::move(fields));

        {
            std::string name;
            parser.required_field(Fields::PACKAGE, name);
            std::string architecture;
            parser.required_field(Fields::ARCHITECTURE, architecture);
            this->spec = PackageSpec(std::move(name), Triplet::from_canonical_name(std::move(architecture)));
        }

        // one or the other
        this->version = parser.optional_field(Fields::VERSION);
        this->feature = parser.optional_field(Fields::FEATURE);

        auto pv_str = parser.optional_field(Fields::PORT_VERSION);
        this->port_version = 0;
        if (!pv_str.empty())
        {
            auto pv_opt = Strings::strto<int>(pv_str);
            if (auto pv = pv_opt.get())
            {
                this->port_version = *pv;
            }
            else
            {
                parser.add_type_error(Fields::PORT_VERSION, msg::format(msgANonNegativeInteger));
            }
        }

        this->description = Strings::split(parser.optional_field(Fields::DESCRIPTION), '\n');
        this->maintainers = Strings::split(parser.optional_field(Fields::MAINTAINER), '\n');

        this->abi = parser.optional_field(Fields::ABI);

        std::string multi_arch;
        parser.required_field(Fields::MULTI_ARCH, multi_arch);

        Triplet my_triplet = this->spec.triplet();
        this->dependencies = Util::fmap(
            parse_qualified_specifier_list(parser.optional_field(Fields::DEPENDS)).value_or_exit(VCPKG_LINE_INFO),
            [my_triplet](const ParsedQualifiedSpecifier& dep) {
                // for compatibility with previous vcpkg versions, we discard all irrelevant information
                return PackageSpec{
                    dep.name,
                    dep.triplet.map([](auto&& s) { return Triplet::from_canonical_name(std::string(s)); })
                        .value_or(my_triplet),
                };
            });
        if (!this->is_feature())
        {
            this->default_features = parse_default_features_list(parser.optional_field(Fields::DEFAULT_FEATURES))
                                         .value_or_exit(VCPKG_LINE_INFO);
        }

        // This is leftover from a previous attempt to add "alias ports", not currently used.
        (void)parser.optional_field("Type");
        if (const auto err = parser.error_info(this->spec.to_string()))
        {
            msg::println_error(msgErrorParsingBinaryParagraph, msg::spec = this->spec);
            print_error_message(err);
            Checks::exit_fail(VCPKG_LINE_INFO);
        }

        // prefer failing above when possible because it gives better information
        Checks::msg_check_exit(VCPKG_LINE_INFO, multi_arch == "same", msgMultiArch, msg::option = multi_arch);

        canonicalize();
    }

    BinaryParagraph::BinaryParagraph(const SourceParagraph& spgh,
                                     Triplet triplet,
                                     const std::string& abi_tag,
                                     std::vector<PackageSpec> deps)
        : spec(spgh.name, triplet)
        , version(spgh.raw_version)
        , port_version(spgh.port_version)
        , description(spgh.description)
        , maintainers(spgh.maintainers)
        , feature()
        , default_features(spgh.default_features)
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
        , port_version()
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

    std::string BinaryParagraph::displayname() const
    {
        if (!this->is_feature() || this->feature == "core")
        {
            return fmt::format("{}:{}", this->spec.name(), this->spec.triplet());
        }

        return fmt::format("{}[{}]:{}", this->spec.name(), this->feature, this->spec.triplet());
    }

    std::string BinaryParagraph::dir() const { return this->spec.dir(); }

    std::string BinaryParagraph::fullstem() const
    {
        return fmt::format("{}_{}_{}", this->spec.name(), this->version, this->spec.triplet());
    }

    bool operator==(const BinaryParagraph& lhs, const BinaryParagraph& rhs)
    {
        if (lhs.spec != rhs.spec) return false;
        if (lhs.version != rhs.version) return false;
        if (lhs.port_version != rhs.port_version) return false;
        if (lhs.description != rhs.description) return false;
        if (lhs.maintainers != rhs.maintainers) return false;
        if (lhs.feature != rhs.feature) return false;
        if (lhs.default_features != rhs.default_features) return false;
        if (lhs.dependencies != rhs.dependencies) return false;
        if (lhs.abi != rhs.abi) return false;

        return true;
    }

    bool operator!=(const BinaryParagraph& lhs, const BinaryParagraph& rhs) { return !(lhs == rhs); }

    static void serialize_string(StringView name, const std::string& field, std::string& out_str)
    {
        if (field.empty())
        {
            return;
        }

        out_str.append(name.begin(), name.end()).append(": ").append(field).push_back('\n');
    }
    static void serialize_array(StringView name,
                                const std::vector<std::string>& array,
                                std::string& out_str,
                                StringLiteral joiner = ", ")
    {
        if (array.empty())
        {
            return;
        }

        out_str.append(name.begin(), name.end()).append(": ");
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

        serialize_string(Fields::PACKAGE, pgh.spec.name(), out_str);

        serialize_string(Fields::VERSION, pgh.version, out_str);
        if (pgh.port_version != 0)
        {
            out_str.append(Fields::PORT_VERSION.data(), Fields::PORT_VERSION.size())
                .append(": ")
                .append(std::to_string(pgh.port_version))
                .push_back('\n');
        }

        if (pgh.is_feature())
        {
            serialize_string(Fields::FEATURE, pgh.feature, out_str);
        }

        if (!pgh.dependencies.empty())
        {
            serialize_string(Fields::DEPENDS, serialize_deps_list(pgh.dependencies, pgh.spec.triplet()), out_str);
        }

        serialize_string(Fields::ARCHITECTURE, pgh.spec.triplet().to_string(), out_str);
        serialize_string(Fields::MULTI_ARCH, "same", out_str);

        serialize_paragraph(Fields::MAINTAINER, pgh.maintainers, out_str);

        serialize_string(Fields::ABI, pgh.abi, out_str);

        serialize_paragraph(Fields::DESCRIPTION, pgh.description, out_str);

        serialize_array(Fields::DEFAULT_FEATURES, pgh.default_features, out_str);

        // sanity check the serialized data
        auto my_paragraph = StringView{out_str}.substr(initial_end);
        auto parsed_paragraph = Paragraphs::parse_single_paragraph(
            StringView{out_str}.substr(initial_end), "vcpkg::serialize(const BinaryParagraph&, std::string&)");
        if (!parsed_paragraph)
        {
            Checks::msg_exit_maybe_upgrade(
                VCPKG_LINE_INFO,
                msg::format(msgFailedToParseSerializedBinParagraph, msg::error_msg = parsed_paragraph.error())
                    .append_raw('\n')
                    .append_raw(my_paragraph));
        }

        auto binary_paragraph = BinaryParagraph(std::move(*parsed_paragraph.get()));
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
            paragraph.spec.to_string(),
            paragraph.version,
            paragraph.port_version,
            Strings::join(join_str, paragraph.description),
            Strings::join(join_str, paragraph.maintainers),
            paragraph.feature,
            Strings::join(join_str, paragraph.default_features),
            Strings::join(join_str, paragraph.dependencies),
            paragraph.abi);
    }
}
