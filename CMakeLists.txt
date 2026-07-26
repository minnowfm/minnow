cmake_minimum_required(VERSION 3.16)
project(minnow VERSION 0.1.1 LANGUAGES CXX)

# The one place the app's own version is set for local/dev builds. Release CI overrides
# this from the pushed tag instead of requiring every packaging manifest to be hand-bumped
# in lockstep - see MINNOW_APP_VERSION usage in release.yml and the packaging/ build scripts.
set(MINNOW_APP_VERSION "${PROJECT_VERSION}" CACHE STRING "Application version string reported by the app")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

include(GNUInstallDirs)

find_package(Qt6 REQUIRED COMPONENTS Widgets Concurrent)
find_package(KF6CoreAddons REQUIRED)
find_package(KF6ConfigWidgets REQUIRED)
find_package(KF6WidgetsAddons REQUIRED)
find_package(KF6KIO REQUIRED)
find_package(KF6FileMetaData REQUIRED)
find_package(KF6Archive REQUIRED)
find_package(KF6Notifications REQUIRED)

add_library(minnow_core STATIC
    src/PathUtils.cpp
    src/PathUtils.h
    src/FileOperations.cpp
    src/FileOperations.h
    src/PropertiesDialog.cpp
    src/PropertiesDialog.h
    src/TaskManager.cpp
    src/TaskManager.h
)

target_link_libraries(minnow_core PUBLIC
    Qt6::Widgets
    Qt6::Concurrent
    KF6::CoreAddons
    KF6::ConfigWidgets
    KF6::WidgetsAddons
    KF6::KIOCore
    KF6::KIOWidgets
    KF6::KIOFileWidgets
    KF6::KIOGui
    KF6::FileMetaData
    KF6::Archive
    KF6::Notifications
)
target_include_directories(minnow_core PUBLIC src)

# KFileMetaData::Property::{VideoCodec,AudioCodec,PixelFormat,ColorSpace} only exist since
# 6.13 - older KFileMetaData (e.g. the one bundled by the org.kde.Sdk 6.7 Flatpak runtime)
# doesn't have them. Gate their use instead of hard requiring a newer KFileMetaData than
# some build environments ship.
if(KF6FileMetaData_VERSION VERSION_GREATER_EQUAL 6.13.0)
    target_compile_definitions(minnow_core PUBLIC MINNOW_HAVE_KFILEMETADATA_CODEC_PROPS)
endif()

add_executable(minnow
    src/main.cpp
    src/MainWindow.cpp
    src/MainWindow.h
    src/PlacesSidebar.cpp
    src/PlacesSidebar.h
    src/BrowserTab.cpp
    src/BrowserTab.h
    src/PathBar.cpp
    src/PathBar.h
    src/TabBar.cpp
    src/TabBar.h
    src/ThumbnailProxyModel.cpp
    src/ThumbnailProxyModel.h
    src/SettingsTab.cpp
    src/SettingsTab.h
    src/TaskListWidget.cpp
    src/TaskListWidget.h
    src/IndeterminateBar.cpp
    src/IndeterminateBar.h
    src/TaskProgressPopup.cpp
    src/TaskProgressPopup.h
    src/ActivityTab.cpp
    src/ActivityTab.h
    src/resources.qrc
)

target_link_libraries(minnow PRIVATE minnow_core)
target_compile_definitions(minnow PRIVATE MINNOW_VERSION="${MINNOW_APP_VERSION}")

# KIO::FileUndoManager::redo() only exists since KF6 6.17 - older KF6 (still common on e.g.
# Fedora 40's default repos) only has undo(). Gate the Redo shortcut on it instead of hard
# requiring a KIO version newer than most distros currently ship.
if(KF6KIO_VERSION VERSION_GREATER_EQUAL 6.17.0)
    target_compile_definitions(minnow PRIVATE MINNOW_HAVE_KIO_REDO)
endif()

install(TARGETS minnow DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES data/io.github.minnowfm.Minnow.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES data/io.github.minnowfm.Minnow.metainfo.xml DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo)
install(FILES data/icons/io.github.minnowfm.Minnow.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
foreach(_icon_size 16 22 24 32 48 64 128)
    install(FILES data/icons/hicolor/${_icon_size}x${_icon_size}/apps/io.github.minnowfm.Minnow.png
            DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/${_icon_size}x${_icon_size}/apps)
endforeach()

option(BUILD_TESTING "Build automated tests" ON)
if(BUILD_TESTING)
    enable_testing()
    find_package(Qt6 REQUIRED COMPONENTS Test)

    add_executable(minnow_tests tests/test_pathutils.cpp)
    target_link_libraries(minnow_tests PRIVATE minnow_core Qt6::Test)
    add_test(NAME minnow_tests COMMAND minnow_tests)
endif()
