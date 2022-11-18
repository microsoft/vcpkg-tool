#include <vcpkg/base/cofffilereader.h>
#include <vcpkg/base/files.h>
#include <vcpkg/base/hash.h>
#include <vcpkg/base/system.debug.h>
#include <vcpkg/base/system.print.h>
#include <vcpkg/base/util.h>

#include <vcpkg/commands.applocal.h>
#include <vcpkg/vcpkgcmdarguments.h>

#include <functional>
#include <map>
#include <regex>
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

    struct AppLocalInvocation
    {
        AppLocalInvocation(Filesystem& fs,
                           const Path& deployment_dir,
                           const Path& installed_bin_dir,
                           WriteFilePointer&& tlog_file,
                           WriteFilePointer&& copied_files_log)
            : m_fs(fs)
            , m_deployment_dir(deployment_dir)
            , m_installed_bin_dir(fs.almost_canonical(installed_bin_dir, VCPKG_LINE_INFO))
            , m_installed_bin_parent(m_installed_bin_dir.parent_path())
            , m_tlog_file(std::move(tlog_file))
            , m_copied_files_log(std::move(copied_files_log))
        {
        }

        void resolve(const Path& binary)
        {
            vcpkg::printf("vcpkg applocal processing: %s\n", binary);
            const auto imported_names = vcpkg::read_dll_imported_dll_names(m_fs.open_for_read(binary, VCPKG_LINE_INFO));
            Debug::print("Imported DLLs of %s were %s", binary, Strings::join("\n", imported_names));
            for (auto&& imported_name : imported_names)
            {
                deploy_binary(m_deployment_dir, m_installed_bin_dir, imported_name);
            }

            // hardcoded for testing
            // deployAzureKinectSensorSDK(m_deployment_dir, m_installed_bin_parent, "k4a.dll");
            // deployOpenNI2(m_deployment_dir, m_installed_bin_parent, "OpenNI2.dll");
            deployQt(m_deployment_dir, m_installed_bin_parent / "plugins", "Qt5Guid.dll");
            /*
            bool g_is_debug = m_installed_bin_parent.stem() == "debug";
            if (g_is_debug)
            {
                deployMagnum(m_deployment_dir, m_installed_bin_parent / "bin\\magnum-d\\", "MagnumAudio-d.dll");
            }
            else
            {
                deployMagnum(m_deployment_dir, m_installed_bin_parent / "bin\\magnum\\", "MagnumAudio-d.dll");
            }
            */
        }

    private:
        // AZURE KINECT SENSOR SDK PLUGIN - MOVE
        void deployAzureKinectSensorSDK(const Path& target_binary_dir,
                                        const Path& installed_dir,
                                        const std::string& target_binary_name)
        {
            if (target_binary_name == "k4a.dll")
            {
                std::string binary_name = "depthengine_2_0.dll";
                Path inst_dir = installed_dir / "tools\\azure-kinect-sensor-sdk\\";

                Debug::print("  Deploying Azure Kinect Sensor SDK Initialization");
                deploy_binary(target_binary_dir, inst_dir, binary_name);
            }
        }

        // OPENNI2 PLUGIN - MOVE
        void deployOpenNI2(const Path& target_binary_dir,
                           const Path& installed_dir,
                           const std::string& target_binary_name)
        {
            if (target_binary_name == "OpenNI2.dll")
            {
                Debug::print("  Deploying OpenNI2 Initialization");
                deploy_binary(target_binary_dir, installed_dir / "bin\\OpenNI2\\", "OpenNI.ini");

                Debug::print("  Deploying OpenNI2 Drivers");
                Path drivers = target_binary_dir / "OpenNI2\\Drivers\\";
                std::error_code ec;
                m_fs.create_directories(drivers, ec);

                std::vector<Path> children =
                    m_fs.get_files_non_recursive(installed_dir / "bin\\OpenNI2\\Drivers\\", ec);

                for (auto c : children)
                {
                    deploy_binary(drivers, installed_dir / "bin\\OpenNI2\\Drivers\\", c.filename().to_string());
                }
            }
        }

        // MAGNUM PLUGINS - MOVE
        // HELPER FUNCTION FOR MAGNUM PLUGINS
        void deployPluginsMagnum(const std::string& plugins_subdir_name,
                                 const Path& target_binary_dir,
                                 const Path& magnum_plugins_dir)
        {
            Path plugins_base = magnum_plugins_dir.stem();

            std::error_code ec;
            if (m_fs.exists(magnum_plugins_dir / plugins_subdir_name, ec))
            {
                Debug::print("  Deploying plugins directory %s", plugins_subdir_name);

                Path new_dir = target_binary_dir / plugins_base / plugins_subdir_name;
                m_fs.create_directories(new_dir, ec);

                std::vector<Path> children = m_fs.get_files_non_recursive(magnum_plugins_dir / plugins_subdir_name, ec);
                for (auto c : children)
                {
                    deploy_binary(new_dir, magnum_plugins_dir / plugins_subdir_name, c.filename().to_string());
                    resolve(c);
                }
            }
            else
            {
                Debug::print("  Skipping plugins directory %s: doesn't exist", plugins_subdir_name);
            }
        }

        void deployMagnum(const Path& target_binary_dir,
                          const Path& magnum_plugins_dir,
                          const std::string& target_binary_name)
        {
            Debug::print("Deploying magnum plugins");

            if (target_binary_name == "MagnumAudio.dll" || target_binary_name == "MagnumAudio-d.dll")
            {
                deployPluginsMagnum("audioimporters", target_binary_dir, magnum_plugins_dir);
            }
            else if (target_binary_name == "MagnumText.dll" || target_binary_name == "MagnumText-d.dll")
            {
                deployPluginsMagnum("fonts", target_binary_dir, magnum_plugins_dir);
                deployPluginsMagnum("fontconverters", target_binary_dir, magnum_plugins_dir);
            }
            else if (target_binary_name == "MagnumTrade.dll" || target_binary_name == "MagnumTrade-d.dll")
            {
                deployPluginsMagnum("importers", target_binary_dir, magnum_plugins_dir);
                deployPluginsMagnum("imageconverters", target_binary_dir, magnum_plugins_dir);
                deployPluginsMagnum("sceneconverters", target_binary_dir, magnum_plugins_dir);
            }
            else if (target_binary_name == "MagnumShaderTools.dll" || target_binary_name == "MagnumShaderTools-d.dll")
            {
                deployPluginsMagnum("shaderconverters", target_binary_dir, magnum_plugins_dir);
            }
        }

        // QT PLUGINS - WILL BE MOVED FROM HERE
        // HELPER FUNCTION FOR QT
        void deployPluginsQt(const std::string& plugins_subdir_name,
                             const Path& target_binary_dir,
                             const Path& qt_plugins_dir)
        {
            std::error_code ec;
            if (m_fs.exists(qt_plugins_dir / plugins_subdir_name, ec))
            {
                Debug::print("  Deploying plugins directory %s", plugins_subdir_name);

                Path new_dir = target_binary_dir / "plugins" / plugins_subdir_name;
                m_fs.create_directories(new_dir, ec);

                std::vector<Path> children = m_fs.get_files_non_recursive(qt_plugins_dir / plugins_subdir_name, ec);

                for (auto c : children)
                {
                    deploy_binary(new_dir, qt_plugins_dir / plugins_subdir_name, c.filename().to_string());
                    resolve(c);
                }
            }
            else
            {
                Debug::print("  Skipping plugins directory %s : doesn't exist", plugins_subdir_name);
            }
        }

        void deployQt(const Path& target_binary_dir, const Path& qt_plugins_dir, const std::string& target_binary_name)
        {
            Path bin_dir = Path(qt_plugins_dir.parent_path()) / "bin";

            if (target_binary_name == "Qt5Cored.dll" || target_binary_name == "Qt5Core.dll")
            {
                std::error_code ec;
                if (!m_fs.exists(target_binary_dir / "qt.conf", ec))
                {
                    m_fs.write_contents(target_binary_dir / "qt.conf", "[Paths]", ec);
                }
            }
            else if (target_binary_dir == "Qt5Guid.dll" || target_binary_name == "Qt5Gui.dll")
            {
                Debug::print("  Deploying platforms");

                std::error_code ec;
                Path new_dir = target_binary_dir / "plugins" / "platforms";
                m_fs.create_directories(new_dir, ec);

                std::vector<Path> children = m_fs.get_files_non_recursive(qt_plugins_dir / "platforms", ec);
                for (auto c : children)
                {
                    std::regex re("qwindows.*.dll");
                    if (std::regex_match(c.filename().to_string(), re))
                    {
                        deploy_binary(new_dir, qt_plugins_dir / "platforms", c.filename().to_string());
                    }
                }
                deployPluginsQt("accessible", target_binary_dir, qt_plugins_dir);
                deployPluginsQt("imageformats", target_binary_dir, qt_plugins_dir);
                deployPluginsQt("iconengines", target_binary_dir, qt_plugins_dir);
                deployPluginsQt("platforminputcontexts", target_binary_dir, qt_plugins_dir);
                deployPluginsQt("styles", target_binary_dir, qt_plugins_dir);
            }
            else if (target_binary_name == "Qt5Networkd.dll" || target_binary_name == "Qt5Network.dll")
            {
                deployPluginsQt("bearer", target_binary_dir, qt_plugins_dir);

                std::error_code ec;
                std::vector<Path> children = m_fs.get_files_non_recursive(bin_dir, ec);
                for (auto c : children)
                {
                    std::regex lc_re("libcrypto-.*.dll");
                    if (std::regex_match(c.filename().to_string(), lc_re))
                    {
                        deploy_binary(target_binary_dir, bin_dir, c.filename().to_string());
                    }

                    std::regex ls_re("libssl-.*.dll");
                    if (std::regex_match(c.filename().to_string(), ls_re))
                    {
                        deploy_binary(target_binary_dir, bin_dir, c.filename().to_string());
                    }
                }
            }
            else if (target_binary_name == "Qt5Sqld.dll" || target_binary_name == "Qt5Sql.dll")
            {
                deployPluginsQt("sqldrivers", target_binary_dir, qt_plugins_dir);
            }
            else if (target_binary_name == "Qt5Multimediad.dll" || target_binary_name == "Qt5Multimedia.dll")
            {
                deployPluginsQt("audio", target_binary_dir, qt_plugins_dir);
                deployPluginsQt("mediaservice", target_binary_dir, qt_plugins_dir);
                deployPluginsQt("playlistformats", target_binary_dir, qt_plugins_dir);
            }
            else if (target_binary_name == "Qt5PrintSupportd.dll" || target_binary_name == "Qt5PrintSupport.dll")
            {
                deployPluginsQt("printsupport", target_binary_dir, qt_plugins_dir);
            }
            else if (target_binary_name == "Qt5Qmld.dll" || target_binary_name == "Qt5Qml.dll")
            {
                std::error_code ec;
                if (!m_fs.exists(target_binary_dir / "qml", ec))
                {
                    if (m_fs.exists(bin_dir / "..\\qml", ec))
                    {
                        m_fs.copy_regular_recursive(bin_dir / "..\\qml", target_binary_dir, VCPKG_LINE_INFO);
                    }
                    else if (m_fs.exists(bin_dir / "..\\..\\qml", ec))
                    {
                        m_fs.copy_regular_recursive(bin_dir / "..\\..\\qml", target_binary_dir, VCPKG_LINE_INFO);
                    }
                    else
                    {
                        Checks::exit_with_message(VCPKG_LINE_INFO, "FAILED", ec.message());
                    }
                }
            }
            else if (target_binary_name == "Qt5Quickd.dll" || target_binary_name == "Qt5Quick.dll")
            {
                std::vector<std::string> libs = {"Qt5QuickControls2.dll",
                                                 "Qt5QuickControls2d.dll",
                                                 "Qt5QuickShapes.dll",
                                                 "Qt5QuickShapesd.dll",
                                                 "Qt5QuickTemplates2.dll",
                                                 "Qt5QuickTemplates2d.dll",
                                                 "Qt5QmlWorkerScript.dll",
                                                 "Qt5QmlWorkerScriptd.dll",
                                                 "Qt5QuickParticles.dll",
                                                 "Qt5QuickParticlesd.dll",
                                                 "Qt5QuickWidgets.dll",
                                                 "Qt5QuickWidgetsd.dll"};
                for (std::string lib : libs)
                {
                    deploy_binary(target_binary_dir, bin_dir, lib);
                }

                deployPluginsQt("scenegraph", target_binary_dir, qt_plugins_dir);
                deployPluginsQt("qmltooling", target_binary_dir, qt_plugins_dir);
            }

            std::error_code ec;
            std::vector<Path> children = m_fs.get_files_non_recursive(target_binary_name, ec);
            for (auto c : children)
            {
                std::regex dec_re("Qt5Declarative.*.dll");
                if (std::regex_match(c.filename().to_string(), dec_re))
                {
                    deployPluginsQt("qml1tooling", target_binary_dir, qt_plugins_dir);
                }

                std::regex pos_re("Qt5Positioning.*.dll");
                if (std::regex_match(c.filename().to_string(), pos_re))
                {
                    deployPluginsQt("position", target_binary_dir, qt_plugins_dir);
                }

                std::regex loc_re("Qt5Location.*.dll");
                if (std::regex_match(c.filename().to_string(), loc_re))
                {
                    deployPluginsQt("geoservices", target_binary_dir, qt_plugins_dir);
                }

                std::regex sen_re("Qt5Sensors.*.dll");
                if (std::regex_match(c.filename().to_string(), sen_re))
                {
                    deployPluginsQt("sensors", target_binary_dir, qt_plugins_dir);
                    deployPluginsQt("sensorgestures", target_binary_dir, qt_plugins_dir);
                }

                std::regex weben_re("Qt5WebEngineCore.*.dll");
                if (std::regex_match(c.filename().to_string(), weben_re))
                {
                    deployPluginsQt("qtwebengine", target_binary_dir, qt_plugins_dir);
                }

                std::regex ren3d_re("Qt53DRenderer.*.dll");
                if (std::regex_match(c.filename().to_string(), ren3d_re))
                {
                    deployPluginsQt("sceneparsers", target_binary_dir, qt_plugins_dir);
                }

                std::regex tex_re("Qt5TextToSpeech.*.dll");
                if (std::regex_match(c.filename().to_string(), tex_re))
                {
                    deployPluginsQt("texttospeech", target_binary_dir, qt_plugins_dir);
                }

                std::regex ser_re("Qt5SerialBus.*.dll");
                if (std::regex_match(c.filename().to_string(), ser_re))
                {
                    deployPluginsQt("canbus", target_binary_dir, qt_plugins_dir);
                }
            }
        }

        bool deploy_binary(const Path& target_binary_dir,
                           const Path& installed_dir,
                           const std::string& target_binary_name)
        {
            const auto source = installed_dir / target_binary_name;
            const auto target = target_binary_dir / target_binary_name;
            // FIXME This should use an NT Mutant (what comes out of CreateMutex) to ensure that different vcpkg.exes
            // running at the same time don't try to deploy the same file at the same time.
            std::error_code ec;
            // FIXME Should this check for last_write_time and choose latest?
            const bool did_deploy = m_fs.copy_file(source, target, CopyOptions::overwrite_existing, ec);
            if (did_deploy)
            {
                vcpkg::printf("%s -> %s done\n", source, target);
            }
            else if (!ec)
            {
                // note that we still print this as "copied" in tlog etc. because it's still a dependency
                vcpkg::printf("%s -> %s skipped, up to date\n", source, target);
            }
            else if (ec == std::errc::no_such_file_or_directory)
            {
                Debug::print("Attempted to deploy %s, but it didn't exist", source);
                return false;
            }
            else
            {
                Checks::exit_with_message(
                    VCPKG_LINE_INFO, "Failed to deploy %s -> %s; error: %s\n", source, target, ec.message());
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

            return did_deploy;
        }

        Filesystem& m_fs;
        Path m_deployment_dir;
        Path m_installed_bin_dir;
        Path m_installed_bin_parent;
        WriteFilePointer m_tlog_file;
        WriteFilePointer m_copied_files_log;
    };
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
            {OPTION_TARGET_BINARY, "Path to the binary to analyze"},
            {OPTION_INSTALLED_DIR, "Path to the installed tree to use"},
            {OPTION_TLOG_FILE, "Path to the tlog file to create"},
            {OPTION_COPIED_FILES_LOG, "Path to the copied files log to create"},
        };

        const CommandStructure COMMAND_STRUCTURE = {
            "--target-binary=\"Path\\to\\binary\" --installed-bin-dir=\"Path\\to\\installed\\bin\" --tlog-file="
            "\"Path\\to\\tlog.tlog\" --copied-files-log=\"Path\\to\\copiedFilesLog.log\"",
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
        AppLocalInvocation invocation(fs,
                                      target_binary_path.parent_path(),
                                      target_installed_bin_dir->second,
                                      maybe_create_log(parsed.settings, OPTION_TLOG_FILE, fs),
                                      maybe_create_log(parsed.settings, OPTION_COPIED_FILES_LOG, fs));
        invocation.resolve(target_binary_path);
        Checks::exit_success(VCPKG_LINE_INFO);
    }
}