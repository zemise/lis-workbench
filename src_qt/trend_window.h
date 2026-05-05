#pragma once

#include "app_settings.h"
#include "search_app.h"
#include "trend_core.h"

#include <QDialog>
#include <QLabel>
#include <QStandardItemModel>
#include <QWidget>
#include <vector>

class QTableView;
class QPushButton;
class QSplitter;

// ── Trend chart widget (pure QPainter, no external deps) ───

class TrendChartWidget : public QWidget {
    Q_OBJECT
public:
    explicit TrendChartWidget(QWidget* parent = nullptr);
    void setData(const std::vector<const search::TrendPoint*>& pts);
    QPixmap exportPixmap(int w, int h);

protected:
    void paintEvent(QPaintEvent*) override;

private:
    std::vector<const search::TrendPoint*> pts_;
    double yMin_ = 0, yMax_ = 0;
    double refLo_ = 0, refHi_ = 0;
    bool hasRef_ = false;
    int yAxisWidth_ = 80;
    int titleHeight_ = 50;
    int xLabelHeight_ = 56;
    int legendWidth_ = 110;
    int padding_ = 10;
};

// ── Trend window (layout shell) ────────────────────────────

class TrendWindow : public QDialog {
    Q_OBJECT
public:
    TrendWindow(const search::DbSettings& db,
                const search::QueryInput& lastQuery,
                QWidget* parent = nullptr);

    // Inject pre-loaded data (demo/test mode — skips DB query)
    void setMockData(const std::vector<search::TrendPoint>& data);

private slots:
    void onItemClicked(const QModelIndex& index);
    void onExportCsv();
    void onExportImages();
    void onTrendDataLoaded();

private:
    void setupUi();
    void loadTrendData();

    const search::DbSettings db_;
    const search::QueryInput lastQuery_;

    QSplitter* mainSplitter_ = nullptr;
    QTableView* itemTable_ = nullptr;
    QTableView* detailTable_ = nullptr;
    QLabel* loadingLabel_ = nullptr;
    QPushButton* exportCsvBtn_ = nullptr;
    QPushButton* exportImageBtn_ = nullptr;
    TrendChartWidget* chart_ = nullptr;

    QStandardItemModel* itemModel_ = nullptr;
    QStandardItemModel* detailModel_ = nullptr;

    std::vector<search::TrendPoint> points_;
    std::vector<search::TrendItemOption> items_;
    std::string currentItemCode_;
};
