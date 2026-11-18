cmake_minimum_required(VERSION 3.20)

# The version comes from git (the tag, or the tag and commit): see cmake/GttyVersion.cmake.
set(GTTY_VERSION_OVERRIDE "" CACHE STRING "Version to report instead of the one git gives (for packagers)")
include(${CMAKE_CURRENT_LIST_DIR}/cmake/GttyVersion.cmake)
gtty_detect_version(${CMAKE_CURRENT_LIST_DIR})
project(gtty VERSION ${GTTY_VERSION_NUMERIC} LANGUAGES C CXX)
message(STATUS "gtty version ${GTTY_VERSION} (package ${GTTY_VERSION_PACKAGE})")

# The files that carry the version are regenerated on every build, so a build made after
# a new commit or tag reports it without re-running CMake by hand.
set(GTTY_GENERATED_DIR ${CMAKE_BINARY_DIR}/generated)
set(GTTY_VERSION_SCRIPT_ARGS -DSOURCE_DIR=${CMAKE_SOURCE_DIR} -DOUT_DIR=${GTTY_GENERATED_DIR}
    -DGTTY_VERSION_OVERRIDE=${GTTY_VERSION_OVERRIDE} -P ${CMAKE_SOURCE_DIR}/cmake/GenerateVersionFiles.cmake)
execute_process(COMMAND ${CMAKE_COMMAND} ${GTTY_VERSION_SCRIPT_ARGS} COMMAND_ERROR_IS_FATAL ANY)
add_custom_target(gtty_version ALL
  COMMAND ${CMAKE_COMMAND} ${GTTY_VERSION_SCRIPT_ARGS}
  BYPRODUCTS ${GTTY_GENERATED_DIR}/gtty_version.h ${GTTY_GENERATED_DIR}/gtty.1 ${GTTY_GENERATED_DIR}/gtty-cli.1
             ${GTTY_GENERATED_DIR}/changelog ${GTTY_GENERATED_DIR}/io.github.gtty.metainfo.xml
  COMMENT "Checking the version in git" VERBATIM)
# The package version below is fixed when CMake runs; re-run it when HEAD moves.
foreach(f HEAD logs/HEAD packed-refs)
  if(EXISTS ${CMAKE_SOURCE_DIR}/.git/${f})
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/.git/${f})
  endif()
endforeach()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()
add_compile_options(-Wall -Wextra -Wno-unused-parameter)
set(CMAKE_INSTALL_DEFAULT_DIRECTORY_PERMISSIONS
    OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)

option(GTTY_BUILD_GUI "Build the GTK4/libadwaita GUI" ON)
option(GTTY_BUILD_TESTS "Build unit tests" ON)

find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)
pkg_check_modules(HAMLIB REQUIRED IMPORTED_TARGET hamlib)
pkg_check_modules(PORTAUDIO REQUIRED IMPORTED_TARGET portaudio-2.0)
pkg_check_modules(FFTW3F REQUIRED IMPORTED_TARGET fftw3f)
pkg_check_modules(SAMPLERATE REQUIRED IMPORTED_TARGET samplerate)
pkg_check_modules(SNDFILE REQUIRED IMPORTED_TARGET sndfile)
pkg_check_modules(SQLITE3 REQUIRED IMPORTED_TARGET sqlite3)
pkg_check_modules(CURL IMPORTED_TARGET libcurl)   # optional: callbook lookups (QRZ, HamQTH)
pkg_check_modules(ALSA IMPORTED_TARGET alsa)      # optional (Linux): what each sound card input/output is (Mic, Line, ...)
pkg_check_modules(CODEC2 IMPORTED_TARGET codec2)  # optional: libcodec2 FSK comparison decoder
if(NOT CODEC2_FOUND)
  message(STATUS "libcodec2 not found; the codec2 comparison decoder will not be built (libcodec2-dev).")
endif()
if(NOT CURL_FOUND)
  message(WARNING "libcurl not found; callbook lookups will be disabled. Install libcurl4-openssl-dev.")
endif()

add_subdirectory(src/core)
add_subdirectory(src/cli)

if(GTTY_BUILD_GUI)
  pkg_check_modules(GTK4 IMPORTED_TARGET gtk4>=4.12)
  pkg_check_modules(ADW IMPORTED_TARGET libadwaita-1>=1.4)
  if(GTK4_FOUND AND ADW_FOUND)
    add_subdirectory(src/gui)
  else()
    message(WARNING "GTK4/libadwaita dev headers not found; GUI will not be built. "
                    "Install libgtk-4-dev libadwaita-1-dev.")
  endif()
endif()

if(GTTY_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

# ------------------------------------------------- docs, man pages, license
find_program(GZIP_EXECUTABLE gzip)
if(GZIP_EXECUTABLE)
  foreach(name changelog gtty.1 gtty-cli.1)   # generated with the version filled in
    add_custom_command(OUTPUT ${CMAKE_BINARY_DIR}/${name}.gz
      COMMAND ${GZIP_EXECUTABLE} -9n -c ${GTTY_GENERATED_DIR}/${name} > ${CMAKE_BINARY_DIR}/${name}.gz
      DEPENDS ${GTTY_GENERATED_DIR}/${name} gtty_version VERBATIM)
    list(APPEND GTTY_GZ_FILES ${CMAKE_BINARY_DIR}/${name}.gz)
  endforeach()
  add_custom_target(gtty_docs ALL DEPENDS ${GTTY_GZ_FILES})
  add_dependencies(gtty_docs gtty_version)
  install(FILES ${CMAKE_BINARY_DIR}/changelog.gz DESTINATION share/doc/gtty)
  install(FILES ${CMAKE_BINARY_DIR}/gtty.1.gz ${CMAKE_BINARY_DIR}/gtty-cli.1.gz DESTINATION share/man/man1)
endif()
install(FILES ${CMAKE_SOURCE_DIR}/packaging/copyright DESTINATION share/doc/gtty)

# ---------------------------------------------------------------- packaging
set(CPACK_PACKAGE_NAME "gtty")
set(CPACK_PACKAGE_VERSION "${GTTY_VERSION_PACKAGE}")   # "0.2.8", or "0.2.8+3.g1a2b3c4" between tags
set(CPACK_PACKAGE_VENDOR "gtty contributors")
set(CPACK_PACKAGE_CONTACT "Gordon Young <gjyoung1974@gmail.com>")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Amateur radio RTTY terminal (GTK4, Hamlib, AFSK/FSK)")
set(CPACK_PACKAGE_DESCRIPTION "RTTY terminal with parallel decoders,
 waterfall tuning, AFSK and FSK keying, Hamlib rig control, logging,
 GridTracker2 and fldigi-compatible XML-RPC integration.")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/gjyoung1974/GTTY")
set(CPACK_RESOURCE_FILE_README "${CMAKE_SOURCE_DIR}/README.md")
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  set(CPACK_GENERATOR "DEB")
else()
  set(CPACK_GENERATOR "TGZ")   # no .deb off Linux; macOS builds install into a prefix
endif()
set(CPACK_DEBIAN_PACKAGE_SECTION "hamradio")
set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)          # dependencies from dpkg-shlibdeps
set(CPACK_DEBIAN_PACKAGE_RECOMMENDS "hamlib-utils, pipewire-audio, pipewire-alsa")  # pipewire-alsa: ALSA "default"/"pipewire" devices on PipeWire desktops
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
set(CPACK_STRIP_FILES ON)
include(CPack)
