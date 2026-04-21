#pragma once

#include "BinaryData.h"
#include <juce_core/juce_core.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include "csound_threaded.hpp"
#include "readerwriterqueue.h"
#include "csoundvst3_version.h"

#include <cstdint>
#include <iostream>
#include <numeric>

#ifndef SIGTRAP
#define SIGTRAP 5
#endif

class CsoundThreadedProcessor : public CsoundThreaded
{
public:
    CsoundThreadedProcessor() = default;

    CSOUND *getCsoundHandle() const
    {
        return csound;
    }

#if defined(CSOUND_VERSION_MAJOR) && (CSOUND_VERSION_MAJOR >= 7)
    void SetHostData(void *hostData)
    {
        csoundSetHostData(csound, hostData);
    }

    void SetMessageCallback(void (*callback)(CSOUND *, int32_t, const char *, va_list))
    {
        csoundSetMessageCallback(csound, callback);
    }

    void SetHostImplementedMIDIIO(int enabled)
    {
        if (enabled != 0)
        {
            csoundSetHostMIDIIO(csound);
        }
    }

    void SetHostImplementedAudioIO(int enabled, int)
    {
        if (enabled != 0)
        {
            csoundSetHostAudioIO(csound);
        }
    }

    void SetExternalMidiInOpenCallback(int32_t (*callback)(CSOUND *, void **, const char *))
    {
        csoundSetExternalMidiInOpenCallback(csound, callback);
    }

    void SetExternalMidiReadCallback(int32_t (*callback)(CSOUND *, void *, unsigned char *, int32_t))
    {
        csoundSetExternalMidiReadCallback(csound, callback);
    }

    void SetExternalMidiInCloseCallback(int32_t (*callback)(CSOUND *, void *))
    {
        csoundSetExternalMidiInCloseCallback(csound, callback);
    }

    void SetExternalMidiOutOpenCallback(int32_t (*callback)(CSOUND *, void **, const char *))
    {
        csoundSetExternalMidiOutOpenCallback(csound, callback);
    }

    void SetExternalMidiWriteCallback(int32_t (*callback)(CSOUND *, void *, const unsigned char *, int32_t))
    {
        csoundSetExternalMidiWriteCallback(csound, callback);
    }

    void SetExternalMidiOutCloseCallback(int32_t (*callback)(CSOUND *, void *))
    {
        csoundSetExternalMidiOutCloseCallback(csound, callback);
    }

    void SetScoreOffsetSeconds(double timeSeconds)
    {
        csoundSetScoreOffsetSeconds(csound, static_cast<MYFLT>(timeSeconds));
    }

#if defined(CSOUND_VERSION_MAJOR) && (CSOUND_VERSION_MAJOR >= 7)
    int32_t GetKsmps() override
    {
        return csoundGetKsmps(GetCsound());
    }

    int32_t GetNchnls() override
    {
        return GetChannels(0);
    }

    int32_t GetNchnlsInput() override
    {
        return GetChannels(1);
    }
#else
    int32_t GetKsmps() override
    {
        return CsoundThreaded::GetKsmps();
    }

    int32_t GetNchnls() override
    {
        return CsoundThreaded::GetNchnls();
    }

    int32_t GetNchnlsInput() override
    {
        return CsoundThreaded::GetNchnlsInput();
    }
#endif

    MYFLT Get0dBFS()
    {
        return csoundGet0dBFS(csound);
    }

    MYFLT *GetSpin()
    {
        return csoundGetSpin(csound);
    }

    const MYFLT *GetSpout()
    {
        return csoundGetSpout(csound);
    }

    MYFLT GetSr()
    {
        return csoundGetSr(csound);
    }

    int32_t SetOption(const char *option)
    {
        return csoundSetOption(csound, option);
    }

    int32_t Start()
    {
        return csoundStart(csound);
    }

    int32_t PerformKsmps()
    {
        return csoundPerformKsmps(csound);
    }

    int32_t Cleanup()
    {
        return 0;
    }

    void Reset()
    {
        csoundReset(csound);
    }
#endif
};

class MidiChannelMessage
{
public:
    int64_t plugin_frame = 0;
    int64_t csound_frame = 0;
    int64_t sequence = 0;
    juce::MidiMessage message;
};

class CsoundVST3AudioProcessor : public juce::AudioProcessor, public juce::ChangeBroadcaster
{
public:
    CsoundVST3AudioProcessor();
    ~CsoundVST3AudioProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

    bool isBusesLayoutSupported(const BusesLayout &layouts) const override;
    void processBlock(juce::AudioBuffer<float> &, juce::MidiBuffer &) override;

    juce::AudioProcessorEditor *createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int index, const juce::String &newName) override;
    void synchronizeScore();

    void getStateInformation(juce::MemoryBlock &destData) override;
    void setStateInformation(const void *data, int sizeInBytes) override;

    static void csoundMessageCallback_(CSOUND *, int32_t, const char *, va_list);
    void csoundMessage(const juce::String message);

    static int midiDeviceOpen(CSOUND *csound, void **userData, const char *devName);
    static int midiDeviceClose(CSOUND *csound, void *userData);
    static int midiRead(CSOUND *csound, void *userData, unsigned char *buf, int nbytes);
    static int midiWrite(CSOUND *csound, void *userData, const unsigned char *buf, int nBytes);
    void synchronizeScore(juce::Optional<juce::AudioPlayHead::PositionInfo> &play_head_position);

    void play();
    void stop();

    CsoundThreadedProcessor csound;
    std::atomic<bool> csoundIsPlaying = false;
    std::function<void(const juce::String &)> messageCallback;
    juce::String csd;
    juce::PluginHostType plugin_host_type;

private:
    double odbfs {};
    double iodbfs {};

    int host_input_channels {};
    int host_output_channels {};
    int host_channels {};
    int csound_input_channels {};
    int csound_output_channels {};

    int64_t csound_frames {};
    int64_t csound_frame {};
    int64_t csound_frame_end {};
    int64_t host_frame {};
    int64_t host_block_frame {};
    int64_t host_audio_buffer_frame {};
    int64_t host_prior_frame {};

    int64_t plugin_frame {};

    int64_t csound_block_begin {};
    int64_t csound_block_end {};
    int64_t host_block_begin {};
    int64_t host_block_end {};
    int64_t midi_input_sequence {};

    moodycamel::ReaderWriterQueue<MidiChannelMessage> midi_input_fifo;
    moodycamel::ReaderWriterQueue<double> audio_input_fifo;
    moodycamel::ReaderWriterQueue<MidiChannelMessage> midi_output_fifo;
    moodycamel::ReaderWriterQueue<double> audio_output_fifo;

public:
    moodycamel::ReaderWriterQueue<juce::String> csound_messages_fifo;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CsoundVST3AudioProcessor)
};
