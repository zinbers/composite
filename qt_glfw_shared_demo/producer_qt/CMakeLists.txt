cmake_minimum_required(VERSION 3.20)

find_package(Qt6 REQUIRED COMPONENTS Core Gui OpenGL Quick Qml)

qt_add_executable(producer_qt
    src/main.cpp
    src/qml_offscreen_renderer.cpp
    src/shared_texture_publisher.cpp
)

# Embed the QML file as a Qt resource (accessible as qrc:/qml/Main.qml)
qt_add_resources(producer_qt "qml_res"
    PREFIX "/"
    FILES
        qml/Main.qml
)

target_include_directories(producer_qt PRIVATE src)

target_link_libraries(producer_qt
    PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::OpenGL
        Qt6::Quick
        Qt6::Qml
        common
)
