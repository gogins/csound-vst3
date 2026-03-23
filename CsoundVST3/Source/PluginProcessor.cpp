#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "csoundvst3_version.h"
#include <cassert>
#include <csignal>

/**
 * Enable this to log behavior of FIFOs.
 */
constexpr bool fifo_debug = false;

/**
 * Permits a programmer to set a breakpoint in order to pause when
 * ThreadSanitiizer issues a report.
 */
extern "C" void __tsan_on_report() {
    std::raise(SIGTRAP);
    std::fprintf(stderr, "__tsan_on_report\n");
}


//==============================================================================
CsoundVST3AudioProcessor::CsoundVST3AudioProcessor()
     : AudioProcessor (BusesProperties()
                        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                        ),
midi_input_fifo(65536),
audio_input_fifo(65536),
midi_output_fifo(65536),
audio_output_fifo(65536),
csound_messages_fifo(65536)
{
}

CsoundVST3AudioProcessor::~CsoundVST3AudioProcessor()
{
}

//==============================================================================
const juce::String CsoundVST3AudioProcessor::getName() const
{
    juce::String plugin_name = JucePlugin_Name;
    plugin_name += " v";
    plugin_name += CSOUNDVST3_VERSION;
    return plugin_name;
}

bool CsoundVST3AudioProcessor::acceptsMidi() const
{
    return true;
}

bool CsoundVST3AudioProcessor::producesMidi() const
{
    return true;
}

bool CsoundVST3AudioProcessor::isMidiEffect() const
{
     return false;
}

double CsoundVST3AudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int CsoundVST3AudioProcessor::getNumPrograms()
{
    // NB: some hosts don't cope very well if you tell them there are 0
    // programs, so this should be at least 1, even if you're not really
    // implementing programs.
    return 1;
}

int CsoundVST3AudioProcessor::getCurrentProgram()
{
    return 0;
}

void CsoundVST3AudioProcessor::setCurrentProgram (int index)
{
}

const juce::String CsoundVST3AudioProcessor::getProgramName (int index)
{
    return {};
}

void CsoundVST3AudioProcessor::changeProgramName (int index, const juce::String& newName)
{
}

void CsoundVST3AudioProcessor::csoundMessage(const juce::String message)
{
    csound_messages_fifo.enqueue(message);
    DBG(message);
}

void CsoundVST3AudioProcessor::csoundMessageCallback_(CSOUND *csound, int level, const char *format, va_list valist)
{
    auto host_data = csoundGetHostData(csound);
    auto processor = static_cast<CsoundVST3AudioProcessor *>(host_data);
    char buffer[0x2000];
    std::vsnprintf(&buffer[0], sizeof(buffer), format, valist);
    processor->csoundMessage(buffer);
}

void CsoundVST3AudioProcessor::releaseResources()
{
    csoundMessage("Stopping due to exit from host...\n");
    DBG("CsoundVST3AudioProcessor::releaseResources...");
    stop();
}

bool CsoundVST3AudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // Check output bus layout
    const auto& mainOutput = layouts.getMainOutputChannelSet();
    if (mainOutput != juce::AudioChannelSet::mono() &&
        mainOutput != juce::AudioChannelSet::stereo())
        return false; // Only mono or stereo output supported
    // Check input bus layout
    const auto& mainInput = layouts.getMainInputChannelSet();
    if (mainInput.isDisabled() || // Allow no input
        mainInput == juce::AudioChannelSet::mono() ||
        mainInput == juce::AudioChannelSet::stereo())
        return true; // Input can be mono, stereo, or none
    return false; // Other configurations not supported
}

int CsoundVST3AudioProcessor::midiDeviceOpen(CSOUND *csound_, void **user_data,
                          const char *devName)
{
    auto csound_host_data = csoundGetHostData(csound_);
    *user_data = (void *)csound_host_data;
    return 0;
}

