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
    codeEditor.setReadOnly(false);
    codeEditor.setColour(juce::CodeEditorComponent::backgroundColourId, juce::Colours::darkgrey);
    codeEditor.setColour(juce::CodeEditorComponent::defaultTextColourId, juce::Colours::bisque);

    // Message Log
    addAndMakeVisible(messageLog);
    messageLog.setReadOnly(true);
    messageLog.setColour(juce::CodeEditorComponent::backgroundColourId, juce::Colours::black);
    messageLog.setColour(juce::CodeEditorComponent::defaultTextColourId, juce::Colours::lightgreen);

    // Vertical Layout
    verticalLayout.setItemLayout(0, -0.1, -0.9, -0.5); // Top window
    verticalLayout.setItemLayout(1, 8, 8, 8);          // Divider
    verticalLayout.setItemLayout(2, -0.1, -0.9, -0.5); // Bottom window
    addAndMakeVisible(divider);
    codeEditor.loadContent(audioProcessor.csd);
    // Set up callback for messages
    audioProcessor.messageCallback = [this](const juce::String& message) {
        juce::MessageManager::callAsync([this, message]() {
            appendToMessageLog(message);
        });
    };

    // Listen for changes from the processor
    audioProcessor.addChangeListener(this);

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
    openButton.setTooltip("Open a .csd file");
    saveButton.setBounds(menuBar.removeFromLeft(80));
    saveButton.setTooltip("Save edited text to the plugin state");
    saveAsButton.setBounds(menuBar.removeFromLeft(100));
    saveAsButton.setTooltip("Save edited text to a .csd file");
    playButton.setBounds(menuBar.removeFromLeft(80));
    playButton.setTooltip("Stop Csound and recompile the .csd");
    stopButton.setBounds(menuBar.removeFromLeft(80));
    stopButton.setTooltip("Stop the Csound performance");
    aboutButton.setBounds(menuBar.removeFromLeft(100));
    aboutButton.setTooltip("About CsoundVST3");

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
                statusBar.setText("Csd loaded.", juce::dontSendNotification);
           }
            else
            {
                statusBar.setText("The selected file does not exist or is not a file.", juce::dontSendNotification);
            }

        });
    }
    else if (button == &saveButton)
    {
        audioProcessor.csd = codeEditor.getDocument().getAllContent();
        statusBar.setText("Saved", juce::dontSendNotification);
    }
    else if (button == &saveAsButton)
    {
        statusBar.setText("Save csd as...", juce::dontSendNotification);
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
        ///juce::MessageManagerLock lock;
        audioProcessor.play();
    }
    else if (button == &stopButton)
    {
        statusBar.setText("Stop...", juce::dontSendNotification);
        ///juce::MessageManagerLock lock;
        audioProcessor.stop();
    }
    else if (button == &aboutButton)
    {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon, "About CsoundVST",
                                                              "This is CsoundVST.vst3 by Michael Gogins. It loads Csound .csd files and plays them as VST3 synthesizers or effects. MIDI channel plus 1 is Csound instrument number (p1), MIDI key is pitch (p4), MIDI velocity is loudness (p5). For more help, see:\n\nhttps://github.com/gogins/csound-vst3.");
    }

}

void CsoundVST3AudioProcessorEditor::appendToMessageLog(const juce::String& message)
{
    messageLog.moveCaretToEnd(false); // Move caret to end to append.
    messageLog.insertTextAtCaret(message);
}

void CsoundVST3AudioProcessorEditor::changeListenerCallback(juce::ChangeBroadcaster*)
{
    // Handle other processor-to-editor communication here if needed.
}

