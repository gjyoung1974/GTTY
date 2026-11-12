foreach(t test_baudot test_modem test_macros test_callsign test_wsjtx test_xmlrpc test_sockets test_cluster test_callbook test_cw test_voter test_codec2 test_skimmer)
  add_executable(${t} ${t}.cpp)
  target_link_libraries(${t} PRIVATE gttycore)
  add_test(NAME ${t} COMMAND ${t})
endforeach()

# Settings/profile storage lives in the GUI tree but needs only GLib.
pkg_check_modules(GLIB IMPORTED_TARGET glib-2.0)
if(GLIB_FOUND)
  add_executable(test_profiles test_profiles.cpp ${CMAKE_SOURCE_DIR}/src/gui/settings.cpp)
  target_include_directories(test_profiles PRIVATE ${CMAKE_SOURCE_DIR}/src/gui)
  target_link_libraries(test_profiles PRIVATE gttycore PkgConfig::GLIB)
  add_test(NAME test_profiles COMMAND test_profiles)
endif()

# gtty driven by Not1MM's own fldigi and rigctld clients (tests/integration).
# Starts a private gtty and a dummy radio, so it is opt-in under ctest:
#   GTTY_INTEGRATION=1 ctest --test-dir build -R not1mm
# and reports "skipped" otherwise, or when Not1MM or a display is missing.
find_package(Python3 COMPONENTS Interpreter)
if(Python3_FOUND AND TARGET gtty)
  add_test(NAME not1mm_integration
           COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/integration/test_not1mm.py)
  set_tests_properties(not1mm_integration PROPERTIES
    SKIP_RETURN_CODE 77 TIMEOUT 400 LABELS integration
    ENVIRONMENT "GTTY_CTEST=1;GTTY_BIN=$<TARGET_FILE:gtty>")
  # gtty driven with TLF's sequence of fldigi calls.
  add_test(NAME tlf_integration
           COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/integration/test_tlf.py)
  set_tests_properties(tlf_integration PROPERTIES
    SKIP_RETURN_CODE 77 TIMEOUT 400 LABELS integration
    ENVIRONMENT "GTTY_CTEST=1;GTTY_BIN=$<TARGET_FILE:gtty>;GTTY_CLI=$<TARGET_FILE:gtty-cli>")
  # The same pairing end to end: Not1MM's real window is driven with keystrokes.
  add_test(NAME not1mm_gui
           COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/integration/test_not1mm_gui.py)
  set_tests_properties(not1mm_gui PROPERTIES
    SKIP_RETURN_CODE 77 TIMEOUT 400 LABELS integration
    ENVIRONMENT "GTTY_CTEST=1;GTTY_BIN=$<TARGET_FILE:gtty>;GTTY_CLI=$<TARGET_FILE:gtty-cli>")
endif()
