#include "detection_page.h"
#include "worker.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QVBoxLayout>

#include "circular_gauge.h"
#include "edgewise_gauge.h"

namespace {
constexpr int kBtnWide = 120;
}

DetectionPage::DetectionPage(QWidget* parent)
    : QWidget(parent)
{
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(10);

    auto* typeRow = new QWidget(this);
    auto* thl = new QHBoxLayout(typeRow);
    thl->setContentsMargins(0, 0, 0, 0);
    thl->addWidget(new QLabel("Gauge type:", this));
    gaugeTypeCombo_ = new QComboBox(this);
    gaugeTypeCombo_->addItem("Circular Gauge");
    gaugeTypeCombo_->addItem("Edgewise Panel Meter");
    gaugeTypeCombo_->setCurrentIndex(0);
    thl->addWidget(gaugeTypeCombo_, 1);
    connect(gaugeTypeCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int index) {
                isEdgewise_ = (index == 1);
                emit gaugeTypeChanged(index);
                if (manualBtn_->isChecked())
                    onManualInstructionChanged(0);
            });
    lay->addWidget(typeRow);

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

    manualBtn_ = new QPushButton("Add Gauges Manually", this);
    manualBtn_->setCheckable(true);
    manualBtn_->setToolTip("Toggle between automatic and manual gauge placement");
    connect(manualBtn_, &QPushButton::toggled, this, [this](bool checked) {
        manualBtn_->setText(checked ? "Automatic Detection" : "Add Gauges Manually");
        emit manualPlacementToggled(checked);
    });
    lay->addWidget(manualBtn_);

    instructionLabel_ = new QLabel(this);
    instructionLabel_->setWordWrap(true);
    instructionLabel_->setStyleSheet("color: #ffff00; font-weight: bold;");
    instructionLabel_->setVisible(false);
    lay->addWidget(instructionLabel_);

    gaugeCountLabel_ = new QLabel("Found 0 gauge(s)", this);
    lay->addWidget(gaugeCountLabel_);

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

void DetectionPage::onDetectionCountChanged(int numGauges) {
    gaugeCountLabel_->setText(QString("Found %1 gauge(s)").arg(numGauges));
    confirmBtn_->setVisible(numGauges > 0);
}

void DetectionPage::onManualPlacementActivated(bool active) {
    cannyRow_->setVisible(!active);
    instructionLabel_->setVisible(active);
    if (!active) instructionLabel_->clear();
}

void DetectionPage::onManualInstructionChanged(int stage) {
    const char* text = isEdgewise_
        ? EdgewiseGauge::manualInstruction(stage)
        : CircularGauge::manualInstruction(stage);
    instructionLabel_->setText(QString::fromUtf8(text));
}

void DetectionPage::connectToWorker(Worker* worker) {
    connect(this, &DetectionPage::manualPlacementToggled,
            worker, &Worker::setManualPlacement);
    connect(this, &DetectionPage::gaugeTypeChanged,
            worker, &Worker::setGaugeType);
    connect(this, &DetectionPage::cannyChanged,
            worker, &Worker::setCanny);
    connect(this, &DetectionPage::confirmClicked,
            worker, &Worker::confirmGauges);
    connect(worker, &Worker::detectionCountChanged,
            this, &DetectionPage::onDetectionCountChanged);
    connect(worker, &Worker::manualPlacementActivated,
            this, &DetectionPage::onManualPlacementActivated);
    connect(worker, &Worker::manualInstructionChanged,
            this, &DetectionPage::onManualInstructionChanged);
}