int CsoundVST3AudioProcessor::midiDeviceClose(CSOUND *csound_, void *user_data)
{
    return 0;
}
/**
 * Called by Csound at every kperiod to receive incoming MIDI messages from
 * the host. Only MIDI channel messages are handled. Timing precision is the
 * audio processing block size, so accurate timing requires ksmps of 128 or so.
 * Messages up to the end of the current Csound block are consumed, and later
 * message are left in the FIFO for the next Csound block.
 */
int CsoundVST3AudioProcessor::midiRead(CSOUND *csound_, void *userData, unsigned char *midi_buffer, int midi_buffer_size)
{
    int bytes_read = 0;
    auto csound_host_data = csoundGetHostData(csound_);
    CsoundVST3AudioProcessor *processor = static_cast<CsoundVST3AudioProcessor *>(csound_host_data);
    int messages = 0;
    while (true)
    {
        auto message = processor->midi_input_fifo.peek();
        if (message == nullptr)
        {
            break;
        }
        // Skipping later messages.
        if (message->plugin_frame >= processor->csound_block_end)
        {
            break;
        }
        messages++;
        char buffer[0x200];
        auto size = message->message.getRawDataSize();
        auto data = message->message.getRawData();
        auto status = data[0];
        // We process only MIDI channel messages.
        if ((0x80 <= status) && (status <= 0xE0))
        {
#if defined(JUCE_DEBUG)
            if (fifo_debug == true)
            {
                ///assert(message.plugin_frame >= processor->csound_block_begin && message.plugin_frame < processor->csound_block_end);
                auto tyme = message->plugin_frame / float(processor->getSampleRate());
                std::snprintf(buffer, sizeof(buffer),
                              "Plugin midiRead   #%5lld: time:%9.4f cs begin  %8llu plugin%8llu msg%8llu cs%8llu cs end  %8llu  %s", message->sequence, tyme, processor->csound_block_begin, processor->plugin_frame, message->plugin_frame, message->csound_frame, processor->csound_block_end, message->message.getDescription().toRawUTF8());
                DBG(buffer);
            }
#endif
            for (int i = 0; i < size; ++i, ++bytes_read)
            {
                midi_buffer[bytes_read] = data[i];
            }
        }
        else
        {
            DBG("Not a MIDI channel message!");
        }
        processor->midi_input_fifo.pop();
    }
    return bytes_read;
}

/**
 * Called by Csound for each output MIDI message, to send  MIDI data to the host.
 */
int CsoundVST3AudioProcessor::midiWrite(CSOUND *csound_, void *userData, const unsigned char *midi_buffer, int midi_buffer_size)
{
    int result = 0;
    auto csound_host_data = csoundGetHostData(csound_);
    CsoundVST3AudioProcessor *processor = static_cast<CsoundVST3AudioProcessor *>(csound_host_data);
    MidiChannelMessage channel_message;
    channel_message.plugin_frame = processor->plugin_frame;
    channel_message.message = juce::MidiMessage(midi_buffer, midi_buffer_size, 0);
    processor->midi_output_fifo.enqueue(channel_message);
    return result;
}

template<typename T> void drain(moodycamel::ReaderWriterQueue<T> &queue);

void CsoundVST3AudioProcessor::requestGlobalRestart()
{
    restart_requested = true;
    orchestra_ready = false;
    drain(midi_input_fifo);
    drain(audio_input_fifo);
    drain(midi_output_fifo);
    drain(audio_output_fifo);
}

void CsoundVST3AudioProcessor::performGlobalRestart(double sample_rate,
                                                    int samples_per_block,
                                                    double score_time_seconds,
                                                    int64_t score_time_samples)
{
    suspendProcessing(false);
    prepareToPlay(sample_rate, samples_per_block);

    csound.SetScoreOffsetSeconds(score_time_seconds);

    host_frame = score_time_samples;
    host_prior_frame = score_time_samples;
    host_block_frame = score_time_samples;
    host_block_begin = score_time_samples;
    host_block_end = score_time_samples;

    plugin_frame = score_time_samples;

    if (csound_frames > 0)
    {
        csound_frame = plugin_frame % csound_frames;
    }
    else
    {
        csound_frame = 0;
    }

    csound_block_begin = plugin_frame - csound_frame;
    csound_block_end = csound_block_begin + csound_frames;
    csound_frame_end = csound_block_end;

    pending_score_time_seconds = score_time_seconds;
    pending_score_time_samples = score_time_samples;

    restart_requested = false;
    orchestra_ready = (csoundIsPlaying == true);
}

