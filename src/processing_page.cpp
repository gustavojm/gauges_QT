#include "processing_page.h"
#include "worker.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
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

    gaugeValuesLayout_ = new QVBoxLayout;
    lay->addLayout(gaugeValuesLayout_);

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

    lay->addStretch();
}

void ProcessingPage::onFrameCountUpdated(int current, int total) {
    frameCountLabel_->setText(
        QString("Frame %1 / %2").arg(current).arg(total));
}

void ProcessingPage::onGaugeValuesUpdated(const QVector<double>& values) {
    while (gaugeValueLabels_.size() < values.size()) {
        auto* lbl = new QLabel(this);
        lbl->setStyleSheet("color: #00ffff; font-size: 14px;");
        gaugeValuesLayout_->addWidget(lbl);
        gaugeValueLabels_.push_back(lbl);
    }
    for (size_t i = 0; i < gaugeValueLabels_.size(); ++i) {
        if (i < values.size()) {
            gaugeValueLabels_[i]->setText(
                QString("Gauge %1: %2")
                    .arg(i + 1)
                    .arg(values[i], 0, 'f', 2));
            gaugeValueLabels_[i]->setVisible(true);
        } else {
            gaugeValueLabels_[i]->setVisible(false);
        }
    }
}

void ProcessingPage::connectToWorker(Worker* worker) {
    connect(this, &ProcessingPage::restartClicked,
            worker, &Worker::restart);
    connect(this, &ProcessingPage::quitClicked,
            worker, &Worker::quit);
    connect(worker, &Worker::gaugeValuesUpdated,
            this, &ProcessingPage::onGaugeValuesUpdated);
    connect(worker, &Worker::frameCountUpdated,
            this, &ProcessingPage::onFrameCountUpdated);
}
