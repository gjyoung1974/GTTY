add_executable(gtty
  main.cpp
  settings.cpp
  waterfall.cpp
  xyscope.cpp
  main_window.cpp
  prefs_dialog.cpp
)
target_link_libraries(gtty PRIVATE gttycore PkgConfig::GTK4 PkgConfig::ADW)
install(TARGETS gtty RUNTIME DESTINATION bin)
install(FILES ${CMAKE_SOURCE_DIR}/data/io.github.gtty.desktop DESTINATION share/applications)
install(FILES ${CMAKE_SOURCE_DIR}/data/io.github.gtty.svg DESTINATION share/icons/hicolor/scalable/apps)
# PNG rendering of the SVG: AppImage .DirIcon must be a PNG, and some launchers want a bitmap.
install(FILES ${CMAKE_SOURCE_DIR}/data/io.github.gtty.png DESTINATION share/icons/hicolor/256x256/apps)
install(FILES ${GTTY_GENERATED_DIR}/io.github.gtty.metainfo.xml DESTINATION share/metainfo)
