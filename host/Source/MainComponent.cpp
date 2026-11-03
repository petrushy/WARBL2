#include "MainComponent.h"

MainComponent::MainComponent()
{
    const juce::Colour background { 0xff1e1f22 };
    monitor = new MonitorPage (receiver, audio);
    tabs.addTab ("Monitor", background, monitor, true);
    tabs.addTab ("Instrument", background, new InstrumentPage (audio), true);
    tabs.addTab ("Sensors", background, new SensorsPage (audio), true);
    tabs.setTabBarDepth (30);
    addAndMakeVisible (tabs);
    setSize (1200, 910);
}

MainComponent::~MainComponent()
{
    tabs.clearTabs();  // Pages go before the engine they use.
}

void MainComponent::resized()
{
    tabs.setBounds (getLocalBounds());
}
