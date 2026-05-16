#include "main_window.h"
#include "trend_window.h"

#include <QApplication>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QIcon>
#include <QFile>
#include <QProcessEnvironment>
#include <QScreen>
#include <QSettings>

static void applyEnvOverrides() {
    auto env = QProcessEnvironment::systemEnvironment();
    QString iniPath = QCoreApplication::applicationDirPath() + "/ClientConfig.ini";
    QString legacyIniPath = QCoreApplication::applicationDirPath() + "/result_search.ini";
    if (!QFile::exists(iniPath) && QFile::exists(legacyIniPath)) {
        QFile::copy(legacyIniPath, iniPath);
    }
    QSettings ini(iniPath, QSettings::IniFormat);

    auto setIf = [&](const char* envKey, const char* iniKey) {
        QString v = env.value(envKey);
        if (!v.isEmpty()) ini.setValue(iniKey, v);
    };
    setIf("RESULTSEARCH_SERVER", "Database/Server");
    setIf("RESULTSEARCH_DB", "Database/InitialDatabase");
    setIf("RESULTSEARCH_USER", "Database/User");
    setIf("RESULTSEARCH_PASSWORD", "Database/Password");
}

// Mock 10 test data points with high/low/reference
static std::vector<search::TrendPoint> buildMockData() {
    std::vector<search::TrendPoint> mock;
    struct Row { const char* d; double v; const char* n; };
    Row rows[] = {
        {"2024-01-15 08:30:00", 5.2, ""},
        {"2024-02-20 09:15:00", 5.5, ""},
        {"2024-03-10 07:45:00", 5.1, ""},
        {"2024-04-05 10:00:00", 6.3, "1"},
        {"2024-05-18 08:00:00", 5.8, ""},
        {"2024-06-22 09:30:00", 5.4, ""},
        {"2024-07-15 07:00:00", 3.5, "5"},
        {"2024-08-10 10:15:00", 5.0, ""},
        {"2024-09-05 08:45:00", 5.6, ""},
        {"2024-10-20 09:00:00", 5.3, ""},
    };
    for (auto& r : rows) {
        search::TrendPoint p;
        p.item_code = "HbA1c"; p.item_name = "糖化血红蛋白";
        p.item_eng = "HbA1c"; p.unit = "%";
        p.result_text = std::to_string(r.v); p.result_value = r.v;
        p.has_numeric_value = true; p.report_time = r.d;
        p.lower_bound = "4.0"; p.upper_bound = "6.0"; p.normal = r.n;
        mock.push_back(p);
    }
    // Second item for list interaction
    for (auto& r : rows) {
        search::TrendPoint p;
        p.item_code = "GLU"; p.item_name = "葡萄糖";
        p.item_eng = "GLU"; p.unit = "mmol/L";
        p.result_text = std::to_string(r.v * 1.1); p.result_value = r.v * 1.1;
        p.has_numeric_value = true; p.report_time = r.d;
        p.lower_bound = "3.9"; p.upper_bound = "6.1"; p.normal = r.n;
        mock.push_back(p);
    }
    return mock;
}

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName("LISWorkbench");
    app.setApplicationDisplayName(QString::fromWCharArray(L"LIS 工作台 - 检验结果查询"));
    app.setWindowIcon(QIcon(":/app.ico"));
    applyEnvOverrides();

    if (argc >= 2 && QString(argv[1]) == "--demo") {
        // Interactive demo — open trend window with mock data
        search::DbSettings db;
        search::QueryInput qi;
        qi.patient_name = "Demo";
        qi.patient_no = "0001";
        qi.start_date = "2024-01-01";
        qi.end_date = "2024-12-31";

        TrendWindow win(db, qi);
        // Inject mock data directly
        auto mock = buildMockData();
        win.setMockData(mock);
        win.show();
        return app.exec();
    }

    MainWindow window;
    window.setGeometry(QGuiApplication::primaryScreen()->availableGeometry());
    window.showMaximized();
    return app.exec();
}
