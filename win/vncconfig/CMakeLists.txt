include_directories(${CMAKE_BINARY_DIR}/win)

add_executable(vncconfig WIN32
  Legacy.cxx
  PasswordDialog.cxx
  vncconfig.cxx
  vncconfig.rc)

find_package(pthreads REQUIRED)
find_package(webp REQUIRED)

target_link_libraries(vncconfig PThreads4W::PThreads4W)
target_link_libraries(vncconfig ${WEBP_LIBRARIES})
target_link_libraries(vncconfig rfb_win32 rfb network rdr ws2_32.lib)

install(TARGETS vncconfig
  RUNTIME DESTINATION ${BIN_DIR}
)

# pthreads DLL are not copied in Debug mode...
add_custom_command(TARGET vncconfig POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy ${PThreads4W_DLL_DIR}/pthreadVC3.dll ${CMAKE_CURRENT_BINARY_DIR}/Debug/pthreadVC3.dll.
    COMMENT "Copied ${CMAKE_CURRENT_BINARY_DIR}/Debug/pthreadVC3.dll"
)