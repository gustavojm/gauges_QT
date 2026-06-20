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

    calibInstruction_ = new QLabel(this);
    calibInstruction_->setWordWrap(true);
    calibInstruction_->setStyleSheet("color: #ffff00; font-weight: bold;");
    calibInstruction_->setText(
        "Drag markers to set min/max positions,\n"
        "adjust values, then confirm");
    lay->addWidget(calibInstruction_);

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

void CalibrationPage::onCalibrationDataReady(const QVector<GaugeCalibData>& calib) {
    if (calib.isEmpty()) return;
    if (sections_.size() != static_cast<size_t>(calib.size()))
        rebuildSections(calib);
}

void CalibrationPage::connectToWorker(Worker* worker) {
    connect(this, &CalibrationPage::confirmCalibClicked,
            worker, &Worker::confirmCalib);
    connect(this, &CalibrationPage::cancelCalibClicked,
            worker, &Worker::quit);
    connect(this, &CalibrationPage::gaugeCalibRangeChanged,
            worker, &Worker::setGaugeCalibRange);
    connect(worker, &Worker::calibrationDataReady,
            this, &CalibrationPage::onCalibrationDataReady);
}
