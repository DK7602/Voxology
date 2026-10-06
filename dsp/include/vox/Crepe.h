#pragma once

#include <array>
#include <vector>

/** CREPE "tiny" (Kim, Salamon, Li, Bello 2018: a convolutional neural network trained to find the pitch of
    a voice / instrument; github.com/marl/crepe, MIT License - see dsp/third_party/CREPE-LICENSE.txt),
    run here without any framework. Six 1-D convolutions (ReLU, batch norm, max-pool) and a 360-way
    classifier over 20-cent steps from C1 to B7; the pitch is the salience-weighted average around the
    peak, the confidence the peak's salience.

    Its input is 1024 samples at 16 kHz (64 ms). About 37 M multiply-adds per estimate (a few ms on a
    desktop core): for offline use (Honey Tune), not the live chain. */
namespace vox::crepe {

inline constexpr double kRate = 16000.0;
inline constexpr int kFrame = 1024;

struct Estimate
{
    double hz = 0.0;
    double confidence = 0.0;   // 0..1 (voiced notes: usually > 0.5)
};

class Tiny
{
public:
    Tiny();
    /** One estimate for 1024 samples at 16 kHz (mean / level don't matter: it's normalised). */
    Estimate run (const float* frame);

private:
    std::vector<float> w;   // all weights, unpacked
    struct Conv { int width, in, out, stride; size_t kernel, bias, scale, shift; };
    std::array<Conv, 6> conv {};
    size_t dense = 0, denseBias = 0;
    std::vector<float> a, b;  // activations (ping-pong)
};

/** x at sampleRate -> 16 kHz (windowed-sinc, anti-aliased). */
std::vector<float> resampleTo16k (const std::vector<float>& x, double sampleRate);

} // namespace vox::crepe
