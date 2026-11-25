#include "InstanceRegistry.h"
#include "PluginProcessor.h"

namespace meq
{

InstanceRegistry& InstanceRegistry::get()
{
    static InstanceRegistry instance;
    return instance;
}

int InstanceRegistry::add (ModernEQProcessor* p)
{
    const juce::ScopedLock sl (lock);
    instances.addIfNotAlreadyThere (p);
    return nextId++;
}

void InstanceRegistry::remove (ModernEQProcessor* p)
{
    const juce::ScopedLock sl (lock);
    instances.removeAllInstancesOf (p);
}

std::vector<InstanceRegistry::Info> InstanceRegistry::list() const
{
    const juce::ScopedLock sl (lock);
    std::vector<Info> out;
    for (auto* p : instances)
        out.push_back ({ p->getInstanceId(), p->getInstanceName(), p->getTrackColour() });
    return out;
}

bool InstanceRegistry::readSpectrumSource (int id, float* dest, int n, double& sampleRate) const
{
    const juce::ScopedLock sl (lock);
    for (auto* p : instances)
    {
        if (p->getInstanceId() == id)
        {
            p->postTap.read (dest, n);
            sampleRate = p->getCurrentSampleRate();
            return true;
        }
    }
    return false;
}

} // namespace meq
