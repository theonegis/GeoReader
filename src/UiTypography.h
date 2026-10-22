#pragma once

#include <QApplication>
#include <QFontInfo>
#include <algorithm>

namespace UiTypography {
// Data and auxiliary text stay close to the user's chosen body size.
inline QFont dataFont()
{
    QFont font = QApplication::font();
    const double points = font.pointSizeF() > 0.0
        ? font.pointSizeF() : QFontInfo(font).pointSizeF();
    font.setPointSizeF(std::max(11.0, points * 0.92));
    return font;
}
}
