/*
    Elypson/qt-collapsible-section
    (c) 2016 Michael A. Voelkel - michael.alexander.voelkel@gmail.com

    This file is part of Elypson/qt-collapsible section.

    Elypson/qt-collapsible-section is free software: you can redistribute it and/or modify
    it under the terms of the GNU Lesser General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    Elypson/qt-collapsible-section is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU Lesser General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Elypson/qt-collapsible-section. If not, see <http://www.gnu.org/licenses/>.
*/

#include <QPropertyAnimation>

#include "q_collapsible_section.h"
#include <QDebug>
namespace ui
{
    QCollapsibleSection::QCollapsibleSection(const QString& title, const int animationDuration, QWidget* parent)
        : QWidget(parent), animationDuration_(animationDuration)
    {
        toggleButton_ = new QToolButton(this);
        colorLabel_ = new QLabel(this);
        headerLine_ = new QFrame(this);
        toggleAnimation_ = new QParallelAnimationGroup(this);
        contentArea_ = new QScrollArea(this);
        mainLayout_ = new QGridLayout(this);

        colorLabel_->setFixedWidth(16);
        colorLabel_->setAlignment(Qt::AlignCenter);
        colorLabel_->setText("\u25A0");
        colorLabel_->setStyleSheet("background: transparent;");

        toggleButton_->setStyleSheet("QToolButton {border: none;}");
        toggleButton_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        toggleButton_->setArrowType(Qt::ArrowType::RightArrow);
        toggleButton_->setText(title);
        toggleButton_->setCheckable(true);
        toggleButton_->setChecked(false);

        headerLine_->setFrameShape(QFrame::HLine);
        headerLine_->setFrameShadow(QFrame::Sunken);
        headerLine_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);

        contentArea_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

        // start out collapsed
        contentArea_->setMaximumHeight(0);
        contentArea_->setMinimumHeight(0);

        // let the entire widget grow and shrink with its content
        toggleAnimation_->addAnimation(new QPropertyAnimation(this, "maximumHeight"));
        toggleAnimation_->addAnimation(new QPropertyAnimation(this, "minimumHeight"));
        toggleAnimation_->addAnimation(new QPropertyAnimation(contentArea_, "maximumHeight"));

        mainLayout_->setVerticalSpacing(0);
        mainLayout_->setContentsMargins(0, 0, 0, 0);

        int row = 0;
        mainLayout_->addWidget(colorLabel_, row, 0, 1, 1, Qt::AlignCenter);
        mainLayout_->addWidget(toggleButton_, row, 1, 1, 1, Qt::AlignLeft);
        mainLayout_->addWidget(headerLine_, row++, 2, 1, 1);
        mainLayout_->addWidget(contentArea_, row, 0, 1, 3);
        setLayout(mainLayout_);

        connect(toggleButton_, &QToolButton::toggled, this, &QCollapsibleSection::toggle);
    }

    void QCollapsibleSection::toggle(bool expanded)
    {
        toggleButton_->setArrowType(expanded ? Qt::ArrowType::DownArrow : Qt::ArrowType::RightArrow);
        toggleAnimation_->setDirection(expanded ? QAbstractAnimation::Forward : QAbstractAnimation::Backward);
        toggleAnimation_->start();
        
        this->isExpanded_ = expanded;
    }

    void QCollapsibleSection::SetContentLayout(QLayout* contentLayout)
    {
        delete contentArea_->layout();
        contentArea_->setLayout(contentLayout);
        collapsedHeight_ = sizeHint().height() - contentArea_->maximumHeight();
        
        UpdateHeights();
    }
    
    void QCollapsibleSection::SetTitle(QString title)
    {
        toggleButton_->setText(std::move(title));
    }

    void QCollapsibleSection::SetColorSwatch(const QString& swatch, const QColor& color)
    {
        colorLabel_->setText(swatch);
        colorLabel_->setStyleSheet(QString("color: %1; background: transparent; font-size: 14px;").arg(color.name()));
    }
    
    void QCollapsibleSection::UpdateHeights()
    {
        int contentHeight = contentArea_->layout()->sizeHint().height();

        for (int i = 0; i < toggleAnimation_->animationCount() - 1; ++i)
        {
            QPropertyAnimation* SectionAnimation = static_cast<QPropertyAnimation *>(toggleAnimation_->animationAt(i));
            SectionAnimation->setDuration(animationDuration_);
            SectionAnimation->setStartValue(collapsedHeight_);
            SectionAnimation->setEndValue(collapsedHeight_ + contentHeight);
        }

        QPropertyAnimation* contentAnimation = static_cast<QPropertyAnimation *>(toggleAnimation_->animationAt(toggleAnimation_->animationCount() - 1));
        contentAnimation->setDuration(animationDuration_);
        contentAnimation->setStartValue(0);
        contentAnimation->setEndValue(contentHeight);
        
        toggleAnimation_->setDirection(isExpanded_ ? QAbstractAnimation::Forward : QAbstractAnimation::Backward);
        toggleAnimation_->start();
    }
}
