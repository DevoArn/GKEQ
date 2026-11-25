// GKEQ - 24-band dynamic EQ plug-in
// Copyright (C) 2026 Roman Gutekunst (GK)
// SPDX-License-Identifier: GPL-3.0-or-later

// Loads the built VST3 like a DAW would, exercises it and renders the editor to PNG.
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <cstdio>
#include <complex>

static int failures = 0;
#define CHECK(cond, ...) do { if (! (cond)) { ++failures; std::printf ("  FAIL: "); std::printf (__VA_ARGS__); std::printf ("\n"); } else { std::printf ("  ok: "); std::printf (__VA_ARGS__); std::printf ("\n"); } } while (0)

static juce::AudioProcessorParameter* findParam (juce::AudioPluginInstance& p, const juce::String& name)
{
    for (auto* prm : p.getParameters())
        if (prm->getName (100) == name) return prm;
    return nullptr;
}

static void setParamValue (juce::AudioPluginInstance& p, const juce::String& name, float denormalised)
{
    auto* prm = findParam (p, name);
    if (prm == nullptr) { std::printf ("  (parameter '%s' not found)\n", name.toRawUTF8()); ++failures; return; }
    if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (prm))
        prm->setValueNotifyingHost (ranged->convertTo0to1 (denormalised));
    else
    {
        // Hosted VST3 parameters are not ranged: search the normalised value whose text matches.
        float best = 0.0f; double bestErr = 1.0e30;
        for (int i = 0; i <= 4000; ++i)
        {
            const float v = i / 4000.0f;
            const double t = prm->getText (v, 64).retainCharacters ("-0123456789.").getDoubleValue()
                           * (prm->getText (v, 64).containsIgnoreCase ("khz") ? 1000.0 : 1.0);
            const double e = std::abs (t - denormalised);
            if (e < bestErr) { bestErr = e; best = v; }
        }
        prm->setValueNotifyingHost (best);
    }
}

static double gainAtDb (const std::vector<float>& h, double w)
{
    std::complex<double> s = 0.0;
    for (size_t n = 0; n < h.size(); ++n) s += (double) h[n] * std::polar (1.0, -w * (double) n);
    return 20.0 * std::log10 (std::abs (s) + 1.0e-12);
}

