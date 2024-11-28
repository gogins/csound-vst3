/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin editor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "csound_threaded.hpp"


//==============================================================================
CsoundVST3AudioProcessorEditor::CsoundVST3AudioProcessorEditor (CsoundVST3AudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p),
    divider(&verticalLayout, 1, false), codeEditor(csd_document, nullptr), messageLog(messages_document, nullptr)
{
    Csound csound;
    // Menu Bar Buttons
    addAndMakeVisible(openButton);
    addAndMakeVisible(saveButton);
    addAndMakeVisible(saveAsButton);
    addAndMakeVisible(playButton);
    addAndMakeVisible(stopButton);
    addAndMakeVisible(aboutButton);

    // Attach listeners
    openButton.addListener(this);
    saveButton.addListener(this);
    saveAsButton.addListener(this);
    playButton.addListener(this);
    stopButton.addListener(this);
    aboutButton.addListener(this);

    // Status Bar
    statusBar.setText("Ready", juce::dontSendNotification);
    statusBar.setJustificationType(juce::Justification::left);
    addAndMakeVisible(statusBar);

    // Code Editor
    addAndMakeVisible(codeEditor);
    codeEditor.setFont(juce::FontOptions("Courier",14.0f, juce::Font::plain));
    codeEditor.setReadOnly(false);
 
    // Message Log
    addAndMakeVisible(messageLog);
    messageLog.setReadOnly(true);

    // Vertical Layout
    verticalLayout.setItemLayout(0, -0.1, -0.9, -0.5); // Top window
    verticalLayout.setItemLayout(1, 8, 8, 8);          // Divider
    verticalLayout.setItemLayout(2, -0.1, -0.9, -0.5); // Bottom window
    addAndMakeVisible(divider);

    codeEditor.loadContent(audioProcessor.csd);
    
    setSize(800, 600);
}

CsoundVST3AudioProcessorEditor::~CsoundVST3AudioProcessorEditor()
{
}

//==============================================================================
void CsoundVST3AudioProcessorEditor::paint (juce::Graphics& g)
{
    // (Our component is opaque, so we must completely fill the background with a solid colour)
    g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));
}

void CsoundVST3AudioProcessorEditor::resized()
{
    auto bounds = getLocalBounds();

    // Menu Bar
    auto menuBar = bounds.removeFromTop(30);
    openButton.setBounds(menuBar.removeFromLeft(80));
    // saveButton.setBounds(menuBar.removeFromLeft(80));
    saveAsButton.setBounds(menuBar.removeFromLeft(100));
    playButton.setBounds(menuBar.removeFromLeft(80));
    stopButton.setBounds(menuBar.removeFromLeft(80));
    aboutButton.setBounds(menuBar.removeFromLeft(100));

    // Status Bar
    auto statusBarHeight = 20;
    statusBar.setBounds(bounds.removeFromBottom(statusBarHeight));

    juce::Component *components[] = {&codeEditor, &divider, &messageLog};
    verticalLayout.layOutComponents(components, 3, bounds.getX(), bounds.getY(), bounds.getWidth(), bounds.getHeight(), true, true) ;
    setWantsKeyboardFocus(true);
    messageLog.setReadOnly(false);
 }

void CsoundVST3AudioProcessorEditor::buttonClicked(juce::Button* button)
{
    if (button == &openButton)
    {
        statusBar.setText("Load csd...", juce::dontSendNotification);
        fileChooser = std::make_unique<juce::FileChooser> ("Please select a .csd file to open...",
                                                   juce::File::getSpecialLocation (juce::File::userHomeDirectory),
                                                   "*.csd");
        auto folderChooserFlags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;
        fileChooser->launchAsync (folderChooserFlags, [this] (const juce::FileChooser& chooser)
        {
            csd_file = chooser.getResult();
            DBG("Selected file to open: " << csd_file.getFullPathName());
            if (csd_file.existsAsFile())
            {
                audioProcessor.csd = csd_file.loadFileAsString();
                codeEditor.loadContent(audioProcessor.csd);
            }
            else
            {
                DBG("The selected file does not exist or is not a file.");
            }

        });
    }
    /*
    else if (button == &saveButton)
    {
        statusBar.setText("Save...", juce::dontSendNotification);
    }
    */
    else if (button == &saveAsButton)
    {
        statusBar.setText("Save csd...", juce::dontSendNotification);
        fileChooser = std::make_unique<juce::FileChooser> ("Please select a .csd file to save to...",
                                                           juce::File::getSpecialLocation (juce::File::userHomeDirectory),
                                                           "*.csd");
        auto folderChooserFlags = juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::useTreeView;
        fileChooser->launchAsync (folderChooserFlags, [this] (const juce::FileChooser& chooser)
        {
                juce::File selectedFile = chooser.getResult();
                if (selectedFile.create().wasOk())
                {
                    audioProcessor.csd = codeEditor.getDocument().getAllContent();
                    if (selectedFile.replaceWithText(audioProcessor.csd))
                    {
                        DBG("File saved successfully: " << selectedFile.getFullPathName());
                    }
                    else
                    {
                        DBG("Failed to write to file: " << selectedFile.getFullPathName());
                    }
                }
                else
                {
                    DBG("Failed to create file: " << selectedFile.getFullPathName());
                }
        });
    }
    else if (button == &playButton)
    {
        statusBar.setText("Play...", juce::dontSendNotification);
        juce::MessageManagerLock lock;
        auto frames_per_second = audioProcessor.getSampleRate();
        auto frame_size = audioProcessor.getBlockSize();
        auto play_head = audioProcessor.getPlayHead();
        audioProcessor.suspendProcessing(true);
        audioProcessor.prepareToPlay(frames_per_second, frame_size);
        audioProcessor.suspendProcessing(false);
    }
    else if (button == &stopButton)
    {
        statusBar.setText("Stop...", juce::dontSendNotification);
        juce::MessageManagerLock lock;
        audioProcessor.suspendProcessing(true);
        audioProcessor.csound.Stop();
        audioProcessor.suspendProcessing(false);
    }
    else if (button == &aboutButton)
    {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon, "About CsoundVST",
                                                              "This is CsoundVST.vst3 by Michael Gogins. It loads Csound .csd files and plays them as VST3 synthesizers or effects. MIDI channel plus 1 is Csound instrument number (p1), MIDI key is pitch (p4), MIDI velocity is loudness (p5). For more help, see:\n\nhttps://github.com/gogins/csound-vst3.");
    }

}

