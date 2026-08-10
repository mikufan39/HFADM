#include "mainwindow.h"

#include <QApplication>
#include <QIcon>
#include <QLocale>
#include <QLocalServer>
#include <QLocalSocket>
#include <QThread>
#include <QTranslator>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    // 应用/窗口图标：使用 assets/Icons/icon.svg（Qt::Svg 已链接，可直接加载 SVG）
    a.setWindowIcon(QIcon(QStringLiteral(":/assets/Icons/icon.svg")));

    // 单实例：已有实例在运行时通知其置前并退出本次启动。
    // 首实例正在启动（监听尚未就绪）时短暂重试；残留命名（异常退出）清理后继续。
    QLocalSocket socket;
    for (int attempt = 0; attempt < 3; ++attempt) {
        socket.connectToServer(QLatin1String(kSingleInstanceKey));
        if (socket.waitForConnected(500)) {
            socket.write("show");
            socket.flush();
            socket.waitForBytesWritten(300);
            return 0;
        }
        socket.abort();
        if (attempt == 0) {
            QLocalServer::removeServer(QLatin1String(kSingleInstanceKey));
        }
        QThread::msleep(200);
    }

    QTranslator translator;
    const QStringList uiLanguages = QLocale::system().uiLanguages();
    for (const QString &locale : uiLanguages) {
        const QString baseName = "HFADM_" + QLocale(locale).name();
        if (translator.load(":/i18n/" + baseName)) {
            a.installTranslator(&translator);
            break;
        }
    }
    MainWindow w;
    w.show();
    return QApplication::exec();
}
