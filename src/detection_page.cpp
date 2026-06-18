#include "detection_page.h"
#include "worker.h"

#include <QCheckBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QVBoxLayout>

namespace {
constexpr int kBtnWide = 120;
}

DetectionPage::DetectionPage(QWidget* parent)
    : QWidget(parent)
{
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(10);

    manualCb_ = new QCheckBox("Click to place gauges manually", this);
    connect(manualCb_, &QCheckBox::toggled,
            this, &DetectionPage::manualPlacementToggled);
    lay->addWidget(manualCb_);

    cannyRow_ = new QWidget(this);
    auto* chl = new QHBoxLayout(cannyRow_);
    chl->setContentsMargins(0, 0, 0, 0);
    chl->addWidget(new QLabel("Canny:", this));
    cannySlider_ = new QSlider(Qt::Horizontal, this);
    cannySlider_->setRange(1, 500);
    cannySlider_->setValue(320);
    cannyValLabel_ = new QLabel("320", this);
    cannyValLabel_->setFixedWidth(30);
    chl->addWidget(cannySlider_, 1);
    chl->addWidget(cannyValLabel_);
    connect(cannySlider_, &QSlider::valueChanged,
            this, &DetectionPage::cannyChanged);
    lay->addWidget(cannyRow_);

    accRow_ = new QWidget(this);
    auto* ahl = new QHBoxLayout(accRow_);
    ahl->setContentsMargins(0, 0, 0, 0);
    ahl->addWidget(new QLabel("Accum:", this));
    accSlider_ = new QSlider(Qt::Horizontal, this);
    accSlider_->setRange(1, 500);
    accSlider_->setValue(40);
    accValLabel_ = new QLabel("40", this);
    accValLabel_->setFixedWidth(30);
    ahl->addWidget(accSlider_, 1);
    ahl->addWidget(accValLabel_);
    connect(accSlider_, &QSlider::valueChanged,
            this, &DetectionPage::accChanged);
    lay->addWidget(accRow_);

    gaugeCountLabel_ = new QLabel("Found 0 gauge(s)", this);
    lay->addWidget(gaugeCountLabel_);

    instructionLabel_ = new QLabel(this);
    instructionLabel_->setWordWrap(true);
    instructionLabel_->setStyleSheet("color: #ffff00; font-weight: bold;");
    instructionLabel_->setVisible(false);
    lay->addWidget(instructionLabel_);

    lay->addSpacing(8);

    auto* btnRow = new QWidget(this);
    auto* bhl = new QHBoxLayout(btnRow);
    bhl->setContentsMargins(0, 0, 0, 0);
    bhl->setSpacing(8);
    confirmBtn_ = new QPushButton("Confirm", this);
    confirmBtn_->setFixedWidth(kBtnWide);
    bhl->addWidget(confirmBtn_);
    connect(confirmBtn_, &QPushButton::clicked,
            this, &DetectionPage::confirmClicked);
    lay->addWidget(btnRow);

    lay->addStretch();
}

void DetectionPage::onDetectionUpdated(int numGauges) {
    gaugeCountLabel_->setText(QString("Found %1 gauge(s)").arg(numGauges));
    confirmBtn_->setVisible(numGauges > 0);
}

void DetectionPage::setManualPlacementActive(bool active) {
    cannyRow_->setVisible(!active);
    accRow_->setVisible(!active);
    instructionLabel_->setVisible(active);
    if (!active) instructionLabel_->clear();
}

void DetectionPage::onManualInstructionChanged(bool centerStage) {
    if (centerStage)
        instructionLabel_->setText("Click on the CENTER of the gauge");
    else
        instructionLabel_->setText("Now click on the EDGE of the gauge face");
}

void DetectionPage::connectToWorker(Worker* worker) {
    connect(this, &DetectionPage::manualPlacementToggled,
            worker, &Worker::setManualPlacement);
    connect(this, &DetectionPage::cannyChanged,
            worker, &Worker::setCanny);
    connect(this, &DetectionPage::accChanged,
            worker, &Worker::setAcc);
    connect(this, &DetectionPage::confirmClicked,
            worker, &Worker::confirmGauges);
    connect(worker, &Worker::detectionUpdated,
            this, &DetectionPage::onDetectionUpdated);
    connect(worker, &Worker::manualPlacementActive,
            this, &DetectionPage::setManualPlacementActive);
    connect(worker, &Worker::manualInstructionChanged,
            this, &DetectionPage::onManualInstructionChanged);
}
