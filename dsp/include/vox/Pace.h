#pragma once

namespace vox::pace {

/** Heavy work off the audio thread (Auto-Edit, the Coach, Auto Analyze's checks) rests on purpose: a few ms after every
    short stretch of work, so it never keeps a CPU core and the memory busy for seconds on end while the DAW plays (Beta 27:
    the song cut out a couple of times while the Coach analysed). Per thread, and off unless a Scope turns it on: the tests
    and anything else run flat out, and the results are the same either way (only the timing changes). */
class Scope
{
public:
    Scope (double workMs, double restMs) noexcept;
    ~Scope();
    Scope (const Scope&) = delete;
    Scope& operator= (const Scope&) = delete;

private:
    bool wasOn;
    double wasWork, wasRest;
};

/** Called often in long loops: rests when this thread has worked `workMs` since its last rest (nothing when off). */
void tick() noexcept;

/** On this thread since its Scope began: the longest stretch between two ticks (ms) and the time spent resting (ms). */
double longestStretchMs() noexcept;
double restedMs() noexcept;

} // namespace vox::pace
