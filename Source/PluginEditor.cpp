#include "PluginEditor.h"

using namespace avel;
using namespace avel::ui;

namespace { constexpr int topBarH = 58, panelH = 200; }

EmberEditor::EmberEditor (EmberProcessor& p)
    : AudioProcessorEditor (&p), proc (p), graph (p), panel (p), io (p)
{
    setLookAndFeel (&lnf);
    juce::LookAndFeel::setDefaultLookAndFeel (&lnf);

    addAndMakeVisible (graph);
    addAndMakeVisible (panel);
    addAndMakeVisible (io);
    graph.onSelect = [this] (int b) { panel.setBand (b); };

    presetBtn.setIcon (icon::chevronDown(), 1.5f);
    presetBtn.setBackgroundVisible (true);
    presetBtn.setText (proc.currentPresetName);
    presetBtn.onClick = [this] { showPresetMenu(); };
    addAndMakeVisible (presetBtn);

    undoBtn.setIcon (icon::undo(), 1.6f);
    redoBtn.setIcon (icon::redo(), 1.6f);
    undoBtn.setTooltip ("Undo");
    redoBtn.setTooltip ("Redo");
    undoBtn.onClick = [this] { proc.undo.undo(); };
    redoBtn.onClick = [this] { proc.undo.redo(); };
    addAndMakeVisible (undoBtn);
    addAndMakeVisible (redoBtn);

    osSeg.setItems ({ "Off", "2x", "4x", "8x" });
    osSeg.setTooltips ({ "No oversampling", "2x oversampling", "4x oversampling (less aliasing)", "8x oversampling (cleanest, more CPU)" });
    osSeg.bind (proc.param ("os"), &proc.undo);
    addAndMakeVisible (osSeg);

    abSeg.setItems ({ "A", "B" });
    abSeg.setSelected (proc.getABSlot(), false);
    abSeg.onChange = [this] (int i) { proc.switchAB (i); };
    addAndMakeVisible (abSeg);
    abCopyBtn.setIcon (icon::copy(), 1.5f);
    abCopyBtn.setTooltip ("Copy the current setting to the other slot");
    abCopyBtn.onClick = [this] { proc.copyABToOther(); };
    addAndMakeVisible (abCopyBtn);

    autoGainSwitch.bind (proc.param ("autoGain"), &proc.undo);
    autoGainSwitch.setAccent (col::band (1));
    addAndMakeVisible (autoGainSwitch);

    setWantsKeyboardFocus (true);
    setResizable (true, true);
    setResizeLimits (1000, 640, 2600, 1600);
    setSize ((int) proc.uiState.getProperty ("width", 1180), (int) proc.uiState.getProperty ("height", 720));
    graph.select ((int) proc.uiState.getProperty ("selected", 0));
    startTimerHz (60);
}

EmberEditor::~EmberEditor()
{
    stopTimer();
    juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
    setLookAndFeel (nullptr);
}

void EmberEditor::paint (juce::Graphics& g)
{
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff141416), 0, 0, juce::Colour (0xff09090a), 0, (float) getHeight(), false));
    g.fillAll();

    // logo: an ember
    auto l = logoArea.toFloat();
    auto mark = l.removeFromLeft (30).withSizeKeepingCentre (26, 26);
    juce::ColourGradient grad (juce::Colour (0xffffd60a), mark.getX(), mark.getY(), juce::Colour (0xffff375f), mark.getRight(), mark.getBottom(), false);
    grad.addColour (0.5, juce::Colour (0xffff9f0a));
    g.setGradientFill (grad);
    g.fillRoundedRectangle (mark, 8.0f);
    juce::Path flame;
    const auto c = mark.getCentre();
    flame.startNewSubPath (c.x, c.y + 7);
    flame.cubicTo (c.x - 7, c.y + 7, c.x - 6, c.y - 1, c.x - 1, c.y - 7);
    flame.cubicTo (c.x - 1, c.y - 2, c.x + 3, c.y - 2, c.x + 3, c.y - 5);
    flame.cubicTo (c.x + 7, c.y - 1, c.x + 7, c.y + 7, c.x, c.y + 7);
    g.setColour (juce::Colours::white);
    g.strokePath (flame, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    l.removeFromLeft (10);
    g.setColour (col::label);
    g.setFont (font (16.0f, Weight::semibold, -0.01f));
    g.drawText ("Avel", l, juce::Justification::centredLeft);
    const float w = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), "Avel") + 5;
    g.setColour (col::label2);
    g.setFont (font (16.0f, Weight::regular, -0.01f));
    g.drawText ("Ember", l.withTrimmedLeft (w), juce::Justification::centredLeft);

    g.setColour (col::label3);
    g.setFont (font (10.0f, Weight::semibold, 0.06f));
    g.drawText ("AUTO GAIN", autoGainLabel, juce::Justification::centredRight);
    g.drawText ("OVERSAMPLING", osLabel, juce::Justification::centredRight);
}

