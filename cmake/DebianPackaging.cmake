# Native Debian amd64 runtime package. Source builds remain available elsewhere.
find_program(SR_DPKG_EXECUTABLE dpkg)
find_program(SR_DPKG_SHLIBDEPS_EXECUTABLE dpkg-shlibdeps)
if(NOT SR_DPKG_EXECUTABLE OR NOT SR_DPKG_SHLIBDEPS_EXECUTABLE)
  message(STATUS "DEB packaging unavailable: install dpkg-dev")
  return()
endif()
execute_process(COMMAND "${SR_DPKG_EXECUTABLE}" --print-architecture
  OUTPUT_VARIABLE SR_DEB_ARCH OUTPUT_STRIP_TRAILING_WHITESPACE
  RESULT_VARIABLE SR_DEB_ARCH_RESULT)
if(NOT SR_DEB_ARCH_RESULT EQUAL 0 OR NOT SR_DEB_ARCH STREQUAL "amd64"
    OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8
    OR NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64|AMD64)$"
    OR CMAKE_CROSSCOMPILING)
  message(STATUS "DEB packaging is supported only for native Linux amd64 builds")
  return()
endif()

install(FILES README README.md METADATA-RULES.md
  CHANGELOG.md PORTING.md THIRD-PARTY.md VERSION COPYING
  DESTINATION share/doc/streamripper-ng COMPONENT Runtime)
install(FILES rules-examples.txt
  DESTINATION share/doc/streamripper-ng/examples COMPONENT Runtime)
install(FILES packaging/debian/copyright
  DESTINATION share/doc/streamripper-ng COMPONENT Runtime)

set(CPACK_GENERATOR DEB)
set(CPACK_PACKAGE_NAME streamripper-ng)
set(CPACK_PACKAGE_VENDOR Ashus)
set(CPACK_PACKAGE_VERSION "${SR_RELEASE_VERSION}")
set(CPACK_PACKAGE_CONTACT "Ashus <github.com@ashus.net>")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/Ashus/streamripper")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Record Internet radio streams with native HTTPS")
set(CPACK_PACKAGE_DESCRIPTION
  "Independent Streamripper CLI fork with verified HTTPS, IPv6, metadata filtering, AAC silence detection and whole-stream HLS/FLAC recording.")
set(CPACK_PACKAGING_INSTALL_PREFIX /usr)
set(CPACK_DEB_COMPONENT_INSTALL ON)
set(CPACK_COMPONENTS_ALL Runtime)
set(CPACK_COMPONENT_RUNTIME_DESCRIPTION
  "Independent CLI fork with verified HTTPS and IPv6. Records MP3, AAC, Ogg Vorbis, native/Ogg FLAC and supported HLS streams, with metadata filtering and AAC silence detection.")
set(CPACK_DEBIAN_RUNTIME_PACKAGE_NAME streamripper-ng)
set(CPACK_DEBIAN_RUNTIME_PACKAGE_CONFLICTS streamripper)
set(CPACK_DEBIAN_RUNTIME_PACKAGE_REPLACES streamripper)
set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE amd64)
set(CPACK_DEBIAN_PACKAGE_RELEASE 1)
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
set(CPACK_DEBIAN_PACKAGE_SECTION sound)
set(CPACK_DEBIAN_PACKAGE_PRIORITY optional)
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
set(CPACK_DEBIAN_RUNTIME_PACKAGE_DEPENDS ca-certificates)
set(CPACK_DEBIAN_COMPRESSION_TYPE xz)
set(CPACK_STRIP_FILES ON)
set(CPACK_INSTALL_DEFAULT_DIRECTORY_PERMISSIONS
  OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
set(CPACK_PACKAGE_DIRECTORY "${CMAKE_BINARY_DIR}/packages")
include(CPack)
