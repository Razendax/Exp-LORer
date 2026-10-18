#pragma once

#include <cstdint>

class QComboBox;
class QSpinBox;

// Combines a Bytes/KB/MB unit QComboBox (index 0/1/2) with a magnitude QSpinBox into a byte count.
// Shared by DuplicateFinderDialog and FragmentationWidget's "minimum file size" filter inputs.
std::uintmax_t minFileSizeBytesFrom(const QComboBox* unitCombo, const QSpinBox* magnitudeSpinBox);
