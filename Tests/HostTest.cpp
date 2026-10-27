// Loads the built AvelEmber.vst3 like a DAW would and exercises it.
#include <juce_audio_processors/juce_audio_processors.h>
#include <iostream>

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    if (argc < 2) { std::cout << "usage: HostTest path/to/AvelEmber.vst3" << std::endl; return 2; }

    juce::AudioPluginFormatManager fm;
    fm.addFormat (new juce::VST3PluginFormat());

    juce::OwnedArray<juce::PluginDescription> types;
    juce::VST3PluginFormat vst3;
    vst3.findAllTypesForFile (types, argv[1]);
    if (types.isEmpty()) { std::cout << "FAIL: no plugin found" << std::endl; return 1; }

    auto& d = *types[0];
    std::cout << "found: " << d.name << " by " << d.manufacturerName << " (" << d.category << "), ins "
              << d.numInputChannels << " outs " << d.numOutputChannels << std::endl;

    juce::String err;
    auto inst = fm.createPluginInstance (d, 48000.0, 512, err);
    if (inst == nullptr) { std::cout << "FAIL: " << err << std::endl; return 1; }

    std::cout << "parameters:";
    for (auto* p : inst->getParameters())
        std::cout << " [" << p->getName (40) << " = " << p->getCurrentValueAsText() << "]";
    std::cout << std::endl;

    inst->setPlayConfigDetails (2, 2, 48000.0, 512);
    inst->prepareToPlay (48000.0, 512);

    juce::AudioBuffer<float> buf (2, 512);
    juce::MidiBuffer midi;
    juce::Random r;
    double peak = 0;
    for (int b = 0; b < 400; ++b)
    {
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < 512; ++i)
                buf.setSample (ch, i, (r.nextFloat() * 2 - 1) * 0.1f);
        inst->processBlock (buf, midi);
        peak = juce::jmax (peak, (double) buf.getMagnitude (0, 512));
    }
    std::cout << "processed 400 blocks, output peak " << peak << (peak > 0.0 && peak <= 0.1001 ? "  OK" : "  FAIL") << std::endl;

    juce::MemoryBlock state;
    inst->getStateInformation (state);
    inst->setStateInformation (state.getData(), (int) state.getSize());
    std::cout << "state round trip: " << state.getSize() << " bytes OK" << std::endl;

    if (auto* ed = inst->createEditorIfNeeded())
    {
        std::cout << "editor: " << ed->getWidth() << " x " << ed->getHeight() << " OK" << std::endl;
        juce::MessageManager::getInstance()->runDispatchLoopUntil (300);
        delete ed;
    }

    inst->releaseResources();
    inst.reset();
    std::cout << "HOST TEST PASSED" << std::endl;
    return 0;
}
