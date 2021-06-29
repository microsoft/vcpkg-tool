#include <vcpkg/base/files.h>
#include <vcpkg/base/parse.h>
#include <vcpkg/base/system.debug.h>
#include <vcpkg/base/system.print.h>
#include <vcpkg/base/util.h>

#include <vcpkg/binaryparagraph.h>
#include <vcpkg/configuration.h>
#include <vcpkg/paragraphparser.h>
#include <vcpkg/paragraphs.h>
#include <vcpkg/registries.h>
#include <vcpkg/vcpkgpaths.h>

using namespace vcpkg::Parse;
using namespace vcpkg;

namespace vcpkg::Parse
{
    static Optional<std::pair<std::string, TextRowCol>> remove_field(Paragraph* fields, const std::string& fieldname)
    {
        auto it = fields->find(fieldname);
        if (it == fields->end())
        {
            return nullopt;
        }

        auto value = std::move(it->second);
        fields->erase(it);
        return value;
    }

    void ParagraphParser::required_field(const std::string& fieldname, std::pair<std::string&, TextRowCol&> out)
    {
        auto maybe_field = remove_field(&fields, fieldname);
        if (const auto field = maybe_field.get())
            out = std::move(*field);
        else
            missing_fields.push_back(fieldname);
    }
    void ParagraphParser::optional_field(const std::string& fieldname, std::pair<std::string&, TextRowCol&> out)
    {
        auto maybe_field = remove_field(&fields, fieldname);
        if (auto field = maybe_field.get()) out = std::move(*field);
    }
    void ParagraphParser::required_field(const std::string& fieldname, std::string& out)
    {
        TextRowCol ignore;
        required_field(fieldname, {out, ignore});
    }
    std::string ParagraphParser::optional_field(const std::string& fieldname)
    {
        std::string out;
        TextRowCol ignore;
        optional_field(fieldname, {out, ignore});
        return out;
    }
    std::string ParagraphParser::required_field(const std::string& fieldname)
    {
        std::string out;
        TextRowCol ignore;
        required_field(fieldname, {out, ignore});
        return out;
    }

    std::unique_ptr<ParseControlErrorInfo> ParagraphParser::error_info(const std::string& name) const
    {
        if (!fields.empty() || !missing_fields.empty())
        {
            auto err = std::make_unique<ParseControlErrorInfo>();
            err->name = name;
            err->extra_fields["CONTROL"] = Util::extract_keys(fields);
            err->missing_fields["CONTROL"] = std::move(missing_fields);
            err->expected_types = std::move(expected_types);
            return err;
        }
        return nullptr;
    }

    template<class T, class F>
    static Optional<std::vector<T>> parse_list_until_eof(StringLiteral plural_item_name, Parse::ParserBase& parser, F f)
    {
        std::vector<T> ret;
        parser.skip_whitespace();
        if (parser.at_eof()) return std::vector<T>{};
        do
        {
            auto item = f(parser);
            if (!item) return nullopt;
            ret.push_back(std::move(item).value_or_exit(VCPKG_LINE_INFO));
            parser.skip_whitespace();
            if (parser.at_eof()) return {std::move(ret)};
            if (parser.cur() != ',')
            {
                parser.add_error(Strings::concat("expected ',' or end of text in ", plural_item_name, " list"));
                return nullopt;
            }
            parser.next();
            parser.skip_whitespace();
        } while (true);
    }

