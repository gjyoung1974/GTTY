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
