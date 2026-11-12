#include "PluginEditor.h"
#include "ParameterMap.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace
{
    constexpr juce::uint32 panelDark = 0xff15191d;
    constexpr juce::uint32 panel = 0xff20262b;
    constexpr juce::uint32 panelRaised = 0xff292f34;
    constexpr juce::uint32 panelInset = 0xff101417;
    constexpr juce::uint32 edge = 0xff4a5055;
    constexpr juce::uint32 ivory = 0xffe9e1d0;
    constexpr juce::uint32 muted = 0xffa9a69e;
    constexpr juce::uint32 signal = 0xffd64d37;
    constexpr juce::uint32 walnut = 0xff79523c;
    constexpr juce::uint32 walnutLight = 0xffa17453;

    class ProphetLookAndFeel final : public juce::LookAndFeel_V4
    {
    public:
        ProphetLookAndFeel()
        {
            setColour(juce::Slider::textBoxTextColourId, juce::Colour(ivory));
            setColour(juce::Slider::textBoxBackgroundColourId, juce::Colour(panelInset));
            setColour(juce::Slider::textBoxOutlineColourId, juce::Colour(edge));
            setColour(juce::ComboBox::backgroundColourId, juce::Colour(panelInset));
            setColour(juce::ComboBox::textColourId, juce::Colour(ivory));
            setColour(juce::ComboBox::outlineColourId, juce::Colour(edge));
            setColour(juce::PopupMenu::backgroundColourId, juce::Colour(panel));
            setColour(juce::PopupMenu::textColourId, juce::Colour(ivory));
            setColour(juce::ToggleButton::textColourId, juce::Colour(ivory));
            setColour(juce::ToggleButton::tickColourId, juce::Colour(signal));
            setColour(juce::TextButton::buttonColourId, juce::Colour(0xff343b40));
            setColour(juce::TextButton::textColourOffId, juce::Colour(ivory));
            setColour(juce::TextButton::textColourOnId, juce::Colours::white);
        }

        void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                              float sliderPos, float startAngle, float endAngle,
                              juce::Slider& slider) override
        {
            juce::ignoreUnused(slider);
            const auto radius = juce::jmax(8.0f, (static_cast<float>(juce::jmin(width, height)) - 13.0f) * 0.5f);
            const auto cx = static_cast<float>(x) + static_cast<float>(width) * 0.5f;
            const auto cy = static_cast<float>(y) + radius + 3.0f;
            const auto angle = startAngle + sliderPos * (endAngle - startAngle);

            juce::Path track;
            track.addCentredArc(cx, cy, radius, radius, 0.0f, startAngle, endAngle, true);
            g.setColour(juce::Colour(0xff4b5359));
            g.strokePath(track, juce::PathStrokeType(3.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

            juce::Path arc;
            arc.addCentredArc(cx, cy, radius, radius, 0.0f, startAngle, angle, true);
            g.setColour(juce::Colour(signal));
            g.strokePath(arc, juce::PathStrokeType(4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

            const auto knobRadius = radius - 5.0f;
            juce::ColourGradient knob(juce::Colour(0xff4a5055), cx - knobRadius, cy - knobRadius,
                                      juce::Colour(0xff171b1f), cx + knobRadius, cy + knobRadius, false);
            g.setGradientFill(knob);
            g.fillEllipse(cx - knobRadius, cy - knobRadius, knobRadius * 2.0f, knobRadius * 2.0f);
            g.setColour(juce::Colour(0xff090b0d).withAlpha(0.8f));
            g.drawEllipse(cx - knobRadius, cy - knobRadius, knobRadius * 2.0f, knobRadius * 2.0f, 1.0f);

            juce::Path pointer;
            pointer.addRoundedRectangle(-1.25f, -knobRadius + 4.0f, 2.5f, knobRadius * 0.43f, 1.1f);
            g.setColour(juce::Colour(ivory));
            g.fillPath(pointer, juce::AffineTransform::rotation(angle).translated(cx, cy));
            g.setColour(juce::Colour(0xff0c0e10));
            g.fillEllipse(cx - 2.0f, cy - 2.0f, 4.0f, 4.0f);
        }

        void drawButtonBackground(juce::Graphics& g, juce::Button& button,
                                  const juce::Colour& background, bool highlighted, bool down) override
        {
            auto bounds = button.getLocalBounds().toFloat().reduced(0.8f);
            auto fill = background;
            if (down) fill = fill.brighter(0.16f);
            else if (highlighted) fill = fill.brighter(0.08f);
            g.setColour(fill);
            g.fillRoundedRectangle(bounds, 4.0f);
            g.setColour(button.getToggleState() ? juce::Colour(signal) : juce::Colour(edge));
            g.drawRoundedRectangle(bounds, 4.0f, button.getToggleState() ? 1.4f : 0.8f);
        }
    };

    ProphetLookAndFeel& sharedLookAndFeel()
    {
        static ProphetLookAndFeel instance;
        return instance;
    }

    struct ControlItem
    {
        std::unique_ptr<juce::Label> label;
        std::unique_ptr<juce::Component> control;
    };

    class ParameterGroup final : public juce::Component
    {
    public:
        ParameterGroup(Prophet6ControlAudioProcessor& processor, const juce::String& heading,
                       std::vector<juce::String> sections, int columns,
                       p6::ParameterDomain domain = p6::ParameterDomain::Program)
            : title({}, heading), columnCount(juce::jmax(1, columns)),
              laf(sharedLookAndFeel()), parameterDomain(domain)
        {
            title.setFont(juce::Font(11.0f, juce::Font::bold));
            title.setColour(juce::Label::textColourId, juce::Colour(ivory));
            title.setJustificationType(juce::Justification::centredLeft);
            addAndMakeVisible(title);

            for (const auto& definition : p6::parameterDefinitions())
            {
                if (definition.domain != parameterDomain)
                    continue;
                if (std::find(sections.begin(), sections.end(), definition.section) == sections.end())
                    continue;

                ControlItem item;
                const auto tooltip = juce::String(definition.name) + "   |   NRPN " + juce::String(definition.nrpn);

                if (definition.toggle)
                {
                    auto toggle = std::make_unique<juce::ToggleButton>(definition.name);
                    toggle->setLookAndFeel(&laf);
                    toggle->setTooltip(tooltip);
                    toggle->setColour(juce::ToggleButton::textColourId, juce::Colour(ivory));
                    toggle->setColour(juce::ToggleButton::tickColourId, juce::Colour(signal));
                    if (definition.readOnly) toggle->setEnabled(false);
                    buttonAttachments.push_back(std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
                        processor.state, definition.id, *toggle));
                    item.control = std::move(toggle);
                }
                else if (definition.maximum - definition.minimum <= 9)
                {
                    item.label = createLabel(definition.name);
                    auto combo = std::make_unique<juce::ComboBox>();
                    for (int value = definition.minimum; value <= definition.maximum; ++value)
                        combo->addItem(juce::String(value), value - definition.minimum + 1);
                    combo->setLookAndFeel(&laf);
                    combo->setTooltip(tooltip);
                    if (definition.readOnly) combo->setEnabled(false);
                    comboAttachments.push_back(std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
                        processor.state, definition.id, *combo));
                    item.control = std::move(combo);
                }
                else
                {
                    item.label = createLabel(definition.name);
                    auto slider = std::make_unique<juce::Slider>();
                    slider->setLookAndFeel(&laf);
                    slider->setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
                    slider->setTextBoxStyle(juce::Slider::TextBoxBelow, false, 48, 14);
                    slider->setRange(definition.minimum, definition.maximum, 1.0);
                    slider->setTooltip(tooltip);
                    slider->setColour(juce::Slider::rotarySliderFillColourId, juce::Colour(signal));
                    slider->setColour(juce::Slider::rotarySliderOutlineColourId, juce::Colour(0xff4b5359));
                    slider->setColour(juce::Slider::thumbColourId, juce::Colour(ivory));
                    if (definition.readOnly) slider->setEnabled(false);
                    sliderAttachments.push_back(std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
                        processor.state, definition.id, *slider));
                    item.control = std::move(slider);
                }

                if (item.label != nullptr) addAndMakeVisible(*item.label);
                addAndMakeVisible(*item.control);
                items.push_back(std::move(item));
            }
        }

        ~ParameterGroup() override
        {
            for (auto& item : items)
                if (item.control != nullptr) item.control->setLookAndFeel(nullptr);
        }

        void paint(juce::Graphics& g) override
        {
            const auto bounds = getLocalBounds().toFloat();
            g.setColour(juce::Colour(panelRaised));
            g.fillRoundedRectangle(bounds, 7.0f);
            g.setColour(juce::Colour(edge));
            g.drawRoundedRectangle(bounds.reduced(0.5f), 7.0f, 0.8f);
            g.setColour(juce::Colour(signal));
            g.fillRoundedRectangle(bounds.getX() + 10.0f, bounds.getY() + 7.0f, 22.0f, 2.0f, 1.0f);
        }

        void resized() override
        {
            auto area = getLocalBounds().reduced(8, 7);
            title.setBounds(area.removeFromTop(20));
            const auto rows = juce::jmax(1, static_cast<int>((items.size() + static_cast<std::size_t>(columnCount) - 1)
                                                       / static_cast<std::size_t>(columnCount)));
            const auto cellHeight = juce::jmax(53, area.getHeight() / rows);
            const auto cellWidth = juce::jmax(40, area.getWidth() / columnCount);

            for (std::size_t i = 0; i < items.size(); ++i)
            {
                const auto column = static_cast<int>(i % static_cast<std::size_t>(columnCount));
                const auto row = static_cast<int>(i / static_cast<std::size_t>(columnCount));
                auto cell = juce::Rectangle<int>(area.getX() + column * cellWidth,
                                                  area.getY() + row * cellHeight, cellWidth - 2, cellHeight - 1);
                if (items[i].label != nullptr) items[i].label->setBounds(cell.removeFromTop(13));
                if (dynamic_cast<juce::Slider*>(items[i].control.get()) != nullptr)
                {
                    items[i].control->setBounds(cell.withSizeKeepingCentre(juce::jmin(68, cell.getWidth()),
                                                                            juce::jmin(57, cell.getHeight())));
                }
                else if (dynamic_cast<juce::ComboBox*>(items[i].control.get()) != nullptr)
                {
                    items[i].control->setBounds(cell.withSizeKeepingCentre(juce::jmax(38, cell.getWidth() - 3),
                                                                            juce::jmin(24, cell.getHeight())));
                }
                else
                {
                    items[i].control->setBounds(cell.withSizeKeepingCentre(juce::jmax(38, cell.getWidth() - 2),
                                                                            juce::jmin(34, cell.getHeight())));
                }
            }
        }

    private:
        static std::unique_ptr<juce::Label> createLabel(const juce::String& text)
        {
            auto label = std::make_unique<juce::Label>();
            label->setText(text, juce::dontSendNotification);
            label->setFont(juce::Font(9.5f));
            label->setColour(juce::Label::textColourId, juce::Colour(muted));
            label->setJustificationType(juce::Justification::centred);
            return label;
        }

        juce::Label title;
        int columnCount = 4;
        ProphetLookAndFeel& laf;
        p6::ParameterDomain parameterDomain;
        std::vector<ControlItem> items;
        std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>> sliderAttachments;
        std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>> buttonAttachments;
        std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>> comboAttachments;
    };

    class MainPanel final : public juce::Component
    {
    public:
        explicit MainPanel(Prophet6ControlAudioProcessor& processor)
        {
            addGroup(processor, "OSC 1", { "Oscillator 1" }, 4, rowOne);
            addGroup(processor, "OSC 2", { "Oscillator 2" }, 3, rowOne);
            addGroup(processor, "MIXER", { "Mixer" }, 4, rowOne);
            addGroup(processor, "GLIDE / UNISON / VOICE", { "Glide / Unison", "Misc" }, 5, rowOne);

            addGroup(processor, "DUAL FILTER", { "Low-pass Filter", "High-pass Filter" }, 5, rowTwo);
            addGroup(processor, "AMP ENVELOPE", { "Amplifier Envelope" }, 3, rowTwo);
            addGroup(processor, "FILTER ENVELOPE", { "Filter Envelope" }, 4, rowTwo);
            addGroup(processor, "LFO", { "LFO" }, 4, rowTwo);

            addGroup(processor, "MODULATION MATRIX", { "LFO Destinations", "Aftertouch", "Aftertouch Destinations",
                     "Poly Mod", "Poly Mod Destinations" }, 8, rowThree);
            addGroup(processor, "EFFECTS", { "Effects", "Effects 1", "Effects 2" }, 6, rowThree);
            addGroup(processor, "ARP / CLOCK", { "Arpeggiator", "Clock" }, 5, rowThree);

            for (auto& group : rowOne) addAndMakeVisible(*group);
            for (auto& group : rowTwo) addAndMakeVisible(*group);
            for (auto& group : rowThree) addAndMakeVisible(*group);
        }

        void paint(juce::Graphics& g) override
        {
            g.fillAll(juce::Colour(panelDark));
            auto body = getLocalBounds().toFloat().reduced(7.0f);
            g.setColour(juce::Colour(0xff32383d));
            g.fillRoundedRectangle(body, 11.0f);
            g.setColour(juce::Colour(edge));
            g.drawRoundedRectangle(body, 11.0f, 0.8f);
            g.setColour(juce::Colour(walnut));
            g.fillRoundedRectangle(body.getX() + 2.0f, body.getY() + 3.0f, 7.0f, body.getHeight() - 6.0f, 2.5f);
            g.fillRoundedRectangle(body.getRight() - 9.0f, body.getY() + 3.0f, 7.0f, body.getHeight() - 6.0f, 2.5f);
            g.setColour(juce::Colour(walnutLight).withAlpha(0.45f));
            g.drawVerticalLine(static_cast<int>(body.getX() + 5.0f), body.getY() + 9.0f, body.getBottom() - 9.0f);
            g.drawVerticalLine(static_cast<int>(body.getRight() - 6.0f), body.getY() + 9.0f, body.getBottom() - 9.0f);
            g.setColour(juce::Colour(panelDark));
            g.drawHorizontalLine(212, body.getX() + 19.0f, body.getRight() - 19.0f);
            g.drawHorizontalLine(427, body.getX() + 19.0f, body.getRight() - 19.0f);
        }

        void resized() override
        {
            placeRow(rowOne, { 260, 290, 250, 420 }, 18, 184, getWidth(), 8);
            placeRow(rowTwo, { 455, 285, 240, 300 }, 222, 192, getWidth(), 8);
            placeRow(rowThree, { 640, 480, 300 }, 436, 238, getWidth(), 8);
        }

    private:
        static void addGroup(Prophet6ControlAudioProcessor& processor, const juce::String& title,
                             std::vector<juce::String> sections, int columns,
                             std::vector<std::unique_ptr<ParameterGroup>>& row)
        {
            row.push_back(std::make_unique<ParameterGroup>(processor, title, std::move(sections), columns));
        }

        static void placeRow(std::vector<std::unique_ptr<ParameterGroup>>& groups, const std::vector<int>& widths,
                             int y, int height, int parentWidth, int gap)
        {
            auto totalWidth = gap * juce::jmax(0, static_cast<int>(groups.size()) - 1);
            for (const auto itemWidth : widths) totalWidth += itemWidth;
            const auto scale = totalWidth > parentWidth - 24 ? static_cast<double>(parentWidth - 24) / totalWidth : 1.0;
            auto x = juce::jmax(10, (parentWidth - static_cast<int>(std::lround(totalWidth * scale))) / 2);
            for (std::size_t i = 0; i < groups.size(); ++i)
            {
                const auto itemWidth = static_cast<int>(std::lround(widths[i] * scale));
                groups[i]->setBounds(x, y, itemWidth, height);
                x += itemWidth + static_cast<int>(std::lround(gap * scale));
            }
        }

        std::vector<std::unique_ptr<ParameterGroup>> rowOne, rowTwo, rowThree;
    };

    class GlobalPanel final : public juce::Component
    {
    public:
        explicit GlobalPanel(Prophet6ControlAudioProcessor& processor)
        {
            makeGroup(processor, "MASTER TUNING", { "Tuning" }, 3);
            makeGroup(processor, "MIDI + CLOCK", { "MIDI & Clock" }, 5);
            makeGroup(processor, "SYSTEM", { "System" }, 5);
        }

        void paint(juce::Graphics& g) override
        {
            g.fillAll(juce::Colour(panelDark));
            auto body = getLocalBounds().toFloat().reduced(7.0f);
            g.setColour(juce::Colour(panel));
            g.fillRoundedRectangle(body, 11.0f);
            g.setColour(juce::Colour(edge));
            g.drawRoundedRectangle(body, 11.0f, 0.8f);
            g.setColour(juce::Colour(walnut));
            g.fillRoundedRectangle(body.getX() + 2.0f, body.getY() + 3.0f, 7.0f, body.getHeight() - 6.0f, 2.5f);
            g.fillRoundedRectangle(body.getRight() - 9.0f, body.getY() + 3.0f, 7.0f, body.getHeight() - 6.0f, 2.5f);
        }

        void resized() override
        {
            const std::vector<int> widths { 340, 490, 440 };
            auto x = juce::jmax(12, (getWidth() - 1294) / 2);
            for (std::size_t i = 0; i < groups.size(); ++i)
            {
                groups[i]->setBounds(x, 42, widths[i], 230);
                x += widths[i] + 12;
            }
        }

    private:
        void makeGroup(Prophet6ControlAudioProcessor& processor, const juce::String& title,
                       std::vector<juce::String> sections, int columns)
        {
            auto group = std::make_unique<ParameterGroup>(processor, title, std::move(sections), columns,
                                                          p6::ParameterDomain::Global);
            addAndMakeVisible(*group);
            groups.push_back(std::move(group));
        }

        std::vector<std::unique_ptr<ParameterGroup>> groups;
    };
}