static std::vector<float> impulse (juce::AudioPluginInstance& p, int len)
{
    juce::AudioBuffer<float> buf (p.getTotalNumInputChannels(), 512);
    juce::MidiBuffer midi;
    std::vector<float> out;
    for (int pos = 0; pos < len; pos += 512)
    {
        buf.clear();
        if (pos == 0) { buf.setSample (0, 0, 1.0f); buf.setSample (1, 0, 1.0f); }
        p.processBlock (buf, midi);
        for (int i = 0; i < 512; ++i) out.push_back (buf.getSample (0, i));
    }
    return out;
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::String path = argc > 1 ? juce::String (argv[1]) : juce::String ("build/ModernEQ_artefacts/Release/VST3/GKEQ.vst3");
    const juce::String pngPath = argc > 2 ? juce::String (argv[2]) : juce::String ("editor.png");

    juce::AudioPluginFormatManager fm;
    fm.addDefaultFormats();
    juce::OwnedArray<juce::PluginDescription> types;
    for (auto* f : fm.getFormats())
        if (f->getName() == "VST3")
            f->findAllTypesForFile (types, juce::File::getCurrentWorkingDirectory().getChildFile (path).getFullPathName());

    std::printf ("[1] Load\n");
    CHECK (types.size() == 1, "found %d plugin type(s) in the bundle", types.size());
    if (types.isEmpty()) return 1;
    std::printf ("  %s by %s, category '%s'\n", types[0]->name.toRawUTF8(), types[0]->manufacturerName.toRawUTF8(), types[0]->category.toRawUTF8());

    juce::String err;
    auto plugin = fm.createPluginInstance (*types[0], 48000.0, 512, err);
    CHECK (plugin != nullptr, "instantiated %s", err.toRawUTF8());
    if (plugin == nullptr) return 1;

    std::printf ("[2] Buses & parameters\n");
    std::printf ("  inputs: %d buses, outputs: %d buses, params: %d\n", plugin->getBusCount (true), plugin->getBusCount (false), plugin->getParameters().size());
    CHECK (plugin->getBusCount (true) == 2, "side-chain input bus present");
    CHECK (plugin->getParameters().size() >= 24 * 15, "parameter count %d", plugin->getParameters().size());

    plugin->setPlayConfigDetails (4, 2, 48000.0, 512);
    plugin->prepareToPlay (48000.0, 512);

    std::printf ("[3] Transparent with no bands\n");
    {
        juce::AudioBuffer<float> buf (plugin->getTotalNumInputChannels(), 512);
        juce::MidiBuffer midi;
        juce::Random rng (3);
        double maxErr = 0.0;
        for (int blk = 0; blk < 50; ++blk)
        {
            juce::AudioBuffer<float> ref (2, 512);
            for (int ch = 0; ch < buf.getNumChannels(); ++ch)
                for (int i = 0; i < 512; ++i)
                    buf.setSample (ch, i, ch < 2 ? rng.nextFloat() - 0.5f : 0.0f);
            for (int ch = 0; ch < 2; ++ch) ref.copyFrom (ch, 0, buf, ch, 0, 512);
            plugin->processBlock (buf, midi);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 512; ++i)
                    maxErr = std::max (maxErr, (double) std::abs (buf.getSample (ch, i) - ref.getSample (ch, i)));
        }
        CHECK (maxErr < 1.0e-6, "flat state is bit-transparent-ish (max dev %.2e)", maxErr);
    }

    std::printf ("[4] Bell +6 dB @ 1 kHz via host parameters\n");
    setParamValue (*plugin, "Band 1 Shape", 0.0f);
    setParamValue (*plugin, "Band 1 Frequency", 1000.0f);
    setParamValue (*plugin, "Band 1 Gain", 6.0f);
    setParamValue (*plugin, "Band 1 Q", 1.0f);
    if (auto* u = findParam (*plugin, "Band 1 Used")) u->setValueNotifyingHost (1.0f);
    {
        impulse (*plugin, 8192);   // let smoothing settle
        auto h = impulse (*plugin, 16384);
        const double g1k = gainAtDb (h, 2.0 * juce::MathConstants<double>::pi * 1000.0 / 48000.0);
        const double g100 = gainAtDb (h, 2.0 * juce::MathConstants<double>::pi * 100.0 / 48000.0);
        CHECK (std::abs (g1k - 6.0) < 0.15, "gain at 1 kHz = %.2f dB", g1k);
        CHECK (std::abs (g100) < 0.3, "gain at 100 Hz = %.2f dB", g100);
    }

    std::printf ("[5] Linear phase mode reports latency\n");
    setParamValue (*plugin, "Processing Mode", 2.0f);
    if (auto* m = findParam (*plugin, "Processing Mode")) m->setValueNotifyingHost (1.0f);
    for (int i = 0; i < 20; ++i) { impulse (*plugin, 512); juce::MessageManager::getInstance()->runDispatchLoopUntil (5); }
    CHECK (plugin->getLatencySamples() == 4096, "latency after switching to Linear Phase = %d", plugin->getLatencySamples());
    if (auto* m = findParam (*plugin, "Processing Mode")) m->setValueNotifyingHost (0.0f);
    for (int i = 0; i < 20; ++i) { impulse (*plugin, 512); juce::MessageManager::getInstance()->runDispatchLoopUntil (5); }
    CHECK (plugin->getLatencySamples() == 0, "latency back in Zero Latency = %d", plugin->getLatencySamples());

    std::printf ("[6] State round trip\n");
    {
        juce::MemoryBlock state;
        plugin->getStateInformation (state);
        if (auto* g = findParam (*plugin, "Band 1 Gain")) g->setValueNotifyingHost (0.5f);
        plugin->setStateInformation (state.getData(), (int) state.getSize());
        auto* g = findParam (*plugin, "Band 1 Gain");
        const auto txt = g != nullptr ? g->getCurrentValueAsText() : juce::String();
        CHECK (txt.contains ("6.0"), "gain restored from state ('%s'), %d bytes", txt.toRawUTF8(), (int) state.getSize());
    }

    std::printf ("[7] Editor\n");
    {
        // A few more bands for a representative screenshot
        auto band = [&] (int b, float type, float f, float g, float q, float slope = 1.0f)
        {
            const auto pre = "Band " + juce::String (b) + " ";
            if (auto* p = findParam (*plugin, pre + "Shape")) p->setValueNotifyingHost (type / 9.0f);
            setParamValue (*plugin, pre + "Frequency", f);
            setParamValue (*plugin, pre + "Gain", g);
            setParamValue (*plugin, pre + "Q", q);
            if (auto* p = findParam (*plugin, pre + "Slope")) p->setValueNotifyingHost (slope / 9.0f);
            if (auto* p = findParam (*plugin, pre + "Used")) p->setValueNotifyingHost (1.0f);
        };
        band (2, 2.0f, 40.0f, 0.0f, 0.71f, 3.0f);    // low cut 24 dB/oct
        band (3, 3.0f, 9000.0f, 3.5f, 0.71f);        // high shelf
        band (4, 0.0f, 320.0f, -4.0f, 2.5f);         // bell cut
        band (5, 5.0f, 4200.0f, 0.0f, 6.0f);         // notch
        if (auto* p = findParam (*plugin, "Band 4 Dynamics")) p->setValueNotifyingHost (1.0f);
        setParamValue (*plugin, "Band 4 Dynamic Range", -6.0f);

        std::unique_ptr<juce::AudioProcessorEditor> ed (plugin->createEditorIfNeeded());
        CHECK (ed != nullptr, "editor created");
        if (ed != nullptr)
        {
            std::printf ("  editor size %d x %d\n", ed->getWidth(), ed->getHeight());
            // Feed pink-ish noise so the analyser has something to show.
            juce::AudioBuffer<float> buf (plugin->getTotalNumInputChannels(), 512);
            juce::MidiBuffer midi;
            juce::Random rng (11);
            float b0 = 0, b1 = 0, b2 = 0;
            for (int blk = 0; blk < 200; ++blk)
            {
                buf.clear();
                for (int i = 0; i < 512; ++i)
                {
                    const float w = rng.nextFloat() * 2.0f - 1.0f;
                    b0 = 0.99765f * b0 + w * 0.0990460f; b1 = 0.96300f * b1 + w * 0.2965164f; b2 = 0.57000f * b2 + w * 1.0526913f;
                    const float pink = 0.08f * (b0 + b1 + b2 + w * 0.1848f);
                    buf.setSample (0, i, pink);
                    buf.setSample (1, i, pink);
                }
                plugin->processBlock (buf, midi);
                juce::MessageManager::getInstance()->runDispatchLoopUntil (4);
            }
            auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
            juce::File out = juce::File::getCurrentWorkingDirectory().getChildFile (pngPath);
            out.deleteFile();
            juce::FileOutputStream os (out);
            juce::PNGImageFormat png;
            CHECK (png.writeImageToStream (img, os), "editor rendered to %s", out.getFullPathName().toRawUTF8());
            plugin->editorBeingDeleted (ed.get());
        }
    }

    plugin->releaseResources();
    plugin.reset();
    std::printf ("\n%s (%d failure%s)\n", failures == 0 ? "HOST TEST PASSED" : "HOST TEST FAILED", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