/**
 * Ensures that Csound's score time tracks the host's performance time. This
 * causes Csound to loop in its own score along with the host.
 *
 * This function is called from processBlock before that function processes any
 * samples. Therefore, the times are always aligned with the start of the block.
 *
 * TODO: What if track has nonzero start -- is that possible?
 */
void CsoundVST3AudioProcessor::synchronizeScore(juce::Optional<juce::AudioPlayHead::PositionInfo> &play_head_position)
{
    if (play_head_position.hasValue() == false)
    {
        host_was_playing = false;
        return;
    }

    const bool host_is_playing = play_head_position->getIsPlaying();

    juce::Optional<int64_t> optional_host_frame_index = play_head_position->getTimeInSamples();
    if (optional_host_frame_index.hasValue() == false)
    {
        host_was_playing = host_is_playing;
        return;
    }

    juce::Optional<double> optional_host_frame_seconds = play_head_position->getTimeInSeconds();
    if (optional_host_frame_seconds.hasValue() == false)
    {
        host_was_playing = host_is_playing;
        return;
    }

    host_frame = *optional_host_frame_index;

    if (host_is_playing == false)
    {
        if (host_was_playing == true)
        {
            pending_score_time_seconds = *optional_host_frame_seconds;
            pending_score_time_samples = host_frame;
            requestGlobalRestart();
        }

        host_prior_frame = host_frame;
        host_was_playing = false;
        return;
    }

    if ((host_was_playing == false) || (host_frame < host_prior_frame))
    {
        pending_score_time_seconds = *optional_host_frame_seconds;
        pending_score_time_samples = host_frame;
        requestGlobalRestart();
    }

    host_prior_frame = host_frame;
    host_was_playing = true;
}
 
template<typename T> void drain(moodycamel::ReaderWriterQueue<T> &queue)
{
    T element;
    while (queue.try_dequeue(element))
    {
        
    }
}
/**
 * Compiles the csd and starts Csound.
 */