Prophet6ControlAudioProcessorEditor::Prophet6ControlAudioProcessorEditor(Prophet6ControlAudioProcessor& p)
    : AudioProcessorEditor(&p), processor(p)
{
    setLookAndFeel(&sharedLookAndFeel());
    setSize(1540, 970);
    setResizable(true, true);
    setResizeLimits(1280, 800, 1920, 1200);

    mainSurface = std::make_unique<MainPanel>(processor);
    mainSurface->setSize(1480, 710);
    globalSurface = std::make_unique<GlobalPanel>(processor);
    globalSurface->setSize(1480, 340);

    viewport.setViewedComponent(mainSurface.get(), false);
    viewport.setScrollBarsShown(false, true);
    viewport.setScrollBarThickness(8);
    addAndMakeVisible(viewport);

    for (auto* button : { &panelButton, &globalsButton, &loadButton, &saveButton, &requestDumpButton,
                          &exportDumpButton, &sendAllButton, &sendGlobalsButton, &recallHardwareButton,
                          &dismissRecallButton })
    {
        button->setLookAndFeel(&sharedLookAndFeel());
        button->setColour(juce::TextButton::buttonColourId, juce::Colour(0xff343b40));
        button->setColour(juce::TextButton::textColourOffId, juce::Colour(ivory));
        addAndMakeVisible(button);
    }

    panelButton.setClickingTogglesState(true);
    globalsButton.setClickingTogglesState(true);
    panelButton.setToggleState(true, juce::dontSendNotification);
    panelButton.onClick = [this] { showGlobalSettings(false); };
    globalsButton.onClick = [this] { showGlobalSettings(true); };

    hardwareLink.setColour(juce::HyperlinkButton::textColourId, juce::Colour(muted));
    hardwareLink.setFont(juce::Font(10.0f), false);
    addAndMakeVisible(hardwareLink);

    recallPromptLabel.setText("Patch staged. Send it to the Prophet 6?", juce::dontSendNotification);
    recallPromptLabel.setColour(juce::Label::textColourId, juce::Colour(0xffffd28a));
    recallPromptLabel.setFont(juce::Font(11.0f, juce::Font::bold));
    addAndMakeVisible(recallPromptLabel);
    recallPromptLabel.setVisible(processor.isHardwareRecallPending());
    recallHardwareButton.setVisible(processor.isHardwareRecallPending());
    dismissRecallButton.setVisible(processor.isHardwareRecallPending());
    recallHardwareButton.onClick = [this] { processor.confirmHardwareRecall(); resized(); };
    dismissRecallButton.onClick = [this] { processor.dismissHardwareRecall(); resized(); };

    loadButton.onClick = [this] { openPresetFile(); };
    saveButton.onClick = [this] { savePresetFile(); };
    requestDumpButton.onClick = [this] { processor.requestCurrentEditBufferDump(); };
    exportDumpButton.onClick = [this] { saveSysExFile(); };
    sendAllButton.onClick = [this] { processor.sendAllProgramParameters(); };
    sendGlobalsButton.onClick = [this] { processor.sendAllGlobalParameters(); };

    channelLabel.setText("MIDI OUT", juce::dontSendNotification);
    channelLabel.setColour(juce::Label::textColourId, juce::Colour(muted));
    channelLabel.setJustificationType(juce::Justification::centredRight);
    channelLabel.setFont(juce::Font(10.0f, juce::Font::bold));
    addAndMakeVisible(channelLabel);
    for (int channel = 1; channel <= 16; ++channel) channelBox.addItem(juce::String(channel), channel);
    channelBox.setSelectedId(processor.getMidiChannel(), juce::dontSendNotification);
    channelBox.setLookAndFeel(&sharedLookAndFeel());
    channelBox.onChange = [this] { processor.setMidiChannel(channelBox.getSelectedId()); };
    addAndMakeVisible(channelBox);

    statusLabel.setColour(juce::Label::textColourId, juce::Colour(muted));
    statusLabel.setFont(juce::Font(10.0f));
    statusLabel.setText(processor.getStatusText(), juce::dontSendNotification);
    addAndMakeVisible(statusLabel);
    startTimerHz(4);
}

