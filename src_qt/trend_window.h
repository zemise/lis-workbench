#pragma once

#include "app_settings.h"
#include "search_app.h"
#include "trend_core.h"

#include <QDialog>
#include <QLabel>
#include <QStandardItemModel>
#include <vector>

#ifdef HAS_QWT
class QwtPlot;
class QwtPlotCurve;
class QwtPlotZoneItem;
class QwtPlotGrid;
#endif

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

#ifdef HAS_QWT
    void renderQwtChart(const std::vector<const search::TrendPoint*>& pts);
    QPixmap renderQwtToPixmap(int w, int h);
#endif

    const search::DbSettings db_;
    const search::QueryInput lastQuery_;

    QSplitter* mainSplitter_ = nullptr;
    QTableView* itemTable_ = nullptr;
    QTableView* detailTable_ = nullptr;
    QLabel* loadingLabel_ = nullptr;
    QPushButton* exportCsvBtn_ = nullptr;
    QPushButton* exportImageBtn_ = nullptr;

    // Chart area (QwtPlot or QLabel placeholder)
    QWidget* chartWidget_ = nullptr;
#ifdef HAS_QWT
    QwtPlot* plot_ = nullptr;
    QwtPlotCurve* lineCurve_ = nullptr;
    QwtPlotCurve* normalScatter_ = nullptr;
    QwtPlotCurve* highScatter_ = nullptr;
    QwtPlotCurve* lowScatter_ = nullptr;
    QwtPlotZoneItem* refZone_ = nullptr;
    QwtPlotCurve* refLegendCurve_ = nullptr;
    QwtPlotGrid* grid_ = nullptr;
#endif

    QStandardItemModel* itemModel_ = nullptr;
    QStandardItemModel* detailModel_ = nullptr;

    std::vector<search::TrendPoint> points_;
    std::vector<search::TrendItemOption> items_;
    std::string currentItemCode_;
};
