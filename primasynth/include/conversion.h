#pragma once
#include <cstdint>

namespace primasynth {
namespace conv {
void initialize();

// attenuation: normalized log scale value, 0.0 = 0dB, 1.0 = 96dB
// amplitude:   normalized linear value in [0, 1]
double attenuationToAmplitude(double atten);
double amplitudeToAttenuation(double amp);

double keyToHertz(double key);
double timecentToSecond(double tc);
double absoluteCentToHertz(double ac);

double concave(double x);
double convex(double x);

std::uint16_t joinBytes(std::uint8_t msb, std::uint8_t lsb);
}
}