Prophet6ControlAudioProcessorEditor::~Prophet6ControlAudioProcessorEditor()
{
    viewport.setViewedComponent(nullptr, false);
    setLookAndFeel(nullptr);
    for (auto* component : { static_cast<juce::Component*>(&panelButton), static_cast<juce::Component*>(&globalsButton),
                             static_cast<juce::Component*>(&loadButton), static_cast<juce::Component*>(&saveButton),
                             static_cast<juce::Component*>(&requestDumpButton), static_cast<juce::Component*>(&exportDumpButton),
                             static_cast<juce::Component*>(&sendAllButton), static_cast<juce::Component*>(&sendGlobalsButton),
                             static_cast<juce::Component*>(&recallHardwareButton), static_cast<juce::Component*>(&dismissRecallButton),
                             static_cast<juce::Component*>(&channelBox) })
        component->setLookAndFeel(nullptr);
}

void Prophet6ControlAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(panelDark));
    auto header = getLocalBounds().removeFromTop(43).toFloat();
    juce::ColourGradient gradient(juce::Colour(0xff292f34), header.getX(), header.getY(),
                                  juce::Colour(0xff171b1f), header.getRight(), header.getBottom(), false);
    g.setGradientFill(gradient);
    g.fillRect(header);
    g.setColour(juce::Colour(walnut));
    g.fillRect(0, 0, 5, getHeight());
    g.fillRect(getWidth() - 5, 0, 5, getHeight());
    g.setColour(juce::Colour(ivory));
    g.setFont(juce::Font(18.0f, juce::Font::bold));
    g.drawText("PROPHET 6 CONTROL", 16, 6, 270, 28, juce::Justification::centredLeft);
    g.setColour(juce::Colour(muted));
    g.setFont(juce::Font(9.5f, juce::Font::bold));
    g.drawText("DESKTOP EDITOR  /  LOGIC AUTOMATION", 298, 9, 340, 22, juce::Justification::centredLeft);
    g.setColour(juce::Colour(signal));
    g.fillEllipse(static_cast<float>(getWidth() - 22), 17.0f, 6.0f, 6.0f);
}

