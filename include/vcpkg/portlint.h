#pragma once

#include <vcpkg/base/fwd/files.h>
#include <vcpkg/base/fwd/stringview.h>

#include <vcpkg/fwd/packagespec.h>
#include <vcpkg/fwd/triplet.h>
#include <vcpkg/fwd/vcpkgpaths.h>

#include <vcpkg/sourceparagraph.h>

namespace vcpkg::Lint
{
    enum class Status : int
    {
        Ok = 0b0,
        Problem = 0b01,
        Fixed = 0b10,
        PartiallyFixed = 0b11
    };

    Status& operator|=(Status& self, Status s);

    enum class Fix
    {
        NO = 0, // A warning message is printed for every found problem
        YES     // The problem is fixed in place (SourceControlFile) and no message is printed
    };

    std::string get_recommended_license_expression(std::string original_license);

    VersionScheme get_recommended_version_scheme(StringView raw_version, VersionScheme original_scheme);

    Status check_used_version_scheme(SourceControlFile& scf, Fix fix);

    Status check_license_expression(SourceControlFile& scf, Fix fix);

    Status check_portfile_deprecated_functions(Filesystem& fs, SourceControlFileAndLocation& scf, Fix fix);
}
