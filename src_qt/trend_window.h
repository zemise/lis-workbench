#pragma once

#include "app_settings.h"
#include "search_app.h"
#include "trend_core.h"

#include <QDialog>
#include <QLabel>
#include <QPixmap>
#include <QStandardItemModel>
#include <vector>

class QTableView;
class QPushButton;
class QSplitter;

class TrendWindow : public QDialog {
    Q_OBJECT
public:
    TrendWindow(const search::DbSettings& db,
                const search::QueryInput& lastQuery,
                QWidget* parent = nullptr);

private slots:
    void onItemClicked(const QModelIndex& index);
    void onExportCsv();
    void onExportImages();
    void onTrendDataLoaded();

private:
    void setupUi();
    void loadTrendData();
    void renderChart(const std::string& itemCode);
    void renderToFile(const std::string& itemCode,
                      const QString& path, int w, int h);
    QString gnuplotPath() const;

    const search::DbSettings db_;
    const search::QueryInput lastQuery_;

    // Widgets
    QSplitter* mainSplitter_ = nullptr;
    QTableView* itemTable_ = nullptr;
    QTableView* detailTable_ = nullptr;
    QLabel* chartLabel_ = nullptr;
    QLabel* loadingLabel_ = nullptr;
    QPushButton* exportCsvBtn_ = nullptr;
    QPushButton* exportImageBtn_ = nullptr;

    // Models
    QStandardItemModel* itemModel_ = nullptr;
    QStandardItemModel* detailModel_ = nullptr;

    // Data
    std::vector<search::TrendPoint> points_;
    std::vector<search::TrendItemOption> items_;
    std::string currentItemCode_;
    QPixmap chartPixmap_;
};