void CsoundVST3AudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    juce::MessageManagerLock lock;
    drain(csound_messages_fifo);
    auto editor = getActiveEditor();
    if (editor)
    {
        auto pluginEditor = reinterpret_cast<CsoundVST3AudioProcessorEditor *>(editor);
        pluginEditor->messageLog->loadContent("");

    }
    csoundMessage("CsoundVST3AudioProcessor::prepareToPlay...\n");
    if (csoundIsPlaying == true)
    {
        csoundIsPlaying = false;
        csound.Stop();
        csound.Cleanup();
        csound.Reset();
    }
    // (Re-)set the Csound message callback.
    csound.SetHostData(this);
    csound.SetMessageCallback(csoundMessageCallback_);
    // Set up connections with the host.
    csound.SetHostImplementedMIDIIO(1);
    csound.SetHostImplementedAudioIO(1, 0);
    csound.SetExternalMidiInOpenCallback(&CsoundVST3AudioProcessor::midiDeviceOpen);
    csound.SetExternalMidiReadCallback(&CsoundVST3AudioProcessor::midiRead);
    csound.SetExternalMidiInCloseCallback(&CsoundVST3AudioProcessor::midiDeviceClose);
    csound.SetExternalMidiOutOpenCallback(&CsoundVST3AudioProcessor::midiDeviceOpen);
    csound.SetExternalMidiWriteCallback(&CsoundVST3AudioProcessor::midiWrite);
    csound.SetExternalMidiOutCloseCallback(&CsoundVST3AudioProcessor::midiDeviceClose);
    auto midi_input_devices = juce::MidiInput::getAvailableDevices();
    auto input_device_count = midi_input_devices.size();
    for (auto device_index = 0; device_index < input_device_count; ++device_index)
    {
        auto device = midi_input_devices.getReference(device_index);
        juce::String message = juce::String::formatted("MIDI input device:  %3d %-40s (id: %s)", device_index, device.name.toUTF8(), device.identifier.toUTF8());
        message = message + "\n";
        csoundMessage(message);
    }
    auto midi_output_devices = juce::MidiOutput::getAvailableDevices();
    auto output_device_count = midi_output_devices.size();
    for (auto device_index = 0; device_index < output_device_count; ++device_index)
    {
        auto device = midi_output_devices.getReference(device_index);
        juce::String message = juce::String::formatted("MIDI output device: %3d %-40s (id: %s)", device_index, device.name.toUTF8(), device.identifier.toUTF8());
        message = message + "\n";
        csoundMessage(message);
    }
    auto initial_delay_frames = csound.GetKsmps();
    setLatencySamples(initial_delay_frames);
    /*
     Message level for standard (terminal) output. Takes the sum of any of the following values:
     1 = note amplitude messages
     2 = samples out of range message
     4 = warning messages
     128 = print benchmark information
     1024 = suppress deprecated messages
     And exactly one of these to select note amplitude format:
     0 = raw amplitudes, no colours
     32 = dB, no colors
     64 = dB, out of range highlighted with red
     96 = dB, all colors
     256 = raw, out of range highlighted with red
     512 = raw, all colours
     All messages can be suppressed by using message level 16.
     */
    // I suggest: 1 + 2 + 128 + 32 = 163.
    char buffer[0x200];
    // Overrride the csd's sample rate.
    int host_sample_rate = getSampleRate();
    snprintf(buffer, sizeof(buffer), "--sample-rate=%d", host_sample_rate);
    csound.SetOption(buffer);
    // Prevents funny characters from being displaned in Csound messages.
    snprintf(buffer, sizeof(buffer), "-+msg_color=0");
    csound.SetOption(buffer);
    // If there is a csd, compile it.
    if (csd.length()  > 0) {
        const char* csd_text = strdup(csd.toRawUTF8());
        if (csd_text) {
            auto result = csound.CompileCsdText(csd_text);
            if (result != 0)
            {
                csoundMessage("prepareToPlay: csound.CompileCsdText failed.\n");
            }
            std::free((void *)csd_text);
            result = csound.Start();
            if (result != 0)
            {
                csoundMessage("prepareToPlay: csound.Start failed.\n");
            }
        }
    }
    odbfs = csound.Get0dBFS();
    iodbfs = 1. / csound.Get0dBFS();
    host_input_channels  = getTotalNumInputChannels();
    host_output_channels = getTotalNumOutputChannels();
    csound_input_channels = csound.GetNchnlsInput();
    csound_output_channels = csound.GetNchnls();
    host_frame = 0;
    host_prior_frame = 0;
    csound_frames = csound.GetKsmps();
    csound_block_begin = 0;
    csound_block_end = csound_block_begin + csound_frames;
    host_block_begin = 0;
    const int host_input_busses = getBusCount(true);
    const int host_output_busses = getBusCount(false);
    csoundMessage(juce::String::formatted("Host input busses:      %3d\n", host_input_busses));
    csoundMessage(juce::String::formatted("host output busses:     %3d\n", host_output_busses));
    csoundMessage(juce::String::formatted("Host input channels:    %3d\n", host_input_channels));
    csoundMessage(juce::String::formatted("Csound input channels:  %3d\n", csound_input_channels));
    csoundMessage(juce::String::formatted("Csound output channels: %3d\n", csound_output_channels));
    csoundMessage(juce::String::formatted("Host output channels:   %3d\n", host_output_channels));
    csoundMessage(juce::String::formatted("Csound ksmps:           %3d\n", csound_frames));
    drain(midi_input_fifo);
    drain(audio_input_fifo);
    drain(midi_output_fifo);
    drain(audio_output_fifo);
    // TODO: the following is a hack, better try something else.
    auto host_description = plugin_host_type.getHostDescription();
    DBG("Host description: " << host_description);
    auto host = juce::String::formatted("Host: %s\n", host_description);
    csoundMessage(host.toUTF8());
    if (plugin_host_type.type == juce::PluginHostType::UnknownHost)
    {
        csoundIsPlaying = false;
        orchestra_ready = false;
        suspendProcessing(true);
        csoundMessage("CsoundVST3AudioProcessor::prepareToPlay: Ready to play.\n");
    }
    else
    {
        csoundIsPlaying = true;
        orchestra_ready = true;
        suspendProcessing(false);
        csoundMessage("CsoundVST3AudioProcessor::prepareToPlay: Csound is plqying.\n");
    }
    plugin_frame = 0;
    midi_input_sequence = 0;
 }

