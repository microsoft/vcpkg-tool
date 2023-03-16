#if defined(_WIN32)
#include <vcpkg/base/cache.h>
#include <vcpkg/base/cofffilereader.h>
#include <vcpkg/base/files.h>
#include <vcpkg/base/hash.h>
#include <vcpkg/base/json.h>
#include <vcpkg/base/jsonreader.h>
#include <vcpkg/base/lazy.h>
#include <vcpkg/base/lineinfo.h>
#include <vcpkg/base/system.debug.h>
#include <vcpkg/base/util.h>

#include <vcpkg/commands.applocal.h>
#include <vcpkg/vcpkgcmdarguments.h>

#include <functional>
#include <map>
#include <set>
#include <string>

namespace
{
    using namespace vcpkg;

    WriteFilePointer maybe_create_log(const std::map<std::string, std::string, std::less<>>& settings,
                                      StringLiteral setting,
                                      Filesystem& fs)
    {
        const auto entry = settings.find(setting);
        if (entry == settings.end())
        {
            return WriteFilePointer();
        }

        return fs.open_for_write(entry->second, VCPKG_LINE_INFO);
    }

    struct MutantGuard
    {
        explicit MutantGuard(StringView name)
        {
            h = ::CreateMutexW(nullptr, FALSE, Strings::to_utf16(name).c_str());
            if (h)
            {
                WaitForSingleObject(h, INFINITE);
            }
            else
            {
                Checks::msg_exit_with_error(VCPKG_LINE_INFO, msgFailedToAcquireMutant, msg::path = name);
            }
        }

        ~MutantGuard()
        {
            if (h)
            {
                ReleaseMutex(h);
                CloseHandle(h);
            }
        }

    private:
        HANDLE h;
    };

    struct Deployment
    {
        enum
        {
            // source = "a/b/c", dest = "a/b/d"
            // a/b/c -> C:/a/b/d
            regular,
            // source = "a/b/x*y", dest = "a/b/d"
            // a/b/xAAy, a/b/xBBy -> C:/a/b/d/xAAy, C:/a/b/d/xBBy, ...
            file_filter,
            // source = "a/b", dest = "a/b/d"
            // a/b/AA, a/b/sub/BB -> C:/a/b/d/AA, C:/a/b/d/sub/BB, ...
            recursive,
        } source_kind;
        Path source;
        Path dest;
    };

    ExpectedL<Unit> parse_deployment_source(Path src, Deployment& out)
    {
        out.source = std::move(src);
        auto idx = out.source.parent_path().find('*');
        if (idx != SIZE_MAX)
        {
            return LocalizedString::from_raw("invalid filename pattern: parent path must not contain wildcards");
        }

        const auto filename = out.source.filename();
        if (filename == "**")
        {
            out.source_kind = Deployment::recursive;
            out.source.make_parent_path();
        }
        else
        {
            idx = filename.find('*');
            if (idx == SIZE_MAX)
            {
                out.source_kind = Deployment::regular;
            }
            else
            {
                out.source_kind = Deployment::file_filter;
                idx = filename.find('*', idx + 1);
                if (idx != SIZE_MAX)
                {
                    return LocalizedString::from_raw(
                        "invalid filename pattern: must contain at most one wildcard or be \"**\"");
                }
            }
        }
        return Unit{};
    }

    struct BuiltinDeployment
    {
        StringLiteral source;
        StringLiteral dest;
    };

    struct BuiltinDeploymentEntry
    {
        StringLiteral dll_name;
        View<BuiltinDeployment> deployments;
    };

