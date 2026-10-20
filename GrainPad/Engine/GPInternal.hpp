// Shared between GPEngine.cpp and GPAnalysis.cpp. Not visible to Swift.
#pragma once
#include <memory>
#include <vector>

namespace gp {

struct Sample {
    std::vector<float> L, R;   // R is empty for mono
    int frames = 0;
    int channels = 1;
    double sampleRate = 48000.0;
    long id = 0;
    const float* right() const { return channels > 1 ? R.data() : L.data(); }
};

// Main-thread registry of the sample each pad currently plays.
std::shared_ptr<Sample> padSample(int pad);
void assignPadSample(int pad, std::shared_ptr<Sample> s);
double engineBpm();

} // namespace gp
