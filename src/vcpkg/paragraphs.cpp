#include <vcpkg/base/fwd/message_sinks.h>

#include <vcpkg/base/chrono.h>
#include <vcpkg/base/contractual-constants.h>
#include <vcpkg/base/files.h>
#include <vcpkg/base/messages.h>
#include <vcpkg/base/parse.h>
#include <vcpkg/base/system.debug.h>
#include <vcpkg/base/util.h>

#include <vcpkg/binaryparagraph.h>
#include <vcpkg/paragraphparser.h>
#include <vcpkg/paragraphs.h>
#include <vcpkg/registries.h>

#include <tuple>

using namespace vcpkg;

static std::atomic<uint64_t> g_load_ports_stats(0);

namespace vcpkg
{
    Optional<FieldValue> ParagraphParser::optional_field(StringLiteral fieldname)
    {
        auto it = fields.find(fieldname.to_string());
        if (it == fields.end())
        {
            return nullopt;
        }

        auto value = std::move(it->second);
        fields.erase(it);
        return value;
    }

    std::string ParagraphParser::optional_field_or_empty(StringLiteral fieldname)
    {
        auto maybe_field = optional_field(fieldname);
        if (auto field = maybe_field.get())
        {
            return std::move(field->first);
        }

        return std::string();
    }

    std::string ParagraphParser::required_field(StringLiteral fieldname)
    {
        auto maybe_field = optional_field(fieldname);
        if (const auto field = maybe_field.get())
        {
            return std::move(field->first);
        }

        errors.emplace_back(LocalizedString::from_raw(origin)
                                .append_raw(": ")
                                .append_raw(ErrorPrefix)
                                .append(msgMissingRequiredField2, msg::json_field = fieldname));
        return std::string();
    }

    void ParagraphParser::add_error(TextRowCol position, msg::MessageT<> error_content)
    {
        errors.emplace_back(LocalizedString::from_raw(origin)
                                .append_raw(fmt::format("{}:{}: ", position.row, position.column))
                                .append_raw(ErrorPrefix)
                                .append(error_content));
    }
} // namespace vcpkg

namespace
{
    void append_errors(LocalizedString& result, const std::vector<LocalizedString>& errors)
    {
        auto error = errors.begin();
        const auto last = errors.end();
        for (;;)
        {
            result.append(*error);
            if (++error == last)
            {
                return;
            }

            result.append_raw('\n');
        }
    }

    void append_field_errors(LocalizedString& result, StringView origin, const Paragraph& fields)
    {
        auto extra_field_entry = fields.begin();
        const auto last = fields.end();
        for (;;)
        {
            result.append_raw(origin)
                .append_raw(fmt::format(
                    "{}:{}: ", extra_field_entry->second.second.row, extra_field_entry->second.second.column))
                .append_raw(ErrorPrefix)
                .append(msgUnexpectedField, msg::json_field = extra_field_entry->first);

            if (++extra_field_entry == last)
            {
                return;
            }

            result.append_raw('\n');
        }
    }
} // unnamed namespace

namespace vcpkg
{
    Optional<LocalizedString> ParagraphParser::error() const
    {
        LocalizedString result;
        if (errors.empty())
        {
            if (fields.empty())
            {
                return nullopt;
            }

            append_field_errors(result, origin, fields);
        }
        else
        {
            append_errors(result, errors);
            if (!fields.empty())
            {
                result.append_raw('\n');
                append_field_errors(result, origin, fields);
            }
        }

        return result;
    }

    template<class T, class Message, class F>
    static Optional<std::vector<T>> parse_list_until_eof(Message bad_comma_message, ParserBase& parser, F f)
    {
        Optional<std::vector<T>> ret;
        auto& vec = ret.emplace();
        parser.skip_whitespace();
        if (parser.at_eof()) return std::vector<T>{};
        for (;;)
        {
            auto item = f(parser);
            if (!item) break;
            vec.push_back(std::move(item).value_or_exit(VCPKG_LINE_INFO));
            parser.skip_whitespace();
            if (parser.at_eof()) return ret;
            if (parser.cur() != ',')
            {
                parser.add_error(msg::format(bad_comma_message));
                break;
            }
            parser.next();
            parser.skip_whitespace();
        }

        ret.clear();
        return ret;
    }

