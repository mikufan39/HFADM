cmake_minimum_required(VERSION 3.19)
project(HFADM_CORE_TESTS LANGUAGES CXX)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Sql Widgets Network Pdf PdfWidgets)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

qt_standard_project_setup()

qt_add_executable(core_tests
    test_core.cpp
    ../database/databasemanager.cpp
    ../database/databasemanager.h
    ../database/drawingrepository.cpp
    ../database/drawingrepository.h
    ../service/nodeservice.cpp
    ../service/nodeservice.h
    ../service/projectservice.cpp
    ../service/projectservice.h
    ../service/drawingservice.cpp
    ../service/drawingservice.h
    ../model/hfdadnode.h
    ../model/part.h
    ../model/drawing.h
    ../model/deletionplan.h
    ../model/projectinfo.h
)

target_link_libraries(core_tests PRIVATE Qt::Core Qt::Sql)
target_include_directories(core_tests PRIVATE ..)

# 远程协议端到端测试（本机回环：服务端开放 → 客户端连接 → 增删改查全流程）
qt_add_executable(remote_tests
    test_remote.cpp
    ../database/databasemanager.cpp
    ../database/databasemanager.h
    ../database/drawingrepository.cpp
    ../database/drawingrepository.h
    ../service/nodeservice.cpp
    ../service/nodeservice.h
    ../service/projectservice.cpp
    ../service/projectservice.h
    ../service/drawingservice.cpp
    ../service/drawingservice.h
    ../service/remoteprotocol.cpp
    ../service/remoteprotocol.h
    ../service/remoteserver.cpp
    ../service/remoteserver.h
    ../service/remoteclient.cpp
    ../service/remoteclient.h
    ../service/crypto.cpp
    ../service/crypto.h
    ../service/clientcredentialstore.cpp
    ../service/clientcredentialstore.h
    ../ui/nodetablemodel.cpp
    ../ui/nodetablemodel.h
    ../ui/tabmanager.cpp
    ../ui/tabmanager.h
    ../ui/pdftabviewer.cpp
    ../ui/pdftabviewer.h
    ../ui/directoryassembler.cpp
    ../ui/directoryassembler.h
    ../model/hfdadnode.h
    ../model/part.h
    ../model/component.h
    ../model/drawing.h
    ../model/deletionplan.h
    ../model/projectinfo.h
    ../model/remotedevice.h
)

target_link_libraries(remote_tests PRIVATE Qt::Core Qt::Sql Qt::Widgets Qt::Network Qt::Pdf Qt::PdfWidgets)
target_include_directories(remote_tests PRIVATE ..)

# Windows：AES-128-GCM 走 BCrypt/CNG 原生加密 API
if(WIN32)
    target_link_libraries(remote_tests PRIVATE bcrypt)
endif()
