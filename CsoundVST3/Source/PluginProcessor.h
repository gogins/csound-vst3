#pragma once

#include "BinaryData.h"
#include <juce_core/juce_core.h> // or wherever you're using JUCE types

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include "csound_threaded.hpp"
#include "readerwriterqueue.h"
#include "csoundvst3_version.h"

#include <iostream>
#include <numeric>

#ifndef SIGTRAP
#define SIGTRAP 5
#endif

class MidiChannelMessage
{
public:
    /**
     * This is the frame counting from the beginning of the plugin's
     * performance, which is used to find whether this essage falls within the
     * current Csound block and should be handled in the MIDI read callback.
     */
    int64_t plugin_frame = 0;
    /**
     * This is a sanity check on position in the spout buffer.
     */
    int64_t csound_frame = 0;
    /**
     * Another sanity check to see if we are missing or duplicating mesaages.
     */
    int64_t sequence = 0;
    juce::MidiMessage message;
};

class CsoundVST3AudioProcessor : public juce::AudioProcessor, public juce::ChangeBroadcaster
{
public:
    //==============================================================================
    CsoundVST3AudioProcessor();
    ~CsoundVST3AudioProcessor() override;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    //==============================================================================
    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    //==============================================================================
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;
    void synchronizeScore();

    //==============================================================================
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;
    static void csoundMessageCallback_(CSOUND *, int, const char *, va_list);
    void csoundMessage(const juce::String message);
    
    static int midiDeviceOpen(CSOUND *csound, void **userData,
                              const char *devName);
    static int midiDeviceClose(CSOUND *csound, void *userData);
    static int midiRead(CSOUND *csound, void *userData, unsigned char *buf, int nbytes);
    static int midiWrite(CSOUND *csound, void *userData, const unsigned char *buf, int nBytes);
    void synchronizeScore(juce::Optional<juce::AudioPlayHead::PositionInfo> &play_head_position);
    void requestGlobalRestart();
    void performGlobalRestart(double sample_rate,
                            int samples_per_block,
                            double score_time_seconds,
                            int64_t score_time_samples);    
    void play();
    void stop();

    Csound csound;
    std::atomic<bool> csoundIsPlaying = false;
    std::function<void(const juce::String&)> messageCallback;
    juce::String csd;
    juce::PluginHostType plugin_host_type;

private:
    /**
     * Amplitude corresponding to zero decibels full scale.
     */
    double odbfs;
    /**
     * Ampltude corresponding to one over zero decibels full scale.
     */
    double iodbfs;
    
    // These are valid after prepareToPlay.
    int host_input_channels;
    int host_output_channels;
    int host_channels;
    int csound_input_channels;
    int csound_output_channels;
    
    // These are valid only during processBlock.
    int64_t csound_frames;
    int64_t csound_frame;
    int64_t csound_frame_end;
    int64_t host_frame;
    int64_t host_block_frame;
    int64_t host_audio_buffer_frame;
    int64_t host_prior_frame;
    
    // Set to 0 in prepareToPlay, incremented in processBlock.
    int64_t plugin_frame;
    
    // These are counting in sample frames from the beginning of the
    // performance.
    int64_t csound_block_begin;
    int64_t csound_block_end;
    int64_t host_block_begin;
    int64_t host_block_end;
    int64_t midi_input_sequence;

    bool host_was_playing = false;
    bool restart_requested = false;
    bool orchestra_ready = false;
    double pending_score_time_seconds = 0.;
    int64_t pending_score_time_samples = 0;

    // These intermediate FIFOs simplify synchronizing overlapping or 
    // incomplete blocks of sample frames.
    moodycamel::ReaderWriterQueue<MidiChannelMessage> midi_input_fifo;
    moodycamel::ReaderWriterQueue<double> audio_input_fifo;
    moodycamel::ReaderWriterQueue<MidiChannelMessage> midi_output_fifo;
    moodycamel::ReaderWriterQueue<double> audio_output_fifo;
public:
    /**
     * Enables efficient asynchronous updating of the Csound message display.
     */
    moodycamel::ReaderWriterQueue<juce::String> csound_messages_fifo;

    //==============================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CsoundVST3AudioProcessor)
};
