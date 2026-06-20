#include "processing_page.h"

#include "q_collapsible_section.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
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

void ProcessingPage::onFrameCountUpdated(int current, int total) {
    frameCountLabel_->setText(
        QString("Frame %1 / %2").arg(current).arg(total));
}

void ProcessingPage::createCollapsibleSections(const QVector<GaugeCalibData>& calib) {
    ::rebuildCollapsibleSections(sections_, sectionsLayout_, scrollContent_, calib, this);
}

void ProcessingPage::onLiveValuesUpdated(const QVector<GaugeCalibData>& calib) {
    for (size_t i = 0; i < sections_.size() && i < static_cast<size_t>(calib.size()); i++) {
        sections_[i].section->setTitle(
            gaugeTitle(i, calib[i].value));
    }
}

void ProcessingPage::connectToWorker(Worker* worker) {
    connect(this, &ProcessingPage::restartClicked,
            worker, &Worker::restart);
    connect(this, &ProcessingPage::quitClicked,
            worker, &Worker::quit);
    connect(this, &ProcessingPage::gaugeCalibRangeChanged,
            worker, &Worker::setGaugeCalibRange);
    connect(worker, &Worker::calibrationDataReady,
            this, &ProcessingPage::createCollapsibleSections);
    connect(worker, &Worker::liveValuesUpdated,
            this, &ProcessingPage::onLiveValuesUpdated);
    connect(worker, &Worker::frameCountUpdated,
            this, &ProcessingPage::onFrameCountUpdated);
}