void Prophet6ControlAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced(13, 7);
    area.removeFromTop(36);
    auto toolbar = area.removeFromTop(36);

    panelButton.setBounds(toolbar.removeFromLeft(64));
    toolbar.removeFromLeft(5);
    globalsButton.setBounds(toolbar.removeFromLeft(76));
    toolbar.removeFromLeft(11);
    loadButton.setBounds(toolbar.removeFromLeft(58));
    toolbar.removeFromLeft(4);
    saveButton.setBounds(toolbar.removeFromLeft(58));
    toolbar.removeFromLeft(4);
    requestDumpButton.setBounds(toolbar.removeFromLeft(103));
    toolbar.removeFromLeft(4);
    exportDumpButton.setBounds(toolbar.removeFromLeft(90));
    toolbar.removeFromLeft(4);
    sendAllButton.setBounds(toolbar.removeFromLeft(112));
    toolbar.removeFromLeft(4);
    sendGlobalsButton.setBounds(toolbar.removeFromLeft(100));
    toolbar.removeFromLeft(9);
    channelLabel.setBounds(toolbar.removeFromLeft(62));
    toolbar.removeFromLeft(3);
    channelBox.setBounds(toolbar.removeFromLeft(48));
    hardwareLink.setBounds(toolbar.removeFromRight(168));

    if (processor.isHardwareRecallPending())
    {
        auto prompt = area.removeFromTop(26);
        recallPromptLabel.setBounds(prompt.removeFromLeft(juce::jmax(250, prompt.getWidth() - 218)));
        recallHardwareButton.setBounds(prompt.removeFromLeft(132).reduced(2, 1));
        dismissRecallButton.setBounds(prompt.removeFromLeft(82).reduced(2, 1));
    }

    statusLabel.setBounds(area.removeFromBottom(20));
    viewport.setBounds(area);
    auto* viewed = globalsMode ? globalSurface.get() : mainSurface.get();
    viewed->setSize(juce::jmax(viewport.getWidth(), 1480), globalsMode ? 340 : 710);
    viewport.setViewedComponent(viewed, false);
}

