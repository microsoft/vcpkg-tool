#include <vcpkg/base/files.h>
#include <vcpkg/base/messages.h>
#include <vcpkg/base/parse.h>
#include <vcpkg/base/system.debug.h>
#include <vcpkg/base/util.h>

#include <vcpkg/binaryparagraph.h>
#include <vcpkg/configuration.h>
#include <vcpkg/paragraphparser.h>
#include <vcpkg/paragraphs.h>
#include <vcpkg/registries.h>

static std::atomic<uint64_t> g_load_ports_stats(0);

namespace vcpkg
{
    void ParseControlErrorInfo::to_string(std::string& target) const
    {
        if (!has_error())
        {
            return;
        }

        target.append(msg::format_error(msgParseControlErrorInfoWhileLoading, msg::path = name).extract_data());
        if (!error.empty())
        {
            target.push_back('\n');
            target.append(error);
        }

        if (!other_errors.empty())
        {
            for (auto&& msg : other_errors)
            {
                target.push_back('\n');
                target.append(msg.data());
            }
        }

        if (!extra_fields.empty())
        {
            target.push_back('\n');
            target.append(msg::format(msgParseControlErrorInfoInvalidFields)
                              .append_raw(' ')
                              .append_raw(Strings::join(", ", extra_fields))
                              .extract_data());
        }

        if (!missing_fields.empty())
        {
            target.push_back('\n');
            target.append(msg::format(msgParseControlErrorInfoMissingFields)
                              .append_raw(' ')
                              .append_raw(Strings::join(", ", missing_fields))
                              .extract_data());
        }

        if (!expected_types.empty())
        {
            auto expected_types_component = msg::format_error(msgParseControlErrorInfoWrongTypeFields);
            for (auto&& pr : expected_types)
            {
                expected_types_component.append_raw('\n').append_indent().append(
                    msgParseControlErrorInfoTypesEntry, msg::value = pr.first, msg::expected = pr.second);
            }

            target.push_back('\n');
            target.append(expected_types_component.extract_data());
        }
    }

    std::string ParseControlErrorInfo::to_string() const
    {
        std::string result;
        to_string(result);
        return result;
    }

    static Optional<std::pair<std::string, TextRowCol>> remove_field(Paragraph* fields, StringView fieldname)
    {
        auto it = fields->find(fieldname.to_string());
        if (it == fields->end())
        {
            return nullopt;
        }

        auto value = std::move(it->second);
        fields->erase(it);
        return value;
    }

    void ParagraphParser::required_field(StringLiteral fieldname, std::pair<std::string&, TextRowCol&> out)
    {
        auto maybe_field = remove_field(&fields, fieldname);
        if (const auto field = maybe_field.get())
            out = std::move(*field);
        else
            missing_fields.emplace_back(fieldname.data());
    }
    void ParagraphParser::optional_field(StringLiteral fieldname, std::pair<std::string&, TextRowCol&> out)
    {
        auto maybe_field = remove_field(&fields, fieldname);
        if (auto field = maybe_field.get()) out = std::move(*field);
    }
    void ParagraphParser::required_field(StringLiteral fieldname, std::string& out)
    {
        TextRowCol ignore;
        required_field(fieldname, {out, ignore});
    }
    std::string ParagraphParser::optional_field(StringLiteral fieldname)
    {
        std::string out;
        TextRowCol ignore;
        optional_field(fieldname, {out, ignore});
        return out;
    }
    std::string ParagraphParser::required_field(StringLiteral fieldname)
    {
        std::string out;
        TextRowCol ignore;
        required_field(fieldname, {out, ignore});
        return out;
    }

    std::unique_ptr<ParseControlErrorInfo> ParagraphParser::error_info(StringView name) const
    {
        if (!fields.empty() || !missing_fields.empty())
        {
            auto err = std::make_unique<ParseControlErrorInfo>();
            err->name = name.to_string();
            err->extra_fields = Util::extract_keys(fields);
            err->missing_fields = missing_fields;
            err->expected_types = expected_types;
            return err;
        }
        return nullptr;
    }