/**
 * Calls csoundPerformKsmps to do the actual processing.
 *
 * The number of input channels may not equal the number of output channels.
 * The number of frames in the buffer may not be the same as Csound's ksmps,
 * and may not be the same on every call. Input data in the host's  buffers is
 * replaced by output data, or cleared.
 *
 * This implementation uses MoodyCamel's ReaderWriterQueue as FIFOs
 * for synchronizing these potential mismatches.
 *
 * In each processBlock call,, the incoming buffers  first have their data
 * pushed onto midi_input_fifo and audio_input_fifo., after which the host's
 * MidiBuffer and AudioBuffer are immediately  cleared. Whenever ksmps frames
 * of audio have been processed, Csound. performKsmps is called, during
 * which sensEvents calls the plugin's MIDI read callback, in which  MIDI
 * messages copied to Csound, and the plugin's MIDI write callback, which
 * pushes  MIDI mrssages from  Csound onto midi_output_fifo. Then, spout
 * is pushed onto audio_output_fifo. These thiings  can happen at any time,
 * and any number of times, during the processBlock call.. processBlock then
 * pops MIDI messages from midi_output_fifo into the hostt's empty
 * MidiBuffer, and pops audio from audio_output_fifo onto the host's empty
 * AudioBuffer. When that is full, processBlock returns.,
 */