    Optional<View<BuiltinDeployment>> get_magnum_deployments(StringView dll_name)
    {
        // clang-format off
#define MAGNUM_PATTERNS(subdir)                                                                                      \
    {"bin/" subdir "/*.conf", subdir},                                                                               \
    {"bin/" subdir "/*.dll", subdir},                                                                                \
    {"bin/" subdir "/*.pdb", subdir}
        // clang-format on

        static constexpr BuiltinDeployment s_rel_MagnumTrade[] = {
            MAGNUM_PATTERNS("magnum/importers"),
            MAGNUM_PATTERNS("magnum/imageconverters"),
            MAGNUM_PATTERNS("magnum/sceneconverters"),
        };
        static constexpr BuiltinDeployment s_dbg_MagnumTrade[] = {
            MAGNUM_PATTERNS("magnum-d/importers"),
            MAGNUM_PATTERNS("magnum-d/imageconverters"),
            MAGNUM_PATTERNS("magnum-d/sceneconverters"),
        };
        static constexpr BuiltinDeployment s_rel_MagnumAudio[] = {MAGNUM_PATTERNS("magnum/audioconverters")};
        static constexpr BuiltinDeployment s_dbg_MagnumAudio[] = {MAGNUM_PATTERNS("magnum-d/audioconverters")};
        static constexpr BuiltinDeployment s_rel_MagnumShaderTools[] = {MAGNUM_PATTERNS("magnum/shaderconverters")};
        static constexpr BuiltinDeployment s_dbg_MagnumShaderTools[] = {MAGNUM_PATTERNS("magnum-d/shaderconverters")};
        static constexpr BuiltinDeployment s_rel_MagnumText[] = {MAGNUM_PATTERNS("magnum/fonts"),
                                                                 MAGNUM_PATTERNS("magnum/fontconverters")};
        static constexpr BuiltinDeployment s_dbg_MagnumText[] = {MAGNUM_PATTERNS("magnum-d/fonts"),
                                                                 MAGNUM_PATTERNS("magnum-d/fontconverters")};
#undef MAGNUM_PATTERNS

        static constexpr BuiltinDeploymentEntry s_magnum_entries[] = {
            {"MagnumAudio.dll", s_rel_MagnumAudio},
            {"MagnumAudio-d.dll", s_dbg_MagnumAudio},
            {"MagnumText.dll", s_rel_MagnumText},
            {"MagnumText-d.dll", s_dbg_MagnumText},
            {"MagnumTrade.dll", s_rel_MagnumTrade},
            {"MagnumTrade-d.dll", s_dbg_MagnumTrade},
            {"MagnumShaderTools.dll", s_rel_MagnumShaderTools},
            {"MagnumShaderTools-d.dll", s_dbg_MagnumShaderTools},
        };

        if (Strings::case_insensitive_ascii_starts_with(dll_name, "magnum"))
            for (auto&& entry : s_magnum_entries)
                if (Strings::case_insensitive_ascii_equals(entry.dll_name, dll_name)) return entry.deployments;
        return nullopt;
    }

