#if defined(_WIN32)
#include <vcpkg/base/cofffilereader.h>
#include <vcpkg/base/contractual-constants.h>
#include <vcpkg/base/files.h>
#include <vcpkg/base/hash.h>
#include <vcpkg/base/system.debug.h>
#include <vcpkg/base/util.h>

#include <vcpkg/commands.z-applocal.h>
#include <vcpkg/vcpkgcmdarguments.h>

#include <functional>
#include <map>
#include <set>
#include <string>
#include <system_error>

using namespace vcpkg;

namespace
{
    WriteFilePointer maybe_create_log(const std::map<StringLiteral, std::string, std::less<>>& settings,
                                      StringLiteral setting,
                                      const Filesystem& fs)
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

    struct BinaryPathDecodedInfo
    {
        Path installed_root;
        bool is_debug;
    };

    BinaryPathDecodedInfo decode_from_canonical_bin_dir(const Path& canonical_bin_dir)
    {
        auto maybe_installed_root = canonical_bin_dir.parent_path();
        static constexpr StringLiteral debug_suffix = "\\debug";
        const bool is_debug = Strings::case_insensitive_ascii_ends_with(maybe_installed_root, debug_suffix);
        if (is_debug)
        {
            maybe_installed_root = maybe_installed_root.substr(0, maybe_installed_root.size() - debug_suffix.size());
        }

        return BinaryPathDecodedInfo{maybe_installed_root, is_debug};
    }

    struct AppLocalInvocation
    {
        AppLocalInvocation(const Filesystem& fs,
                           const Path& deployment_dir,
                           const Path& installed_bin_dir,
                           const Path& installed,
                           bool is_debug,
                           WriteFilePointer&& tlog_file,
                           WriteFilePointer&& copied_files_log)
            : m_fs(fs)
            , m_deployment_dir(deployment_dir)
            , m_installed_bin_dir(installed_bin_dir)
            , m_installed(installed)
            , m_is_debug(is_debug)
            , m_tlog_file(std::move(tlog_file))
            , m_copied_files_log(std::move(copied_files_log))
            , m_openni2_installed(m_fs.exists(m_installed / "bin/OpenNI2/openni2deploy.ps1", VCPKG_LINE_INFO))
            , m_azurekinectsdk_installed(
                  m_fs.exists(m_installed / "tools/azure-kinect-sensor-sdk/k4adeploy.ps1", VCPKG_LINE_INFO))
            , m_magnum_installed(m_fs.exists(m_installed / "bin/magnum/magnumdeploy.ps1", VCPKG_LINE_INFO) ||
                                 m_fs.exists(m_installed / "bin/magnum-d/magnumdeploy.ps1", VCPKG_LINE_INFO))
            , m_qt_installed(m_fs.exists(m_installed / "plugins/qtdeploy.ps1", VCPKG_LINE_INFO))
        {
        }

        void resolve(const Path& binary)
        {
            msg::print(LocalizedString::from_raw(binary)
                           .append_raw(": ")
                           .append_raw(MessagePrefix)
                           .append(msgApplocalProcessing)
                           .append_raw('\n'));

            auto dll_file = m_fs.open_for_read(binary, VCPKG_LINE_INFO);
            const auto dll_metadata = vcpkg::try_read_dll_metadata_required(dll_file).value_or_exit(VCPKG_LINE_INFO);
            const auto imported_names =
                vcpkg::try_read_dll_imported_dll_names(dll_metadata, dll_file).value_or_exit(VCPKG_LINE_INFO);
            dll_file.close();
            resolve_explicit(binary, imported_names);
        }

        void resolve_explicit(const Path& binary, const std::vector<std::string>& imported_names)
        {
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
                        deployOpenNI2(target_binary_dir, m_installed, imported_name);
                    }

                    if (m_azurekinectsdk_installed)
                    {
                        deployAzureKinectSensorSDK(target_binary_dir, m_installed, imported_name);
                    }

