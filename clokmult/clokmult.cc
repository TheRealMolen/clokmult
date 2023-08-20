//********************************************************************************
//  **  k l o k k e n f ä n g l e r  **
//
// fangling the clocks
//

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include "pico/stdlib.h"

constexpr int ClockInPin = 6;
constexpr int BtnUpInPin = 8;
constexpr int BtnDnInPin = 9;
constexpr int LedPin = 25;

constexpr int ClockOutAPin = 14;
constexpr int ClockOutALedPin = 12;
constexpr int ClockOutBPin = 15;
constexpr int ClockOutBLedPin = 13;


class SevenSeg
{
public:
    using pins_t = std::array<int, 7>;

    constexpr SevenSeg(const pins_t& pins) : mPins(pins)
    {
        for (int pin : mPins)
        {
            if (pin < 0)
                continue;
                
            gpio_init(pin);
            gpio_set_dir(pin, GPIO_OUT);
        }
    }

    void SetRaw(uint m) const;

private:
    pins_t mPins;
};

void SevenSeg::SetRaw(uint m) const
{
    for (int pin : mPins)
    {
        if (pin >= 0)
        {
            bool set = (m & 1) != 0;
            gpio_put(pin, set);
        }

        m >>= 1;
    }
}

SevenSeg gDisp({17, 16, 20, 21, 22, 18, 19});


class ClockOut
{
public:
    ClockOut(char name, int clockPin, int ledPin) : mName(name), mClockPin(clockPin), mLedPin(ledPin)
    {
        gpio_init(clockPin);
        gpio_set_dir(clockPin, GPIO_OUT);
        gpio_init(ledPin);
        gpio_set_dir(ledPin, GPIO_OUT);

        SetGateState(false);
    }

    void SetBeatPeriodUs(uint32_t periodUs)
    {
        mPeriodUs = periodUs;

        // reset the phase if it's near a boundary
        uint32_t tolerance = (periodUs / 8);
        if (mPhaseUs < tolerance || mPhaseUs > (periodUs - tolerance))
        {
            printf("clk%c: new period %dus, RESETTING\n", mName, periodUs);
            mPhaseUs = 0;
        }
        else
        {
            printf("clk%c: new period %dus, not resetting\n", mName, periodUs);
        }
    }

    void Tick(uint32_t usSinceLastBeat);

private:
    void SetGateState(bool output)
    {
        // NOTE: our clock output is inverted as it gets inverted back when buffered
        gpio_put(mClockPin, !output);
        gpio_put(mLedPin, output);
    }

    static const uint32_t TrigLengthUs = 4000;

    char mName = 'x';

    int mClockPin;
    int mLedPin;

    uint32_t mPeriodUs = ~0;
    uint32_t mPhaseUs = 0;
};

void ClockOut::Tick(uint32_t deltaTimeUs)
{
    mPhaseUs += deltaTimeUs;
    if (mPhaseUs >= mPeriodUs)
        mPhaseUs -= mPeriodUs;

    const uint32_t gateLen = mPeriodUs / 2;

    bool trigOut = (mPhaseUs < TrigLengthUs);
    bool gateOut = (mPhaseUs < gateLen);
    
    // NOTE: our clock output is inverted as it gets inverted back when buffered
    gpio_put(mClockPin, !trigOut);
    gpio_put(mLedPin, gateOut);
}



ClockOut gClockOutA('A', ClockOutAPin, ClockOutALedPin);
ClockOut gClockOutB('B', ClockOutBPin, ClockOutBLedPin);


inline uint64_t micros()
{
    return to_us_since_boot(get_absolute_time());
}

inline uint32_t millis()
{
    return to_ms_since_boot(get_absolute_time());
}


inline bool readInputPin()
{
    // NB. input gets inverted while we level shift it
    return !gpio_get(ClockInPin);
}



int main()
{
    gpio_init(ClockInPin);
    gpio_set_dir(ClockInPin, GPIO_IN);
    gpio_pull_up(ClockInPin);
    gpio_init(BtnUpInPin);
    gpio_set_dir(BtnUpInPin, GPIO_IN);
    gpio_pull_up(BtnUpInPin);
    gpio_init(BtnDnInPin);
    gpio_set_dir(BtnDnInPin, GPIO_IN);
    gpio_pull_up(BtnDnInPin);

    gpio_init(LedPin);
    gpio_set_dir(LedPin, GPIO_OUT);

    stdio_usb_init();

    uint sevensegMask = 0xc6;
    int32_t timeTillNextSS = 0;
    gDisp.SetRaw(sevensegMask);

    int clocksReceived = 0;
    bool lastInputState = readInputPin();
    uint64_t lastGateStartUs = 0;
    uint32_t gatePeriodUs = ~0;
    uint64_t lastTimeUs = micros();
    bool beat = false;
    for(;;)
    {
        const uint64_t nowUs = micros();
        const uint32_t deltaTimeUs = uint32_t(nowUs - lastTimeUs);
        lastTimeUs = nowUs;

        // tick the 7seg
        timeTillNextSS -= deltaTimeUs;
        if (timeTillNextSS <= 0)
        {
            uint newMask = (sevensegMask >> 1) | ((sevensegMask & 1) << 6);
            sevensegMask = newMask;
            gDisp.SetRaw(sevensegMask);
            timeTillNextSS = 150*1000;
        }

        if (!gpio_get(BtnUpInPin))
        {
            gDisp.SetRaw(0x73);
        }
        else if (!gpio_get(BtnDnInPin))
        {
            gDisp.SetRaw(0x5e);
        }

        // read from clock input
        const bool inputState = readInputPin();

        // was that a rising edge?
        if (!lastInputState && inputState)
        {
            const uint64_t thisGateUs = nowUs;

            // if that was the first rising edge we've seen, we just start waiting for the next one
            if (clocksReceived == 0)
            {
                lastGateStartUs = thisGateUs;
                ++clocksReceived;
                lastInputState = inputState;
                continue;
            }
            
            const uint32_t timeSincePrevGate = nowUs - lastGateStartUs;
            constexpr uint32_t GateDebounceUs = 2000;
            if (timeSincePrevGate > GateDebounceUs)
            {
                // ok, we've had multiple clocks, so we know our time base
                gatePeriodUs = timeSincePrevGate;
                lastGateStartUs = thisGateUs;

                gClockOutA.SetBeatPeriodUs(timeSincePrevGate * 2);
                gClockOutB.SetBeatPeriodUs(timeSincePrevGate / 4);

                if (clocksReceived < 2)
                    ++clocksReceived;

                beat = !beat;

                printf("beat @ %dus, period=%d\n", int(nowUs), int(gatePeriodUs));
            }
        }
        else
        {
            // if the clock input has been awol for two beats, assume it's stopped
            const uint32_t timeSincePrevGate = nowUs - lastGateStartUs;
            if (timeSincePrevGate > (2 * gatePeriodUs))
            {
                puts("clock stopped");
                clocksReceived = 0;
            }
        }

        // echo the incoming gate on the built-in LED
        lastInputState = inputState;
        gpio_put(LedPin, beat);

        if (clocksReceived < 2 || deltaTimeUs == 0)
            continue;

        gClockOutA.Tick(deltaTimeUs);
        gClockOutB.Tick(deltaTimeUs);
    }

    return 0;
}
