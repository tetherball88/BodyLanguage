vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO alandtse/CommonLibSSE-NG
    REF 12d61f3c39f7d4ba1b1ab543df2bca0e613dc747
    SHA512 3c8dd8991e1a39c55c25fa06bca53a79349563227a210558d4edd7e8950c8cd7b8ee07afd306f6658bf4667fb47cb6f5e955e930f0e32ee47388a5f13af22b55
    HEAD_REF ng
)
vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH2
    REPO ValveSoftware/openvr
    REF 60eb187801956ad277f1cae6680e3a410ee0873b
    SHA512 bb85b4705e7095ac65df9969112b2df8930cee7917cc5f14231c5a0ffeed7a73ffa60727fd32f8786a403656f95a3ec0f80bf3ceabc5b8ede964aefb920bc718
    HEAD_REF master
)

file(GLOB OPENVR_FILES "${SOURCE_PATH2}/*")

# CommonLib's patch-safety option compiles MinHook's three-file hde64 decoder.
# vcpkg configures ports with FetchContent disconnected, so provide that source
# explicitly instead of allowing CommonLib's nested FetchContent download.
vcpkg_from_github(
    OUT_SOURCE_PATH HDE64_SOURCE_PATH
    REPO TsudaKageyu/minhook
    REF v1.3.4
    SHA512 8a33233598b56ad9da44d22d470c2432f68364dac31bc719fcd6b085e681fa10ddd41865fbde056ee7f4e7a075cc135344b6bf444eadbd7e7314ee1bedfd89b5
    HEAD_REF master
)

# CommonLib's build expects the OpenVR submodule to exist at extern/openvr in the
# source tree (CMakeLists.txt adds ${CMAKE_CURRENT_SOURCE_DIR}/extern/openvr/headers).
# vcpkg only fetched the standalone openvr repo into SOURCE_PATH2, so copy it into the
# CommonLib checkout before configure runs. This is what avoids the missing openvr.h error.
file(MAKE_DIRECTORY "${SOURCE_PATH}/extern/openvr")
file(COPY "${SOURCE_PATH2}/headers" "${SOURCE_PATH2}/lib"
    DESTINATION "${SOURCE_PATH}/extern/openvr")

vcpkg_configure_cmake(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
    -DBUILD_TESTS=off
    -DSKSE_SUPPORT_XBYAK=off
    -DSKSE_SUPPORT_PATCH_SAFETY=on
    -DENABLE_SKYRIM_SE=on
    -DENABLE_SKYRIM_AE=on
    -DENABLE_SKYRIM_VR=on
    "-DFETCHCONTENT_SOURCE_DIR_HDE64=${HDE64_SOURCE_PATH}"
)

vcpkg_install_cmake()
vcpkg_cmake_config_fixup(PACKAGE_NAME CommonLibSSE CONFIG_PATH lib/cmake/CommonLibSSE)

# CommonLibSSEConfig.cmake includes this helper module (add_commonlibsse_plugin etc.)
file(INSTALL "${SOURCE_PATH2}/headers/openvr.h" DESTINATION ${CURRENT_PACKAGES_DIR}/include)
file(INSTALL "${SOURCE_PATH}/cmake/CommonLibSSE.cmake" DESTINATION "${CURRENT_PACKAGES_DIR}/share/CommonLibSSE")

# Install openvr_api.lib so its path in the exported targets file is relocatable
file(INSTALL "${SOURCE_PATH2}/lib/win64/openvr_api.lib" DESTINATION "${CURRENT_PACKAGES_DIR}/lib")

# Patch the absolute buildtrees path to openvr_api.lib that CMake bakes into the exported targets
set(_openvr_repl [[:${_IMPORT_PREFIX}/lib/openvr_api.lib]])
file(READ "${CURRENT_PACKAGES_DIR}/share/CommonLibSSE/CommonLibSSE-targets.cmake" _targets)
string(REGEX REPLACE
    ":[A-Za-z]:[^;\"<>]*/openvr_api\\.lib"
    "${_openvr_repl}"
    _targets "${_targets}")
file(WRITE "${CURRENT_PACKAGES_DIR}/share/CommonLibSSE/CommonLibSSE-targets.cmake" "${_targets}")

file(REMOVE_RECURSE
    "${CURRENT_PACKAGES_DIR}/debug/include"
    "${CURRENT_PACKAGES_DIR}/debug/share"
)

vcpkg_install_copyright(
    FILE_LIST
    "${SOURCE_PATH}/COPYING.txt"
    "${SOURCE_PATH}/EXCEPTIONS.md"
    "${HDE64_SOURCE_PATH}/LICENSE.txt"
    "${SOURCE_PATH2}/LICENSE"
)