    ExpectedS<std::vector<std::string>> parse_default_features_list(const std::string& str,
                                                                    StringView origin,
                                                                    TextRowCol textrowcol)
    {
        auto parser = Parse::ParserBase(str, origin, textrowcol);
        auto opt = parse_list_until_eof<std::string>("default features", parser, &parse_feature_name);
        if (!opt) return {parser.get_error()->format(), expected_right_tag};
        return {std::move(opt).value_or_exit(VCPKG_LINE_INFO), expected_left_tag};
    }
    ExpectedS<std::vector<ParsedQualifiedSpecifier>> parse_qualified_specifier_list(const std::string& str,
                                                                                    StringView origin,
                                                                                    TextRowCol textrowcol)
    {
        auto parser = Parse::ParserBase(str, origin, textrowcol);
        auto opt = parse_list_until_eof<ParsedQualifiedSpecifier>(
            "dependencies", parser, [](ParserBase& parser) { return parse_qualified_specifier(parser); });
        if (!opt) return {parser.get_error()->format(), expected_right_tag};

        return {std::move(opt).value_or_exit(VCPKG_LINE_INFO), expected_left_tag};
    }
    ExpectedS<std::vector<Dependency>> parse_dependencies_list(const std::string& str,
                                                               StringView origin,
                                                               TextRowCol textrowcol)
    {
        auto parser = Parse::ParserBase(str, origin, textrowcol);
        auto opt = parse_list_until_eof<Dependency>("dependencies", parser, [](ParserBase& parser) {
            auto loc = parser.cur_loc();
            return parse_qualified_specifier(parser).then([&](ParsedQualifiedSpecifier&& pqs) -> Optional<Dependency> {
                if (pqs.triplet)
                {
                    parser.add_error("triplet specifier not allowed in this context", loc);
                    return nullopt;
                }
                return Dependency{pqs.name, pqs.features.value_or({}), pqs.platform.value_or({})};
            });
        });
        if (!opt) return {parser.get_error()->format(), expected_right_tag};

        return {std::move(opt).value_or_exit(VCPKG_LINE_INFO), expected_left_tag};
    }
}

namespace vcpkg::Paragraphs
{
    struct PghParser : private Parse::ParserBase
    {
    private:
        void get_fieldvalue(std::string& fieldvalue)
        {
            fieldvalue.clear();

            do
            {
                // scan to end of current line (it is part of the field value)
                Strings::append(fieldvalue, match_until(is_lineend));
                skip_newline();

                if (cur() != ' ') return;
                auto spacing = skip_tabs_spaces();
                if (is_lineend(cur())) return add_error("unexpected end of line, to span a blank line use \"  .\"");
                Strings::append(fieldvalue, "\n", spacing);
            } while (true);
        }

        void get_fieldname(std::string& fieldname)
        {
            fieldname = match_zero_or_more(is_alphanumdash).to_string();
            if (fieldname.empty()) return add_error("expected fieldname");
        }

        void get_paragraph(Paragraph& fields)
        {
            fields.clear();
            std::string fieldname;
            std::string fieldvalue;
            do
            {
                if (cur() == '#')
                {
                    skip_line();
                    continue;
                }

                auto loc = cur_loc();
                get_fieldname(fieldname);
                if (cur() != ':') return add_error("expected ':' after field name");
                if (Util::Sets::contains(fields, fieldname)) return add_error("duplicate field", loc);
                next();
                skip_tabs_spaces();
                auto rowcol = cur_rowcol();
                get_fieldvalue(fieldvalue);

                fields.emplace(fieldname, std::make_pair(fieldvalue, rowcol));
            } while (!is_lineend(cur()));
        }

    public:
        PghParser(StringView text, StringView origin) : Parse::ParserBase(text, origin) { }

        ExpectedS<std::vector<Paragraph>> get_paragraphs()
        {
            std::vector<Paragraph> paragraphs;

            skip_whitespace();
            while (!at_eof())
            {
                paragraphs.emplace_back();
                get_paragraph(paragraphs.back());
                match_zero_or_more(is_lineend);
            }
            if (get_error()) return get_error()->format();

            return paragraphs;
        }
    };

    ExpectedS<Paragraph> parse_single_paragraph(const std::string& str, const std::string& origin)
    {
        auto pghs = PghParser(str, origin).get_paragraphs();

        if (auto p = pghs.get())
        {
            if (p->size() != 1) return {"There should be exactly one paragraph", expected_right_tag};
            return std::move(p->front());
        }
        else
        {
            return pghs.error();
        }
    }

    ExpectedS<Paragraph> get_single_paragraph(const Filesystem& fs, const path& control_path)
    {
        const Expected<std::string> contents = fs.read_contents(control_path);
        if (auto spgh = contents.get())
        {
            return parse_single_paragraph(*spgh, vcpkg::u8string(control_path));
        }

        return contents.error().message();
    }

    ExpectedS<std::vector<Paragraph>> get_paragraphs_text(const std::string& text, const std::string& origin)
    {
        return parse_paragraphs(text, origin);
    }

    ExpectedS<std::vector<Paragraph>> get_paragraphs(const Filesystem& fs, const path& control_path)
    {
        const Expected<std::string> contents = fs.read_contents(control_path);
        if (auto spgh = contents.get())
        {
            return parse_paragraphs(*spgh, vcpkg::u8string(control_path));
        }

        return contents.error().message();
    }

