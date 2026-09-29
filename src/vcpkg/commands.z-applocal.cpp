#include <vcpkg/base/system-headers.h>

#include <vcpkg/base/fwd/diagnostics.h>

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
#include <unordered_set>

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

#if defined(_WIN32)
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
#endif // defined(_WIN32)

    struct BinaryPathDecodedInfo
    {
        Path installed_root;
        bool is_debug;
    };

    struct Deployment
    {
        Path source;
        Path destination;
    };

    struct DeploymentLockGuard
    {
#if defined(_WIN32)
        DeploymentLockGuard(const Filesystem&, const Path&, const Path& target_directory)
            : m_mutant("vcpkg-applocal-" +
                       Hash::get_string_sha256(Strings::ascii_to_lowercase(target_directory.native())))
        {
        }
#else  // ^^^ _WIN32 / !_WIN32 vvv
        DeploymentLockGuard(const Filesystem& fs, const Path& temp_dir, const Path& target_directory)
            : m_file_lock(fs.take_exclusive_file_lock(
                  stderr_diagnostic_context,
                  temp_dir / ("vcpkg-applocal-" + Hash::get_string_sha256(target_directory))))
        {
        }
#endif // ^^^ !_WIN32

        DeploymentLockGuard(const DeploymentLockGuard&) = delete;
        DeploymentLockGuard& operator=(const DeploymentLockGuard&) = delete;

    private:
#if defined(_WIN32)
        MutantGuard m_mutant;
#else  // ^^^ _WIN32 / !_WIN32 vvv
        std::unique_ptr<IExclusiveFileLock> m_file_lock;
#endif // ^^^ !_WIN32
    };

    BinaryPathDecodedInfo decode_from_canonical_bin_dir(const Path& canonical_bin_dir)
    {
        Path installed_root = canonical_bin_dir.parent_path();
        const bool is_debug = Strings::case_insensitive_ascii_equals(installed_root.filename(), "debug");
        return BinaryPathDecodedInfo{installed_root, is_debug};
    }

    struct AppLocalInvocation
    {
        AppLocalInvocation(const Filesystem& fs,
                           const Path& deployment_dir,
                           const Path& installed_bin_dir,
                           const Path& installed,
                           bool is_debug,
                           bool verbose,
#if defined(_WIN32)
                           WriteFilePointer&& tlog_file,
#endif // ^^^ _WIN32
                           WriteFilePointer&& copied_files_log)
            : m_fs(fs)
            , m_deployment_dir(deployment_dir)
            , m_installed_bin_dir(installed_bin_dir)
            , m_installed(installed)
            , m_is_debug(is_debug)
            , m_verbose(verbose)
#if defined(_WIN32)
            , m_tlog_file(std::move(tlog_file))
#endif // ^^^ _WIN32
            , m_copied_files_log(std::move(copied_files_log))
            , m_openni2_installed(m_fs.exists(m_installed / "bin/OpenNI2/openni2deploy.ps1", VCPKG_LINE_INFO))
            , m_azurekinectsdk_installed(
                  m_fs.exists(m_installed / "tools/azure-kinect-sensor-sdk/k4adeploy.ps1", VCPKG_LINE_INFO))
            , m_magnum_installed(m_fs.exists(m_installed / "bin/magnum/magnumdeploy.ps1", VCPKG_LINE_INFO) ||
                                 m_fs.exists(m_installed / "bin/magnum-d/magnumdeploy.ps1", VCPKG_LINE_INFO))
            , m_qt_installed(m_fs.exists(m_installed / "plugins/qtdeploy.ps1", VCPKG_LINE_INFO))
#if !defined(_WIN32)
            , m_temp_dir(m_fs.create_or_get_temp_directory(VCPKG_LINE_INFO))
#endif // ^^^ !_WIN32
        {
        }

        void resolve(const Path& source_binary, const Path& destination_binary_relative)
        {
            if (m_verbose)
            {
                msg::print(LocalizedString::from_raw(source_binary)
                               .append_raw(": ")
                               .append_raw(MessagePrefix)
                               .append(msgApplocalProcessing)
                               .append_raw('\n'));
            }

            auto dll_file = m_fs.open_for_read(source_binary, VCPKG_LINE_INFO);
            const auto dll_metadata = vcpkg::try_read_dll_metadata_required(dll_file).value_or_exit(VCPKG_LINE_INFO);
            const auto imported_names =
                vcpkg::try_read_dll_imported_dll_names(dll_metadata, dll_file).value_or_exit(VCPKG_LINE_INFO);
            dll_file.close();
            resolve_explicit(destination_binary_relative, imported_names);
        }

        void resolve_explicit(const Path& destination_binary_relative, const std::vector<std::string>& imported_names)
        {
            Debug::print("Imported DLLs deployed as ",
                         m_deployment_dir / destination_binary_relative,
                         " were ",
                         Strings::join("\n", imported_names),
                         "\n");

            for (auto&& imported_name : imported_names)
            {
                const auto normalized_imported_name = Strings::ascii_to_lowercase(imported_name);
                Path target_binary_dir_relative = destination_binary_relative.parent_path();
                target_binary_dir_relative.make_preferred();
                auto search_key = target_binary_dir_relative.native();
#if defined(_WIN32)
                Strings::inplace_ascii_to_lowercase(search_key);
#endif // defined(_WIN32)
                search_key.push_back('\0');
                search_key.append(normalized_imported_name);
                if (!m_searched.insert(std::move(search_key)).second)
                {
                    Debug::println(" ", imported_name, "previously searched - Skip");
                    continue;
                }

                Path installed_item_file_path = m_installed_bin_dir / imported_name;

                if (m_fs.exists(installed_item_file_path, VCPKG_LINE_INFO))
                {
                    plan_binary_deployment(target_binary_dir_relative, m_installed_bin_dir, imported_name);

                    if (m_openni2_installed)
                    {
                        deployOpenNI2(target_binary_dir_relative, m_installed, imported_name);
                    }

                    if (m_azurekinectsdk_installed)
                    {
                        deployAzureKinectSensorSDK(target_binary_dir_relative, m_installed, imported_name);
                    }

                    if (m_magnum_installed)
                    {
                        deployMagnum(target_binary_dir_relative,
                                     m_installed / (m_is_debug ? "bin/magnum-d" : "bin/magnum"),
                                     imported_name);
                    }

                    if (m_qt_installed)
                    {
                        deployQt(target_binary_dir_relative, m_installed / "plugins", imported_name);
                    }

                    resolve(installed_item_file_path, target_binary_dir_relative / imported_name);
                }
                else
                {
                    Debug::println("  ", imported_name, ": ", installed_item_file_path, " not found");
                }
            }
        }

        void execute_deployment_plan()
        {
            for (const auto& deployment : m_deployments)
            {
                const DeploymentLockGuard guard(m_fs, m_temp_dir, deployment.destination.parent_path());
                deploy_file(deployment);
            }

            for (const auto& deployment : m_recursive_deployments)
            {
                const DeploymentLockGuard guard(m_fs, m_temp_dir, deployment.destination);
                if (!m_fs.exists(deployment.destination, VCPKG_LINE_INFO))
                {
                    m_fs.copy_regular_recursive(deployment.source, deployment.destination, VCPKG_LINE_INFO);
                }
            }

            for (const auto& qt_conf : m_qt_conf_files)
            {
                const DeploymentLockGuard guard(m_fs, m_temp_dir, qt_conf.parent_path());
                if (!m_fs.exists(qt_conf, VCPKG_LINE_INFO))
                {
                    m_fs.write_contents(qt_conf, "[Paths]\n", VCPKG_LINE_INFO);
                }
            }
        }

    private:
        // Azure kinect sensor SDK plugins
        void deployAzureKinectSensorSDK(const Path& target_binary_dir,
                                        const Path& installed_dir,
                                        const std::string& target_binary_name)
        {
            if (Strings::case_insensitive_ascii_equals(target_binary_name, "k4a.dll"))
            {
                std::string binary_name = "depthengine_2_0.dll";
                Path inst_dir = installed_dir / "tools/azure-kinect-sensor-sdk";

                Debug::println("  Deploying Azure Kinect Sensor SDK Initialization");
                plan_binary_deployment(target_binary_dir, inst_dir, binary_name);
            }
        }

        // OpenNi plugins
        void deployOpenNI2(const Path& target_binary_dir,
                           const Path& installed_dir,
                           const std::string& target_binary_name)
        {
            if (Strings::case_insensitive_ascii_equals(target_binary_name, "OpenNI2.dll"))
            {
                Debug::println("  Deploying OpenNI2 Initialization");
                plan_binary_deployment(target_binary_dir, installed_dir / "bin/OpenNI2", "OpenNI.ini");

                Debug::println("  Deploying OpenNI2 Drivers");
                Path drivers = target_binary_dir / "OpenNI2/Drivers";
                std::error_code ec;
                std::vector<Path> children = m_fs.get_files_non_recursive(installed_dir / "bin/OpenNI2/Drivers", ec);

                for (auto&& c : children)
                {
                    const auto filename = c.filename();
                    if (Strings::case_insensitive_ascii_ends_with(filename, ".dll") ||
                        Strings::case_insensitive_ascii_ends_with(filename, ".ini"))
                    {
                        plan_binary_deployment(drivers, installed_dir / "bin/OpenNI2/Drivers", filename);
                    }
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

                std::vector<Path> children = m_fs.get_files_non_recursive(magnum_plugins_dir / plugins_subdir_name, ec);
                for (auto c : children)
                {
                    const auto filename = c.filename();
                    const bool is_dll = Strings::case_insensitive_ascii_ends_with(filename, ".dll");
                    if (is_dll || Strings::case_insensitive_ascii_ends_with(filename, ".conf") ||
                        Strings::case_insensitive_ascii_ends_with(filename, ".pdb"))
                    {
                        plan_binary_deployment(new_dir, magnum_plugins_dir / plugins_subdir_name, filename);
                        if (is_dll)
                        {
                            resolve(c, new_dir / filename);
                        }
                    }
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

            if (Strings::case_insensitive_ascii_equals(target_binary_name, "MagnumAudio.dll") ||
                Strings::case_insensitive_ascii_equals(target_binary_name, "MagnumAudio-d.dll"))
            {
                deployPluginsMagnum("audioimporters", target_binary_dir, magnum_plugins_dir);
            }
            else if (Strings::case_insensitive_ascii_equals(target_binary_name, "MagnumText.dll") ||
                     Strings::case_insensitive_ascii_equals(target_binary_name, "MagnumText-d.dll"))
            {
                deployPluginsMagnum("fonts", target_binary_dir, magnum_plugins_dir);
                deployPluginsMagnum("fontconverters", target_binary_dir, magnum_plugins_dir);
            }
            else if (Strings::case_insensitive_ascii_equals(target_binary_name, "MagnumTrade.dll") ||
                     Strings::case_insensitive_ascii_equals(target_binary_name, "MagnumTrade-d.dll"))
            {
                deployPluginsMagnum("importers", target_binary_dir, magnum_plugins_dir);
                deployPluginsMagnum("imageconverters", target_binary_dir, magnum_plugins_dir);
                deployPluginsMagnum("sceneconverters", target_binary_dir, magnum_plugins_dir);
            }
            else if (Strings::case_insensitive_ascii_equals(target_binary_name, "MagnumShaderTools.dll") ||
                     Strings::case_insensitive_ascii_equals(target_binary_name, "MagnumShaderTools-d.dll"))
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

                std::vector<Path> children = m_fs.get_files_non_recursive(qt_plugins_dir / plugins_subdir_name, ec);

                for (auto&& c : children)
                {
                    const auto c_filename = c.filename();
                    if (Strings::case_insensitive_ascii_ends_with(c_filename, ".dll"))
                    {
                        plan_binary_deployment(new_dir, qt_plugins_dir / plugins_subdir_name, c_filename);
                        resolve(c, new_dir / c_filename);
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

            if (Strings::case_insensitive_ascii_equals(target_binary_name, "Qt5Cored.dll") ||
                Strings::case_insensitive_ascii_equals(target_binary_name, "Qt5Core.dll"))
            {
                m_qt_conf_files.push_back(m_deployment_dir / target_binary_dir / "qt.conf");
            }
            else if (Strings::case_insensitive_ascii_equals(target_binary_name, "Qt5Guid.dll") ||
                     Strings::case_insensitive_ascii_equals(target_binary_name, "Qt5Gui.dll"))
            {
                Debug::println("  Deploying platforms");

                std::error_code ec;
                Path new_dir = target_binary_dir / "plugins" / "platforms";

                std::vector<Path> children = m_fs.get_files_non_recursive(qt_plugins_dir / "platforms", ec);
                for (auto&& c : children)
                {
                    auto c_filename = c.filename();
                    if (Strings::case_insensitive_ascii_starts_with(c_filename, "qwindows") &&
                        Strings::case_insensitive_ascii_ends_with(c_filename, ".dll"))
                    {
                        plan_binary_deployment(new_dir, qt_plugins_dir / "platforms", c_filename);
                    }
                }
                deployPluginsQt("accessible", target_binary_dir, qt_plugins_dir);
                deployPluginsQt("imageformats", target_binary_dir, qt_plugins_dir);
                deployPluginsQt("iconengines", target_binary_dir, qt_plugins_dir);
                deployPluginsQt("platforminputcontexts", target_binary_dir, qt_plugins_dir);
                deployPluginsQt("styles", target_binary_dir, qt_plugins_dir);
            }
            else if (Strings::case_insensitive_ascii_equals(target_binary_name, "Qt5Networkd.dll") ||
                     Strings::case_insensitive_ascii_equals(target_binary_name, "Qt5Network.dll"))
            {
                deployPluginsQt("bearer", target_binary_dir, qt_plugins_dir);

                std::error_code ec;
                std::vector<Path> children = m_fs.get_files_non_recursive(bin_dir, ec);
                for (auto&& c : children)
                {
                    const auto c_filename = c.filename();
                    if (Strings::case_insensitive_ascii_starts_with(c_filename, "libcrypto-") &&
                        Strings::case_insensitive_ascii_ends_with(c_filename, ".dll"))
                    {
                        plan_binary_deployment(target_binary_dir, bin_dir, c_filename);
                    }

                    if (Strings::case_insensitive_ascii_starts_with(c_filename, "libssl-") &&
                        Strings::case_insensitive_ascii_ends_with(c_filename, ".dll"))
                    {
                        plan_binary_deployment(target_binary_dir, bin_dir, c_filename);
                    }
                }
            }
            else if (Strings::case_insensitive_ascii_equals(target_binary_name, "Qt5Sqld.dll") ||
                     Strings::case_insensitive_ascii_equals(target_binary_name, "Qt5Sql.dll"))
            {
                deployPluginsQt("sqldrivers", target_binary_dir, qt_plugins_dir);
            }
            else if (Strings::case_insensitive_ascii_equals(target_binary_name, "Qt5Multimediad.dll") ||
                     Strings::case_insensitive_ascii_equals(target_binary_name, "Qt5Multimedia.dll"))
            {
                deployPluginsQt("audio", target_binary_dir, qt_plugins_dir);
                deployPluginsQt("mediaservice", target_binary_dir, qt_plugins_dir);
                deployPluginsQt("playlistformats", target_binary_dir, qt_plugins_dir);
            }
            else if (Strings::case_insensitive_ascii_equals(target_binary_name, "Qt5PrintSupportd.dll") ||
                     Strings::case_insensitive_ascii_equals(target_binary_name, "Qt5PrintSupport.dll"))
            {
                deployPluginsQt("printsupport", target_binary_dir, qt_plugins_dir);
            }
            else if (Strings::case_insensitive_ascii_equals(target_binary_name, "Qt5Qmld.dll") ||
                     Strings::case_insensitive_ascii_equals(target_binary_name, "Qt5Qml.dll"))
            {
                std::error_code ec;
                const auto target_qml_dir = m_deployment_dir / target_binary_dir / "qml";
                if (!m_fs.exists(target_qml_dir, ec))
                {
                    if (m_fs.exists(bin_dir / "../qml", ec))
                    {
                        m_recursive_deployments.push_back({bin_dir / "../qml", target_qml_dir});
                    }
                    else if (m_fs.exists(bin_dir / "../../qml", ec))
                    {
                        m_recursive_deployments.push_back({bin_dir / "../../qml", target_qml_dir});
                    }
                    else
                    {
                        msg::write_unlocalized_text(Color::error, "qml directory must exist with Qt5Qml.dll\n");
                        Checks::exit_fail(VCPKG_LINE_INFO);
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
                    plan_binary_deployment(target_binary_dir, bin_dir, lib);
                }

                deployPluginsQt("scenegraph", target_binary_dir, qt_plugins_dir);
                deployPluginsQt("qmltooling", target_binary_dir, qt_plugins_dir);
            }
            else if (Strings::case_insensitive_ascii_equals(target_binary_name, "Qt5Quickd.dll") ||
                     Strings::case_insensitive_ascii_equals(target_binary_name, "Qt5Quick.dll"))
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
                    plan_binary_deployment(target_binary_dir, bin_dir, lib);
                }

                deployPluginsQt("scenegraph", target_binary_dir, qt_plugins_dir);
                deployPluginsQt("qmltooling", target_binary_dir, qt_plugins_dir);
            }
            else if (Strings::case_insensitive_ascii_starts_with(target_binary_name, "Qt5Declarative") &&
                     Strings::case_insensitive_ascii_ends_with(target_binary_name, ".dll"))
            {
                deployPluginsQt("qml1tooling", target_binary_dir, qt_plugins_dir);
            }
            else if (Strings::case_insensitive_ascii_starts_with(target_binary_name, "Qt5Positioning") &&
                     Strings::case_insensitive_ascii_ends_with(target_binary_name, ".dll"))
            {
                deployPluginsQt("position", target_binary_dir, qt_plugins_dir);
            }
            else if (Strings::case_insensitive_ascii_starts_with(target_binary_name, "Qt5Location") &&
                     Strings::case_insensitive_ascii_ends_with(target_binary_name, ".dll"))
            {
                deployPluginsQt("geoservices", target_binary_dir, qt_plugins_dir);
            }
            else if (Strings::case_insensitive_ascii_starts_with(target_binary_name, "Qt5Sensors") &&
                     Strings::case_insensitive_ascii_ends_with(target_binary_name, ".dll"))
            {
                deployPluginsQt("sensors", target_binary_dir, qt_plugins_dir);
                deployPluginsQt("sensorgestures", target_binary_dir, qt_plugins_dir);
            }
            else if (Strings::case_insensitive_ascii_starts_with(target_binary_name, "Qt5WebEngineCore") &&
                     Strings::case_insensitive_ascii_ends_with(target_binary_name, ".dll"))
            {
                deployPluginsQt("qtwebengine", target_binary_dir, qt_plugins_dir);
            }
            else if (Strings::case_insensitive_ascii_starts_with(target_binary_name, "Qt53DRenderer") &&
                     Strings::case_insensitive_ascii_ends_with(target_binary_name, ".dll"))
            {
                deployPluginsQt("sceneparsers", target_binary_dir, qt_plugins_dir);
            }
            else if (Strings::case_insensitive_ascii_starts_with(target_binary_name, "Qt5TextToSpeech") &&
                     Strings::case_insensitive_ascii_ends_with(target_binary_name, ".dll"))
            {
                deployPluginsQt("texttospeech", target_binary_dir, qt_plugins_dir);
            }
            else if (Strings::case_insensitive_ascii_starts_with(target_binary_name, "Qt5SerialBus") &&
                     Strings::case_insensitive_ascii_ends_with(target_binary_name, ".dll"))
            {
                deployPluginsQt("canbus", target_binary_dir, qt_plugins_dir);
            }
        }

        void plan_binary_deployment(const Path& target_binary_dir_relative,
                                    const Path& installed_dir,
                                    StringView target_binary_name)
        {
            auto source = installed_dir / target_binary_name;
            source.make_preferred();
            auto target = m_deployment_dir / target_binary_dir_relative / target_binary_name;
            target.make_preferred();
            m_deployments.push_back({std::move(source), std::move(target)});
        }

        void deploy_file(const Deployment& deployment)
        {
            m_fs.create_directories(deployment.destination.parent_path(), VCPKG_LINE_INFO);
            std::error_code ec;
            const bool did_deploy =
                m_fs.copy_file(deployment.source, deployment.destination, CopyOptions::update_existing, ec);
            if (did_deploy)
            {
                msg::println(msgInstallCopiedFile,
                             msg::path_source = deployment.source,
                             msg::path_destination = deployment.destination);
            }
            else if (!ec)
            {
                if (m_verbose)
                {
                    msg::println(msgInstallSkippedUpToDateFile,
                                 msg::path_source = deployment.source,
                                 msg::path_destination = deployment.destination);
                }
            }
            else if (is_not_found_errc(ec))
            {
                Debug::println("Attempted to deploy ", deployment.source, ", but it didn't exist");
                return;
            }
            else
            {
                Checks::msg_exit_with_message(
                    VCPKG_LINE_INFO,
                    format_filesystem_call_error(
                        ec, "copy_file", {deployment.source, deployment.destination, "CopyOptions::update_existing"}));
            }

#if defined(_WIN32)
            if (m_tlog_file)
            {
                const auto as_utf16 = Strings::to_utf16(deployment.source);
                Checks::check_exit(VCPKG_LINE_INFO,
                                   m_tlog_file.write(as_utf16.data(), sizeof(wchar_t), as_utf16.size()) ==
                                       as_utf16.size());
                static constexpr wchar_t native_newline = L'\n';
                Checks::check_exit(VCPKG_LINE_INFO, m_tlog_file.write(&native_newline, sizeof(wchar_t), 1) == 1);
            }
#endif // ^^^ _WIN32

            if (m_copied_files_log)
            {
                const auto& native = deployment.source.native();
                Checks::check_exit(VCPKG_LINE_INFO,
                                   m_copied_files_log.write(native.c_str(), 1, native.size()) == native.size());
                Checks::check_exit(VCPKG_LINE_INFO, m_copied_files_log.put('\n') == '\n');
            }
        }

        const Filesystem& m_fs;
        Path m_deployment_dir;
        Path m_installed_bin_dir;
        Path m_installed;
        bool m_is_debug;
        bool m_verbose;
#if defined(_WIN32)
        WriteFilePointer m_tlog_file;
#endif // ^^^ _WIN32
        WriteFilePointer m_copied_files_log;
        std::unordered_set<std::string> m_searched;
        std::vector<Deployment> m_deployments;
        std::vector<Deployment> m_recursive_deployments;
        std::vector<Path> m_qt_conf_files;
        bool m_openni2_installed;
        bool m_azurekinectsdk_installed;
        bool m_magnum_installed;
        bool m_qt_installed;
        Path m_temp_dir;
    };

    constexpr CommandSwitch SWITCHES[] = {
        {SwitchVerbose, msgCmdZApplocalOptVerbose},
    };

    constexpr CommandSetting SETTINGS[] = {
        {SwitchTargetBinary, msgCmdSettingTargetBin},
        {SwitchInstalledBinDir, msgCmdSettingInstalledDir},
#if defined(_WIN32)
        {SwitchTLogFile, msgCmdSettingTLogFile},
#endif // ^^^ _WIN32
        {SwitchCopiedFilesLog, msgCmdSettingCopiedFilesLog},
    };
} // unnamed namespace

namespace vcpkg
{
    constexpr CommandMetadata CommandZApplocalMetadata{
        "z-applocal",
        msgCmdZApplocalSynopsis,
#if defined(_WIN32)
        {"vcpkg z-applocal --target-binary=\"Path/to/binary\" --installed-bin-dir=\"Path/to/installed/bin\" "
         "--tlog-file=\"Path/to/tlog.tlog\" --copied-files-log=\"Path/to/copiedFilesLog.log\""},
#else  // ^^^ _WIN32 / !_WIN32 vvv
        {"vcpkg z-applocal --target-binary=\"Path/to/binary\" --installed-bin-dir=\"Path/to/installed/bin\" "
         "--copied-files-log=\"Path/to/copiedFilesLog.log\""},
#endif // ^^^ !_WIN32
        Undocumented,
        AutocompletePriority::Internal,
        0,
        0,
        {SWITCHES, SETTINGS},
        nullptr,
    };

    void command_z_applocal_and_exit(const VcpkgCmdArguments& args, const Filesystem& fs)
    {
        auto parsed = args.parse_arguments(CommandZApplocalMetadata);
        const bool verbose = Util::Sets::contains(parsed.switches, SwitchVerbose);
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
        if (verbose)
        {
            msg::print(LocalizedString::from_raw(target_binary_path)
                           .append_raw(": ")
                           .append_raw(MessagePrefix)
                           .append(msgApplocalProcessing)
                           .append_raw('\n'));
        }

        std::error_code ec;
        auto dll_file = fs.open_for_read(target_binary_path, ec);
        if (ec)
        {
            auto io_error = ec.message();
            if (is_not_found_errc(ec))
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
                                      fs.almost_canonical(target_binary_path, VCPKG_LINE_INFO).parent_path(),
                                      target_installed_bin_dir,
                                      decoded.installed_root,
                                      decoded.is_debug,
                                      verbose,
#if defined(_WIN32)
                                      maybe_create_log(parsed.settings, SwitchTLogFile, fs),
#endif // ^^^ _WIN32
                                      maybe_create_log(parsed.settings, SwitchCopiedFilesLog, fs));
        invocation.resolve_explicit(target_binary_path.filename(), imported_names);
        invocation.execute_deployment_plan();
        Checks::exit_success(VCPKG_LINE_INFO);
    }
} // namespace vcpkg