void Prophet6ControlAudioProcessorEditor::showGlobalSettings(bool shouldShow)
{
    globalsMode = shouldShow;
    panelButton.setToggleState(!globalsMode, juce::dontSendNotification);
    globalsButton.setToggleState(globalsMode, juce::dontSendNotification);
    resized();
    viewport.setViewPosition(0, 0);
}

void Prophet6ControlAudioProcessorEditor::timerCallback()
{
    statusLabel.setText(processor.getStatusText(), juce::dontSendNotification);
    const bool recallPending = processor.isHardwareRecallPending();
    if (recallPromptLabel.isVisible() != recallPending)
    {
        recallPromptLabel.setVisible(recallPending);
        recallHardwareButton.setVisible(recallPending);
        dismissRecallButton.setVisible(recallPending);
        resized();
    }
    exportDumpButton.setEnabled(processor.hasCapturedSysEx());
    if (channelBox.getSelectedId() != processor.getMidiChannel())
        channelBox.setSelectedId(processor.getMidiChannel(), juce::dontSendNotification);
}

void Prophet6ControlAudioProcessorEditor::openPresetFile()
{
    fileChooser = std::make_unique<juce::FileChooser>("Open Prophet-6 preset or SysEx", juce::File{}, "*.p6preset;*.syx");
    juce::Component::SafePointer<Prophet6ControlAudioProcessorEditor> safeThis(this);
    fileChooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [safeThis](const juce::FileChooser& chooser)
        {
            if (safeThis == nullptr) return;
            const auto file = chooser.getResult();
            if (file == juce::File{}) return;
            if (file.hasFileExtension("syx")) safeThis->processor.importSysEx(file);
            else safeThis->processor.importPreset(file);
        });
}

