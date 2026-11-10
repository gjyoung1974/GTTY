add_executable(gtty
  main.cpp
  settings.cpp
  waterfall.cpp
  xyscope.cpp
  main_window.cpp
  prefs_dialog.cpp
)
target_link_libraries(gtty PRIVATE gttycore PkgConfig::GTK4 PkgConfig::ADW)
target_compile_definitions(gtty PRIVATE GTTY_VERSION="${PROJECT_VERSION}")
install(TARGETS gtty RUNTIME DESTINATION bin)
install(FILES ${CMAKE_SOURCE_DIR}/data/io.github.gtty.desktop DESTINATION share/applications)
install(FILES ${CMAKE_SOURCE_DIR}/data/io.github.gtty.svg DESTINATION share/icons/hicolor/scalable/apps)
install(FILES ${CMAKE_SOURCE_DIR}/data/io.github.gtty.metainfo.xml DESTINATION share/metainfo)
