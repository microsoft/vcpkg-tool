#include <vcpkg/base/file-contents.h>
#include <vcpkg/base/fmt.h>
#include <vcpkg/base/json.h>
#include <vcpkg/base/messages.h>

#include <vcpkg/bundlesettings.h>

namespace
{
    using namespace vcpkg;

    static constexpr StringLiteral GIT = "Git";
    static constexpr StringLiteral ONE_LINER = "OneLiner";
    static constexpr StringLiteral VISUAL_STUDIO = "VisualStudio";

    bool parse_optional_json_bool(const Json::Object& doc, StringLiteral field_name, bool& output)
    {
        auto value = doc.get(field_name);
        if (!value)
        {
            return true;
        }

        if (!value->is_boolean())
        {
            return false;
        }

        output = value->boolean(VCPKG_LINE_INFO);
        return true;
    }

    bool parse_optional_json_string(const Json::Object& doc, StringLiteral field_name, Optional<std::string>& output)
    {
        auto value = doc.get(field_name);
        if (!value)
        {
            return true;
        }

        if (!value->is_string())
        {
            return false;
        }

        output = value->string(VCPKG_LINE_INFO).to_string();
        return true;
    }
}

namespace vcpkg
{
    std::string to_string(DeploymentKind dt) { return to_string_literal(dt).to_string(); }

    StringLiteral to_string_literal(DeploymentKind dt) noexcept
    {
        switch (dt)
        {
            case DeploymentKind::Git: return GIT;
            case DeploymentKind::OneLiner: return ONE_LINER;
            case DeploymentKind::VisualStudio: return VISUAL_STUDIO;
            default: Checks::unreachable(VCPKG_LINE_INFO);
        }
    }

    std::string BundleSettings::to_string() const
    {
        return fmt::format("readonly={}, usegitregistry={}, embeddedsha={}, deployment={}, vsversion={}",
                           read_only,
                           use_git_registry,
                           embedded_git_sha.value_or("nullopt"),
                           deployment,
                           vsversion.value_or("nullopt"));
    }

    ExpectedL<BundleSettings> try_parse_bundle_settings(const FileContents& bundle_contents)
    {
        auto maybe_bundle_doc = Json::parse_object(bundle_contents.content, bundle_contents.origin);
        auto doc = maybe_bundle_doc.get();
        if (!doc)
        {
            return msg::format(msgInvalidBundleDefinition).append_raw('\n').append_raw(maybe_bundle_doc.error());
        }

        BundleSettings ret;
        Optional<std::string> maybe_deployment_string;
        if (!parse_optional_json_bool(*doc, "readonly", ret.read_only) ||
            !parse_optional_json_bool(*doc, "usegitregistry", ret.use_git_registry) ||
            !parse_optional_json_string(*doc, "embeddedsha", ret.embedded_git_sha) ||
            !parse_optional_json_string(*doc, "deployment", maybe_deployment_string) ||
            !parse_optional_json_string(*doc, "vsversion", ret.vsversion))
        {
            return msg::format(msgInvalidBundleDefinition);
        }

        if (auto deployment_string = maybe_deployment_string.get())
        {
            if (GIT == *deployment_string)
            {
                // Already DeploymentKind::Git
            }
            else if (ONE_LINER == *deployment_string)
            {
                ret.deployment = DeploymentKind::OneLiner;
            }
            else if (VISUAL_STUDIO == *deployment_string)
            {
                ret.deployment = DeploymentKind::VisualStudio;
            }
            else
            {
                return msg::format(msgInvalidBundleDefinition);
            }
        }

        return ret;
    }
}