#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "AboutDialog.h"
#include "CsoundTokeniser.h"
#include "csound_threaded.hpp"
#include "csd_ids.h"


//==============================================================================
CsoundVST3AudioProcessorEditor::CsoundVST3AudioProcessorEditor (CsoundVST3AudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p),
    divider(&verticalLayout, 1, false)
{
    Csound csound;
    // Menu Bar Buttons
    addAndMakeVisible(openButton);
    addAndMakeVisible(saveButton);
    addAndMakeVisible(saveAsButton);
    addAndMakeVisible(playButton);
    addAndMakeVisible(stopButton);
    addAndMakeVisible(findButton);
    addAndMakeVisible(aboutButton);

    // Attach listeners
    openButton.addListener(this);
    saveButton.addListener(this);
    saveAsButton.addListener(this);
    playButton.addListener(this);
    stopButton.addListener(this);
    findButton.addListener(this);
    aboutButton.addListener(this);

    // Status Bar
    statusBar.setText("Ready", juce::dontSendNotification);
    statusBar.setJustificationType(juce::Justification::left);
    addAndMakeVisible(statusBar);

    // Code Editor
    csd_code_tokeniser = std::make_unique<CsoundTokeniser>();
    codeEditor = std::make_unique<juce::CodeEditorComponent>(csd_document, csd_code_tokeniser.get());
    addAndMakeVisible(*codeEditor);
    codeEditor->setReadOnly(false);
    codeEditor->setColour(juce::CodeEditorComponent::backgroundColourId, juce::Colours::darkslategrey);
    codeEditor->setColour(juce::CodeEditorComponent::defaultTextColourId, juce::Colours::seashell);

    // Message Log
    messageLog = std::make_unique<juce::CodeEditorComponent>(messages_document, nullptr);
    addAndMakeVisible(*messageLog);
    messageLog->setReadOnly(true);
    messageLog->setColour(juce::CodeEditorComponent::backgroundColourId, juce::Colours::black);
    messageLog->setColour(juce::CodeEditorComponent::defaultTextColourId, juce::Colours::lightgreen);

    // Vertical Layout
    verticalLayout.setItemLayout(0, -0.1, -0.9, -0.5); // Top window
    verticalLayout.setItemLayout(1, 8, 8, 8);          // Divider
    verticalLayout.setItemLayout(2, -0.1, -0.9, -0.5); // Bottom window
    addAndMakeVisible(divider);
    codeEditor->loadContent(audioProcessor.csd);
    
    // Listen for changes from the processor
    audioProcessor.addChangeListener(this);
    startTimer(100);

    setSize(800, 600);
    setResizable(true, true);
}

CsoundVST3AudioProcessorEditor::~CsoundVST3AudioProcessorEditor()
{
    audioProcessor.removeChangeListener(this);
    stopTimer();
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
    saveButton.setTooltip("Save edited .csd to the plugin state");
    saveAsButton.setBounds(menuBar.removeFromLeft(100));
    saveAsButton.setTooltip("Save edited .csd to a .csd file");
    playButton.setBounds(menuBar.removeFromLeft(80));
    playButton.setTooltip("Stop Csound, compile the .csd, and start the performance");
    stopButton.setBounds(menuBar.removeFromLeft(80));
    stopButton.setTooltip("Stop the Csound performance");
    findButton.setBounds(menuBar.removeFromLeft(80));
    findButton.setTooltip("Search and replace...");
    aboutButton.setBounds(menuBar.removeFromLeft(100));
    aboutButton.setTooltip("About CsoundVST3");

    // Status Bar
    auto statusBarHeight = 20;
    statusBar.setBounds(bounds.removeFromBottom(statusBarHeight));

    juce::Component *components[] = {codeEditor.get(), &divider, messageLog.get()};
    verticalLayout.layOutComponents(components, 3, bounds.getX(), bounds.getY(), bounds.getWidth(), bounds.getHeight(), true, true) ;
    setWantsKeyboardFocus(true);
    messageLog->setReadOnly(false);
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
                codeEditor->loadContent(audioProcessor.csd);
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
        audioProcessor.csd = codeEditor->getDocument().getAllContent();
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
                    audioProcessor.csd = codeEditor->getDocument().getAllContent();
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
        audioProcessor.play();
        audioProcessor.suspendProcessing(false);
        audioProcessor.csoundMessage("Playing...\n");
        audioProcessor.csoundIsPlaying = true;
    }
    else if (button == &stopButton)
    {
        statusBar.setText("Stop...", juce::dontSendNotification);
        juce::MessageManagerLock lock;
        audioProcessor.stop();
    }
    else if (button == &findButton)
    {
        //auto* dialog = new juce::DialogWindow("Search and Replace", juce::Colours::darkgrey, true);
        //dialog->setContentOwned(new SearchAndReplaceDialog(*codeEditor), true);
        //dialog->centreWithSize(400, 150);
        juce::DialogWindow::showDialog("Search and Replace", new SearchAndReplaceDialog(*codeEditor), nullptr, juce::Colours::darkgrey, true, false);
    }
    else if (button == &aboutButton)
    {
        showAboutDialog(this);
    }

}

void CsoundVST3AudioProcessorEditor::changeListenerCallback(juce::ChangeBroadcaster*)
{
}

void CsoundVST3AudioProcessorEditor::timerCallback()
{
    while (auto message = audioProcessor.csound_messages_fifo.peek())
    {
        messageLog->insertTextAtCaret(*message);
        audioProcessor.csound_messages_fifo.pop();
    }
}
