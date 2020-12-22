include_directories(${CMAKE_SOURCE_DIR}/common)

set(NETWORK_SOURCES
  Socket.cxx
  TcpSocket.cxx
  websocket.c
  websockify.c)

if(NOT WIN32)
  set(NETWORK_SOURCES ${NETWORK_SOURCES} UnixSocket.cxx)
endif()

add_library(network STATIC ${NETWORK_SOURCES})

if(WIN32)
  find_package(pthreads REQUIRED)
  find_path(DIRENT_INCLUDE_DIRS "dirent.h")
  find_path(GETOPT_INCLUDE_DIRS "getopt.h")

  target_include_directories(network PRIVATE ${DIRENT_INCLUDE_DIRS} ${GETOPT_INCLUDE_DIRS})
  target_link_libraries(network ws2_32)
  target_link_libraries(network  OpenSSL::SSL OpenSSL::Crypto)
  target_link_libraries(network PThreads4W::PThreads4W)

endif()

if(UNIX)
  libtool_create_control_file(network)
endif()
