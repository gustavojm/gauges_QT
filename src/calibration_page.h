#pragma once

#include <QWidget>

#include "gauge_detector.h"

class QLabel;
class QPushButton;
class QSpinBox;

class CalibrationPage : public QWidget {
    Q_OBJECT
public:
    explicit CalibrationPage(QWidget* parent = nullptr);

public slots:
    void onCalibUIUpdated(const CalibUIState& state);

signals:
    void startCalibrationClicked();
    void confirmCalibClicked();
    void cancelCalibClicked();
    void minValChanged(int value);
    void maxValChanged(int value);

private:
    QLabel* gaugeProgress_ = nullptr;
    QLabel* calibInstruction_ = nullptr;
    QPushButton* startCalibBtn_ = nullptr;
    QWidget* calibConfirmWidget_ = nullptr;
    QSpinBox* minValSpin_ = nullptr;
    QSpinBox* maxValSpin_ = nullptr;
    QPushButton* confirmCalibBtn_ = nullptr;
    QPushButton* cancelCalibBtn_ = nullptr;
    bool calibConfirmInitialized_ = false;
};
