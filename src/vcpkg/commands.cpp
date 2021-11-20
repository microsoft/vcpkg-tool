#include <vcpkg/base/system.print.h>

#include <vcpkg/build.h>
#include <vcpkg/commands.activate.h>
#include <vcpkg/commands.add-version.h>
#include <vcpkg/commands.add.h>
#include <vcpkg/commands.autocomplete.h>
#include <vcpkg/commands.buildexternal.h>
#include <vcpkg/commands.cache.h>
#include <vcpkg/commands.check-support.h>
#include <vcpkg/commands.ci.h>
#include <vcpkg/commands.ciclean.h>
#include <vcpkg/commands.civerifyversions.h>
#include <vcpkg/commands.contact.h>
#include <vcpkg/commands.create.h>
#include <vcpkg/commands.dependinfo.h>
#include <vcpkg/commands.edit.h>
#include <vcpkg/commands.env.h>
#include <vcpkg/commands.fetch.h>
#include <vcpkg/commands.format-manifest.h>
#include <vcpkg/commands.generate-message-map.h>
#include <vcpkg/commands.h>
#include <vcpkg/commands.hash.h>
#include <vcpkg/commands.info.h>
#include <vcpkg/commands.init-registry.h>
#include <vcpkg/commands.integrate.h>
#include <vcpkg/commands.list.h>
#include <vcpkg/commands.new.h>
#include <vcpkg/commands.owns.h>
#include <vcpkg/commands.porthistory.h>
#include <vcpkg/commands.portsdiff.h>
#include <vcpkg/commands.search.h>
#include <vcpkg/commands.setinstalled.h>
#include <vcpkg/commands.upgrade.h>
#include <vcpkg/commands.upload-metrics.h>
#include <vcpkg/commands.version.h>
#include <vcpkg/commands.xdownload.h>
#include <vcpkg/commands.xvsinstances.h>
#include <vcpkg/commands.zbootstrap-readonly.h>
#include <vcpkg/commands.zce.h>
#include <vcpkg/commands.zprintconfig.h>
#include <vcpkg/export.h>
#include <vcpkg/help.h>
#include <vcpkg/install.h>
#include <vcpkg/remove.h>
#include <vcpkg/update.h>

namespace vcpkg::Commands
{
    Span<const PackageNameAndFunction<const BasicCommand*>> get_available_basic_commands()
    {
        static const Version::VersionCommand version{};
        static const Contact::ContactCommand contact{};
        static const InitRegistry::InitRegistryCommand init_registry{};
        static const X_Download::XDownloadCommand xdownload{};
        static const GenerateDefaultMessageMapCommand generate_message_map{};
        static const Hash::HashCommand hash{};
        static const BootstrapReadonlyCommand zboostrap_readonly{};
#if defined(_WIN32)
        static const UploadMetrics::UploadMetricsCommand upload_metrics{};
#endif // defined(_WIN32)

        static std::vector<PackageNameAndFunction<const BasicCommand*>> t = {
            {"version", &version},
            {"contact", &contact},
            {"hash", &hash},
            {"x-init-registry", &init_registry},
            {"x-download", &xdownload},
            {"x-generate-default-message-map", &generate_message_map},
            {"z-bootstrap-readonly", &zboostrap_readonly},
#if defined(_WIN32)
            {"x-upload-metrics", &upload_metrics},
#endif // defined(_WIN32)
        };
        return t;
    }

    Span<const PackageNameAndFunction<const PathsCommand*>> get_available_paths_commands()
    {
        static const Activate::ActivateCommand activate{};
        static const AddCommand add{};
        static const AddVersion::AddVersionCommand add_version{};
        static const Autocomplete::AutocompleteCommand autocomplete{};
        static const Cache::CacheCommand cache{};
        static const CIClean::CICleanCommand ciclean{};
        static const CIVerifyVersions::CIVerifyVersionsCommand ci_verify_versions{};
        static const Create::CreateCommand create{};
        static const Edit::EditCommand edit{};
        static const Fetch::FetchCommand fetch{};
        static const FormatManifest::FormatManifestCommand format_manifest{};
        static const Help::HelpCommand help{};
        static const Info::InfoCommand info{};
        static const Integrate::IntegrateCommand integrate{};
        static const List::ListCommand list{};
        static const New::NewCommand new_{};
        static const Owns::OwnsCommand owns{};
        static const PortHistory::PortHistoryCommand porthistory{};
        static const PortsDiff::PortsDiffCommand portsdiff{};
        static const Search::SearchCommand search{};
        static const Update::UpdateCommand update{};
        static const X_VSInstances::VSInstancesCommand vsinstances{};
        static const ZCeCommand ce{};

        static std::vector<PackageNameAndFunction<const PathsCommand*>> t = {
            {"/?", &help},
            {"help", &help},
            {"activate", &activate},
            {"add", &add},
            {"autocomplete", &autocomplete},
            {"cache", &cache},
            {"create", &create},
            {"edit", &edit},
            {"fetch", &fetch},
            {"format-manifest", &format_manifest},
            {"integrate", &integrate},
            {"list", &list},
            {"new", &new_},
            {"owns", &owns},
            {"portsdiff", &portsdiff},
            {"search", &search},
            {"update", &update},
            {"x-add-version", &add_version},
            {"x-ci-clean", &ciclean},
            {"x-ci-verify-versions", &ci_verify_versions},
            {"x-history", &porthistory},
            {"x-package-info", &info},
            {"x-vsinstances", &vsinstances},
            {"z-ce", &ce},
        };
        return t;
    }

    Span<const PackageNameAndFunction<const TripletCommand*>> get_available_triplet_commands()
    {
        static const Install::InstallCommand install{};
        static const SetInstalled::SetInstalledCommand set_installed{};
        static const CI::CICommand ci{};
        static const Remove::RemoveCommand remove{};
        static const Upgrade::UpgradeCommand upgrade{};
        static const Build::BuildCommand build{};
        static const Env::EnvCommand env{};
        static const BuildExternal::BuildExternalCommand build_external{};
        static const Export::ExportCommand export_command{};
        static const DependInfo::DependInfoCommand depend_info{};
        static const CheckSupport::CheckSupportCommand check_support{};
        static const Z_PrintConfig::PrintConfigCommand print_config{};

        static std::vector<PackageNameAndFunction<const TripletCommand*>> t = {
            {"install", &install},
            {"x-set-installed", &set_installed},
            {"ci", &ci},
            {"remove", &remove},
            {"upgrade", &upgrade},
            {"build", &build},
            {"env", &env},
            {"build-external", &build_external},
            {"export", &export_command},
            {"depend-info", &depend_info},
            {"x-check-support", &check_support},
            {"z-print-config", &print_config},
        };
        return t;
    }
}
