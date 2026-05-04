#include "settings_dialog.h"

#include <QApplication>
#include <QMainWindow>
#include <QMenuBar>
#include <QMessageBox>
#include <QSettings>
#include <QStatusBar>

static search::DbSettings loadDbSettings() {
    QSettings ini("result_search.ini", QSettings::IniFormat);
    search::DbSettings s;
    s.server = ini.value("Database/Server").toString().toStdWString();
    s.initial_database = ini.value("Database/InitialDatabase").toString().toStdWString();
    s.user = ini.value("Database/User").toString().toStdWString();
    s.password = ini.value("Database/Password").toString().toStdWString();
    return s;
}

static void saveDbSettings(const search::DbSettings& s) {
    QSettings ini("result_search.ini", QSettings::IniFormat);
    ini.setValue("Database/Server", QString::fromStdWString(s.server));
    ini.setValue("Database/InitialDatabase", QString::fromStdWString(s.initial_database));
    ini.setValue("Database/User", QString::fromStdWString(s.user));
    ini.setValue("Database/Password", QString::fromStdWString(s.password));
}

static int loadFontSize() {
    QSettings ini("result_search.ini", QSettings::IniFormat);
    return ini.value("UI/FontSize", 9).toInt();
}

static void saveFontSize(int size) {
    QSettings ini("result_search.ini", QSettings::IniFormat);
    ini.setValue("UI/FontSize", size);
}

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName("ResultSearch");
    app.setApplicationDisplayName(QString::fromWCharArray(L"检验结果查询"));

    QMainWindow window;
    window.setWindowTitle(QString::fromWCharArray(L"检验结果查询 — Qt 5.15"));
    window.resize(1100, 700);
    window.statusBar()->showMessage(QString::fromWCharArray(L"就绪"));

    auto* settingsMenu = window.menuBar()->addMenu(QString::fromWCharArray(L"文件"));
    auto* settingsAction = settingsMenu->addAction(QString::fromWCharArray(L"数据库设置..."));
    QObject::connect(settingsAction, &QAction::triggered, [&window]() {
        SettingsDialog dlg(loadDbSettings(), loadFontSize(), &window);
        if (dlg.exec() == QDialog::Accepted) {
            saveDbSettings(dlg.dbSettings());
            saveFontSize(dlg.fontSize());
            window.statusBar()->showMessage(QString::fromWCharArray(L"设置已保存"), 3000);
        }
    });

    settingsMenu->addSeparator();
    auto* exitAction = settingsMenu->addAction(QString::fromWCharArray(L"退出"));
    QObject::connect(exitAction, &QAction::quit, &app, &QApplication::quit);

    window.show();
    return app.exec();
}
