#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Params.h"

namespace meq::presets
{

/** Edits a parameter tree (as produced by APVTS::copyState). */
class StateBuilder
{
public:
    explicit StateBuilder (juce::ValueTree defaults) : state (std::move (defaults)) {}

    StateBuilder& set (const juce::String& id, float value);
    StateBuilder& global (const char* id, float value) { return set (id, value); }
    StateBuilder& band (int b, FilterType t, double freq, double gain, double q, int slopeIdx = 1,
                        Placement place = Placement::Stereo);
    StateBuilder& dynamic (int b, double range, double threshold, double attackMs = 10.0, double releaseMs = 150.0);

    juce::ValueTree get() const { return state; }

private:
    juce::ValueTree state;
};

juce::StringArray factoryNames();
juce::ValueTree   makeFactory (int index, const juce::ValueTree& defaults);

juce::File               userFolder();
juce::Array<juce::File>  listUserPresets();
bool                     saveUserPreset (const juce::ValueTree& params, const juce::String& name);
juce::ValueTree          loadUserPreset (const juce::File& file);

} // namespace meq::presets
