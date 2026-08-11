cmake_minimum_required(VERSION 3.19)
project(HFADM LANGUAGES CXX)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Widgets Sql Pdf PdfWidgets LinguistTools Svg Network)

qt_standard_project_setup()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

qt_add_executable(HFADM
    WIN32 MACOSX_BUNDLE
    main.cpp
    mainwindow.cpp
    mainwindow.h
    mainwindow.ui
    $<$<BOOL:${WIN32}>:${CMAKE_CURRENT_BINARY_DIR}/icon.rc>
    database/databasemanager.cpp
    database/databasemanager.h
    database/drawingrepository.cpp
    database/drawingrepository.h
    service/projectservice.cpp
    service/projectservice.h
    service/nodeservice.cpp
    service/nodeservice.h
    service/drawingservice.cpp
    service/drawingservice.h
    service/remoteprotocol.cpp
    service/remoteprotocol.h
    service/pinyin.cpp
    service/pinyin.h
    service/remoteserver.cpp
    service/remoteserver.h
    service/remoteclient.cpp
    service/remoteclient.h
    service/crypto.cpp
    service/crypto.h
    service/clientcredentialstore.cpp
    service/clientcredentialstore.h
    service/appconfig.cpp
    service/appconfig.h
    model/hfdadnode.h
    model/projectinfo.h
    model/part.h
    model/drawing.h
    model/remotedevice.h
    ui/nodetablemodel.cpp
    ui/nodetablemodel.h
    ui/pdftabviewer.cpp
    ui/pdftabviewer.h
    ui/dialogs.cpp
    ui/dialogs.h
    ui/recentprojectsmenu.cpp
    ui/recentprojectsmenu.h
    ui/tabmanager.cpp
    ui/tabmanager.h
    ui/browsertabbar.cpp
    ui/browsertabbar.h
    ui/browsertabwidget.cpp
    ui/browsertabwidget.h
    ui/sessionmanager.cpp
    ui/sessionmanager.h
    ui/welcomepage.cpp
    ui/welcomepage.h
    ui/contextmenus.cpp
    ui/contextmenus.h
    ui/directorynavigator.cpp
    ui/directorynavigator.h
    ui/directoryassembler.cpp
    ui/directoryassembler.h
    ui/clipboard.cpp
    ui/clipboard.h
    ui/drawingoperations.cpp
    ui/drawingoperations.h
    ui/nodeoperations.cpp
    ui/nodeoperations.h
    ui/deleteprogressdialog.cpp
    ui/deleteprogressdialog.h
    ui/parteditordialog.cpp
    ui/parteditordialog.h
    ui/pdfpreviewdialog.cpp
    ui/pdfpreviewdialog.h
    ui/dropimportdialog.cpp
    ui/dropimportdialog.h
    ui/remotedialog.cpp
    ui/remotedialog.h
    ui/remoteoperations.cpp
    ui/remoteoperations.h
    ui/remoteparteditordialog.cpp
    ui/remoteparteditordialog.h
    ui/remarkdelegate.cpp
    ui/remarkdelegate.h
    ui/pairingdialog.cpp
    ui/pairingdialog.h
    ui/pinpromptdialog.cpp
    ui/pinpromptdialog.h
    ui/pinlabel.cpp
    ui/pinlabel.h
    ui/devicemanagerdialog.cpp
    ui/devicemanagerdialog.h
)

qt_add_translations(
    TARGETS HFADM
    TS_FILES HFADM_zh_CN.ts
)

# 欢迎页资源：背景图 + 三个操作按钮图标（挂载到 :/assets/welcome/ 前缀）
qt_add_resources(HFADM "welcome_assets"
    PREFIX "/assets/welcome"
    BASE "assets"
    FILES
        assets/welcome-page-background.jpg
        assets/welcome-page-create-project-icon.svg
        assets/welcome-page-open-project-icon.svg
        assets/welcome-page-connect_remote_icon.svg
)

# 工具栏图标（挂载到 :/assets/Icons/ 前缀，供 setupToolbarIcons 加载；icon.svg 兼作应用/窗口图标）
qt_add_resources(HFADM "toolbar_icons"
    PREFIX "/assets/Icons"
    BASE "assets/Icons"
    FILES
        assets/Icons/arrow-left.svg
        assets/Icons/arrow-right.svg
        assets/Icons/arrow-up.svg
        assets/Icons/refresh.svg
        assets/Icons/home.svg
        assets/Icons/new.svg
        assets/Icons/icon.svg
)

# Windows 可执行文件图标：icon.rc.in -> configure_file 注入绝对路径 -> windres 编译进 exe
if(WIN32)
    set(ICON_PATH "${CMAKE_CURRENT_SOURCE_DIR}/assets/Icons/icon.ico")
    configure_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/assets/Icons/icon.rc.in
        ${CMAKE_CURRENT_BINARY_DIR}/icon.rc
        @ONLY
    )
endif()

target_link_libraries(HFADM
    PRIVATE
        Qt::Core
        Qt::Widgets
        Qt::Sql
        Qt::Pdf
        Qt::PdfWidgets
        Qt::Svg
        Qt::Network
)

# Windows：AES-128-GCM 走 BCrypt/CNG 原生加密 API
if(WIN32)
    target_link_libraries(HFADM PRIVATE bcrypt)
endif()

target_include_directories(HFADM
    PRIVATE
        ${CMAKE_SOURCE_DIR}
)

install(TARGETS HFADM
    BUNDLE  DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

qt_generate_deploy_app_script(
    TARGET HFADM
    OUTPUT_SCRIPT deploy_script
    NO_UNSUPPORTED_PLATFORM_ERROR
)
install(SCRIPT ${deploy_script})
