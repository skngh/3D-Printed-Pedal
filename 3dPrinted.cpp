#include "daisy_seed.h"
#include "daisysp.h"

using namespace daisy;
using namespace daisy::seed;
using namespace daisysp;

DaisySeed hw;

// Constants
#define kPreGain 1.5
#define kPostGain 1.0f
#define kSampleRate 48000

// IO
GPIO   bypassLED;
Switch footswitch;
bool   bypass = true;

#define FootswitchInput D19
#define LEDInput D30
#define KnobOneInput A9
#define KnobTwoInput A10
#define KnobThreeInput A11

// Effects
Chorus                chorus;
Overdrive             crazyOverdrive;
SmoothRandomGenerator randomDelay, randomTrem;
Tremolo               trem;
PitchShifter          pitchshift;
#define MAX_DELAY static_cast<size_t>(48000 * 0.75f)
static DelayLine<float, MAX_DELAY> delCrazy;

Overdrive overdrive;

float gCrazyDryWet = 0.0f;

// Delay variables
float gDelayTime, gCurrentDelayTime = 0.5f;
float gDelayFeedback = 0.5f;
float gDelayDryWet   = 0.37f;

// chorus variables
float gChorusDepth    = .5f;
float gChorusFreq     = .5f;
float gChorusFeedback = .3f;
float gChorusDelay    = .5f;

// Overdrive variables
float gOverdriveCrazyAmount = 0.6f;
float gOverdriveAmount      = 0.5f;

enum AdcChannel
{
    knobOne,
    knobTwo,
    knobThree,
    NUM_ADC_CHANNELS
};

void InitializeCrazyEffects()
{
    crazyOverdrive.Init();
    crazyOverdrive.SetDrive(gOverdriveCrazyAmount);

    randomDelay.Init(kSampleRate);
    randomDelay.SetFreq(1.0f);

    randomTrem.Init(kSampleRate);
    randomTrem.SetFreq(1.0f);

    trem.Init(kSampleRate);
    trem.SetDepth(0.5f);
    trem.SetWaveform(Oscillator::WAVE_SIN);

    pitchshift.Init(kSampleRate);
    pitchshift.SetTransposition(-12.0f);

    delCrazy.Init();
    delCrazy.SetDelay(kSampleRate * 0.5f);
}
void InitializeChorus()
{
    chorus.Init(kSampleRate);
    chorus.SetLfoFreq(gChorusFreq);
    chorus.SetLfoDepth(gChorusDepth);
    chorus.SetFeedback(gChorusFeedback);
    chorus.SetDelay(gChorusDelay);
}

void ProccessADC()
{
    footswitch.Debounce();
    bypass ^= footswitch.RisingEdge();

    gOverdriveAmount
        = fmap(hw.adc.GetFloat(knobOne), 0.5f, 1, daisysp::Mapping::EXP);
    gCrazyDryWet    = fmap(hw.adc.GetFloat(knobTwo), 0, .30f);
    gChorusFeedback = fmap(hw.adc.GetFloat(knobThree), 0, .8f);

    overdrive.SetDrive(gOverdriveAmount);
    chorus.SetFeedback(gChorusFeedback);
}

void InitializeADC()
{
    AdcChannelConfig adc_config[NUM_ADC_CHANNELS];
    adc_config[knobOne].InitSingle(KnobOneInput);
    adc_config[knobTwo].InitSingle(KnobTwoInput);
    adc_config[knobThree].InitSingle(KnobThreeInput);
    hw.adc.Init(adc_config, NUM_ADC_CHANNELS);
    hw.adc.Start();

    footswitch.Init(FootswitchInput, 1000);
    bypassLED.Init(LEDInput, GPIO::Mode::OUTPUT, GPIO::Pull::PULLUP);
}

float ProcessCrazyEffects(float in)
{
    float sig, del_out, feedback;

    delCrazy.SetDelay(kSampleRate
                      * fmap(abs(randomDelay.Process()), 0.05f, 0.5f));

    trem.SetFreq(fmap(abs(randomTrem.Process()), 4, 40));
    sig = crazyOverdrive.Process(in) + (pitchshift.Process(in) * 0.3f);

    // DELAY
    del_out = delCrazy.Read();

    sig      = (del_out * 0.37f) + (sig * (1.0f - 0.37f));
    feedback = (del_out * 0.5f) + sig;

    delCrazy.Write(feedback);

    sig = trem.Process(sig);

    return sig;
}

void AudioCallback(AudioHandle::InputBuffer  in,
                   AudioHandle::OutputBuffer out,
                   size_t                    size)
{
    float sig, crazy_sig;
    ProccessADC();


    for(size_t i = 0; i < size; i++)
    {
        sig = in[0][i] * kPreGain;

        if(!bypass)
        {
            sig = chorus.Process(sig) * 2.4f; // CHORUS
            sig = overdrive.Process(sig);     // OVERDRIVE

            crazy_sig = ProcessCrazyEffects(sig);
            // out[0][i]       = sig * kPostGain;
            out[0][i] = (crazy_sig * gCrazyDryWet)
                        + (sig * (1.0f - gCrazyDryWet)) * kPostGain;
        }
        else
        {
            out[0][i] = sig; // Bypass
        }
    }
}

int main(void)
{
    hw.Init();
    hw.SetAudioBlockSize(4); // number of samples handled per callback
    hw.SetAudioSampleRate(SaiHandle::Config::SampleRate::SAI_48KHZ);
    hw.SetLed(true);

    InitializeADC();
    InitializeCrazyEffects();
    InitializeChorus();
    overdrive.Init();
    overdrive.SetDrive(gOverdriveAmount);

    hw.StartAudio(AudioCallback);

    while(1)
    {
        bypassLED.Write(!bypass);
    }
}
