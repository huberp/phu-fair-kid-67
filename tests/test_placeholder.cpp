#include <cassert>
#include <cmath>
#include <iostream>

#include "../src/DSP/UnitScaling.h"

static void testUnitScaling() {
    // sampleToVolts: ±1.0 sample <-> ±10 V
    assert(std::abs(UnitScaling::sampleToVolts(1.0f) - 10.0f) < 1e-6f);
    assert(std::abs(UnitScaling::sampleToVolts(-1.0f) - (-10.0f)) < 1e-6f);
    assert(std::abs(UnitScaling::sampleToVolts(0.0f)) < 1e-6f);

    // voltsToSample: round-trip
    for (float v : {-10.0f, -5.0f, 0.0f, 5.0f, 10.0f}) {
        float roundTrip = UnitScaling::sampleToVolts(UnitScaling::voltsToSample(v));
        assert(std::abs(roundTrip - v) < 1e-5f);
    }

    std::cout << "  UnitScaling tests passed." << std::endl;
}

int main() {
    testUnitScaling();
    std::cout << "All placeholder tests passed." << std::endl;
    return 0;
}

