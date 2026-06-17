#pragma once

#include <QVector>
#include <QWidget>
#include <vector>

#include "Section.h"
#include "gauge_detector.h"
#include "worker.h"

class QLabel;
class QPushButton;
class QScrollArea;
class QSpinBox;
class QVBoxLayout;

struct CalibGaugeSection {
    ui::Section* section = nullptr;
    QSpinBox* minSpin = nullptr;
    QSpinBox* maxSpin = nullptr;
};

class CalibrationPage : public QWidget {
    Q_OBJECT
public:
    explicit CalibrationPage(QWidget* parent = nullptr);
    void connectToWorker(class Worker* worker);

public slots:
    void onCalibUIUpdated(const CalibUIState& state);
    void onGaugeCalibUpdated(const QVector<GaugeCalibData>& calib);

signals:
    void startCalibrationClicked();
    void confirmCalibClicked();
    void cancelCalibClicked();
    void gaugeCalibRangeChanged(int idx, double minVal, double maxVal);

private:
    void rebuildSections(const QVector<GaugeCalibData>& calib);

    QLabel* gaugeProgress_ = nullptr;
    QLabel* calibInstruction_ = nullptr;
    QPushButton* startCalibBtn_ = nullptr;
    QScrollArea* scrollArea_ = nullptr;
    QWidget* scrollContent_ = nullptr;
    QVBoxLayout* sectionsLayout_ = nullptr;
    std::vector<CalibGaugeSection> sections_;
    QPushButton* confirmCalibBtn_ = nullptr;
    QPushButton* cancelCalibBtn_ = nullptr;
    int currentGaugeIdx_ = 0;
};
