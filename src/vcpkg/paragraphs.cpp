#include <vcpkg/base/fwd/message_sinks.h>

#include <vcpkg/base/chrono.h>
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

#include <tuple>

static std::atomic<uint64_t> g_load_ports_stats(0);

namespace vcpkg
{
    void ParseControlErrorInfo::to_string(std::string& target) const
    {
        if (!has_error())
        {
            return;
        }

        bool newline_needed = false;
        if (!error.empty())
        {
            target.append(error.data());
            newline_needed = true;
        }

        for (auto&& msg : other_errors)
        {
            if (newline_needed)
            {
                target.push_back('\n');
            }

            target.append(msg.data());
            newline_needed = true;
        }

        if (!extra_fields.empty())
        {
            if (newline_needed)
            {
                target.push_back('\n');
            }

            target.append(msg::format_error(msgParseControlErrorInfoWhileLoading, msg::path = name)
                              .append_raw(' ')
                              .append(msgParseControlErrorInfoInvalidFields)
                              .append_raw(' ')
                              .append_raw(Strings::join(", ", extra_fields))
                              .extract_data());
            newline_needed = true;
        }

        if (!missing_fields.empty())
        {
            if (newline_needed)
            {
                target.push_back('\n');
            }

            target.append(msg::format_error(msgParseControlErrorInfoWhileLoading, msg::path = name)
                              .append_raw(' ')
                              .append(msgParseControlErrorInfoMissingFields)
                              .append_raw(' ')
                              .append_raw(Strings::join(", ", missing_fields))
                              .extract_data());
            newline_needed = true;
        }

        if (!expected_types.empty())
        {
            if (newline_needed)
            {
                target.push_back('\n');
            }

            target.append(msg::format_error(msgParseControlErrorInfoWhileLoading, msg::path = name)
                              .append_raw(' ')
                              .append(msgParseControlErrorInfoWrongTypeFields)
                              .extract_data());
            for (auto&& pr : expected_types)
            {
                target.append(
                    LocalizedString::from_raw("\n")
                        .append_indent()
                        .append(msgParseControlErrorInfoTypesEntry, msg::value = pr.first, msg::expected = pr.second)
                        .extract_data());
            }

            newline_needed = true;
        }
    }

    std::string ParseControlErrorInfo::to_string() const
    {
        std::string result;
        to_string(result);
        return result;
    }