    Optional<std::vector<std::string>> parse_default_features_list(DiagnosticContext& context,
                                                                   const std::string& str,
                                                                   Optional<StringView> origin,
                                                                   int init_row)
    {
        auto parser = ParserBase(context, str, origin, init_row);
        return parse_list_until_eof<std::string>(msgExpectedDefaultFeaturesList, parser, &parse_feature_name);
    }
    ExpectedL<std::vector<std::string>> parse_default_features_list(const std::string& str,
                                                                    Optional<StringView> origin,
                                                                    int init_row)
    {
        return adapt_context_to_expected(
            static_cast<Optional<std::vector<std::string>> (*)(
                DiagnosticContext&, const std::string&, Optional<StringView>, int)>(parse_default_features_list),
            str,
            origin,
            init_row);
    }
    Optional<std::vector<ParsedQualifiedSpecifier>> parse_qualified_specifier_list(DiagnosticContext& context,
                                                                                   const std::string& str,
                                                                                   Optional<StringView> origin,
                                                                                   int init_row)
    {
        auto parser = ParserBase(context, str, origin, init_row);
        return parse_list_until_eof<ParsedQualifiedSpecifier>(
            msgExpectedDependenciesList, parser, [](ParserBase& parser) {
                return parse_qualified_specifier(
                    parser, AllowFeatures::Yes, ParseExplicitTriplet::Allow, AllowPlatformSpec::Yes);
            });
    }
    ExpectedL<std::vector<ParsedQualifiedSpecifier>> parse_qualified_specifier_list(const std::string& str,
                                                                                    Optional<StringView> origin,
                                                                                    int init_row)
    {
        return adapt_context_to_expected(
            static_cast<Optional<std::vector<ParsedQualifiedSpecifier>> (*)(
                DiagnosticContext&, const std::string&, Optional<StringView>, int)>(parse_qualified_specifier_list),
            str,
            origin,
            init_row);
    }
    Optional<std::vector<Dependency>> parse_dependencies_list(DiagnosticContext& context,
                                                              const std::string& str,
                                                              StringView origin,
                                                              int init_row)
    {
        auto parser = ParserBase(context, str, origin, init_row);
        return parse_list_until_eof<Dependency>(msgExpectedDependenciesList, parser, [](ParserBase& parser) {
            return parse_qualified_specifier(
                       parser, AllowFeatures::Yes, ParseExplicitTriplet::Forbid, AllowPlatformSpec::Yes)
                .then([&](ParsedQualifiedSpecifier&& pqs) -> Optional<Dependency> {
                    Dependency dependency{pqs.name, {}, pqs.platform.value_or({})};
                    for (const auto& feature : pqs.features.value_or({}))
                    {
                        if (feature == FeatureNameCore)
                        {
                            dependency.default_features = false;
                        }
                        else
                        {
                            dependency.features.push_back({feature});
                        }
                    }
                    return dependency;
                });
        });
    }
    ExpectedL<std::vector<Dependency>> parse_dependencies_list(const std::string& str, StringView origin, int init_row)
    {
        return adapt_context_to_expected(
            static_cast<Optional<std::vector<Dependency>> (*)(DiagnosticContext&, const std::string&, StringView, int)>(
                parse_dependencies_list),
            str,
            origin,
            init_row);
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
        PghParser(DiagnosticContext& context, StringView text, StringView origin, int init_row)
            : ParserBase(context, text, origin, init_row)
        {
        }

        Optional<std::vector<Paragraph>> get_paragraphs()
        {
            std::vector<Paragraph> paragraphs;

            skip_whitespace();
            while (!at_eof())
            {
                get_paragraph(paragraphs.emplace_back());
                match_while(is_lineend);
            }

            if (any_errors())
            {
                return nullopt;
            }

            return paragraphs;
        }
    };

