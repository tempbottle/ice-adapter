cmake_minimum_required(VERSION 3.5)

project(faf-ice-testclient
  LANGUAGES CXX
  )

find_package(Boost REQUIRED COMPONENTS log program_options thread system date_time)
include_directories(${Boost_INCLUDE_DIRS})

if(NOT PKGCONFIG_FOUND)
  find_package(PkgConfig REQUIRED)
endif()
if(NOT (JSON_LIBRARIES AND JSON_INCLUDE_DIRS))
  pkg_search_module(JSON REQUIRED jsoncpp)
endif()
include_directories(${JSON_INCLUDE_DIRS})

include_directories(${CMAKE_CURRENT_SOURCE_DIR})

find_package(Qt5Widgets REQUIRED)
find_package(Qt5Network REQUIRED)
set(CMAKE_AUTOMOC ON)

if(MINGW)
  pkg_check_modules(QT5ALL REQUIRED Qt5Widgets Qt5Gui Qt5Network freetype2)
  set(faf_test_libs
    Qt5::QWindowsIntegrationPlugin
    libQt5FontDatabaseSupport.a
    libQt5ThemeSupport.a
    libQt5EventDispatcherSupport.a
    ${QT5ALL_LDFLAGS}
    )
else()
  set(faf_test_libs
    Qt5::Widgets
    Qt5::Network
    pthread)
endif()

include_directories(${Qt5Widgets_INCLUDE_DIRS})
include_directories(${Qt5Network_INCLUDE_DIRS})

add_executable(faf-ice-testserver
    Testserver.cpp
    JsonRpcProtocol.cpp
    JsonRpcQTcpServer.cpp
    JsonRpcQTcpSocket.cpp
    Socket.cpp
    logging.cpp
)
target_link_libraries(faf-ice-testserver
  ${faf_test_libs}
  ${JSON_LIBRARIES}
  ${Boost_LIBRARIES}
)
set_property(TARGET faf-ice-testserver PROPERTY CXX_STANDARD 11)
set_property(TARGET faf-ice-testserver PROPERTY CXX_STANDARD_REQUIRED ON)
if(NOT MINGW)
  target_compile_definitions(faf-ice-testserver PUBLIC BOOST_LOG_DYN_LINK)
endif()
if (NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
  add_custom_command(TARGET faf-ice-testserver
    POST_BUILD
    COMMAND ${CMAKE_STRIP} -s "$<TARGET_FILE:faf-ice-testserver>"
    COMMENT "Stripping executable $<TARGET_FILE_NAME:faf-ice-testserver>"
    )
endif()


qt5_wrap_ui(FAF_TEST_UIS_H Testclient.ui)
include_directories(${CMAKE_CURRENT_BINARY_DIR})

add_executable(faf-ice-testclient WIN32
    ${FAF_TEST_UIS_H}
    GPGNetClient.cpp
    GPGNetMessage.cpp
    IceCandidate.cpp
    JsonRpcProtocol.cpp
    JsonRpcQTcpServer.cpp
    JsonRpcQTcpSocket.cpp
    logging.cpp
    Pingtracker.cpp
    Socket.cpp
    Testclient.cpp
    Testclient.ui
)
target_link_libraries(faf-ice-testclient
  ${faf_test_libs}
  ${JSON_LIBRARIES}
  ${Boost_LIBRARIES}
)
set_property(TARGET faf-ice-testclient PROPERTY CXX_STANDARD 11)
set_property(TARGET faf-ice-testclient PROPERTY CXX_STANDARD_REQUIRED ON)
if(NOT MINGW)
  target_compile_definitions(faf-ice-testclient PUBLIC BOOST_LOG_DYN_LINK)
endif()
if (NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
  add_custom_command(TARGET faf-ice-testclient
    POST_BUILD
    COMMAND ${CMAKE_STRIP} -s "$<TARGET_FILE:faf-ice-testclient>"
    COMMENT "Stripping executable $<TARGET_FILE_NAME:faf-ice-testclient>"
    )
endif()
