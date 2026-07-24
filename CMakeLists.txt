cmake_minimum_required(VERSION 3.16)
project(minnow VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets)
find_package(KF6CoreAddons REQUIRED)
find_package(KF6ConfigWidgets REQUIRED)
find_package(KF6WidgetsAddons REQUIRED)
find_package(KF6KIO REQUIRED)

add_executable(minnow
    src/main.cpp
    src/MainWindow.cpp
    src/MainWindow.h
    src/PlacesSidebar.cpp
    src/PlacesSidebar.h
    src/FileOperations.cpp
    src/FileOperations.h
)

target_link_libraries(minnow PRIVATE
    Qt6::Widgets
    KF6::CoreAddons
    KF6::ConfigWidgets
    KF6::WidgetsAddons
    KF6::KIOCore
    KF6::KIOWidgets
    KF6::KIOFileWidgets
    KF6::KIOGui
)

install(TARGETS minnow DESTINATION bin)
