cmake_minimum_required(VERSION 3.16)

project(RusticCppIDE LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 QUIET COMPONENTS Widgets)
if(Qt6_FOUND)
    set(QT_LIB Qt6::Widgets)
    set(QT_VERSION_MAJOR 6)
else()
    find_package(Qt5 REQUIRED COMPONENTS Widgets)
    set(QT_LIB Qt5::Widgets)
    set(QT_VERSION_MAJOR 5)
endif()

set(SOURCES
    src/main.cpp
    src/MainWindow.cpp
    src/CodeEditor.cpp
    src/CppRusticHighlighter.cpp
    src/BuildManager.cpp
    src/ProjectManager.cpp
    src/LspClient.cpp
    src/GdbMiClient.cpp
    src/FindReplaceDialog.cpp
    src/ProjectSettingsDialog.cpp
    src/ShortcutSettingsDialog.cpp
)

set(HEADERS
    src/MainWindow.h
    src/CodeEditor.h
    src/CppRusticHighlighter.h
    src/BuildManager.h
    src/ProjectManager.h
    src/LspClient.h
    src/GdbMiClient.h
    src/FindReplaceDialog.h
    src/ProjectSettingsDialog.h
    src/ShortcutSettingsDialog.h
)

add_executable(${PROJECT_NAME} ${SOURCES} ${HEADERS})

target_include_directories(${PROJECT_NAME} PRIVATE src)
target_link_libraries(${PROJECT_NAME} PRIVATE ${QT_LIB})
