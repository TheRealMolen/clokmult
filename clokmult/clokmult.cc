//********************************************************************************
//  **  k l o k k e n f ä n g l e r  **
//
// fangling the clocks
//

#include <cstdint>
#include <cstdio>
#include "pico/stdlib.h"

constexpr int ClockInPin = 6;
constexpr int LedPin = 25;

constexpr int ClockOutAPin = 14;
constexpr int ClockOutALedPin = 12;
constexpr int ClockOutBPin = 15;
constexpr int ClockOutBLedPin = 13;


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

    gpio_init(LedPin);
    gpio_set_dir(LedPin, GPIO_OUT);

    //stdio_usb_init();
    stdio_init_all();
    puts("hello dave");

    int clocksReceived = 0;
    bool lastInputState = readInputPin();
    uint64_t lastGateStartUs = 0;
    uint32_t gatePeriodUs = ~0;
    uint64_t lastTimeUs = micros();
    bool beat = false;
    for(;;)
    {
        // read from clock input
        const bool inputState = readInputPin();
        const uint64_t nowUs = micros();
        const uint32_t deltaTimeUs = uint32_t(nowUs - lastTimeUs);
        lastTimeUs = nowUs;

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