    Optional<Paragraph> parse_single_merged_paragraph(DiagnosticContext& context,
                                                      StringView str,
                                                      StringView origin,
                                                      int init_row)
    {
        return PghParser(context, str, origin, init_row).get_paragraphs().map([](std::vector<Paragraph>&& paragraphs) {
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

    Optional<Paragraph> parse_single_paragraph(DiagnosticContext& context,
                                               StringView str,
                                               StringView origin,
                                               int init_row)
    {
        PghParser parser(context, str, origin, init_row);
        auto maybe_paragraphs = parser.get_paragraphs();
        if (auto paragraphs = maybe_paragraphs.get())
        {
            if (paragraphs->size() == 1)
            {
                return std::move(paragraphs->front());
            }

            context.report(DiagnosticLine{DiagKind::Error, origin, msg::format(msgParagraphExactlyOne)});
        }

        return nullopt;
    }

    Optional<Paragraph> get_single_paragraph(DiagnosticContext& context,
                                             const ReadOnlyFilesystem& fs,
                                             const Path& control_path)
    {
        std::error_code ec;
        std::string contents = fs.read_contents(control_path, ec);
        if (ec)
        {
            context.report(DiagnosticLine{DiagKind::Error, control_path, LocalizedString::from_raw(ec.message())});
            return nullopt;
        }

        return parse_single_paragraph(context, contents, control_path, 1);
    }

    ExpectedL<std::vector<Paragraph>> get_paragraphs(const ReadOnlyFilesystem& fs, const Path& control_path)
    {
        std::error_code ec;
        std::string contents = fs.read_contents(control_path, ec);
        if (ec)
        {
            return LocalizedString::from_raw(ec.message());
        }

        return parse_paragraphs(contents, control_path, 1);
    }

    Optional<std::vector<Paragraph>> parse_paragraphs(DiagnosticContext& context,
                                                      StringView str,
                                                      StringView origin,
                                                      int init_row)
    {
        return PghParser(context, str, origin, init_row).get_paragraphs();
    }

    ExpectedL<std::vector<Paragraph>> parse_paragraphs(StringView str, StringView origin, int init_row)
    {
        return adapt_context_to_expected(
            static_cast<Optional<std::vector<Paragraph>> (*)(DiagnosticContext&, StringView, StringView, int)>(
                parse_paragraphs),
            str,
            origin,
            init_row);
    }

    bool is_port_directory(const ReadOnlyFilesystem& fs, const Path& maybe_directory)
    {
        return fs.exists(maybe_directory / "CONTROL", IgnoreErrors{}) ||
               fs.exists(maybe_directory / "vcpkg.json", IgnoreErrors{});
    }

    ExpectedL<std::unique_ptr<SourceControlFile>> try_load_project_manifest_text(StringView text,
                                                                                 StringView control_path,
                                                                                 MessageSink& warning_sink)
    {
        StatsTimer timer(g_load_ports_stats);
        return Json::parse_object(text, control_path).then([&](Json::Object&& object) {
            return SourceControlFile::parse_project_manifest_object(control_path, std::move(object), warning_sink);
        });
    }

    ExpectedL<std::unique_ptr<SourceControlFile>> try_load_port_manifest_text(StringView text,
                                                                              StringView control_path,
                                                                              MessageSink& warning_sink)
    {
        StatsTimer timer(g_load_ports_stats);
        return Json::parse_object(text, control_path).then([&](Json::Object&& object) {
            return SourceControlFile::parse_port_manifest_object(control_path, std::move(object), warning_sink);
        });
    }

    ExpectedL<std::unique_ptr<SourceControlFile>> try_load_control_file_text(StringView text,
                                                                             StringView control_path,
                                                                             int init_row)
    {
        StatsTimer timer(g_load_ports_stats);
        return parse_paragraphs(text, control_path, init_row).then([&](std::vector<Paragraph>&& vector_pghs) {
            return SourceControlFile::parse_control_file(control_path, std::move(vector_pghs));
        });
    }

    PortLoadResult try_load_port(const ReadOnlyFilesystem& fs, StringView port_name, const PortLocation& port_location)
    {
        StatsTimer timer(g_load_ports_stats);

        auto manifest_path = port_location.port_directory / "vcpkg.json";
        auto control_path = port_location.port_directory / "CONTROL";
        std::error_code ec;
        auto manifest_contents = fs.read_contents(manifest_path, ec);
        if (!ec)
        {
            if (fs.exists(control_path, IgnoreErrors{}))
            {
                return PortLoadResult{LocalizedString::from_raw(port_location.port_directory)
                                          .append_raw(": ")
                                          .append_raw(ErrorPrefix)
                                          .append(msgManifestConflict2),
                                      std::string{}};
            }

            return PortLoadResult{try_load_port_manifest_text(manifest_contents, manifest_path, stdout_sink)
                                      .map([&](std::unique_ptr<SourceControlFile>&& scf) {
                                          return SourceControlFileAndLocation{
                                              std::move(scf), std::move(manifest_path), port_location.spdx_location};
                                      }),
                                  manifest_contents};
        }

        auto manifest_exists = ec != std::errc::no_such_file_or_directory;
        if (manifest_exists)
        {
            return PortLoadResult{LocalizedString::from_raw(port_location.port_directory)
                                      .append_raw(": ")
                                      .append(format_filesystem_call_error(ec, "read_contents", {manifest_path})),
                                  std::string{}};
        }

        auto control_contents = fs.read_contents(control_path, ec);
        if (!ec)
        {
            return PortLoadResult{try_load_control_file_text(control_contents, control_path, 1)
                                      .map([&](std::unique_ptr<SourceControlFile>&& scf) {
                                          return SourceControlFileAndLocation{
                                              std::move(scf), std::move(control_path), port_location.spdx_location};
                                      }),
                                  control_contents};
        }

        if (ec != std::errc::no_such_file_or_directory)
        {
            return PortLoadResult{LocalizedString::from_raw(port_location.port_directory)
                                      .append_raw(": ")
                                      .append(format_filesystem_call_error(ec, "read_contents", {control_path})),
                                  std::string{}};
        }

        if (fs.exists(port_location.port_directory, IgnoreErrors{}))
        {
            return PortLoadResult{LocalizedString::from_raw(port_location.port_directory)
                                      .append_raw(": ")
                                      .append_raw(ErrorPrefix)
                                      .append(msgPortMissingManifest2, msg::package_name = port_name),
                                  std::string{}};
        }

        return PortLoadResult{LocalizedString::from_raw(port_location.port_directory)
                                  .append_raw(": ")
                                  .append_raw(ErrorPrefix)
                                  .append(msgPortDoesNotExist, msg::package_name = port_name),
                              std::string{}};
    }

    PortLoadResult try_load_port_required(const ReadOnlyFilesystem& fs,
                                          StringView port_name,
                                          const PortLocation& port_location)
    {
        auto load_result = try_load_port(fs, port_name, port_location);
        auto maybe_res = load_result.maybe_scfl.get();
        if (maybe_res)
        {
            auto res = maybe_res->source_control_file.get();
            if (!res)
            {
                load_result.maybe_scfl = msg::format_error(msgPortDoesNotExist, msg::package_name = port_name);
            }
        }

        return load_result;
    }

    ExpectedL<BinaryControlFile> try_load_cached_package(const ReadOnlyFilesystem& fs,
                                                         const Path& package_dir,
                                                         const PackageSpec& spec)
    {
        StatsTimer timer(g_load_ports_stats);

        auto control_path = package_dir / "CONTROL";
        ExpectedL<std::vector<Paragraph>> maybe_paragraphs = get_paragraphs(fs, control_path);
        if (auto pparagraphs = maybe_paragraphs.get())
        {
            auto& paragraphs = *pparagraphs;
            BinaryControlFile bcf;
            bcf.core_paragraph = BinaryParagraph(control_path, std::move(paragraphs[0]));
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
                bcf.features.emplace_back(BinaryParagraph{control_path, std::move(paragraphs[idx])});
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
            auto maybe_result = try_load_port_required(fs, port_name, *port_location);
            if (const auto scfl = maybe_result.maybe_scfl.get())
            {
                ret.paragraphs.push_back(std::move(*scfl));
            }
            else
            {
                ret.errors.emplace_back(std::piecewise_construct,
                                        std::forward_as_tuple(port_name.data(), port_name.size()),
                                        std::forward_as_tuple(std::move(maybe_result.maybe_scfl).error()));
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

    LoadResults try_load_overlay_ports(const ReadOnlyFilesystem& fs, const Path& directory)
    {
        LoadResults ret;

        auto port_dirs = fs.get_directories_non_recursive(directory, VCPKG_LINE_INFO);
        Util::sort(port_dirs);

        Util::erase_remove_if(port_dirs,
                              [&](auto&& port_dir_entry) { return port_dir_entry.filename() == FileDotDsStore; });

        for (auto&& path : port_dirs)
        {
            auto port_name = path.filename();
            auto maybe_spgh = try_load_port_required(fs, port_name, PortLocation{path}).maybe_scfl;
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

    std::vector<SourceControlFileAndLocation> load_overlay_ports(const ReadOnlyFilesystem& fs, const Path& directory)
    {
        auto results = try_load_overlay_ports(fs, directory);
        load_results_print_error(results);
        return std::move(results.paragraphs);
    }

    uint64_t get_load_ports_stats() { return g_load_ports_stats.load(); }
}