    ExpectedS<std::vector<Paragraph>> parse_paragraphs(const std::string& str, const std::string& origin)
    {
        return PghParser(str, origin).get_paragraphs();
    }

    bool is_port_directory(const Filesystem& fs, const path& maybe_directory)
    {
        return fs.exists(maybe_directory / vcpkg::u8path("CONTROL")) ||
               fs.exists(maybe_directory / vcpkg::u8path("vcpkg.json"));
    }

    static ParseExpected<SourceControlFile> try_load_manifest_object(
        const std::string& origin,
        const ExpectedT<std::pair<vcpkg::Json::Value, vcpkg::Json::JsonStyle>, std::unique_ptr<Parse::IParseError>>&
            res)
    {
        auto error_info = std::make_unique<ParseControlErrorInfo>();
        if (auto val = res.get())
        {
            if (val->first.is_object())
            {
                return SourceControlFile::parse_manifest_object(origin, val->first.object());
            }
            else
            {
                error_info->name = origin;
                error_info->error = "Manifest files must have a top-level object";
                return error_info;
            }
        }
        else
        {
            error_info->name = origin;
            error_info->error = res.error()->format();
            return error_info;
        }
    }

    static ParseExpected<SourceControlFile> try_load_manifest_text(const std::string& text, const std::string& origin)
    {
        auto res = Json::parse(text);
        return try_load_manifest_object(origin, res);
    }

    static ParseExpected<SourceControlFile> try_load_manifest(const Filesystem& fs,
                                                              const std::string& port_name,
                                                              const path& manifest_path,
                                                              std::error_code& ec)
    {
        (void)port_name;

        auto error_info = std::make_unique<ParseControlErrorInfo>();
        auto res = Json::parse_file(fs, manifest_path, ec);
        if (ec) return error_info;

        return try_load_manifest_object(vcpkg::u8string(manifest_path), res);
    }

    ParseExpected<SourceControlFile> try_load_port_text(const std::string& text,
                                                        const std::string& origin,
                                                        bool is_manifest)
    {
        if (is_manifest)
        {
            return try_load_manifest_text(text, origin);
        }

        ExpectedS<std::vector<Paragraph>> pghs = get_paragraphs_text(text, origin);
        if (auto vector_pghs = pghs.get())
        {
            return SourceControlFile::parse_control_file(origin, std::move(*vector_pghs));
        }
        auto error_info = std::make_unique<ParseControlErrorInfo>();
        error_info->name = vcpkg::u8string(origin);
        error_info->error = pghs.error();
        return error_info;
    }

    ParseExpected<SourceControlFile> try_load_port(const Filesystem& fs, const path& port_directory)
    {
        const auto manifest_path = port_directory / vcpkg::u8path("vcpkg.json");
        const auto control_path = port_directory / vcpkg::u8path("CONTROL");
        if (fs.exists(manifest_path))
        {
            vcpkg::Checks::check_exit(VCPKG_LINE_INFO,
                                      !fs.exists(control_path),
                                      "Found both manifest and CONTROL file in port %s; please rename one or the other",
                                      vcpkg::u8string(port_directory));

            std::error_code ec;
            auto res = try_load_manifest(fs, vcpkg::u8string(port_directory.filename()), manifest_path, ec);
            if (ec)
            {
                auto error_info = std::make_unique<ParseControlErrorInfo>();
                error_info->name = vcpkg::u8string(port_directory.filename());
                error_info->error = Strings::format(
                    "Failed to load manifest file for port: %s\n", vcpkg::u8string(manifest_path), ec.message());
                return error_info;
            }

            return res;
        }

        if (fs.exists(control_path))
        {
            ExpectedS<std::vector<Paragraph>> pghs = get_paragraphs(fs, control_path);
            if (auto vector_pghs = pghs.get())
            {
                return SourceControlFile::parse_control_file(vcpkg::u8string(control_path), std::move(*vector_pghs));
            }
            auto error_info = std::make_unique<ParseControlErrorInfo>();
            error_info->name = vcpkg::u8string(port_directory.filename());
            error_info->error = pghs.error();
            return error_info;
        }

        auto error_info = std::make_unique<ParseControlErrorInfo>();
        error_info->name = vcpkg::u8string(port_directory.filename());
        if (fs.exists(port_directory))
        {
            error_info->error = "Failed to find either a CONTROL file or vcpkg.json file.";
        }
        else
        {
            error_info->error = "The port directory (" + vcpkg::u8string(port_directory) + ") does not exist";
        }
        return error_info;
    }