    template<class T, class F>
    static Optional<std::vector<T>> parse_list_until_eof(StringLiteral plural_item_name, ParserBase& parser, F f)
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
        auto parser = ParserBase(str, origin, textrowcol);
        auto opt = parse_list_until_eof<std::string>("default features", parser, &parse_feature_name);
        if (!opt) return {parser.get_error()->to_string(), expected_right_tag};
        return {std::move(opt).value_or_exit(VCPKG_LINE_INFO), expected_left_tag};
    }
    ExpectedS<std::vector<ParsedQualifiedSpecifier>> parse_qualified_specifier_list(const std::string& str,
                                                                                    StringView origin,
                                                                                    TextRowCol textrowcol)
    {
        auto parser = ParserBase(str, origin, textrowcol);
        auto opt = parse_list_until_eof<ParsedQualifiedSpecifier>(
            "dependencies", parser, [](ParserBase& parser) { return parse_qualified_specifier(parser); });
        if (!opt) return {parser.get_error()->to_string(), expected_right_tag};

        return {std::move(opt).value_or_exit(VCPKG_LINE_INFO), expected_left_tag};
    }
    ExpectedS<std::vector<Dependency>> parse_dependencies_list(const std::string& str,
                                                               StringView origin,
                                                               TextRowCol textrowcol,
                                                               ImplicitDefault implicit_defaults)
    {
        auto parser = ParserBase(str, origin, textrowcol);
        auto opt = parse_list_until_eof<Dependency>("dependencies", parser, [implicit_defaults](ParserBase& parser) {
            auto loc = parser.cur_loc();
            return parse_qualified_specifier(parser).then([&](ParsedQualifiedSpecifier&& pqs) -> Optional<Dependency> {
                if (pqs.triplet)
                {
                    parser.add_error("triplet specifier not allowed in this context", loc);
                    return nullopt;
                }
                Dependency dependency{pqs.name, {}, pqs.platform.value_or({})};
                dependency.default_features = (implicit_defaults == ImplicitDefault::YES);
                for (const auto& feature : pqs.features.value_or({}))
                {
                    if (feature == "core")
                    {
                        dependency.default_features = false;
                    }
                    else
                    {
                        dependency.features.emplace_back(feature);
                    }
                }
                return dependency;
            });
        });
        if (!opt) return {parser.get_error()->to_string(), expected_right_tag};

        return {std::move(opt).value_or_exit(VCPKG_LINE_INFO), expected_left_tag};
    }
}

namespace vcpkg::Paragraphs
{
    struct PghParser : private ParserBase
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
            fieldname = match_while(is_alphanumdash).to_string();
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
        PghParser(StringView text, StringView origin) : ParserBase(text, origin) { }