    std::unique_ptr<ParseControlErrorInfo> ParseControlErrorInfo::from_error(StringView name, LocalizedString&& ls)
    {
        auto error_info = std::make_unique<ParseControlErrorInfo>();
        error_info->name.assign(name.data(), name.size());
        error_info->error = std::move(ls);
        return error_info;
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

    template<class T, class Message, class F>
    static Optional<std::vector<T>> parse_list_until_eof(Message bad_comma_message, ParserBase& parser, F f)
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
                parser.add_error(msg::format(bad_comma_message));
                return nullopt;
            }
            parser.next();
            parser.skip_whitespace();
        } while (true);
    }

    ExpectedL<std::vector<std::string>> parse_default_features_list(const std::string& str,
                                                                    StringView origin,
                                                                    TextRowCol textrowcol)
    {
        auto parser = ParserBase(str, origin, textrowcol);
        auto opt = parse_list_until_eof<std::string>(msgExpectedDefaultFeaturesList, parser, &parse_feature_name);
        if (!opt) return {LocalizedString::from_raw(parser.get_error()->to_string()), expected_right_tag};
        return {std::move(opt).value_or_exit(VCPKG_LINE_INFO), expected_left_tag};
    }
    ExpectedL<std::vector<ParsedQualifiedSpecifier>> parse_qualified_specifier_list(const std::string& str,
                                                                                    StringView origin,
                                                                                    TextRowCol textrowcol)
    {
        auto parser = ParserBase(str, origin, textrowcol);
        auto opt = parse_list_until_eof<ParsedQualifiedSpecifier>(
            msgExpectedDependenciesList, parser, [](ParserBase& parser) { return parse_qualified_specifier(parser); });
        if (!opt) return {LocalizedString::from_raw(parser.get_error()->to_string()), expected_right_tag};

        return {std::move(opt).value_or_exit(VCPKG_LINE_INFO), expected_left_tag};
    }
    ExpectedL<std::vector<Dependency>> parse_dependencies_list(const std::string& str,
                                                               StringView origin,
                                                               TextRowCol textrowcol)
    {
        auto parser = ParserBase(str, origin, textrowcol);
        auto opt = parse_list_until_eof<Dependency>(msgExpectedDependenciesList, parser, [](ParserBase& parser) {
            auto loc = parser.cur_loc();
            return parse_qualified_specifier(parser).then([&](ParsedQualifiedSpecifier&& pqs) -> Optional<Dependency> {
                if (const auto triplet = pqs.triplet.get())
                {
                    parser.add_error(msg::format(msgAddTripletExpressionNotAllowed,
                                                 msg::package_name = pqs.name,
                                                 msg::triplet = *triplet),
                                     loc);
                    return nullopt;
                }
                Dependency dependency{pqs.name, {}, pqs.platform.value_or({})};
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
        if (!opt) return {LocalizedString::from_raw(parser.get_error()->to_string()), expected_right_tag};

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
                if (is_lineend(cur())) return add_error(msg::format(msgParagraphUnexpectedEndOfLine));
                Strings::append(fieldvalue, "\n", spacing);
            } while (true);
        }

        void get_fieldname(std::string& fieldname)
        {
            fieldname = match_while(is_alphanumdash).to_string();
            if (fieldname.empty()) return add_error(msg::format(msgParagraphExpectedFieldName));
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
                if (cur() != ':') return add_error(msg::format(msgParagraphExpectedColonAfterField));
                if (Util::Sets::contains(fields, fieldname))
                    return add_error(msg::format(msgParagraphDuplicateField), loc);
                next();
                skip_tabs_spaces();
                auto rowcol = cur_rowcol();
                get_fieldvalue(fieldvalue);

                fields.emplace(fieldname, std::make_pair(fieldvalue, rowcol));
            } while (!is_lineend(cur()));
        }

    public:
        PghParser(StringView text, StringView origin) : ParserBase(text, origin) { }

        ExpectedL<std::vector<Paragraph>> get_paragraphs()
        {
            std::vector<Paragraph> paragraphs;

            skip_whitespace();
            while (!at_eof())
            {
                get_paragraph(paragraphs.emplace_back());
                match_while(is_lineend);
            }
            if (get_error()) return LocalizedString::from_raw(get_error()->to_string());

            return paragraphs;
        }
    };

    ExpectedL<Paragraph> parse_single_merged_paragraph(StringView str, StringView origin)
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

    ExpectedL<Paragraph> parse_single_paragraph(StringView str, StringView origin)
    {
        return PghParser(str, origin)
            .get_paragraphs()
            .then([](std::vector<Paragraph>&& paragraphs) -> ExpectedL<Paragraph> {
                if (paragraphs.size() == 1)
                {
                    return std::move(paragraphs.front());
                }
                else
                {
                    return msg::format(msgParagraphExactlyOne);
                }
            });
    }

    ExpectedL<Paragraph> get_single_paragraph(const ReadOnlyFilesystem& fs, const Path& control_path)
    {
        std::error_code ec;
        std::string contents = fs.read_contents(control_path, ec);
        if (ec)
        {
            return format_filesystem_call_error(ec, "read_contents", {control_path});
        }

        return parse_single_paragraph(contents, control_path);
    }

    ExpectedL<std::vector<Paragraph>> get_paragraphs(const ReadOnlyFilesystem& fs, const Path& control_path)
    {
        std::error_code ec;
        std::string contents = fs.read_contents(control_path, ec);
        if (ec)
        {
            return LocalizedString::from_raw(ec.message());
        }

        return parse_paragraphs(contents, control_path);
    }

    ExpectedL<std::vector<Paragraph>> parse_paragraphs(StringView str, StringView origin)
    {
        return PghParser(str, origin).get_paragraphs();
    }

    bool is_port_directory(const ReadOnlyFilesystem& fs, const Path& maybe_directory)
    {
        return fs.exists(maybe_directory / "CONTROL", IgnoreErrors{}) ||
               fs.exists(maybe_directory / "vcpkg.json", IgnoreErrors{});
    }

    ExpectedL<SourceControlFileAndLocation> try_load_port_manifest_text(StringView text,
                                                                        StringView origin,
                                                                        MessageSink& warning_sink)
    {
        StatsTimer timer(g_load_ports_stats);
        auto maybe_object = Json::parse_object(text, origin);
        if (auto object = maybe_object.get())
        {
            auto maybe_parsed = map_parse_expected_to_localized_string(
                SourceControlFile::parse_port_manifest_object(origin, *object, warning_sink));
            if (auto parsed = maybe_parsed.get())
            {
                return SourceControlFileAndLocation{std::move(*parsed), origin.to_string()};
            }

            return std::move(maybe_parsed).error();
        }

        return std::move(maybe_object).error();
    }

    ExpectedL<SourceControlFileAndLocation> try_load_control_file_text(StringView text, StringView origin)
    {
        StatsTimer timer(g_load_ports_stats);
        ExpectedL<std::vector<Paragraph>> pghs = parse_paragraphs(text, origin);
        if (auto vector_pghs = pghs.get())
        {
            auto maybe_parsed = map_parse_expected_to_localized_string(
                SourceControlFile::parse_control_file(origin, std::move(*vector_pghs)));
            if (auto parsed = maybe_parsed.get())
            {
                return SourceControlFileAndLocation{std::move(*parsed), origin.to_string()};
            }

            return std::move(maybe_parsed).error();
        }

        return std::move(pghs).error();
    }

    PortLoadResult try_load_port(const ReadOnlyFilesystem& fs, StringView port_name, const Path& port_directory)
    {
        StatsTimer timer(g_load_ports_stats);

        const auto manifest_path = port_directory / "vcpkg.json";
        const auto control_path = port_directory / "CONTROL";
        std::error_code ec;
        auto manifest_contents = fs.read_contents(manifest_path, ec);
        if (ec)
        {
            auto manifest_exists = ec != std::errc::no_such_file_or_directory;
            if (manifest_exists)
            {
                return PortLoadResult{LocalizedString::from_raw(port_directory)
                                          .append_raw(": ")
                                          .append(format_filesystem_call_error(ec, "read_contents", {manifest_path})),
                                      std::string{}};
            }

            auto control_contents = fs.read_contents(control_path, ec);
            if (ec)
            {
                if (ec != std::errc::no_such_file_or_directory)
                {
                    return PortLoadResult{
                        LocalizedString::from_raw(port_directory)
                            .append_raw(": ")
                            .append(format_filesystem_call_error(ec, "read_contents", {control_path})),
                        std::string{}};
                }

                if (fs.exists(port_directory, IgnoreErrors{}))
                {
                    return PortLoadResult{LocalizedString::from_raw(port_directory)
                                              .append_raw(": ")
                                              .append(msgErrorMessage)
                                              .append(msgPortMissingManifest2, msg::package_name = port_name),
                                          std::string{}};
                }

                return PortLoadResult{LocalizedString::from_raw(port_directory)
                                          .append_raw(": ")
                                          .append(msgErrorMessage)
                                          .append(msgPortDoesNotExist, msg::package_name = port_name),
                                      std::string{}};
            }

            return PortLoadResult{try_load_control_file_text(control_contents, control_path), control_contents};
        }

        if (fs.exists(control_path, IgnoreErrors{}))
        {
            return PortLoadResult{LocalizedString::from_raw(port_directory)
                                      .append_raw(": ")
                                      .append(msgErrorMessage)
                                      .append(msgManifestConflict2),
                                  std::string{}};
        }

        return PortLoadResult{try_load_port_manifest_text(manifest_contents, manifest_path, stdout_sink),
                              manifest_contents};
    }

    PortLoadResult try_load_port_required(const ReadOnlyFilesystem& fs,
                                          StringView port_name,
                                          const Path& port_directory)
    {
        auto maybe_maybe_res = try_load_port(fs, port_name, port_directory);
        auto maybe_res = maybe_maybe_res.maybe_scfl.get();
        if (maybe_res)
        {
            auto res = maybe_res->source_control_file.get();
            if (!res)
            {
                maybe_maybe_res.maybe_scfl = msg::format_error(msgPortDoesNotExist, msg::package_name = port_name);
            }
        }

        return maybe_maybe_res;
    }

    ExpectedL<BinaryControlFile> try_load_cached_package(const ReadOnlyFilesystem& fs,
                                                         const Path& package_dir,
                                                         const PackageSpec& spec)
    {
        StatsTimer timer(g_load_ports_stats);

        ExpectedL<std::vector<Paragraph>> maybe_paragraphs = get_paragraphs(fs, package_dir / "CONTROL");
        if (auto pparagraphs = maybe_paragraphs.get())
        {
            auto& paragraphs = *pparagraphs;
            BinaryControlFile bcf;
            bcf.core_paragraph = BinaryParagraph(std::move(paragraphs[0]));
            if (bcf.core_paragraph.spec != spec)
            {
                return msg::format(msgMismatchedSpec,
                                   msg::path = package_dir,
                                   msg::expected = spec,
                                   msg::actual = bcf.core_paragraph.spec);
            }

            bcf.features.reserve(paragraphs.size() - 1);
            for (std::size_t idx = 1; idx < paragraphs.size(); ++idx)
            {
                bcf.features.emplace_back(BinaryParagraph{std::move(paragraphs[idx])});
            }

            return bcf;
        }

        return maybe_paragraphs.error();
    }

    LoadResults try_load_all_registry_ports(const ReadOnlyFilesystem& fs, const RegistrySet& registries)
    {
        LoadResults ret;
        std::vector<std::string> ports = registries.get_all_reachable_port_names().value_or_exit(VCPKG_LINE_INFO);
        for (const auto& port_name : ports)
        {
            const auto impl = registries.registry_for_port(port_name);
            if (!impl)
            {
                // this is a port for which no registry is set
                // this can happen when there's no default registry,
                // and a registry has a port definition which it doesn't own the name of.
                continue;
            }

            auto maybe_baseline_version = impl->get_baseline_version(port_name).value_or_exit(VCPKG_LINE_INFO);
            auto baseline_version = maybe_baseline_version.get();
            if (!baseline_version) continue; // port is attributed to this registry, but it is not in the baseline
            auto maybe_port_entry = impl->get_port_entry(port_name);
            const auto port_entry = maybe_port_entry.get();
            if (!port_entry) continue;  // port is attributed to this registry, but loading it failed
            if (!*port_entry) continue; // port is attributed to this registry, but doesn't exist in this registry
            auto maybe_port_location = (*port_entry)->get_version(*baseline_version);
            const auto port_location = maybe_port_location.get();
            if (!port_location) continue; // baseline version was not in version db (registry consistency issue)
            auto maybe_spgh = try_load_port_required(fs, port_name, port_location->path).maybe_scfl;
            if (const auto spgh = maybe_spgh.get())
            {
                ret.paragraphs.push_back(std::move(*spgh));
            }
            else
            {
                ret.errors.emplace_back(std::piecewise_construct,
                                        std::forward_as_tuple(port_name.data(), port_name.size()),
                                        std::forward_as_tuple(std::move(maybe_spgh).error()));
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
                print_error_message(LocalizedString::from_raw(
                    Strings::join("\n",
                                  results.errors,
                                  [](const std::pair<std::string, LocalizedString>& err) -> const LocalizedString& {
                                      return err.second;
                                  })));
            }
            else
            {
                for (auto&& error : results.errors)
                {
                    msg::println_warning(msgErrorWhileParsing, msg::path = error.first);
                }

                msg::println_warning(msgGetParseFailureInfo);
            }
        }
    }

    std::vector<SourceControlFileAndLocation> load_all_registry_ports(const ReadOnlyFilesystem& fs,
                                                                      const RegistrySet& registries)
    {
        auto results = try_load_all_registry_ports(fs, registries);
        load_results_print_error(results);
        return std::move(results.paragraphs);
    }

    std::vector<SourceControlFileAndLocation> load_overlay_ports(const ReadOnlyFilesystem& fs, const Path& directory)
    {
        LoadResults ret;

        auto port_dirs = fs.get_directories_non_recursive(directory, VCPKG_LINE_INFO);
        Util::sort(port_dirs);

        Util::erase_remove_if(port_dirs,
                              [&](auto&& port_dir_entry) { return port_dir_entry.filename() == ".DS_Store"; });

        for (auto&& path : port_dirs)
        {
            auto port_name = path.filename();
            auto maybe_spgh = try_load_port_required(fs, port_name, path).maybe_scfl;
            if (const auto spgh = maybe_spgh.get())
            {
                ret.paragraphs.push_back(std::move(*spgh));
            }
            else
            {
                ret.errors.emplace_back(std::piecewise_construct,
                                        std::forward_as_tuple(port_name.data(), port_name.size()),
                                        std::forward_as_tuple(std::move(maybe_spgh).error()));
            }
        }

        load_results_print_error(ret);
        return std::move(ret.paragraphs);
    }

    uint64_t get_load_ports_stats() { return g_load_ports_stats.load(); }
}
