#include "processing_page.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QVBoxLayout>

namespace {
constexpr int kBtnNarrow = 80;
}

ProcessingPage::ProcessingPage(QWidget* parent)
    : QWidget(parent)
{
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(10);

    frameCountLabel_ = new QLabel("Frame 0 / 0", this);
    lay->addWidget(frameCountLabel_);

    scrollArea_ = new QScrollArea(this);
    scrollArea_->setWidgetResizable(true);
    scrollContent_ = new QWidget(this);
    sectionsLayout_ = new QVBoxLayout(scrollContent_);
    sectionsLayout_->setContentsMargins(0, 0, 0, 0);
    sectionsLayout_->setSpacing(4);
    scrollArea_->setWidget(scrollContent_);
    lay->addWidget(scrollArea_, 1);

    lay->addSpacing(12);

    auto* btnRow = new QWidget(this);
    auto* bhl = new QHBoxLayout(btnRow);
    bhl->setContentsMargins(0, 0, 0, 0);
    bhl->setSpacing(8);
    restartBtn_ = new QPushButton("Restart", this);
    restartBtn_->setFixedWidth(kBtnNarrow);
    quitBtn_ = new QPushButton("Quit", this);
    quitBtn_->setFixedWidth(kBtnNarrow);
    bhl->addWidget(restartBtn_);
    bhl->addWidget(quitBtn_);
    connect(restartBtn_, &QPushButton::clicked,
            this, &ProcessingPage::restartClicked);
    connect(quitBtn_, &QPushButton::clicked,
            this, &ProcessingPage::quitClicked);
    lay->addWidget(btnRow);
}

void ProcessingPage::rebuildSections(const QVector<GaugeCalibData>& calib) {
    while (!sections_.empty()) {
        auto& s = sections_.back();
        sectionsLayout_->removeWidget(s.section);
        delete s.section;
        sections_.pop_back();
    }

    for (int i = 0; i < calib.size(); i++) {
        QString colorName = QString("#%1").arg(calib[i].colorRgb, 6, 16, QChar('0'));
        auto* sec = new ui::Section(
            QString("Gauge %1: %2").arg(i + 1).arg(calib[i].value, 0, 'f', 2),
            0, scrollContent_);
        sec->setStyleSheet(
            QString("ui--Section { background: #2a2a32; border-radius: 4px; }"
                    "ui--Section QToolButton { color: %1; }")
                .arg(colorName));

        auto* cl = new QVBoxLayout();
        cl->setContentsMargins(8, 4, 8, 8);
        cl->setSpacing(6);

        auto* minRow = new QWidget();
        auto* minLay = new QHBoxLayout(minRow);
        minLay->setContentsMargins(0, 0, 0, 0);
        minLay->addWidget(new QLabel("Min:"));
        auto* minSpin = new QSpinBox();
        minSpin->setRange(0, 10000);
        minSpin->setValue(static_cast<int>(calib[i].minValue));
        minLay->addWidget(minSpin, 1);
        cl->addWidget(minRow);

        auto* maxRow = new QWidget();
        auto* maxLay = new QHBoxLayout(maxRow);
        maxLay->setContentsMargins(0, 0, 0, 0);
        maxLay->addWidget(new QLabel("Max:"));
        auto* maxSpin = new QSpinBox();
        maxSpin->setRange(0, 10000);
        maxSpin->setValue(static_cast<int>(calib[i].maxValue));
        maxLay->addWidget(maxSpin, 1);
        cl->addWidget(maxRow);

        sec->setContentLayout(*cl);
        sectionsLayout_->addWidget(sec);

        connect(minSpin, QOverload<int>::of(&QSpinBox::valueChanged), this,
                [this, i](int v) {
                    emit gaugeCalibRangeChanged(i, static_cast<double>(v),
                                                sections_[i].maxSpin->value());
                });
        connect(maxSpin, QOverload<int>::of(&QSpinBox::valueChanged), this,
                [this, i](int v) {
                    emit gaugeCalibRangeChanged(i,
                                                sections_[i].minSpin->value(),
                                                static_cast<double>(v));
                });

        sections_.push_back({sec, minSpin, maxSpin});
    }
}

void ProcessingPage::onFrameCountUpdated(int current, int total) {
    frameCountLabel_->setText(
        QString("Frame %1 / %2").arg(current).arg(total));
}

void ProcessingPage::onGaugeCalibUpdated(const QVector<GaugeCalibData>& calib) {
    if (sections_.size() != static_cast<size_t>(calib.size())) {
        rebuildSections(calib);
        return;
    }
    for (size_t i = 0; i < sections_.size() && i < static_cast<size_t>(calib.size()); i++) {
        sections_[i].section->setTitle(
            QString("Gauge %1: %2").arg(i + 1).arg(calib[i].value, 0, 'f', 2));
    }
}

void ProcessingPage::connectToWorker(Worker* worker) {
    connect(this, &ProcessingPage::restartClicked,
            worker, &Worker::restart);
    connect(this, &ProcessingPage::quitClicked,
            worker, &Worker::quit);
    connect(this, &ProcessingPage::gaugeCalibRangeChanged,
            worker, &Worker::setGaugeCalibRange);
    connect(worker, &Worker::gaugeCalibUpdated,
            this, &ProcessingPage::onGaugeCalibUpdated);
    connect(worker, &Worker::frameCountUpdated,
            this, &ProcessingPage::onFrameCountUpdated);
}