                    if (m_magnum_installed)
                    {
                        if (m_is_debug)
                        {
                            deployMagnum(target_binary_dir, m_installed / "bin/magnum-d", imported_name);
                        }
                        else
                        {
                            deployMagnum(target_binary_dir, m_installed / "bin/magnum", imported_name);
                        }
                    }

                    if (m_qt_installed)
                    {
                        deployQt(m_deployment_dir, m_installed / "plugins", imported_name);
                    }

                    resolve(m_deployment_dir / imported_name);
                }
                else if (m_fs.exists(target_item_file_path, VCPKG_LINE_INFO))
                {
                    Debug::println("  ", imported_name, " not found in ", m_installed, "; locally deployed");
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
                    const auto c_filename = c.filename();
                    if (c_filename.ends_with(".dll"))
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

        void deployQt(const Path& target_binary_dir, const Path& qt_plugins_dir, StringView target_binary_name)
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
                    auto c_filename = c.filename();
                    if (c_filename.starts_with("qwindows") && c_filename.ends_with(".dll"))
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
                    const auto c_filename = c.filename();
                    if (c_filename.starts_with("libcrypto-") && c_filename.ends_with(".dll"))
                    {
                        deploy_binary(target_binary_dir, bin_dir, c_filename);
                    }

                    if (c_filename.starts_with("libssl-") && c_filename.ends_with(".dll"))
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
            else if (target_binary_name.starts_with("Qt5Declarative") && target_binary_name.ends_with(".dll"))
            {
                deployPluginsQt("qml1tooling", target_binary_dir, qt_plugins_dir);
            }
            else if (target_binary_name.starts_with("Qt5Positioning") && target_binary_name.ends_with(".dll"))
            {
                deployPluginsQt("position", target_binary_dir, qt_plugins_dir);
            }
            else if (target_binary_name.starts_with("Qt5Location") && target_binary_name.ends_with(".dll"))
            {
                deployPluginsQt("geoservices", target_binary_dir, qt_plugins_dir);
            }
            else if (target_binary_name.starts_with("Qt5Sensors") && target_binary_name.ends_with(".dll"))
            {
                deployPluginsQt("sensors", target_binary_dir, qt_plugins_dir);
                deployPluginsQt("sensorgestures", target_binary_dir, qt_plugins_dir);
            }
            else if (target_binary_name.starts_with("Qt5WebEngineCore") && target_binary_name.ends_with(".dll"))
            {
                deployPluginsQt("qtwebengine", target_binary_dir, qt_plugins_dir);
            }
            else if (target_binary_name.starts_with("Qt53DRenderer") && target_binary_name.ends_with(".dll"))
            {
                deployPluginsQt("sceneparsers", target_binary_dir, qt_plugins_dir);
            }
            else if (target_binary_name.starts_with("Qt5TextToSpeech") && target_binary_name.ends_with(".dll"))
            {
                deployPluginsQt("texttospeech", target_binary_dir, qt_plugins_dir);
            }
            else if (target_binary_name.starts_with("Qt5SerialBus") && target_binary_name.ends_with(".dll"))
            {
                deployPluginsQt("canbus", target_binary_dir, qt_plugins_dir);
            }
        }

        bool deploy_binary(const Path& target_binary_dir, const Path& installed_dir, StringView target_binary_name)
        {
            auto source = installed_dir / target_binary_name;
            source.make_preferred();
            auto target = target_binary_dir / target_binary_name;
            target.make_preferred();
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

        const Filesystem& m_fs;
        Path m_deployment_dir;
        Path m_installed_bin_dir;
        Path m_installed;
        bool m_is_debug;
        WriteFilePointer m_tlog_file;
        WriteFilePointer m_copied_files_log;
        std::unordered_set<std::string> m_searched;
        bool m_openni2_installed;
        bool m_azurekinectsdk_installed;
        bool m_magnum_installed;
        bool m_qt_installed;
    };

    constexpr CommandSetting SETTINGS[] = {
        {SwitchTargetBinary, msgCmdSettingTargetBin},
        {SwitchInstalledBinDir, msgCmdSettingInstalledDir},
        {SwitchTLogFile, msgCmdSettingTLogFile},
        {SwitchCopiedFilesLog, msgCmdSettingCopiedFilesLog},
    };
} // unnamed namespace

