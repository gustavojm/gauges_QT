#pragma once

#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QString>
#include <QVBoxLayout>
#include <QVector>
#include <QWidget>

#include <vector>

#include "q_collapsible_section.h"
#include "worker.h"

struct GaugeSectionWidgets {
    ui::QCollapsibleSection* section = nullptr;
    QDoubleSpinBox* minSpin = nullptr;
    QDoubleSpinBox* maxSpin = nullptr;
};

template<typename Page>
void rebuildGaugeSections(
    std::vector<GaugeSectionWidgets>& sections,
    QVBoxLayout* sectionsLayout,
    QWidget* scrollContent,
    const QVector<GaugeCalibData>& calib,
    Page* page)
{
    while (!sections.empty()) {
        auto& s = sections.back();
        sectionsLayout->removeWidget(s.section);
        delete s.section;
        sections.pop_back();
    }

    for (int i = 0; i < calib.size(); i++) {
        QString colorName = QString("#%1").arg(calib[i].colorRgb, 6, 16, QChar('0'));
        auto* sec = new ui::QCollapsibleSection(
            QString("Gauge %1: %2").arg(i + 1).arg(calib[i].value, 0, 'f', 2),
            0, scrollContent);
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
        auto* minSpin = new QDoubleSpinBox();
        minSpin->setRange(-99999, 99999);
        minSpin->setDecimals(1);
        minSpin->setValue(calib[i].minValue);
        minLay->addWidget(minSpin, 1);
        cl->addWidget(minRow);

        auto* maxRow = new QWidget();
        auto* maxLay = new QHBoxLayout(maxRow);
        maxLay->setContentsMargins(0, 0, 0, 0);
        maxLay->addWidget(new QLabel("Max:"));
        auto* maxSpin = new QDoubleSpinBox();
        maxSpin->setRange(-99999, 99999);
        maxSpin->setDecimals(1);
        maxSpin->setValue(calib[i].maxValue);
        maxLay->addWidget(maxSpin, 1);
        cl->addWidget(maxRow);

        sec->setContentLayout(cl);
        sectionsLayout->addWidget(sec);

        QObject::connect(minSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), page,
                [&sections, i, page](double v) {
                    emit page->gaugeCalibRangeChanged(
                        i, v,
                        sections[i].maxSpin->value());
                });
        QObject::connect(maxSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), page,
                [&sections, i, page](double v) {
                    emit page->gaugeCalibRangeChanged(
                        i, sections[i].minSpin->value(),
                        v);
                });

        sections.push_back({sec, minSpin, maxSpin});
    }
}
