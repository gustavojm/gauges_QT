#pragma once

#include <QWidget>
#include <QVector>

#include "gauge_section_helper.h"
#include "worker.h"

class QLabel;
class QPushButton;
class QVBoxLayout;
class QScrollArea;

class ProcessingPage : public QWidget {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(ProcessingPage)
    
public:
    explicit ProcessingPage(QWidget* parent = nullptr);
    void connectToWorker(class Worker* worker);

public slots:
    void onFrameCountUpdated(int current, int total);
    void createCollapsibleSections(const QVector<GaugeCalibData>& calib);
    void onLiveValuesUpdated(const QVector<GaugeCalibData>& calib);

signals:
    void restartClicked();
    void quitClicked();
    void gaugeCalibRangeChanged(int idx, double minVal, double maxVal);

private:
    QLabel* frameCountLabel_ = nullptr;
    QScrollArea* scrollArea_ = nullptr;
    QWidget* scrollContent_ = nullptr;
    QVBoxLayout* sectionsLayout_ = nullptr;
    std::vector<GaugeSectionWidgets> sections_;
    QPushButton* restartBtn_ = nullptr;
    QPushButton* quitBtn_ = nullptr;
};
