#include "audio_output.h"
#include "midi_input.h"
#include "synthesizer.h"
#include "third_party/cmdline.h"

int main(int argc, char** argv) {
    try {
        using namespace primesynth;

        cmdline::parser argparser;
        argparser.set_program_name("primesynth");
        argparser.add<unsigned int>("in", 'i', "input MIDI device ID", false, 0);
        argparser.add<unsigned int>("out", 'o', "output audio device ID", false);
        argparser.add<double>("volume", 'v', "volume (1 = 100%)", false, 1.0);
        argparser.add<double>("samplerate", 's', "sample rate (Hz)", false);
        argparser.add<unsigned int>("buffer", 'b', "audio output buffer size", false, 1 << 12);
        argparser.add<unsigned int>("channels", 'c', "number of MIDI channels", false, 16);
        argparser.add<std::string>("std", '\0', "MIDI standard, affects bank selection (gm, gs, xg)", false, "gs",
                                   cmdline::oneof<std::string>("gm", "gs", "xg"));
        argparser.add("fix-std", '\0', "do not respond to GM/XG System On, GS Reset, etc.");
        argparser.add("print-msg", 'p', "print received MIDI messages");
        argparser.footer("[soundfonts] ...");
        argparser.parse_check(argc, argv);
        if (argparser.rest().empty()) {
            throw std::runtime_error("SoundFont file required");
        }

        const double sampleRate =
            argparser.exist("samplerate") ? argparser.get<double>("samplerate") : AudioOutput::getDefaultSampleRate();

        auto midiStandard = midi::Standard::GM;
        if (argparser.get<std::string>("std") == "gs") {
            midiStandard = midi::Standard::GS;
        } else if (argparser.get<std::string>("std") == "xg") {
            midiStandard = midi::Standard::XG;
        }

        Synthesizer synth(sampleRate, argparser.get<unsigned int>("channels"));
        synth.setMIDIStandard(midiStandard, argparser.exist("fix-std"));
        synth.setVolume(argparser.get<double>("volume"));
        for (const std::string& filename : argparser.rest()) {
            std::cout << "loading " << filename << std::endl;
            synth.loadSoundFont(filename);
        }

        MIDIInput midiInput(synth, argparser.get<unsigned int>("in"), argparser.exist("print-msg"));
        AudioOutput audioOutput(synth, argparser.get<unsigned int>("buffer"),
                                argparser.exist("out") ? argparser.get<unsigned int>("out")
                                                       : AudioOutput::getDefaultDeviceID(),
                                sampleRate);

        SetPriorityClass(GetCurrentProcess(), REALTIME_PRIORITY_CLASS);

        std::cout << "Press enter to exit" << std::endl;
        std::getchar();
    } catch (const std::exception& ex) {
        std::cerr << ex.what() << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
