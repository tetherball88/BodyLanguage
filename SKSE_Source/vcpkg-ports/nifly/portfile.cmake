vcpkg_check_linkage(ONLY_STATIC_LIBRARY)

vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO ousnius/nifly
    REF 6cb15fb5da71527b33cea8ff7913be8790e1c633
    SHA512 011ea51d938ca99f9d4e19f33509fa10456cc95a87693812813184147955c370c1eed9823e551096afbd9e74423f173dd2fe28426ba8259d57e76e6be9fa3418
    HEAD_REF main
)

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
    -DBUILD_TESTING=OFF
)
vcpkg_cmake_install()

file(REMOVE_RECURSE
    "${CURRENT_PACKAGES_DIR}/debug/include"
    "${CURRENT_PACKAGES_DIR}/debug/share"
    "${CURRENT_PACKAGES_DIR}/cmake"
)

# Write a clean CMake config that does NOT propagate /EH flags
file(WRITE "${CURRENT_PACKAGES_DIR}/share/${PORT}/niflyConfig.cmake"
    "include(\"\${CMAKE_CURRENT_LIST_DIR}/nifly-targets.cmake\")\n")

file(WRITE "${CURRENT_PACKAGES_DIR}/share/${PORT}/nifly-targets.cmake" [=[
get_filename_component(PACKAGE_PREFIX_DIR "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

if(NOT TARGET nifly)
    add_library(nifly STATIC IMPORTED)
    set_target_properties(nifly PROPERTIES
        IMPORTED_CONFIGURATIONS RELEASE
        IMPORTED_LOCATION_RELEASE "${PACKAGE_PREFIX_DIR}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}nifly${CMAKE_STATIC_LIBRARY_SUFFIX}"
        INTERFACE_INCLUDE_DIRECTORIES "${PACKAGE_PREFIX_DIR}/include/nifly;${PACKAGE_PREFIX_DIR}/include"
        INTERFACE_COMPILE_FEATURES "cxx_std_17"
    )
endif()
]=])

vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")
