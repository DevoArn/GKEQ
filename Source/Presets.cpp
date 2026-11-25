// GKEQ - 24-band dynamic EQ plug-in
// Copyright (C) 2026 Roman Gutekunst (GK)
// SPDX-License-Identifier: GPL-3.0-or-later

#include "Presets.h"

namespace meq::presets
{

StateBuilder& StateBuilder::set (const juce::String& id, float value)
{
    for (auto child : state)
    {
        if (child.getProperty ("id").toString() == id)
        {
            child.setProperty ("value", value, nullptr);
            return *this;
        }
    }
    juce::ValueTree p ("PARAM");
    p.setProperty ("id", id, nullptr);
    p.setProperty ("value", value, nullptr);
    state.addChild (p, -1, nullptr);
    return *this;
}

StateBuilder& StateBuilder::band (int b, FilterType t, double freq, double gain, double q, int slopeIdx, Placement place)
{
    set (ID::band (b, ID::used), 1.0f);
    set (ID::band (b, ID::enabled), 1.0f);
    set (ID::band (b, ID::type), (float) (int) t);
    set (ID::band (b, ID::freq), (float) freq);
    set (ID::band (b, ID::gain), (float) gain);
    set (ID::band (b, ID::q), (float) q);
    set (ID::band (b, ID::slope), (float) slopeIdx);
    set (ID::band (b, ID::placement), (float) (int) place);
    return *this;
}

StateBuilder& StateBuilder::dynamic (int b, double range, double threshold, double attackMs, double releaseMs)
{
    set (ID::band (b, ID::dynOn), 1.0f);
    set (ID::band (b, ID::dynRange), (float) range);
    set (ID::band (b, ID::threshold), (float) threshold);
    set (ID::band (b, ID::attack), (float) attackMs);
    set (ID::band (b, ID::release), (float) releaseMs);
    return *this;
}

// ---------------------------------------------------------------------------
juce::StringArray factoryNames()
{
    return { "Default",
             "Vocal - Presence & De-Ess",
             "Vocal - Warm Body",
             "Kick - Punch",
             "Bass - Tighten (Dynamic)",
             "Acoustic Guitar - Sparkle",
             "Drum Bus - Glue",
             "Master - Air & Weight",
             "Master - Linear Phase Polish",
             "Mid/Side - Wider Top, Mono Bass",
             "Telephone",
             "De-Mud (Dynamic)",
             "Tilt - Brighter",
             "Rumble & Hiss Clean-up" };
}

juce::ValueTree makeFactory (int index, const juce::ValueTree& defaults)
{
    StateBuilder s (defaults.createCopy());
    using T = FilterType;
    using P = Placement;
    constexpr double bq = kDefaultQ;

    switch (index)
    {
        case 1:
            s.band (0, T::LowCut, 90, 0, bq, 3)
             .band (1, T::Bell, 280, -2.5, 1.2)
             .band (2, T::Bell, 3200, 2.5, 0.9)
             .band (3, T::Bell, 7200, 0, 3.0).dynamic (3, -6, -32, 2, 80)
             .band (4, T::HighShelf, 11000, 2.0, bq);
            break;
        case 2:
            s.band (0, T::LowCut, 70, 0, bq, 3)
             .band (1, T::Bell, 180, 2.0, 0.8)
             .band (2, T::Bell, 900, -1.5, 1.4)
             .band (3, T::Bell, 4500, -1.0, 2.0).dynamic (3, -3, -30)
             .band (4, T::HighShelf, 9000, 1.0, 0.5);
            break;
        case 3:
            s.band (0, T::LowCut, 28, 0, bq, 6)
             .band (1, T::Bell, 58, 3.0, 1.4)
             .band (2, T::Bell, 380, -4.0, 2.2)
             .band (3, T::Bell, 3800, 2.5, 1.6)
             .band (4, T::HighCut, 14000, 0, bq, 1);
            break;
        case 4:
            s.band (0, T::LowCut, 32, 0, bq, 6)
             .band (1, T::Bell, 85, 1.5, 1.0)
             .band (2, T::Bell, 220, 0, 1.6).dynamic (2, -5, -28, 8, 120)
             .band (3, T::Bell, 900, 1.5, 1.2)
             .band (4, T::HighCut, 7000, 0, bq, 1);
            break;
        case 5:
            s.band (0, T::LowCut, 100, 0, bq, 3)
             .band (1, T::Bell, 250, -2.5, 1.5)
             .band (2, T::Bell, 2500, -1.0, 2.5).dynamic (2, -3, -30)
             .band (3, T::HighShelf, 8000, 3.0, 0.6);
            break;
        case 6:
            s.band (0, T::LowCut, 30, 0, bq, 3)
             .band (1, T::LowShelf, 80, 1.5, bq)
             .band (2, T::Bell, 450, -2.0, 1.0)
             .band (3, T::Bell, 5000, 1.5, 0.7)
             .band (4, T::HighShelf, 12000, 1.0, 0.5);
            break;
        case 7:
            s.global (ID::mode, (float) (int) ProcMode::NaturalPhase);
            s.band (0, T::LowCut, 18, 0, bq, 1)
             .band (1, T::LowShelf, 70, 1.0, 0.6)
             .band (2, T::Bell, 350, -0.8, 0.8)
             .band (3, T::HighShelf, 12000, 1.5, 0.5);
            break;
        case 8:
            s.global (ID::mode, (float) (int) ProcMode::LinearPhase).global (ID::lpRes, 2.0f);
            s.band (0, T::LowCut, 22, 0, bq, 6)
             .band (1, T::Bell, 220, -0.8, 0.9)
             .band (2, T::Bell, 2800, -0.6, 1.4).dynamic (2, -2, -24)
             .band (3, T::TiltShelf, 1000, 1.2, 0.5, 0);
            break;
        case 9:
            s.band (0, T::LowCut, 140, 0, bq, 3, P::Side)
             .band (1, T::HighShelf, 7000, 2.5, 0.6, 1, P::Side)
             .band (2, T::Bell, 3000, -0.8, 1.0, 1, P::Mid);
            break;
        case 10:
            s.band (0, T::LowCut, 380, 0, bq, 6)
             .band (1, T::HighCut, 3400, 0, bq, 6)
             .band (2, T::Bell, 1500, 6.0, 1.2);
            s.global (ID::character, (float) (int) CharacterMode::Warm);
            break;
        case 11:
            s.band (0, T::Bell, 320, 0, 1.4).dynamic (0, -5, -26, 15, 180)
             .band (1, T::Bell, 600, 0, 2.0).dynamic (1, -3, -30, 15, 180);
            break;
        case 12:
            s.band (0, T::FlatTilt, 1000, 4.0, 1.0);
            break;
        case 13:
            s.band (0, T::LowCut, 35, 0, bq, 7)
             .band (1, T::HighCut, 17000, 0, bq, 3)
             .band (2, T::Notch, 50, 0, 8.0, 1);
            break;
        default:
            break;
    }
    return s.get();
}

// ---------------------------------------------------------------------------
juce::File userFolder()
{
    const auto docs = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);
    auto f = docs.getChildFile ("GKEQ").getChildFile ("Presets");
    if (! f.exists())
    {
        // Presets saved under the previous product name are carried over.
        const auto old = docs.getChildFile ("Modern EQ").getChildFile ("Presets");
        if (old.isDirectory()) old.copyDirectoryTo (f);
        else                   f.createDirectory();
    }
    return f;
}

juce::Array<juce::File> listUserPresets()
{
    auto files = userFolder().findChildFiles (juce::File::findFiles, false, "*.meqpreset");
    files.sort();
    return files;
}

bool saveUserPreset (const juce::ValueTree& params, const juce::String& name)
{
    const auto safe = juce::File::createLegalFileName (name.trim());
    if (safe.isEmpty()) return false;
    auto xml = params.createXml();
    if (xml == nullptr) return false;
    return xml->writeTo (userFolder().getChildFile (safe + ".meqpreset"));
}

juce::ValueTree loadUserPreset (const juce::File& file)
{
    if (auto xml = juce::XmlDocument::parse (file))
        return juce::ValueTree::fromXml (*xml);
    return {};
}

} // namespace meq::presets
