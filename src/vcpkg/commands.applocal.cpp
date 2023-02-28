#if defined(_WIN32)
#include <vcpkg/base/cofffilereader.h>
#include <vcpkg/base/files.h>
#include <vcpkg/base/hash.h>
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
        MutantGuard(StringView name)
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
            , m_openni2_installed(
                  m_fs.exists(m_installed_bin_parent / "bin/OpenNI2/openni2deploy.ps1", VCPKG_LINE_INFO))
            , m_azurekinectsdk_installed(
                  m_fs.exists(m_installed_bin_parent / "tools/azure-kinect-sensor-sdk/k4adeploy.ps1", VCPKG_LINE_INFO))
            , m_magnum_installed(m_fs.exists(m_installed_bin_parent / "bin/magnum/magnumdeploy.ps1", VCPKG_LINE_INFO) ||
                                 m_fs.exists(m_installed_bin_parent / "bin/magnum-d/magnumdeploy.ps1", VCPKG_LINE_INFO))
            , m_qt_installed(m_fs.exists(m_installed_bin_parent / "plugins/qtdeploy.ps1", VCPKG_LINE_INFO))
        {
        }

        void resolve(const Path& binary)
        {
            msg::println(msgApplocalProcessing, msg::path = binary);
            auto dll_file = m_fs.open_for_read(binary, VCPKG_LINE_INFO);
            const auto dll_metadata = vcpkg::try_read_dll_metadata(dll_file).value_or_exit(VCPKG_LINE_INFO);
            const auto imported_names =
                vcpkg::try_read_dll_imported_dll_names(dll_metadata, dll_file).value_or_exit(VCPKG_LINE_INFO);
            Debug::print("Imported DLLs of ", binary, " were ", Strings::join("\n", imported_names), "\n");

            for (auto&& imported_name : imported_names)
            {
                if (m_searched.find(imported_name) != m_searched.end())
                {
                    Debug::println(" ", imported_name, "previously searched - Skip");
                    continue;
                }
                m_searched.insert(imported_name);

                Path target_binary_dir = binary.parent_path();
                Path installed_item_file_path = m_installed_bin_dir / imported_name;
                Path target_item_file_path = target_binary_dir / imported_name;

                if (m_fs.exists(installed_item_file_path, VCPKG_LINE_INFO))
                {
                    deploy_binary(m_deployment_dir, m_installed_bin_dir, imported_name);

                    if (m_openni2_installed)
                    {
                        deployOpenNI2(target_binary_dir, m_installed_bin_parent, imported_name);
                    }

                    if (m_azurekinectsdk_installed)
                    {
                        deployAzureKinectSensorSDK(target_binary_dir, m_installed_bin_parent, imported_name);
                    }

                    if (m_magnum_installed)
                    {
                        bool g_is_debug = m_installed_bin_parent.stem() == "debug";
                        if (g_is_debug)
                        {
                            deployMagnum(target_binary_dir, m_installed_bin_parent / "bin/magnum-d", imported_name);
                        }
                        else
                        {
                            deployMagnum(target_binary_dir, m_installed_bin_parent / "bin/magnum", imported_name);
                        }
                    }

                    if (m_qt_installed)
                    {
                        deployQt(m_deployment_dir, m_installed_bin_parent / "plugins", imported_name);
                    }

                    resolve(m_deployment_dir / imported_name);
                }
                else if (m_fs.exists(target_item_file_path, VCPKG_LINE_INFO))
                {
                    Debug::println("  ", imported_name, " not found in ", m_installed_bin_parent, "; locally deployed");
                    resolve(target_item_file_path);
                }
                else
                {
                    Debug::println("  ", imported_name, ": ", installed_item_file_path, " not found");
                }
            }
        }

    private:
        // Azure kinect sensor SDK plugins
        void deployAzureKinectSensorSDK(const Path& target_binary_dir,
                                        const Path& installed_dir,
                                        const std::string& target_binary_name)
        {
            if (target_binary_name == "k4a.dll")
            {
                std::string binary_name = "depthengine_2_0.dll";
                Path inst_dir = installed_dir / "tools/azure-kinect-sensor-sdk";

                Debug::println("  Deploying Azure Kinect Sensor SDK Initialization");
                deploy_binary(target_binary_dir, inst_dir, binary_name);
            }
        }

        // OpenNi plugins
        void deployOpenNI2(const Path& target_binary_dir,
                           const Path& installed_dir,
                           const std::string& target_binary_name)
        {
            if (target_binary_name == "OpenNI2.dll")
            {
                Debug::println("  Deploying OpenNI2 Initialization");
                deploy_binary(target_binary_dir, installed_dir / "bin/OpenNI2", "OpenNI.ini");

                Debug::println("  Deploying OpenNI2 Drivers");
                Path drivers = target_binary_dir / "OpenNI2/Drivers";
                std::error_code ec;
                m_fs.create_directories(drivers, ec);

                std::vector<Path> children = m_fs.get_files_non_recursive(installed_dir / "bin/OpenNI2/Drivers", ec);

                for (auto&& c : children)
                {
                    deploy_binary(drivers, installed_dir / "bin/OpenNI2/Drivers", c.filename().to_string());
                }
            }
        }

        // Magnum plugins
        // Helper function for magnum plugins
        void deployPluginsMagnum(const std::string& plugins_subdir_name,
                                 const Path& target_binary_dir,
                                 const Path& magnum_plugins_dir)
        {
            Path plugins_base = magnum_plugins_dir.stem();

            std::error_code ec;
            if (m_fs.exists(magnum_plugins_dir / plugins_subdir_name, ec))
            {
                Debug::println(" Deploying plugins directory ", plugins_subdir_name);

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
                Debug::println("  Skipping plugins directory ", plugins_subdir_name, ": doesn't exist");
            }
        }

        void deployMagnum(const Path& target_binary_dir,
                          const Path& magnum_plugins_dir,
                          const std::string& target_binary_name)
        {
            Debug::println("Deploying magnum plugins");

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

        // Qt plugins
        // Helper function for Qt
        void deployPluginsQt(const std::string& plugins_subdir_name,
                             const Path& target_binary_dir,
                             const Path& qt_plugins_dir)
        {
            std::error_code ec;
            if (m_fs.exists(qt_plugins_dir / plugins_subdir_name, ec))
            {
                Debug::println("  Deploying plugins directory ", plugins_subdir_name);

                Path new_dir = target_binary_dir / "plugins" / plugins_subdir_name;
                m_fs.create_directories(new_dir, ec);

                std::vector<Path> children = m_fs.get_files_non_recursive(qt_plugins_dir / plugins_subdir_name, ec);

                for (auto&& c : children)
                {
                    const std::string& c_filename = c.filename().to_string();
                    if (Strings::ends_with(c_filename, ".dll"))
                    {
                        deploy_binary(new_dir, qt_plugins_dir / plugins_subdir_name, c_filename);
                        resolve(c);
                    }
                }
            }
            else
            {
                Debug::println("  Skipping plugins directory ", plugins_subdir_name, ": doesn't exist");
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
                    m_fs.write_contents(target_binary_dir / "qt.conf", "[Paths]\n", ec);
                }
            }
            else if (target_binary_name == "Qt5Guid.dll" || target_binary_name == "Qt5Gui.dll")
            {
                Debug::println("  Deploying platforms");

                std::error_code ec;
                Path new_dir = target_binary_dir / "plugins" / "platforms";
                m_fs.create_directories(new_dir, ec);

                std::vector<Path> children = m_fs.get_files_non_recursive(qt_plugins_dir / "platforms", ec);
                for (auto&& c : children)
                {
                    const std::string& c_filename = c.filename().to_string();
                    if (Strings::starts_with(c_filename, "qwindows") && Strings::ends_with(c_filename, ".dll"))
                    {
                        deploy_binary(new_dir, qt_plugins_dir / "platforms", c_filename);
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
                for (auto&& c : children)
                {
                    const std::string& c_filename = c.filename().to_string();
                    if (Strings::starts_with(c_filename, "libcrypto-") && Strings::ends_with(c_filename, ".dll"))
                    {
                        deploy_binary(target_binary_dir, bin_dir, c_filename);
                    }

                    if (Strings::starts_with(c_filename, "libssl-") && Strings::ends_with(c_filename, ".dll"))
                    {
                        deploy_binary(target_binary_dir, bin_dir, c_filename);
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
                    if (m_fs.exists(bin_dir / "../qml", ec))
                    {
                        m_fs.copy_regular_recursive(bin_dir / "../qml", target_binary_dir, VCPKG_LINE_INFO);
                    }
                    else if (m_fs.exists(bin_dir / "../../qml", ec))
                    {
                        m_fs.copy_regular_recursive(bin_dir / "../../qml", target_binary_dir, VCPKG_LINE_INFO);
                    }
                    else
                    {
                        Checks::exit_with_message(VCPKG_LINE_INFO, "qml directory must exist with Qt5Qml.dll");
                    }
                }
                std::vector<std::string> libs = {"Qt5Quick.dll",
                                                 "Qt5Quickd.dll",
                                                 "Qt5QmlModels.dll",
                                                 "Qt5QmlModelsd.dll",
                                                 "Qt5QuickControls2.dll",
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
                for (const auto& lib : libs)
                {
                    deploy_binary(target_binary_dir, bin_dir, lib);
                }

                deployPluginsQt("scenegraph", target_binary_dir, qt_plugins_dir);
                deployPluginsQt("qmltooling", target_binary_dir, qt_plugins_dir);
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
                for (const auto& lib : libs)
                {
                    deploy_binary(target_binary_dir, bin_dir, lib);
                }

                deployPluginsQt("scenegraph", target_binary_dir, qt_plugins_dir);
                deployPluginsQt("qmltooling", target_binary_dir, qt_plugins_dir);
            }
            else if (Strings::starts_with(target_binary_name, "Qt5Declarative") &&
                     Strings::ends_with(target_binary_name, ".dll"))
            {
                deployPluginsQt("qml1tooling", target_binary_dir, qt_plugins_dir);
            }
            else if (Strings::starts_with(target_binary_name, "Qt5Positioning") &&
                     Strings::ends_with(target_binary_name, ".dll"))
            {
                deployPluginsQt("position", target_binary_dir, qt_plugins_dir);
            }
            else if (Strings::starts_with(target_binary_name, "Qt5Location") &&
                     Strings::ends_with(target_binary_name, ".dll"))
            {
                deployPluginsQt("geoservices", target_binary_dir, qt_plugins_dir);
            }
            else if (Strings::starts_with(target_binary_name, "Qt5Sensors") &&
                     Strings::ends_with(target_binary_name, ".dll"))
            {
                deployPluginsQt("sensors", target_binary_dir, qt_plugins_dir);
                deployPluginsQt("sensorgestures", target_binary_dir, qt_plugins_dir);
            }
            else if (Strings::starts_with(target_binary_name, "Qt5WebEngineCore") &&
                     Strings::ends_with(target_binary_name, ".dll"))
            {
                deployPluginsQt("qtwebengine", target_binary_dir, qt_plugins_dir);
            }
            else if (Strings::starts_with(target_binary_name, "Qt53DRenderer") &&
                     Strings::ends_with(target_binary_name, ".dll"))
            {
                deployPluginsQt("sceneparsers", target_binary_dir, qt_plugins_dir);
            }
            else if (Strings::starts_with(target_binary_name, "Qt5TextToSpeech") &&
                     Strings::ends_with(target_binary_name, ".dll"))
            {
                deployPluginsQt("texttospeech", target_binary_dir, qt_plugins_dir);
            }
            else if (Strings::starts_with(target_binary_name, "Qt5SerialBus") &&
                     Strings::ends_with(target_binary_name, ".dll"))
            {
                deployPluginsQt("canbus", target_binary_dir, qt_plugins_dir);
            }
        }

        bool deploy_binary(const Path& target_binary_dir,
                           const Path& installed_dir,
                           const std::string& target_binary_name)
        {
            const auto source = installed_dir / target_binary_name;
            const auto target = target_binary_dir / target_binary_name;
            const auto mutant_name = "vcpkg-applocal-" + Hash::get_string_sha256(target_binary_dir);
            const MutantGuard mutant(mutant_name);

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
                return false;
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

            return did_deploy;
        }

        Filesystem& m_fs;
        Path m_deployment_dir;
        Path m_installed_bin_dir;
        Path m_installed_bin_parent;
        WriteFilePointer m_tlog_file;
        WriteFilePointer m_copied_files_log;
        std::unordered_set<std::string> m_searched;
        bool m_openni2_installed;
        bool m_azurekinectsdk_installed;
        bool m_magnum_installed;
        bool m_qt_installed;
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
        AppLocalInvocation invocation(fs,
                                      target_binary_path.parent_path(),
                                      target_installed_bin_dir->second,
                                      maybe_create_log(parsed.settings, OPTION_TLOG_FILE, fs),
                                      maybe_create_log(parsed.settings, OPTION_COPIED_FILES_LOG, fs));
        invocation.resolve(target_binary_path);
        Checks::exit_success(VCPKG_LINE_INFO);
    }
}
#endif
