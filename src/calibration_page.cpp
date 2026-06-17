#include "calibration_page.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace {
constexpr int kBtnWide = 120;
}

CalibrationPage::CalibrationPage(QWidget* parent)
    : QWidget(parent)
{
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(10);

    gaugeProgress_ = new QLabel(this);
    lay->addWidget(gaugeProgress_);

    calibInstruction_ = new QLabel(this);
    calibInstruction_->setWordWrap(true);
    calibInstruction_->setStyleSheet("color: #ffff00; font-weight: bold;");
    lay->addWidget(calibInstruction_);

    startCalibBtn_ = new QPushButton("Start calibration", this);
    startCalibBtn_->setFixedWidth(kBtnWide);
    connect(startCalibBtn_, &QPushButton::clicked,
            this, &CalibrationPage::startCalibrationClicked);
    startCalibBtn_->setVisible(false);
    lay->addWidget(startCalibBtn_);

    calibConfirmWidget_ = new QWidget(this);
    auto* cl = new QVBoxLayout(calibConfirmWidget_);
    cl->setContentsMargins(0, 0, 0, 0);
    cl->setSpacing(8);

    auto* minRow = new QWidget(this);
    auto* mhl = new QHBoxLayout(minRow);
    mhl->setContentsMargins(0, 0, 0, 0);
    mhl->addWidget(new QLabel("Min value:", this));
    minValSpin_ = new QSpinBox(this);
    minValSpin_->setRange(0, 10000);
    mhl->addWidget(minValSpin_, 1);
    cl->addWidget(minRow);

    auto* maxRow = new QWidget(this);
    auto* Mhl = new QHBoxLayout(maxRow);
    Mhl->setContentsMargins(0, 0, 0, 0);
    Mhl->addWidget(new QLabel("Max value:", this));
    maxValSpin_ = new QSpinBox(this);
    maxValSpin_->setRange(0, 10000);
    maxValSpin_->setValue(1000);
    Mhl->addWidget(maxValSpin_, 1);
    cl->addWidget(maxRow);

    auto* calBtnRow = new QWidget(this);
    auto* cbl = new QHBoxLayout(calBtnRow);
    cbl->setContentsMargins(0, 0, 0, 0);
    cbl->setSpacing(8);
    confirmCalibBtn_ = new QPushButton("Confirm", this);
    confirmCalibBtn_->setFixedWidth(kBtnWide);
    cancelCalibBtn_ = new QPushButton("Cancel", this);
    cancelCalibBtn_->setFixedWidth(kBtnWide);
    cbl->addWidget(confirmCalibBtn_);
    cbl->addWidget(cancelCalibBtn_);
    connect(confirmCalibBtn_, &QPushButton::clicked,
            this, &CalibrationPage::confirmCalibClicked);
    connect(cancelCalibBtn_, &QPushButton::clicked,
            this, &CalibrationPage::cancelCalibClicked);
    cl->addWidget(calBtnRow);

    connect(minValSpin_, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &CalibrationPage::minValChanged);
    connect(maxValSpin_, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &CalibrationPage::maxValChanged);

    calibConfirmWidget_->setVisible(false);
    lay->addWidget(calibConfirmWidget_);

    lay->addStretch();
}

void CalibrationPage::onCalibUIUpdated(const CalibUIState& calib) {
    if (!calib.initialized) {
        calibConfirmInitialized_ = false;
        return;
    }

    bool showCalibrating = false;

    switch (calib.state) {
    case GaugeState::kCircleManual:
        if (calib.circleStage == 1)
            calibInstruction_->setText(
                "Click on the CENTER of the gauge");
        else if (calib.circleStage == 2)
            calibInstruction_->setText(
                "Now click on the EDGE of the gauge face");
        else if (calib.circleStage == 3)
            calibInstruction_->setText(
                QString("Gauge %1 placed").arg(calib.totalGauges));
        break;

    case GaugeState::kCalibrating:
        if (calib.totalGauges > 1)
            gaugeProgress_->setText(
                QString("Gauge %1 / %2")
                    .arg(calib.currentGauge + 1)
                    .arg(calib.totalGauges));
        else
            gaugeProgress_->clear();
        calibInstruction_->setText(
            "Drag markers to set min/max positions,\n"
            "adjust values, then confirm");
        showCalibrating = true;

        if (!calibConfirmInitialized_) {
            minValSpin_->setValue(calib.calibTrackMin);
            maxValSpin_->setValue(calib.calibTrackMax);
            calibConfirmInitialized_ = true;
        }
        break;

    default:
        break;
    }

    startCalibBtn_->setVisible(false);
    calibConfirmWidget_->setVisible(showCalibrating);

    if (calib.state != GaugeState::kCalibrating)
        calibConfirmInitialized_ = false;
}
