#include "main_window.h"
#include "trend_window.h"

#include <QApplication>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QIcon>
#include <QMessageBox>
#include <QProcessEnvironment>
#include <QScreen>
#include <QSettings>

static void applyEnvOverrides() {
    auto env = QProcessEnvironment::systemEnvironment();
    QString iniPath = QCoreApplication::applicationDirPath() + "/result_search.ini";
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

static QPixmap makeDemoChart() {
    // Build mock data — a complete trend chart without database
    std::vector<search::TrendPoint> mock;
    auto add = [&](const char* date, double val, const char* normal = "") {
        search::TrendPoint p;
        p.item_code = "HbA1c";
        p.item_name = "糖化血红蛋白";
        p.item_eng = "HbA1c";
        p.unit = "%";
        p.result_text = std::to_string(val);
        p.result_value = val;
        p.has_numeric_value = true;
        p.report_time = date;
        p.lower_bound = "4.0";
        p.upper_bound = "6.0";
        p.normal = normal;
        mock.push_back(p);
    };
    add("2024-01-15 08:30:00", 5.2);
    add("2024-02-20 09:15:00", 5.5);
    add("2024-03-10 07:45:00", 5.1);
    add("2024-04-05 10:00:00", 6.3, "1");   // high
    add("2024-05-18 08:00:00", 5.8);
    add("2024-06-22 09:30:00", 5.4);
    add("2024-07-15 07:00:00", 3.5, "5");   // low
    add("2024-08-10 10:15:00", 5.0);
    add("2024-09-05 08:45:00", 5.6);
    add("2024-10-20 09:00:00", 5.3);

    TrendChartWidget chart;
    std::vector<const search::TrendPoint*> ptrs;
    for (auto& p : mock) ptrs.push_back(&p);
    chart.setData(ptrs);
    chart.resize(1600, 900);
    chart.show();
    QApplication::processEvents();
    QPixmap pix = chart.grab();
    return pix;
}

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName("ResultSearch");
    app.setApplicationDisplayName(QString::fromWCharArray(L"检验结果查询"));
    app.setWindowIcon(QIcon(":/app.ico"));
    applyEnvOverrides();

    if (argc >= 2 && QString(argv[1]) == "--demo") {
        // Demo mode — generate chart with mock data, save PNG, exit
        QPixmap pix = makeDemoChart();
        QString outPath = QCoreApplication::applicationDirPath() + "/demo_chart.png";
        if (pix.save(outPath, "PNG")) {
            QMessageBox::information(nullptr, "Demo",
                QString::fromWCharArray(L"演示图已保存到:\n") + outPath);
        }
        return 0;
    }

    MainWindow window;
    window.setGeometry(QGuiApplication::primaryScreen()->availableGeometry());
    window.showMaximized();
    return app.exec();
}
