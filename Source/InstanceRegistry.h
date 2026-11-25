// GKEQ - 24-band dynamic EQ plug-in
// Copyright (C) 2026 Roman Gutekunst (GK)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <juce_core/juce_core.h>
#include <juce_graphics/juce_graphics.h>
#include "SnapshotBuffer.h"

namespace meq
{

class ModernEQProcessor;

/** Process-wide list of GKEQ instances (all instances inside one host
    process share it), used for the instance spectrum overlay, collision
    detection and EQ matching against another track. */
class InstanceRegistry
{
public:
    struct Info
    {
        int          id = 0;
        juce::String name;
        juce::Colour colour;
    };

    static InstanceRegistry& get();

    int  add (ModernEQProcessor* p);
    void remove (ModernEQProcessor* p);

    std::vector<Info> list() const;

    /** Copies the latest post-EQ samples of instance id; false if gone. */
    bool readSpectrumSource (int id, float* dest, int n, double& sampleRate) const;

private:
    InstanceRegistry() = default;

    mutable juce::CriticalSection lock;
    juce::Array<ModernEQProcessor*> instances;
    int nextId = 1;
};

} // namespace meq
