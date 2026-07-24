cmake_minimum_required(VERSION 3.16)
project(minnow VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

include(GNUInstallDirs)

find_package(Qt6 REQUIRED COMPONENTS Widgets)
find_package(KF6CoreAddons REQUIRED)
find_package(KF6ConfigWidgets REQUIRED)
find_package(KF6WidgetsAddons REQUIRED)
find_package(KF6KIO REQUIRED)

add_library(minnow_core STATIC
    src/PathUtils.cpp
    src/PathUtils.h
    src/FileOperations.cpp
    src/FileOperations.h
)

target_link_libraries(minnow_core PUBLIC
    Qt6::Widgets
    KF6::CoreAddons
    KF6::ConfigWidgets
    KF6::WidgetsAddons
    KF6::KIOCore
    KF6::KIOWidgets
    KF6::KIOFileWidgets
    KF6::KIOGui
)
target_include_directories(minnow_core PUBLIC src)

add_executable(minnow
    src/main.cpp
    src/MainWindow.cpp
    src/MainWindow.h
    src/PlacesSidebar.cpp
    src/PlacesSidebar.h
)

target_link_libraries(minnow PRIVATE minnow_core)

install(TARGETS minnow DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES data/minnow.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES data/net.minnow.Minnow.metainfo.xml DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo)
install(FILES data/icons/minnow.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)

option(BUILD_TESTING "Build automated tests" ON)
if(BUILD_TESTING)
    enable_testing()
    find_package(Qt6 REQUIRED COMPONENTS Test)

    add_executable(minnow_tests tests/test_pathutils.cpp)
    target_link_libraries(minnow_tests PRIVATE minnow_core Qt6::Test)
    add_test(NAME minnow_tests COMMAND minnow_tests)
endif()
