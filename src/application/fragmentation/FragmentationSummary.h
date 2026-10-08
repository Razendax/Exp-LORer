#pragma once

#include <cstddef>
#include <cstdint>

// Drive/scan-level rollup of a FileFragmentationInfo list (Architecture.md §14.52), always computed
// on demand via FragmentationUseCase::summarize -- never persisted itself, so there's exactly one
// source of truth.
struct FragmentationSummary
{
    std::size_t filesScanned = 0;
    std::uintmax_t totalBytesScanned = 0;
    std::size_t fragmentedFileCount = 0;
    std::uint64_t totalFragments = 0;
};
