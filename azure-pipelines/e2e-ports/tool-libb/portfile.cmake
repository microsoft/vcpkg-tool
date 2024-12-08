set(VCPKG_POLICY_EMPTY_PACKAGE enabled)

if(NOT DEFINED _HOST_TRIPLET)
    message(FATAL_ERROR "tool-libb requires _HOST_TRIPLET to be defined")
endif()

get_filename_component(base_install_dir "${CURRENT_INSTALLED_DIR}" DIRECTORY)

# This is needed to cut the requirement for an atomic change across both Microsoft/vcpkg and Microsoft/vcpkg-tool
if(DEFINED CURRENT_HOST_INSTALLED_DIR AND NOT CURRENT_HOST_INSTALLED_DIR STREQUAL "${base_install_dir}/${_HOST_TRIPLET}")
    message(FATAL_ERROR "tool-libb requires CURRENT_HOST_INSTALLED_DIR to be defined to \"${base_install_dir}/${_HOST_TRIPLET}\"")
endif()

set(CURRENT_HOST_INSTALLED_DIR "${base_install_dir}/${_HOST_TRIPLET}")

if(NOT EXISTS "${CURRENT_HOST_INSTALLED_DIR}/share/tool-manifest")
    message(FATAL_ERROR "tool-libb requires tool-manifest on the host (${CURRENT_HOST_INSTALLED_DIR}/share/tool-manifest)")
endif()
if(NOT EXISTS "${CURRENT_HOST_INSTALLED_DIR}/share/tool-control")
    message(FATAL_ERROR "tool-libb requires tool-control on the host (${CURRENT_HOST_INSTALLED_DIR}/share/tool-control)")
endif()