void Prophet6ControlAudioProcessorEditor::savePresetFile()
{
    fileChooser = std::make_unique<juce::FileChooser>(
        "Save Prophet-6 Control preset",
        juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("New Prophet Patch.p6preset"),
        "*.p6preset");
    juce::Component::SafePointer<Prophet6ControlAudioProcessorEditor> safeThis(this);
    fileChooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
        [safeThis](const juce::FileChooser& chooser)
        {
            if (safeThis == nullptr) return;
            const auto file = chooser.getResult();
            if (file != juce::File{}) safeThis->processor.exportPreset(file, file.getFileNameWithoutExtension());
        });
}

void Prophet6ControlAudioProcessorEditor::saveSysExFile()
{
    fileChooser = std::make_unique<juce::FileChooser>(
        "Export captured Prophet-6 SysEx",
        juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("Prophet-6 Edit Buffer.syx"),
        "*.syx");
    juce::Component::SafePointer<Prophet6ControlAudioProcessorEditor> safeThis(this);
    fileChooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
        [safeThis](const juce::FileChooser& chooser)
        {
            if (safeThis == nullptr) return;
            const auto file = chooser.getResult();
            if (file != juce::File{}) safeThis->processor.exportCapturedSysEx(file.withFileExtension("syx"));
        });
}