    Optional<View<BuiltinDeployment>> get_qt_deployments(StringView dll_name)
    {
#define QT_DEPLOY_PLUGINS(subdir) BuiltinDeployment({"plugins/" #subdir "/*.dll", "plugins/" #subdir})

        static constexpr BuiltinDeployment s_rel_Qt5Gui[] = {
            {"plugins/platforms/qwindows.dll", "plugins/platforms/qwindows.dll"},
            QT_DEPLOY_PLUGINS(accessible),
            QT_DEPLOY_PLUGINS(imageformats),
            QT_DEPLOY_PLUGINS(iconengines),
            QT_DEPLOY_PLUGINS(platforminputcontexts),
            QT_DEPLOY_PLUGINS(styles),
        };
        static constexpr BuiltinDeployment s_dbg_Qt5Gui[] = {
            {"plugins/platforms/qwindowsd.dll", "plugins/platforms/qwindowsd.dll"},
            QT_DEPLOY_PLUGINS(accessible),
            QT_DEPLOY_PLUGINS(imageformats),
            QT_DEPLOY_PLUGINS(iconengines),
            QT_DEPLOY_PLUGINS(platforminputcontexts),
            QT_DEPLOY_PLUGINS(styles),
        };
        static constexpr BuiltinDeployment s_rel_Qt5Qml[] = {
            {"bin/Qt5Quick.dll", "Qt5Quick.dll"},
            {"bin/Qt5QmlModels.dll", "Qt5QmlModels.dll"},
        };
        static constexpr BuiltinDeployment s_dbg_Qt5Qml[] = {
            {"bin/Qt5Quickd.dll", "Qt5Quickd.dll"},
            {"bin/Qt5QmlModelsd.dll", "Qt5QmlModelsd.dll"},
        };
        static constexpr BuiltinDeployment s_rel_Qt5Quick[] = {
            {"qml/*", "qml"},
            {"bin/Qt5QuickControls2.dll", "Qt5QuickControls2.dll"},
            {"bin/Qt5QuickShapes.dll", "Qt5QuickShapes.dll"},
            {"bin/Qt5QuickTemplates2.dll", "Qt5QuickTemplates2.dll"},
            {"bin/Qt5QmlWorkerScript.dll", "Qt5QmlWorkerScript.dll"},
            {"bin/Qt5QuickParticles.dll", "Qt5QuickParticles.dll"},
            {"bin/Qt5QuickWidgets.dll", "Qt5QuickWidgets.dll"},
            QT_DEPLOY_PLUGINS(scenegraph),
            QT_DEPLOY_PLUGINS(qmltooling),
        };
        static constexpr BuiltinDeployment s_dbg_Qt5Quick[] = {
            {"../qml/*", "qml"},
            {"bin/Qt5QuickControls2d.dll", "Qt5QuickControls2d.dll"},
            {"bin/Qt5QuickShapesd.dll", "Qt5QuickShapesd.dll"},
            {"bin/Qt5QuickTemplates2d.dll", "Qt5QuickTemplates2d.dll"},
            {"bin/Qt5QmlWorkerScriptd.dll", "Qt5QmlWorkerScriptd.dll"},
            {"bin/Qt5QuickParticlesd.dll", "Qt5QuickParticlesd.dll"},
            {"bin/Qt5QuickWidgetsd.dll", "Qt5QuickWidgetsd.dll"},
            QT_DEPLOY_PLUGINS(scenegraph),
            QT_DEPLOY_PLUGINS(qmltooling),
        };
        static constexpr BuiltinDeployment s_Qt5Declarative[] = {QT_DEPLOY_PLUGINS(qml1tooling)};
        static constexpr BuiltinDeployment s_Qt5Positioning[] = {QT_DEPLOY_PLUGINS(position)};
        static constexpr BuiltinDeployment s_Qt5Location[] = {QT_DEPLOY_PLUGINS(geoservices)};
        static constexpr BuiltinDeployment s_Qt5Sensors[] = {QT_DEPLOY_PLUGINS(sensors),
                                                             QT_DEPLOY_PLUGINS(sensorgestures)};
        static constexpr BuiltinDeployment s_Qt5WebEngineCore[] = {QT_DEPLOY_PLUGINS(qtwebengine)};
        static constexpr BuiltinDeployment s_Qt53DRenderer[] = {QT_DEPLOY_PLUGINS(sceneparsers)};
        static constexpr BuiltinDeployment s_Qt5TextToSpeech[] = {QT_DEPLOY_PLUGINS(texttospeech)};
        static constexpr BuiltinDeployment s_Qt5SerialBus[] = {QT_DEPLOY_PLUGINS(canbus)};
        static constexpr BuiltinDeployment s_Qt5Network[] = {
            QT_DEPLOY_PLUGINS(bearer),
            {"bin/libcrypto-*.dll", "./"},
            {"bin/libssl-*.dll", "./"},
        };
        static constexpr BuiltinDeployment s_Qt5Sql[] = {QT_DEPLOY_PLUGINS(sqldrivers)};
        static constexpr BuiltinDeployment s_Qt5Multimedia[] = {
            QT_DEPLOY_PLUGINS(audio),
            QT_DEPLOY_PLUGINS(mediaservice),
            QT_DEPLOY_PLUGINS(playlistformats),
        };
        static constexpr BuiltinDeployment s_Qt5PrintSupport[] = {
            {"plugins/printsupport/windowsprintersupport.dll", "windowsprintersupport.dll"},
        };
#undef QT_DEPLOY_PLUGINS

#define QT_ENTRY_BOTH(root)                                                                                            \
    BuiltinDeploymentEntry({#root ".dll", s_##root}), BuiltinDeploymentEntry({#root "d.dll", s_##root})
#define QT_ENTRY_SPLIT(root)                                                                                           \
    BuiltinDeploymentEntry({#root ".dll", s_rel_##root}), BuiltinDeploymentEntry({#root "d.dll", s_dbg_##root})
        static constexpr BuiltinDeploymentEntry s_qt_entries[] = {
            QT_ENTRY_SPLIT(Qt5Gui),
            QT_ENTRY_SPLIT(Qt5Qml),
            QT_ENTRY_SPLIT(Qt5Quick),
            QT_ENTRY_BOTH(Qt5Declarative),
            QT_ENTRY_BOTH(Qt5Positioning),
            QT_ENTRY_BOTH(Qt5Location),
            QT_ENTRY_BOTH(Qt5Sensors),
            QT_ENTRY_BOTH(Qt5WebEngineCore),
            QT_ENTRY_BOTH(Qt53DRenderer),
            QT_ENTRY_BOTH(Qt5TextToSpeech),
            QT_ENTRY_BOTH(Qt5SerialBus),
            QT_ENTRY_BOTH(Qt5Network),
            QT_ENTRY_BOTH(Qt5Sql),
            QT_ENTRY_BOTH(Qt5Multimedia),
            QT_ENTRY_BOTH(Qt5PrintSupport),
        };
#undef QT_ENTRY_BOTH
#undef QT_ENTRY_SPLIT

        if (Strings::case_insensitive_ascii_starts_with(dll_name, "qt5"))
        {
            if (Strings::case_insensitive_ascii_equals(dll_name, "Qt5Core.dll") ||
                Strings::case_insensitive_ascii_equals(dll_name, "Qt5Cored.dll"))
                return View<BuiltinDeployment>{};

            for (auto&& entry : s_qt_entries)
                if (Strings::case_insensitive_ascii_equals(entry.dll_name, dll_name)) return entry.deployments;
        }
        return nullopt;
    }

    Optional<View<BuiltinDeployment>> get_openni2_deployments(StringView dll_name)
    {
        static constexpr BuiltinDeployment s_openni2_deploy[] = {
            {"bin/OpenNI2/OpenNI.ini", "OpenNI.ini"},
            {"bin/OpenNI2/Drivers/Kinect.dll", "OpenNI2/Drivers/Kinect.dll"},
            {"bin/OpenNI2/Drivers/OniFile.dll", "OpenNI2/Drivers/OniFile.dll"},
            {"bin/OpenNI2/Drivers/PS1080.dll", "OpenNI2/Drivers/PS1080.dll"},
            {"bin/OpenNI2/Drivers/PS1080.ini", "OpenNI2/Drivers/PS1080.ini"},
            {"bin/OpenNI2/Drivers/PSLink.dll", "OpenNI2/Drivers/PSLink.dll"},
            {"bin/OpenNI2/Drivers/PSLink.ini", "OpenNI2/Drivers/PSLink.ini"},
        };
        if (Strings::case_insensitive_ascii_equals(dll_name, "OpenNI2.dll")) return s_openni2_deploy;
        return nullopt;
    }

    Optional<View<BuiltinDeployment>> get_k4a_deployments(StringView dll_name)
    {
        static constexpr BuiltinDeployment s_k4a_deploy[] = {
            {"tools/azure-kinect-sensor-sdk/depthengine_2_0.dll", "depthengine_2_0.dll"}};
        if (Strings::case_insensitive_ascii_equals(dll_name, "k4a.dll")) return s_k4a_deploy;
        return nullopt;
    }

    ExpectedL<std::vector<std::string>> get_imported_names(const Filesystem& fs, const Path& binary)
    {
        std::error_code ec;
        auto dll_file = fs.open_for_read(binary, ec);
        if (ec) return format_filesystem_call_error(ec, "open_for_read", {binary});
        return vcpkg::try_read_dll_metadata(dll_file).then([&dll_file](const DllMetadata& dll_metadata) {
            return vcpkg::try_read_dll_imported_dll_names(dll_metadata, dll_file);
        });
    }

    struct FileDeployer
    {
        Filesystem& m_fs;
        WriteFilePointer m_tlog_file;
        WriteFilePointer m_copied_files_log;

        void deploy_file(const Path& source, const Path& target) const
        {
            MutantGuard mutant("vcpkg-applocal-" + Hash::get_string_sha256(target));
            std::error_code ec;
            const bool did_deploy = m_fs.copy_file(source, target, CopyOptions::update_existing, ec);
            if (did_deploy)
            {
                msg::println(msgInstallCopiedFile, msg::path_source = source, msg::path_destination = target);
            }
            else if (!ec)
            {
                msg::println(msgInstallSkippedUpToDateFile, msg::path_source = source, msg::path_destination = target);
            }
            else if (ec == std::errc::no_such_file_or_directory)
            {
                Debug::println("Attempted to deploy ", source, ", but it didn't exist");
                return;
            }
            else
            {
                Checks::msg_exit_with_message(
                    VCPKG_LINE_INFO,
                    format_filesystem_call_error(ec, "copy_file", {source, target, "CopyOptions::update_existing"}));
            }

            if (m_tlog_file)
            {
                const auto as_utf16 = Strings::to_utf16(source);
                Checks::check_exit(VCPKG_LINE_INFO,
                                   m_tlog_file.write(as_utf16.data(), sizeof(wchar_t), as_utf16.size()) ==
                                       as_utf16.size());
                static constexpr wchar_t native_newline = L'\n';
                Checks::check_exit(VCPKG_LINE_INFO, m_tlog_file.write(&native_newline, sizeof(wchar_t), 1) == 1);
            }

            if (m_copied_files_log)
            {
                const auto& native = source.native();
                Checks::check_exit(VCPKG_LINE_INFO,
                                   m_copied_files_log.write(native.c_str(), 1, native.size()) == native.size());
                Checks::check_exit(VCPKG_LINE_INFO, m_copied_files_log.put('\n') == '\n');
            }
        }

        // Patterns supported:
        // 1. "C:/a/b/c", "C:/a/b/d" -- copies file "C:/a/b/c" to "C:/a/b/d" (note: renames file to "d")
        // 2. "C:/a/b/x*y", "C:/a/b/d" -- copies all regular files in "C:/a/b" with optional prefix "x" and optional
        //    suffix "y" into directory "C:/a/b/d"
        // 3. "C:/a/b/c/**", "C:/a/b/d" -- copies all files recursively in "C:/a/b/c" into directory "C:/a/b/d"
        void deploy_pattern(const Path& source, const Path& target, std::vector<Path>& out_dlls) const
        {
            const auto filename = source.filename();
            const NotExtensionCaseInsensitive not_dll{".dll"};
            const auto wildcard = Util::find(filename, '*');
            if (wildcard != filename.end())
            {
                if (filename == "**")
                {
                    // (3) glob all files recursively
                    const auto parent = source.parent_path();
                    std::vector<Path> files =
                        m_fs.get_regular_files_recursive_lexically_proximate(parent, IgnoreErrors{});
                    if (!files.empty())
                    {
                        Path source_file, target_file;
                        m_fs.create_directories(target, IgnoreErrors{});
                        for (auto&& file : files)
                        {
                            source_file = parent;
                            source_file /= file;
                            target_file = target;
                            target_file /= file;
                            deploy_file(source_file, target_file);
                            if (not_dll(file)) continue;
                            out_dlls.push_back(target_file);
                        }
                    }
                }
                else
                {
                    // (2) prefix+suffix pattern
                    const StringView prefix{filename.begin(), wildcard}, suffix{wildcard + 1, filename.end()};
                    if (Strings::contains(suffix, '*'))
                    {
                        // only support one wildcard
                        msg::println_error(LocalizedString::from_raw("* must only appear once in a pattern"));
                    }
                    else
                    {
                        const auto parent = source.parent_path();
                        std::vector<Path> files = m_fs.get_regular_files_non_recursive(parent, IgnoreErrors{});
                        if (!prefix.empty() || !suffix.empty())
                        {
                            Util::erase_remove_if(files, [prefix, suffix](const Path& p) {
                                const auto filename = p.filename();
                                return !(prefix.empty() ||
                                         Strings::case_insensitive_ascii_starts_with(filename, prefix)) ||
                                       !(suffix.empty() || Strings::case_insensitive_ascii_ends_with(filename, suffix));
                            });
                        }
                        if (!files.empty())
                        {
                            Path file_to_deploy;
                            m_fs.create_directories(target, IgnoreErrors{});
                            // deploy files in directory
                            for (auto&& file : files)
                            {
                                file_to_deploy = target;
                                file_to_deploy /= file.filename();
                                deploy_file(file, file_to_deploy);
                                if (not_dll(file)) continue;
                                out_dlls.push_back(file_to_deploy);
                            }
                        }
                    }
                }
            }
            else
            {
                // (1) simple file copy
                deploy_file(source, target);
                if (!not_dll(target)) out_dlls.push_back(target);
            }
        }
    };

    struct Deployments
    {
        Deployments() : deps(), create_qt_conf(false) { }
        Deployments(std::vector<Deployment> deps, bool create_qt_conf)
            : deps(std::move(deps)), create_qt_conf(create_qt_conf)
        {
        }
        std::vector<Deployment> deps;
        // Qt has a unique one-off behavior of creating a qt.conf file.
        bool create_qt_conf;
    };

    Deployments instantiate_deployments(View<BuiltinDeployment> v)
    {
        return {Util::fmap(v,
                           [](const BuiltinDeployment& d) {
                               Deployment r;
                               // based on static data -- should be unreachable
                               parse_deployment_source(d.source.to_string(), r).value_or_exit(VCPKG_LINE_INFO);
                               r.dest = d.dest.to_string();
                               return r;
                           }),
                false};
    }

    struct DeploymentPatternSetDeserializer : Json::IDeserializer<std::vector<Deployment>>
    {
        virtual LocalizedString type_name() const override
        {
            return LocalizedString::from_raw("a deployment pattern set");
        }

        virtual Optional<std::vector<Deployment>> visit_object(Json::Reader& r, const Json::Object& obj) const override
        {
            std::vector<Deployment> ret;
            for (auto&& p : obj)
            {
                ret.emplace_back();
                parse_deployment_source(p.first.to_string(), ret.back()).consume_error([&r](LocalizedString&& err) {
                    r.add_generic_error(DeploymentPatternSetDeserializer::instance.type_name(), std::move(err));
                });
                r.visit_in_key(p.second, p.first, ret.back().dest, Json::PathDeserializer::instance);
            }
            return std::move(ret);
        }
        static DeploymentPatternSetDeserializer instance;
    };
    DeploymentPatternSetDeserializer DeploymentPatternSetDeserializer::instance;
    struct PluginFileDeserializer : Json::IDeserializer<Deployments>
    {
        virtual LocalizedString type_name() const override { return LocalizedString::from_raw("a plugin file"); }
        static constexpr StringLiteral FIELD_CREATE_QT_CONF = "create_qt_conf";
        static constexpr StringLiteral FIELD_PATTERNS = "patterns";
        virtual View<StringView> valid_fields() const override
        {
            static constexpr StringView fields[] = {FIELD_CREATE_QT_CONF, FIELD_PATTERNS};
            return fields;
        }
        virtual Optional<Deployments> visit_object(Json::Reader& r, const Json::Object& obj) const override
        {
            Deployments ret;
            r.optional_object_field(obj, FIELD_CREATE_QT_CONF, ret.create_qt_conf, Json::BooleanDeserializer::instance);
            r.optional_object_field(obj, FIELD_PATTERNS, ret.deps, DeploymentPatternSetDeserializer::instance);
            return std::move(ret);
        }
        static PluginFileDeserializer instance;
    };
    PluginFileDeserializer PluginFileDeserializer::instance;

    ExpectedL<Deployments> parse_deployments(const std::string& info, StringView origin)
    {
        LocalizedString err;
        auto parsed = Json::parse(info, origin);
        if (auto p = parsed.get())
        {
            Deployments ret;
            Json::Reader r;
            r.visit_here(p->value, ret, PluginFileDeserializer::instance);
            for (auto&& s : r.warnings())
                err.append(s).append_raw('\n');
            for (auto&& s : r.errors())
                err.append(s).append_raw('\n');

            if (err.empty()) return std::move(ret);
        }
        else
        {
            err = LocalizedString::from_raw(parsed.error()->to_string());
        }
        return err;
    }

    struct DeploymentProvider
    {
        const Filesystem& m_fs;
        const Path& m_installed_dir;
        // Avoid checking for ps1 existence multiple times -- these apply to multiple DLLs
        Lazy<bool> m_magnum_ps1;
        Lazy<bool> m_qt_ps1;

        Deployments get_deployments(const Path& src_path) const
        {
            const auto filename = src_path.filename();
            Path json_path = src_path + ".plugin.json";
            std::error_code ec;
            auto plugin_info = m_fs.read_contents(json_path, ec);
            if (!ec)
            {
                auto parsed = parse_deployments(plugin_info, json_path);
                if (auto p = parsed.get())
                {
                    return std::move(*p);
                }
                else
                {
                    msg::print(parsed.error());
                    return {};
                }
            }
            else if (ec != std::errc::no_such_file_or_directory)
            {
                msg::println_error(format_filesystem_call_error(ec, "read_contents", {json_path}));
                return {};
            }

            // Check for backcompat definitions
            if (auto a = get_qt_deployments(filename))
            {
                if (m_qt_ps1.get_lazy(
                        [this]() { return m_fs.exists(m_installed_dir / "plugins/qtdeploy.ps1", IgnoreErrors{}); }))
                {
                    Deployments ret = instantiate_deployments(*a.get());
                    ret.create_qt_conf = Strings::case_insensitive_ascii_equals(filename, "Qt5Core.dll") ||
                                         Strings::case_insensitive_ascii_equals(filename, "Qt5Cored.dll");
                    return std::move(ret);
                }
            }
            else if (auto b = get_magnum_deployments(filename))
            {
                if (m_magnum_ps1.get_lazy([this]() {
                        return m_fs.exists(m_installed_dir / "bin/magnum/magnumdeploy.ps1", IgnoreErrors{}) ||
                               m_fs.exists(m_installed_dir / "bin/magnum-d/magnumdeploy.ps1", IgnoreErrors{});
                    }))
                    return instantiate_deployments(*b.get());
            }
            else if (auto c = get_openni2_deployments(filename))
            {
                if (m_fs.exists(m_installed_dir / "bin/OpenNI2/openni2deploy.ps1", IgnoreErrors{}))
                    return instantiate_deployments(*c.get());
            }
            else if (auto d = get_k4a_deployments(filename))
            {
                if (m_fs.exists(m_installed_dir / "tools/azure-kinect-sensor-sdk/k4adeploy.ps1", IgnoreErrors{}))
                    return instantiate_deployments(*d.get());
            }
            return {};
        }
    };

    void copy_deps(Filesystem& fs,
                   const Path& app_dir,
                   const Path& installed_dir,
                   View<Path> roots,
                   WriteFilePointer&& tlog_file,
                   WriteFilePointer&& copied_files_log)
    {
        const FileDeployer deployer{fs, std::move(tlog_file), std::move(copied_files_log)};
        const DeploymentProvider builtins{fs, installed_dir};

        const Path bin_dir = installed_dir / "bin";
        Path src_path, dest_path;
        std::unordered_set<std::string> examined;
        std::vector<Path> to_examine{roots.begin(), roots.end()};

        while (!to_examine.empty())
        {
            Path p = std::move(to_examine.back());
            to_examine.pop_back();

            auto maybe_names = get_imported_names(fs, p);
            if (!maybe_names)
            {
                msg::println(maybe_names.error());
                continue;
            }
            for (auto&& name : *maybe_names.get())
            {
                // skip names that have been examined
                if (!examined.insert(name).second) continue;

                dest_path = app_dir;
                dest_path /= name;
                src_path = bin_dir;
                src_path /= name;
                if (fs.exists(src_path, VCPKG_LINE_INFO))
                {
                    deployer.deploy_file(src_path, dest_path);
                    auto deployments = builtins.get_deployments(src_path);
                    if (deployments.create_qt_conf)
                    {
                        Path conf_file = app_dir / "qt.conf";
                        if (!fs.exists(conf_file, IgnoreErrors{}))
                            fs.write_contents(conf_file, "[Paths]\n", IgnoreErrors{});
                    }
                    for (auto&& d : deployments.deps)
                    {
                        deployer.deploy_pattern(installed_dir / d.source, app_dir / d.dest, to_examine);
                    }
                }
                if (fs.exists(dest_path, VCPKG_LINE_INFO)) to_examine.push_back(dest_path);
            }
        }
    }
}

namespace vcpkg::Commands
{
    void AppLocalCommand::perform_and_exit(const VcpkgCmdArguments& args, Filesystem&) const
    {
        static constexpr StringLiteral OPTION_TARGET_BINARY = "target-binary";
        static constexpr StringLiteral OPTION_INSTALLED_DIR = "installed-bin-dir";
        static constexpr StringLiteral OPTION_TLOG_FILE = "tlog-file";
        static constexpr StringLiteral OPTION_COPIED_FILES_LOG = "copied-files-log";

        static constexpr CommandSetting SETTINGS[] = {
            {OPTION_TARGET_BINARY, []() { return msg::format(msgCmdSettingTargetBin); }},
            {OPTION_INSTALLED_DIR, []() { return msg::format(msgCmdSettingInstalledDir); }},
            {OPTION_TLOG_FILE, []() { return msg::format(msgCmdSettingTLogFile); }},
            {OPTION_COPIED_FILES_LOG, []() { return msg::format(msgCmdSettingCopiedFilesLog); }},
        };

        const CommandStructure COMMAND_STRUCTURE = {
            [] {
                return LocalizedString::from_raw(
                    "--target-binary=\"Path/to/binary\" --installed-bin-dir=\"Path/to/installed/bin\" --tlog-file="
                    "\"Path/to/tlog.tlog\" --copied-files-log=\"Path/to/copiedFilesLog.log\"");
            },
            0,
            0,
            {{}, SETTINGS, {}},
            nullptr};

        auto& fs = get_real_filesystem();

        auto parsed = args.parse_arguments(COMMAND_STRUCTURE);
        const auto target_binary = parsed.settings.find(OPTION_TARGET_BINARY);
        Checks::check_exit(
            VCPKG_LINE_INFO, target_binary != parsed.settings.end(), "The --target-binary setting is required.");
        const auto target_installed_bin_dir = parsed.settings.find(OPTION_INSTALLED_DIR);
        Checks::check_exit(VCPKG_LINE_INFO,
                           target_installed_bin_dir != parsed.settings.end(),
                           "The --installed-bin-dir setting is required.");

        const auto target_binary_path = fs.almost_canonical(target_binary->second, VCPKG_LINE_INFO);
        copy_deps(fs,
                  target_binary_path.parent_path(),
                  Path(target_installed_bin_dir->second).parent_path(),
                  {&target_binary_path, 1},
                  maybe_create_log(parsed.settings, OPTION_TLOG_FILE, fs),
                  maybe_create_log(parsed.settings, OPTION_COPIED_FILES_LOG, fs));
        Checks::exit_success(VCPKG_LINE_INFO);
    }
}
#endif
