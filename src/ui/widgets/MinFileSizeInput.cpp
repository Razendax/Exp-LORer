#include "MinFileSizeInput.h"

#include <QComboBox>
#include <QSpinBox>

std::uintmax_t minFileSizeBytesFrom(const QComboBox* unitCombo, const QSpinBox* magnitudeSpinBox)
{
    std::uintmax_t multiplier = 1;
    switch (unitCombo->currentIndex())
    {
        case 0: // B
            multiplier = 1;
            break;
        case 1: // KB
            multiplier = 1024;
            break;
        case 2: // MB
            multiplier = 1024 * 1024;
            break;
        default:
            break;
    }
    return static_cast<std::uintmax_t>(magnitudeSpinBox->value()) * multiplier;
}
