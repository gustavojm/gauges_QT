#pragma once

#include <QWidget>

#include "gauge_section_helper.h"
#include "circular_gauge.h"
#include "worker.h"

class QLabel;
class QPushButton;
class QScrollArea;
class QVBoxLayout;

class CalibrationPage : public QWidget {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(CalibrationPage)
    
public:
    explicit CalibrationPage(QWidget* parent = nullptr);
    void connectToWorker(class Worker* worker);

public slots:
    void onCalibrationDataReady(const QVector<GaugeCalibData>& calib);

signals:
    void confirmCalibClicked();
    void cancelCalibClicked();
    void gaugeCalibRangeChanged(int idx, double minVal, double maxVal);

private:
    void rebuildSections(const QVector<GaugeCalibData>& calib);

    QLabel* calibInstruction_ = nullptr;
    QScrollArea* scrollArea_ = nullptr;
    QWidget* scrollContent_ = nullptr;
    QVBoxLayout* sectionsLayout_ = nullptr;
    std::vector<GaugeSectionWidgets> sections_;
    QPushButton* confirmCalibBtn_ = nullptr;
    QPushButton* cancelCalibBtn_ = nullptr;
};