namespace vcpkg
{
    constexpr CommandMetadata CommandZApplocalMetadata{
        "z-applocal",
        msgCmdZApplocalSynopsis,
        {"vcpkg z-applocal --target-binary=\"Path/to/binary\" --installed-bin-dir=\"Path/to/installed/bin\" "
         "--tlog-file=\"Path/to/tlog.tlog\" --copied-files-log=\"Path/to/copiedFilesLog.log\""},
        Undocumented,
        AutocompletePriority::Internal,
        0,
        0,
        {{}, SETTINGS},
        nullptr,
    };

    void command_z_applocal_and_exit(const VcpkgCmdArguments& args, const Filesystem& fs)
    {
        auto parsed = args.parse_arguments(CommandZApplocalMetadata);
        const auto target_binary = parsed.settings.find(SwitchTargetBinary);
        if (target_binary == parsed.settings.end())
        {
            Checks::msg_exit_with_error(VCPKG_LINE_INFO, msgOptionRequiresAValue, msg::option = SwitchTargetBinary);
        }

        const auto target_installed_bin_setting = parsed.settings.find(SwitchInstalledBinDir);
        if (target_installed_bin_setting == parsed.settings.end())
        {
            Checks::msg_exit_with_error(VCPKG_LINE_INFO, msgOptionRequiresAValue, msg::option = SwitchInstalledBinDir);
        }

        const auto target_installed_bin_dir =
            fs.almost_canonical(target_installed_bin_setting->second, VCPKG_LINE_INFO);
        const auto decoded = decode_from_canonical_bin_dir(target_installed_bin_dir);

        // the first binary is special in that it might not be a DLL or might not exist
        const Path target_binary_path = target_binary->second;
        msg::print(LocalizedString::from_raw(target_binary_path)
                       .append_raw(": ")
                       .append_raw(MessagePrefix)
                       .append(msgApplocalProcessing)
                       .append_raw('\n'));

        std::error_code ec;
        auto dll_file = fs.open_for_read(target_binary_path, ec);
        if (ec)
        {
            auto io_error = ec.message();
            if (ec == std::errc::no_such_file_or_directory)
            {
                msg::print(Color::warning,
                           LocalizedString::from_raw(target_binary_path)
                               .append_raw(": ")
                               .append_raw(WarningPrefix)
                               .append_raw(io_error)
                               .append_raw('\n'));
            }
            else
            {
                msg::print(Color::error,
                           LocalizedString::from_raw(target_binary_path)
                               .append_raw(": ")
                               .append_raw(ErrorPrefix)
                               .append_raw(io_error)
                               .append_raw('\n'));
            }

            Checks::exit_fail(VCPKG_LINE_INFO);
        }

        auto maybe_dll_metadata = vcpkg::try_read_dll_metadata(dll_file).value_or_exit(VCPKG_LINE_INFO);
        auto dll_metadata = maybe_dll_metadata.get();
        if (!dll_metadata)
        {
            msg::print(Color::warning,
                       LocalizedString::from_raw(target_binary_path)
                           .append_raw(": ")
                           .append_raw(WarningPrefix)
                           .append(msgFileIsNotExecutable)
                           .append_raw('\n'));

            Checks::exit_fail(VCPKG_LINE_INFO);
        }

        const auto imported_names =
            vcpkg::try_read_dll_imported_dll_names(*dll_metadata, dll_file).value_or_exit(VCPKG_LINE_INFO);
        dll_file.close();

        AppLocalInvocation invocation(fs,
                                      target_binary_path.parent_path(),
                                      target_installed_bin_dir,
                                      decoded.installed_root,
                                      decoded.is_debug,
                                      maybe_create_log(parsed.settings, SwitchTLogFile, fs),
                                      maybe_create_log(parsed.settings, SwitchCopiedFilesLog, fs));
        invocation.resolve_explicit(target_binary_path, imported_names);
        Checks::exit_success(VCPKG_LINE_INFO);
    }
} // namespace vcpkg
#endif // ^^^ _WIN32
