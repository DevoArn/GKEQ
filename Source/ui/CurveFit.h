// GKEQ - 24-band dynamic EQ plug-in
// Copyright (C) 2026 Roman Gutekunst (GK)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "../Params.h"
#include <vector>

namespace meq::ui
{

struct FittedBand
{
    FilterType type = FilterType::Bell;
    double freq = 1000.0, gain = 0.0, q = 1.0;
};

/** Fits up to maxBands bells/shelves to a target curve given in dB on the
    analyser point grid (only points i0..i1 are considered). Greedy placement
    on the largest residual followed by joint refinement. */
std::vector<FittedBand> fitCurve (const std::vector<double>& target, int i0, int i1, int maxBands,
                                  double sampleRate, double& residualRmsDb, double tolerance = 0.35);

} // namespace meq::ui
