add_executable(gtty-cli gtty_cli.cpp)
target_link_libraries(gtty-cli PRIVATE gttycore)
install(TARGETS gtty-cli RUNTIME DESTINATION bin)