    ExpectedS<BinaryControlFile> try_load_cached_package(const VcpkgPaths& paths, const PackageSpec& spec)
    {
        ExpectedS<std::vector<Paragraph>> pghs =
            get_paragraphs(paths.get_filesystem(), paths.package_dir(spec) / "CONTROL");

        if (auto p = pghs.get())
        {
            BinaryControlFile bcf;
            bcf.core_paragraph = BinaryParagraph(p->front());
            p->erase(p->begin());

            bcf.features =
                Util::fmap(*p, [&](auto&& raw_feature) -> BinaryParagraph { return BinaryParagraph(raw_feature); });

            if (bcf.core_paragraph.spec != spec)
            {
                return Strings::concat("Mismatched spec in package at ",
                                       vcpkg::u8string(paths.package_dir(spec)),
                                       ": expected ",
                                       spec,
                                       ", actual ",
                                       bcf.core_paragraph.spec);
            }

            return bcf;
        }

        return pghs.error();
    }

    LoadResults try_load_all_registry_ports(const VcpkgPaths& paths)
    {
        LoadResults ret;
        const auto& fs = paths.get_filesystem();

        std::vector<std::string> ports;

        const auto& registries = paths.get_configuration().registry_set;

        for (const auto& registry : registries.registries())
        {
            const auto packages = registry.packages();
            ports.insert(end(ports), begin(packages), end(packages));
        }
        if (auto registry = registries.default_registry())
        {
            registry->get_all_port_names(ports, paths);
        }

        Util::sort_unique_erase(ports);

        for (const auto& port_name : ports)
        {
            auto impl = registries.registry_for_port(port_name);
            if (!impl)
            {
                // this is a port for which no registry is set
                // this can happen when there's no default registry,
                // and a registry has a port definition which it doesn't own the name of.
                continue;
            }

            auto port_entry = impl->get_port_entry(paths, port_name);
            auto baseline_version = impl->get_baseline_version(paths, port_name);
            if (port_entry && baseline_version)
            {
                auto port_path =
                    port_entry->get_path_to_version(paths, *baseline_version.get()).value_or_exit(VCPKG_LINE_INFO);
                auto maybe_spgh = try_load_port(fs, port_path);
                if (const auto spgh = maybe_spgh.get())
                {
                    ret.paragraphs.emplace_back(std::move(*spgh), std::move(port_path));
                }
                else
                {
                    ret.errors.emplace_back(std::move(maybe_spgh).error());
                }
            }
            else
            {
                // the registry that owns the name of this port does not actually contain the port
                // this can happen if R1 contains the port definition for <abc>, but doesn't
                // declare it owns <abc>.
                continue;
            }
        }

        return ret;
    }

    static void load_results_print_error(const LoadResults& results)
    {
        if (!results.errors.empty())
        {
            if (Debug::g_debugging)
            {
                print_error_message(results.errors);
            }
            else
            {
                for (auto&& error : results.errors)
                {
                    print2(Color::warning, "Warning: an error occurred while parsing '", error->name, "'\n");
                }
                print2(Color::warning, "Use '--debug' to get more information about the parse failures.\n\n");
            }
        }
    }

    std::vector<SourceControlFileLocation> load_all_registry_ports(const VcpkgPaths& paths)
    {
        auto results = try_load_all_registry_ports(paths);
        load_results_print_error(results);
        return std::move(results.paragraphs);
    }

    std::vector<SourceControlFileLocation> load_overlay_ports(const Filesystem& fs, const path& directory)
    {
        LoadResults ret;

        auto port_dirs = fs.get_files_non_recursive(directory);
        Util::sort(port_dirs);

        Util::erase_remove_if(port_dirs,
                              [&](auto&& port_dir_entry) { return port_dir_entry.filename() == ".DS_Store"; });

        for (auto&& path : port_dirs)
        {
            auto maybe_spgh = try_load_port(fs, path);
            if (const auto spgh = maybe_spgh.get())
            {
                ret.paragraphs.emplace_back(std::move(*spgh), std::move(path));
            }
            else
            {
                ret.errors.emplace_back(std::move(maybe_spgh).error());
            }
        }

        load_results_print_error(ret);
        return std::move(ret.paragraphs);
    }
}
