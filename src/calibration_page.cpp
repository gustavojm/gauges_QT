#include "calibration_page.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

#include "gauge_section_helper.h"

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

    scrollArea_ = new QScrollArea(this);
    scrollArea_->setWidgetResizable(true);
    scrollContent_ = new QWidget(this);
    sectionsLayout_ = new QVBoxLayout(scrollContent_);
    sectionsLayout_->setContentsMargins(0, 0, 0, 0);
    sectionsLayout_->setSpacing(4);
    scrollArea_->setWidget(scrollContent_);
    lay->addWidget(scrollArea_, 1);

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
    lay->addWidget(calBtnRow);
}

void CalibrationPage::rebuildSections(const QVector<GaugeCalibData>& calib) {
    ::rebuildGaugeSections(sections_, sectionsLayout_, scrollContent_, calib, this);
}

void CalibrationPage::onCalibUIUpdated(const CalibUIState& calib) {
    if (!calib.initialized) return;

    currentGaugeIdx_ = static_cast<int>(calib.currentGauge);

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
        break;

    default:
        break;
    }

    startCalibBtn_->setVisible(calib.state == GaugeState::kCircleManual);
}

void CalibrationPage::onGaugeCalibUpdated(const QVector<GaugeCalibData>& calib) {
    if (calib.isEmpty()) return;

    if (sections_.size() != static_cast<size_t>(calib.size()))
        rebuildSections(calib);
}

void CalibrationPage::connectToWorker(Worker* worker) {
    connect(this, &CalibrationPage::startCalibrationClicked,
            worker, &Worker::startCalibration);
    connect(this, &CalibrationPage::confirmCalibClicked,
            worker, &Worker::confirmCalib);
    connect(this, &CalibrationPage::cancelCalibClicked,
            worker, &Worker::quit);
    connect(this, &CalibrationPage::gaugeCalibRangeChanged,
            worker, &Worker::setGaugeCalibRange);
    connect(worker, &Worker::calibUIUpdated,
            this, &CalibrationPage::onCalibUIUpdated);
    connect(worker, &Worker::gaugeCalibUpdated,
            this, &CalibrationPage::onGaugeCalibUpdated);
}
