#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

class Prophet6ControlAudioProcessorEditor : public juce::AudioProcessorEditor,
                                             private juce::Timer
{
public:
    explicit Prophet6ControlAudioProcessorEditor(Prophet6ControlAudioProcessor&);
    ~Prophet6ControlAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void openPresetFile();
    void savePresetFile();
    void saveSysExFile();
    void showGlobalSettings(bool shouldShow);

    Prophet6ControlAudioProcessor& processor;
    juce::Viewport viewport;
    std::unique_ptr<juce::Component> mainSurface;
    std::unique_ptr<juce::Component> globalSurface;
    bool globalsMode = false;

    juce::TextButton panelButton { "PANEL" };
    juce::TextButton globalsButton { "GLOBALS" };
    juce::TextButton loadButton { "LOAD" };
    juce::TextButton saveButton { "SAVE" };
    juce::TextButton requestDumpButton { "REQUEST PATCH" };
    juce::TextButton exportDumpButton { "EXPORT SYSEX" };
    juce::TextButton sendAllButton { "SEND PARAMETERS" };
    juce::TextButton sendGlobalsButton { "SEND GLOBALS" };
    juce::HyperlinkButton hardwareLink { "Prophet 6 Desktop reference", juce::URL("https://sequential.com/modern-analog/prophet-6/") };

    juce::Label recallPromptLabel;
    juce::TextButton recallHardwareButton { "RECALL HARDWARE" };
    juce::TextButton dismissRecallButton { "NOT NOW" };
    juce::Label channelLabel;
    juce::ComboBox channelBox;
    juce::Label statusLabel;
    std::unique_ptr<juce::FileChooser> fileChooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Prophet6ControlAudioProcessorEditor)
};