void CsoundVST3AudioProcessor::processBlock (juce::AudioBuffer<float>& host_audio_buffer, juce::MidiBuffer& host_midi_buffer)
{
    auto play_head = getPlayHead();
    if (play_head == nullptr)
    {
        host_audio_buffer.clear();
        host_midi_buffer.clear();
        return;
    }
    auto play_head_position = play_head->getPosition();
    synchronizeScore(play_head_position);

    bool host_is_playing = false;
    if (play_head_position.hasValue())
    {
        host_is_playing = play_head_position->getIsPlaying();
        auto optional_time_in_samples = play_head_position->getTimeInSamples();
        if (optional_time_in_samples.hasValue())
        {
            host_block_frame = *optional_time_in_samples;
        }
    }

    if (host_is_playing == false)
    {
        host_audio_buffer.clear();
        host_midi_buffer.clear();
        return;
    }

    if (restart_requested == true)
    {
        performGlobalRestart(getSampleRate(),
                            getBlockSize(),
                            pending_score_time_seconds,
                            pending_score_time_samples);
        host_audio_buffer.clear();
        host_midi_buffer.clear();
        return;
    }

    if ((csoundIsPlaying == false) || (orchestra_ready == false))
    {
        host_audio_buffer.clear();
        host_midi_buffer.clear();
        return;
    }
    juce::ScopedNoDenormals noDenormals;
    auto host_audio_buffer_frames = host_audio_buffer.getNumSamples();
    host_block_begin = host_frame;
    host_block_end = host_block_begin + host_audio_buffer_frames;
    // Csound reads audio input from this buffer.
    auto spin = csound.GetSpin();
    // Csound writes audio output to this buffer.
    auto spout = csound.GetSpout();
    if (spout == nullptr)
    {
        csoundMessage("Null spout...\n");
        return;
    }
    auto host_audio_input_buffer = getBusBuffer(host_audio_buffer, true, 0);
    auto host_audio_output_buffer = getBusBuffer(host_audio_buffer, false, 0);
    
    // Csound's spin and spout buffers are indexed [frame][channel].
    // The host audio buffer is indexed [channel][frame].
    // As far as I can tell, both `getWritePointer` and `setSample` mark the
    // host audio buffer as not clear, and have the same addresses for the
    // same elements. The host buffer channel count is the greater of (inputs
    // + side chains) and outputs. Input channels are followed by side chain
    // channels. Output channels overlap inputs and possibly side chains.
    // (CsoundVST3 does not use side chains.) The getBusBuffer function
    // gathers the appropriate channel pointers into a channel array for
    // the indicated buffer, but care must be used to respect the outputs
    // overlapping other channels.
        
    // Push all inputs onto FIFOs. Here, frame is the frame of the message
    // counting from the beginning of performance. Only MIDI channel messages
    // are handled, although these can of course include PRNs and NPRNs.
    int input_messages = 0;
    int output_messages = 0;
    for (const auto metadata : host_midi_buffer)
    {
        auto message = metadata.getMessage();
        MidiChannelMessage channel_message;
        channel_message.sequence = midi_input_sequence++;
        channel_message.message = message;
        channel_message.plugin_frame = host_block_begin + metadata.samplePosition;
        channel_message.csound_frame = channel_message.plugin_frame % csound_frames;
        
        auto status = metadata.data[0];
        // We process only MIDI channel messages.
        if ((0x80 <= status) && (status <= 0xE0))        {
             midi_input_fifo.enqueue(channel_message);
#if defined(JUCE_DEBUG)
            if (fifo_debug == true)
            {
                input_messages++;
                char buffer[0x200];
                // The channel message frame must be in [host_block_begin, host_block_end).
                auto tyme = plugin_frame / float(csound.GetSr());
                assert(channel_message.plugin_frame >= host_block_begin && channel_message.plugin_frame < host_block_end);
                std::snprintf(buffer, sizeof(buffer),
                              "Host processBlock #%5lld: time:%9.4f host begin%8llu plugin%8llu msg%8llu cs%8llu host end%8llu  %s", channel_message.sequence, tyme, host_block_begin, plugin_frame, channel_message.plugin_frame, channel_message.csound_frame, host_block_end, message.getDescription().toRawUTF8());
                DBG(buffer);
            }
#endif
        }
    }
    host_midi_buffer.clear();
    for (int host_audio_buffer_frame = 0; host_audio_buffer_frame < host_audio_buffer_frames; ++host_audio_buffer_frame)
    {
        for (int host_audio_buffer_channel = 0; host_audio_buffer_channel < host_input_channels; ++host_audio_buffer_channel)
        {
            auto sample = host_audio_buffer.getSample(host_audio_buffer_channel, host_audio_buffer_frame);
            audio_input_fifo.enqueue(sample);
        }
    }
    host_audio_buffer.clear();
    // FIFOs being loaded, now process.
    // The host block pops audio input...
    for (host_audio_buffer_frame = 0; host_audio_buffer_frame < host_audio_buffer_frames; ++host_audio_buffer_frame, ++host_frame, ++csound_frame, ++plugin_frame)
    {
        int channel_index;
        csound_frame = plugin_frame % csound_frames;
        for (channel_index = 0; channel_index < host_input_channels; channel_index++)
        {
            float sample = 0;
            if (audio_input_fifo.try_dequeue(sample))
            {
                sample = sample * odbfs;
            }
            spin[(csound_frame * csound_input_channels) + channel_index] = double(sample);
        }
        // ...until spin is full...
        if (csound_frame == (csound_frames - 1))
        {
            csound_block_begin = plugin_frame - (csound_frames - 1);
            csound_block_end = csound_block_begin + csound_frames;
            auto result = csound.PerformKsmps();
            if (result != 0)
            {
                pending_score_time_seconds = host_block_begin / getSampleRate();
                pending_score_time_samples = host_block_begin;
                requestGlobalRestart();
                host_audio_buffer.clear();
                host_midi_buffer.clear();
                return;
            }
            for (int csound_block_frame = 0; csound_block_frame < csound_frames; ++csound_block_frame)
            {
                for (int csound_output_channel = 0; csound_output_channel < csound_output_channels; ++csound_output_channel)
                {
                    auto sample = iodbfs * spout[(csound_block_frame * csound_output_channels) + csound_output_channel];
                    audio_output_fifo.enqueue(sample);
                }
            }
        }
    }
    // Processing of the host block being completed,
    // now pop from the output FIFOs until JUCE outputs are full.
    while (true)
    {
        auto message = midi_output_fifo.peek();
        if (message == nullptr)
        {
            break;
        }
        if ((message->plugin_frame >= host_block_begin) && (message->plugin_frame < host_block_end))
        {
            auto timestamp = message->plugin_frame - host_block_begin;
            host_midi_buffer.addEvent(message->message, int(timestamp));
            midi_output_fifo.pop();
#if defined(JUCE_DEBUG)
            if (fifo_debug == true)
            {
                output_messages++;
                char buffer[0x200];
                std::snprintf(buffer, sizeof(buffer),
                              "MIDI output to host#%5d: frame%8llu timestamp%8llu csound: begin%8llu frame %8llu %8llu end%8llu %s", output_messages, plugin_frame, timestamp, csound_block_begin, plugin_frame, csound_frame, csound_block_end, message->message.getDescription().toRawUTF8());
                DBG(buffer);
            }
#endif
        }
    }
    for (int host_audio_buffer_frame = 0; host_audio_buffer_frame < host_audio_buffer_frames; ++host_audio_buffer_frame)
    {
        for (int host_output_channel = 0; host_output_channel < host_output_channels; ++host_output_channel)
        {
            // TODO: This is a hack.
            auto sample = audio_output_fifo.peek();
            if (sample != nullptr)
            {
                host_audio_buffer.setSample(host_output_channel, host_audio_buffer_frame, iodbfs * *sample);
                audio_output_fifo.pop();
            }
            else
            {
                DBG("processBlock: WARNING! Audio output FIFO is empty but shouldn't be!");
            }
        }
    }
}

