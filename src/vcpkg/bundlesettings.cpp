#include <vcpkg/base/format.h>
#include <vcpkg/base/json.h>
#include <vcpkg/base/messages.h>

#include <vcpkg/bundlesettings.h>

namespace vcpkg
{
    std::string BundleSettings::to_string() const
    {
        return fmt::format("readonly={}, usegitregistry={}, embeddedsha={}",
                           read_only,
                           use_git_registry,
                           embedded_git_sha.value_or("nullopt"));
    }

    ExpectedL<BundleSettings> try_parse_bundle_settings(const FileContents& bundle_contents)
    {
        auto maybe_bundle_doc = Json::parse_object(bundle_contents.content, bundle_contents.origin);
        auto doc = maybe_bundle_doc.get();
        if (!doc)
        {
            return msg::format(msgInvalidBundleDefinition).append_raw('\n').append_raw(maybe_bundle_doc.error());
        }

        auto read_only = doc->get("readonly");
        auto use_git_registry = doc->get("usegitregistry");
        auto embedded_sha = doc->get("embeddedsha");

        if ((read_only && !read_only->is_boolean()) || (use_git_registry && !use_git_registry->is_boolean()) ||
            (embedded_sha && !embedded_sha->is_string()))
        {
            return msg::format(msgInvalidBundleDefinition);
        }

        BundleSettings ret;
        if (read_only)
        {
            ret.read_only = read_only->boolean(VCPKG_LINE_INFO);
        }

        if (use_git_registry)
        {
            ret.use_git_registry = use_git_registry->boolean(VCPKG_LINE_INFO);
        }

        if (embedded_sha)
        {
            ret.embedded_git_sha = embedded_sha->string(VCPKG_LINE_INFO).to_string();
        }

        return ret;
    }
}