        ExpectedS<std::vector<Paragraph>> get_paragraphs()
        {
            std::vector<Paragraph> paragraphs;

            skip_whitespace();
            while (!at_eof())
            {
                paragraphs.emplace_back();
                get_paragraph(paragraphs.back());
                match_while(is_lineend);
            }
            if (get_error()) return get_error()->to_string();

            return paragraphs;
        }
    };

    ExpectedS<Paragraph> parse_single_merged_paragraph(StringView str, StringView origin)
    {
        return PghParser(str, origin).get_paragraphs().map([](std::vector<Paragraph>&& paragraphs) {
            if (paragraphs.empty())
            {
                return Paragraph{};
            }

            auto& front = paragraphs.front();
            for (size_t x = 1; x < paragraphs.size(); ++x)
            {
                for (auto&& extra_line : paragraphs[x])
                {
                    front.insert(extra_line);
                }
            }

            return std::move(front);
        });
    }

    ExpectedS<Paragraph> parse_single_paragraph(StringView str, StringView origin)
    {
        return PghParser(str, origin).get_paragraphs().then([](std::vector<Paragraph>&& paragraphs) {
            if (paragraphs.size() == 1)
            {
                return ExpectedS<Paragraph>{std::move(paragraphs.front())};
            }
            else
            {
                return ExpectedS<Paragraph>{"There should be exactly one paragraph"};
            }
        });
    }

    ExpectedS<Paragraph> get_single_paragraph(const Filesystem& fs, const Path& control_path)
    {
        std::error_code ec;
        std::string contents = fs.read_contents(control_path, ec);
        if (ec)
        {
            return ec.message();
        }

        return parse_single_paragraph(contents, control_path);
    }

    static ExpectedS<std::vector<Paragraph>> get_paragraphs_text(StringView text, StringView origin)
    {
        return parse_paragraphs(text, origin);
    }

    ExpectedS<std::vector<Paragraph>> get_paragraphs(const Filesystem& fs, const Path& control_path)
    {
        std::error_code ec;
        std::string contents = fs.read_contents(control_path, ec);
        if (ec)
        {
            return ec.message();
        }

        return parse_paragraphs(contents, control_path);
    }

    ExpectedS<std::vector<Paragraph>> parse_paragraphs(StringView str, StringView origin)
    {
        return PghParser(str, origin).get_paragraphs();
    }

    bool is_port_directory(const Filesystem& fs, const Path& maybe_directory)
    {
        return fs.exists(maybe_directory / "CONTROL", IgnoreErrors{}) ||
               fs.exists(maybe_directory / "vcpkg.json", IgnoreErrors{});
    }

    static ParseExpected<SourceControlFile> try_load_manifest_text(const std::string& text,
                                                                   StringView origin,
                                                                   MessageSink& warning_sink)
    {
        auto res = Json::parse(text, origin);

        std::string error;
        if (auto val = res.get())
        {
            if (val->first.is_object())
            {
                return SourceControlFile::parse_port_manifest_object(
                    origin, val->first.object(VCPKG_LINE_INFO), warning_sink);
            }

            error = "Manifest files must have a top-level object";
        }
        else
        {
            error = res.error()->to_string();
        }
        auto error_info = std::make_unique<ParseControlErrorInfo>();
        error_info->name = origin.to_string();
        error_info->error = std::move(error);
        return error_info;
    }

    ParseExpected<SourceControlFile> try_load_port_text(const std::string& text,
                                                        StringView origin,
                                                        bool is_manifest,
                                                        MessageSink& warning_sink)
    {
        StatsTimer timer(g_load_ports_stats);

        if (is_manifest)
        {
            return try_load_manifest_text(text, origin, warning_sink);
        }

        ExpectedS<std::vector<Paragraph>> pghs = get_paragraphs_text(text, origin);
        if (auto vector_pghs = pghs.get())
        {
            return SourceControlFile::parse_control_file(origin, std::move(*vector_pghs));
        }
        auto error_info = std::make_unique<ParseControlErrorInfo>();
        error_info->name = origin.to_string();
        error_info->error = pghs.error();
        return error_info;
    }

    ParseExpected<SourceControlFile> try_load_port(const Filesystem& fs, const Path& port_directory)
    {
        StatsTimer timer(g_load_ports_stats);

        const auto manifest_path = port_directory / "vcpkg.json";
        const auto control_path = port_directory / "CONTROL";
        const auto port_name = port_directory.filename().to_string();
        std::error_code ec;
        auto manifest_contents = fs.read_contents(manifest_path, ec);
        if (ec)
        {
            if (fs.exists(manifest_path, IgnoreErrors{}))
            {
                auto error_info = std::make_unique<ParseControlErrorInfo>();
                error_info->name = port_name;
                error_info->error =
                    Strings::format("Failed to load manifest file for port: %s\n", manifest_path, ec.message());
                return error_info;
            }
        }
        else
        {
            vcpkg::Checks::msg_check_exit(VCPKG_LINE_INFO,
                                          !fs.exists(control_path, IgnoreErrors{}),
                                          msgManifestConflict,
                                          msg::path = port_directory);

            return try_load_manifest_text(manifest_contents, manifest_path, stdout_sink);
        }

        if (fs.exists(control_path, IgnoreErrors{}))
        {
            ExpectedS<std::vector<Paragraph>> pghs = get_paragraphs(fs, control_path);
            if (auto vector_pghs = pghs.get())
            {
                return SourceControlFile::parse_control_file(control_path, std::move(*vector_pghs));
            }
            auto error_info = std::make_unique<ParseControlErrorInfo>();
            error_info->name = port_name;
            error_info->error = pghs.error();
            return error_info;
        }

        auto error_info = std::make_unique<ParseControlErrorInfo>();
        error_info->name = port_name;
        if (fs.exists(port_directory, IgnoreErrors{}))
        {
            error_info->error = "Failed to find either a CONTROL file or vcpkg.json file.";
        }
        else
        {
            error_info->error = Strings::concat("The port directory (", port_directory, ") does not exist");
        }

        return error_info;
    }

    ExpectedS<BinaryControlFile> try_load_cached_package(const Filesystem& fs,
                                                         const Path& package_dir,
                                                         const PackageSpec& spec)
    {
        StatsTimer timer(g_load_ports_stats);

        ExpectedS<std::vector<Paragraph>> pghs = get_paragraphs(fs, package_dir / "CONTROL");

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
                                       package_dir,
                                       ": expected ",
                                       spec,
                                       ", actual ",
                                       bcf.core_paragraph.spec);
            }

            return bcf;
        }

        return pghs.error();
    }

    LoadResults try_load_all_registry_ports(const Filesystem& fs, const RegistrySet& registries)
    {
        LoadResults ret;

        std::vector<std::string> ports;

        for (const auto& registry : registries.registries())
        {
            const auto packages = registry.packages();
            ports.insert(end(ports), begin(packages), end(packages));
        }
        if (auto registry = registries.default_registry())
        {
            registry->get_all_port_names(ports);
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

            const auto baseline_version = impl->get_baseline_version(port_name);
            if (!baseline_version) continue; // port is attributed to this registry, but it is not in the baseline
            const auto port_entry = impl->get_port_entry(port_name);
            if (!port_entry) continue; // port is attributed to this registry, but there is no version db
            auto port_location = port_entry->get_version(*baseline_version.get());
            if (!port_location) continue; // baseline version was not in version db (registry consistency issue)
            auto maybe_spgh = try_load_port(fs, port_location.get()->path);
            if (const auto spgh = maybe_spgh.get())
            {
                ret.paragraphs.push_back({
                    std::move(*spgh),
                    std::move(port_location.get()->path),
                    std::move(port_location.get()->location),
                });
            }
            else
            {
                ret.errors.emplace_back(std::move(maybe_spgh).error());
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
                    msg::println_warning(msgErrorWhileParsing, msg::path = error->name);
                }
                msg::println_warning(msgGetParseFailureInfo);
            }
        }
    }

    std::vector<SourceControlFileAndLocation> load_all_registry_ports(const Filesystem& fs,
                                                                      const RegistrySet& registries)
    {
        auto results = try_load_all_registry_ports(fs, registries);
        load_results_print_error(results);
        return std::move(results.paragraphs);
    }

    std::vector<SourceControlFileAndLocation> load_overlay_ports(const Filesystem& fs, const Path& directory)
    {
        LoadResults ret;

        auto port_dirs = fs.get_directories_non_recursive(directory, VCPKG_LINE_INFO);
        Util::sort(port_dirs);

        Util::erase_remove_if(port_dirs,
                              [&](auto&& port_dir_entry) { return port_dir_entry.filename() == ".DS_Store"; });

        for (auto&& path : port_dirs)
        {
            auto maybe_spgh = try_load_port(fs, path);
            if (const auto spgh = maybe_spgh.get())
            {
                ret.paragraphs.push_back({std::move(*spgh), std::move(path)});
            }
            else
            {
                ret.errors.emplace_back(std::move(maybe_spgh).error());
            }
        }

        load_results_print_error(ret);
        return std::move(ret.paragraphs);
    }

    uint64_t get_load_ports_stats() { return g_load_ports_stats.load(); }
}