//==============================================================================
bool CsoundVST3AudioProcessor::hasEditor() const
{
    return true;
}

juce::AudioProcessorEditor* CsoundVST3AudioProcessor::createEditor()
{
    return new CsoundVST3AudioProcessorEditor (*this);
}

//==============================================================================
void CsoundVST3AudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::ValueTree state("CsoundVstState");
    state.setProperty("csd", csd, nullptr);
    juce::MemoryOutputStream stream(destData, false);
    state.writeToStream(stream);
}

void CsoundVST3AudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    juce::MessageManagerLock lock;
    juce::ValueTree state = juce::ValueTree::readFromData(data, static_cast<size_t>(sizeInBytes));
    if (state.isValid() && state.hasType("CsoundVstState"))
    {
        csd = state.getProperty("csd", "").toString();
        auto editor = getActiveEditor();
        if (editor) {
            auto pluginEditor = reinterpret_cast<CsoundVST3AudioProcessorEditor *>(editor);
            pluginEditor->codeEditor->loadContent(csd);
        }
    }
}

//==============================================================================
// This creates new instances of the plugin..
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new CsoundVST3AudioProcessor();
}

void CsoundVST3AudioProcessor::play()
{
    stop();
    suspendProcessing(false);
    auto frames_per_second = getSampleRate();
    auto frame_size = getBlockSize();
    prepareToPlay(frames_per_second, frame_size);
 }

void CsoundVST3AudioProcessor::stop()
{
    suspendProcessing(true);
    csoundIsPlaying = false;
    orchestra_ready = false;
    restart_requested = false;
    csound.Stop();
    csound.Cleanup();
    csound.Reset();
}


