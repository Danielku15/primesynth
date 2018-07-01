#include "synthesizer.h"

namespace primasynth {
Synthesizer::Synthesizer(double outputRate, std::size_t numChannels)
    : volume_(1.0), midiStd_(midi::Standard::GM), defaultMIDIStd_(midi::Standard::GM), stdFixed_(false) {
    conv::initialize();

    channels_.reserve(numChannels);
    for (std::size_t i = 0; i < numChannels; ++i) {
        channels_.emplace_back(std::make_unique<Channel>(outputRate));
    }
}

StereoValue Synthesizer::render() const {
    StereoValue sum{0.0, 0.0};
    for (const auto& channel : channels_) {
        sum += channel->render();
    }
    return volume_ * sum;
}

void Synthesizer::loadSoundFont(const std::string& filename) {
    soundFonts_.emplace_back(std::make_unique<SoundFont>(filename));
}

void Synthesizer::setVolume(double volume) {
    volume_ = std::max(0.0, volume);
}

void Synthesizer::setMIDIStandard(midi::Standard midiStandard, bool fixed) {
    midiStd_ = midiStandard;
    defaultMIDIStd_ = midiStandard;
    stdFixed_ = fixed;
}

void Synthesizer::processShortMessage(unsigned long param) {
    const auto msg = reinterpret_cast<std::uint8_t*>(&param);
    const auto status = msg[0] & 0xf0;
    if (status != 0xf0) {
        processChannelMessage(param);
    }
}

template <std::size_t N>
bool matchSysEx(const char* data, std::size_t length, const std::array<unsigned char, N>& sysEx) {
    if (length != N) {
        return false;
    }

    for (std::size_t i = 0; i < N; ++i) {
        if (i == 2) {
            // respond to all device IDs
            continue;
        } else if (data[i] != static_cast<char>(sysEx.at(i))) {
            return false;
        }
    }
    return true;
}

void Synthesizer::processSysEx(const char* data, std::size_t length) {
    static constexpr std::array<unsigned char, 6> GM_SYSTEM_ON = {0xf0, 0x7e, 0, 0x09, 0x01, 0xf7};
    static constexpr std::array<unsigned char, 6> GM_SYSTEM_OFF = {0xf0, 0x7e, 0, 0x09, 0x02, 0xf7};
    static constexpr std::array<unsigned char, 11> GS_RESET = {0xf0, 0x41, 0,    0x42, 0x12, 0x40,
                                                               0x00, 0x7f, 0x00, 0x41, 0xf7};
    static constexpr std::array<unsigned char, 11> GS_SYSTEM_MODE_SET1 = {0xf0, 0x41, 0,    0x42, 0x12, 0x00,
                                                                          0x00, 0x7f, 0x00, 0x01, 0xf7};
    static constexpr std::array<unsigned char, 11> GS_SYSTEM_MODE_SET2 = {0xf0, 0x41, 0,    0x42, 0x12, 0x00,
                                                                          0x00, 0x7f, 0x01, 0x00, 0xf7};
    static constexpr std::array<unsigned char, 9> XG_SYSTEM_ON = {0xf0, 0x43, 0, 0x4c, 0x00, 0x00, 0x7e, 0x00, 0xf7};

    if (stdFixed_) {
        return;
    }
    if (matchSysEx(data, length, GM_SYSTEM_ON)) {
        midiStd_ = midi::Standard::GM;
    } else if (matchSysEx(data, length, GM_SYSTEM_OFF)) {
        midiStd_ = defaultMIDIStd_;
    } else if (matchSysEx(data, length, GS_RESET) || matchSysEx(data, length, GS_SYSTEM_MODE_SET1) ||
               matchSysEx(data, length, GS_SYSTEM_MODE_SET2)) {
        midiStd_ = midi::Standard::GS;
    } else if (matchSysEx(data, length, XG_SYSTEM_ON)) {
        midiStd_ = midi::Standard::XG;
    }
}

std::shared_ptr<const Preset> Synthesizer::findPreset(std::uint16_t bank, std::uint16_t presetID) const {
    for (const auto& sf : soundFonts_) {
        for (const auto& preset : sf->getPresetPtrs()) {
            if (preset->bank == bank && preset->presetID == presetID) {
                return preset;
            }
        }
    }

    // fallback
    if (bank == PERCUSSION_BANK) {
        // fall back to GM percussion
        if (presetID != 0) {
            return findPreset(bank, 0);
        } else {
            throw std::runtime_error("failed to find preset 128:0 (GM Percussion)");
        }
    } else if (bank != 0) {
        // fall back to GM bank
        return findPreset(0, presetID);
    } else if (presetID != 0) {
        // preset not found even in GM bank, fall back to Piano
        return findPreset(0, 0);
    } else {
        // Piano not found, there is no more fallback
        throw std::runtime_error("failed to find preset 0:0 (GM Acoustic Grand Piano)");
    }
}

void Synthesizer::processChannelMessage(unsigned long param) {
    const auto msg = reinterpret_cast<std::uint8_t*>(&param);

    const std::uint8_t channelID = msg[0] & 0xf;
    if (channelID >= channels_.size()) {
        return;
    }
    const auto& channel = channels_.at(channelID);

    const auto status = static_cast<midi::MessageStatus>(msg[0] & 0xf0);
    switch (status) {
    case midi::MessageStatus::NoteOff:
        channel->noteOff(msg[1]);
        break;
    case midi::MessageStatus::NoteOn:
        if (!channel->hasPreset()) {
            channel->setPreset(channelID == midi::PERCUSSION_CHANNEL ? findPreset(PERCUSSION_BANK, 0)
                                                                     : findPreset(0, 0));
        }
        channel->noteOn(msg[1], msg[2]);
        break;
    case midi::MessageStatus::KeyPressure:
        channel->keyPressure(msg[1], msg[2]);
        break;
    case midi::MessageStatus::ControlChange:
        channel->controlChange(msg[1], msg[2]);
        break;
    case midi::MessageStatus::ProgramChange: {
        const auto midiBank = channel->getBank();
        std::uint16_t sfBank = 0;
        switch (midiStd_) {
        case midi::Standard::GM:
            break;
        case midi::Standard::GS:
            sfBank = midiBank.msb;
            break;
        case midi::Standard::XG:
            // assuming no one uses XG voices bank MSBs of which overlap normal voices' bank LSBs
            // e.g. SFX voice, MSB=64
            sfBank = midiBank.msb == 127 ? PERCUSSION_BANK : midiBank.lsb;
            break;
        default:
            throw std::runtime_error("unknown MIDI standard");
        }
        channel->setPreset(findPreset(channelID == midi::PERCUSSION_CHANNEL ? PERCUSSION_BANK : sfBank, msg[1]));
        break;
    }
    case midi::MessageStatus::ChannelPressure:
        channel->channelPressure(msg[1]);
        break;
    case midi::MessageStatus::PitchBend:
        channel->pitchBend(midi::joinBytes(msg[2], msg[1]));
        break;
    }
}
}
