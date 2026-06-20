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

inline QString gaugeTitle(int index, double value) {
    return QString("Gauge %1: %2")
        .arg(index + 1)
        .arg(value, 0, 'f', 2);
}

template<typename Page>
void rebuildCollapsibleSections(
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
        auto* sec = new ui::QCollapsibleSection(
            gaugeTitle(i, calib[i].value),
            0, scrollContent);
        sec->setColorSwatch("\u25A0", QColor::fromRgb(calib[i].colorRgb));
        sec->setStyleSheet(
            "ui--QCollapsibleSection { background: #2a2a32; border-radius: 4px; }"
            "ui--QCollapsibleSection QToolButton { color: black; }");

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