void EmberEditor::resized()
{
    proc.uiState.setProperty ("width", getWidth(), nullptr);
    proc.uiState.setProperty ("height", getHeight(), nullptr);

    auto r = getLocalBounds();
    auto top = r.removeFromTop (topBarH).reduced (18, 12);
    logoArea = top.removeFromLeft (124);
    top.removeFromLeft (10);
    presetBtn.setBounds (top.removeFromLeft (170).reduced (0, 2));
    top.removeFromLeft (10);
    undoBtn.setBounds (top.removeFromLeft (32).reduced (0, 1));
    top.removeFromLeft (2);
    redoBtn.setBounds (top.removeFromLeft (32).reduced (0, 1));

    auto right = top.removeFromRight (300);
    autoGainSwitch.setBounds (right.removeFromRight (44));
    autoGainLabel = right.removeFromRight (74).withTrimmedRight (6);
    right.removeFromRight (20);
    abCopyBtn.setBounds (right.removeFromRight (32).reduced (0, 1));
    right.removeFromRight (4);
    abSeg.setBounds (right.removeFromRight (86).reduced (0, 1));

    auto centre = top.withSizeKeepingCentre (juce::jmin (top.getWidth(), 330), top.getHeight());
    osLabel = centre.removeFromLeft (100).withTrimmedRight (10);
    osSeg.setBounds (centre.reduced (0, 1));

    r = r.reduced (12, 0).withTrimmedBottom (12);
    io.setBounds (r.removeFromRight (84));
    r.removeFromRight (6);
    graphArea = r;
    graph.setBounds (r);

    const int pw = juce::jmin (graphArea.getWidth() - 20, 960);
    panel.setBounds (juce::Rectangle<int> (pw, panelH).withCentre ({ graphArea.getCentreX(), 0 }).withY (graphArea.getBottom() - 22 - panelH));
    graph.setBottomReserve ((float) panelH);
}

void EmberEditor::timerCallback()
{
    graph.tick();
    io.tick();
    if (panel.isVisible()) panel.refresh();
    undoBtn.setEnabled (proc.undo.canUndo());
    redoBtn.setEnabled (proc.undo.canRedo());
    presetBtn.setText (proc.currentPresetName);
    if (abSeg.getSelected() != proc.getABSlot()) abSeg.setSelected (proc.getABSlot());
}

void EmberEditor::showPresetMenu()
{
    juce::PopupMenu m;
    const auto factory = EmberProcessor::getFactoryPresetNames();
    m.addSectionHeader ("Factory");
    for (int i = 0; i < factory.size(); ++i) m.addItem (1 + i, factory[i], true, proc.currentPresetName == factory[i]);

    juce::PopupMenu user;
    juce::Array<juce::File> files (EmberProcessor::getUserPresetFolder().findChildFiles (juce::File::findFiles, false, "*.emberpreset"));
    struct ByName { static int compareElements (const juce::File& a, const juce::File& b) { return a.getFileName().compareNatural (b.getFileName()); } } cmp;
    files.sort (cmp);
    for (int i = 0; i < files.size(); ++i) user.addItem (1000 + i, files[i].getFileNameWithoutExtension());
    if (files.isEmpty()) user.addItem (999, "No saved presets", false);
    m.addSeparator();
    m.addSubMenu ("My Presets", user);
    m.addSeparator();
    m.addItem (500, "Save Preset...");
    m.addItem (501, "Show Presets Folder");

    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&presetBtn),
                     [this, files] (int r)
                     {
                         if (r >= 1 && r < 500) { proc.loadFactoryPreset (r - 1); graph.select (0); }
                         else if (r >= 1000) proc.loadUserPreset (files[r - 1000]);
                         else if (r == 501) EmberProcessor::getUserPresetFolder().startAsProcess();
                         else if (r == 500)
                         {
                             chooser = std::make_unique<juce::FileChooser> ("Save preset",
                                         EmberProcessor::getUserPresetFolder().getChildFile (proc.currentPresetName + ".emberpreset"), "*.emberpreset");
                             chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                                                   [this] (const juce::FileChooser& fc)
                                                   {
                                                       if (fc.getResult() != juce::File()) proc.saveUserPreset (fc.getResult().withFileExtension ("emberpreset"));
                                                   });
                         }
                     });
}
