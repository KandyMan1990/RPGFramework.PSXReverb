#include "psx_reverb.h"

#include "AudioPluginInterface.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <new>

namespace
{
enum Parameter
{
    PARAMETER_PRESET,
    PARAMETER_DEPTH,
    PARAMETER_DELAY,
    PARAMETER_FEEDBACK,
    PARAMETER_COUNT
};

// Defaults are the ones psx_reverb_create starts with.
UnityAudioParameterDefinition parameters[PARAMETER_COUNT] = {
    {"Preset", "", "0 off, 1 room, 2-4 studio A-C, 5 hall, 6 space, 7 echo, 8 delay, 9 pipe. A change cuts the tail.",
     0.0f, 9.0f, 4.0f, 1.0f, 1.0f},
    {"Depth", "", "The reverb's output volume, 0-127 as the console's tools give it.", 0.0f, 127.0f, 40.0f, 1.0f, 1.0f},
    {"Delay", "", "Echo and delay's delay time.", 1.0f, 127.0f, 127.0f, 1.0f, 1.0f},
    {"Feedback", "", "Echo's feedback.", 0.0f, 127.0f, 127.0f, 1.0f, 1.0f},
};

const size_t CHUNK_FRAMES = 256;
const int STATUS_LENGTH = 3 + PARAMETER_COUNT;

// Unity sets parameters on one thread and processes on another, so a setting waits in requested for the next block.
struct Effect
{
    psx_reverb *reverb;
    uint32_t sample_rate;
    std::atomic<float> requested[PARAMETER_COUNT];
    int applied[PARAMETER_COUNT];
    float stereo[CHUNK_FRAMES * 2];
};

static_assert(std::atomic<float>::is_always_lock_free, "a parameter must be set without a lock");

int whole(float value)
{
    const int rounded = static_cast<int>(std::lround(value));
    return rounded;
}

void apply_requested(Effect *effect)
{
    for (int i = 0; i < PARAMETER_COUNT; i++)
    {
        const int value = whole(effect->requested[i].load(std::memory_order_relaxed));
        if (value == effect->applied[i])
        {
            continue;
        }
        effect->applied[i] = value;
        switch (i)
        {
        case PARAMETER_PRESET:
            psx_reverb_set_preset(effect->reverb, value);
            break;
        case PARAMETER_DEPTH:
            psx_reverb_set_depth(effect->reverb, value);
            break;
        case PARAMETER_DELAY:
            psx_reverb_set_delay(effect->reverb, value);
            break;
        default:
            psx_reverb_set_feedback(effect->reverb, value);
            break;
        }
    }
}

// The first two channels feed the reverb; one channel feeds both sides.
void gather(float *stereo, const float *input, size_t frames, size_t channels)
{
    const size_t right = channels == 1 ? 0 : 1;
    for (size_t i = 0; i < frames; i++)
    {
        stereo[i * 2] = input[i * channels];
        stereo[i * 2 + 1] = input[i * channels + right];
    }
}

// The reverb's answer goes to the first two channels and any others are silent; one channel hears both sides' average.
void scatter(const float *stereo, float *output, size_t frames, size_t channels)
{
    for (size_t i = 0; i < frames; i++)
    {
        float *frame = output + i * channels;
        if (channels == 1)
        {
            frame[0] = (stereo[i * 2] + stereo[i * 2 + 1]) * 0.5f;
            continue;
        }
        frame[0] = stereo[i * 2];
        frame[1] = stereo[i * 2 + 1];
        std::fill(frame + 2, frame + channels, 0.0f);
    }
}

UNITY_AUDIODSP_RESULT UNITY_AUDIODSP_CALLBACK create(UnityAudioEffectState *state)
{
    Effect *effect = new (std::nothrow) Effect();
    if (!effect)
    {
        return UNITY_AUDIODSP_ERR_UNSUPPORTED;
    }
    effect->reverb = psx_reverb_create(state->samplerate);
    effect->sample_rate = state->samplerate;
    for (int i = 0; i < PARAMETER_COUNT; i++)
    {
        effect->requested[i].store(parameters[i].defaultval, std::memory_order_relaxed);
        effect->applied[i] = whole(parameters[i].defaultval);
    }
    state->effectdata = effect;
    return UNITY_AUDIODSP_OK;
}

UNITY_AUDIODSP_RESULT UNITY_AUDIODSP_CALLBACK release(UnityAudioEffectState *state)
{
    Effect *effect = static_cast<Effect *>(state->effectdata);
    psx_reverb_destroy(effect->reverb);
    delete effect;
    return UNITY_AUDIODSP_OK;
}

// A rate the reverb was not made for, or cannot run at, plays silence: making it again allocates, which the audio
// thread must not.
UNITY_AUDIODSP_RESULT UNITY_AUDIODSP_CALLBACK process(UnityAudioEffectState *state, float *input, float *output,
                                                      unsigned int length, int inchannels, int outchannels)
{
    Effect *effect = static_cast<Effect *>(state->effectdata);
    const size_t frames = length;
    const size_t in_channels = static_cast<size_t>(inchannels);
    const size_t out_channels = static_cast<size_t>(outchannels);
    if (!effect->reverb || state->samplerate != effect->sample_rate)
    {
        std::fill(output, output + frames * out_channels, 0.0f);
        return UNITY_AUDIODSP_OK;
    }
    apply_requested(effect);
    if (in_channels == 2 && out_channels == 2)
    {
        psx_reverb_process(effect->reverb, input, output, frames);
        return UNITY_AUDIODSP_OK;
    }
    for (size_t first = 0; first < frames; first += CHUNK_FRAMES)
    {
        const size_t count = std::min(CHUNK_FRAMES, frames - first);
        gather(effect->stereo, input + first * in_channels, count, in_channels);
        psx_reverb_process(effect->reverb, effect->stereo, effect->stereo, count);
        scatter(effect->stereo, output + first * out_channels, count, out_channels);
    }
    return UNITY_AUDIODSP_OK;
}

UNITY_AUDIODSP_RESULT UNITY_AUDIODSP_CALLBACK set_float_parameter(UnityAudioEffectState *state, int index, float value)
{
    if (index < 0 || index >= PARAMETER_COUNT)
    {
        return UNITY_AUDIODSP_ERR_UNSUPPORTED;
    }
    Effect *effect = static_cast<Effect *>(state->effectdata);
    const float held = std::clamp(value, parameters[index].min, parameters[index].max);
    effect->requested[index].store(held, std::memory_order_relaxed);
    return UNITY_AUDIODSP_OK;
}

// The text is left empty: the SDK does not say how long Unity's buffer is.
UNITY_AUDIODSP_RESULT UNITY_AUDIODSP_CALLBACK get_float_parameter(UnityAudioEffectState *state, int index, float *value,
                                                                  char *valuestr)
{
    if (index < 0 || index >= PARAMETER_COUNT)
    {
        return UNITY_AUDIODSP_ERR_UNSUPPORTED;
    }
    const Effect *effect = static_cast<const Effect *>(state->effectdata);
    if (value)
    {
        *value = effect->requested[index].load(std::memory_order_relaxed);
    }
    if (valuestr)
    {
        valuestr[0] = '\0';
    }
    return UNITY_AUDIODSP_OK;
}

// "Status", for the editor: 1 if the reverb is running or 0 if it plays silence, the rate it was made for, the rate the
// mixer runs at, then each parameter as the plugin holds it.
UNITY_AUDIODSP_RESULT UNITY_AUDIODSP_CALLBACK get_float_buffer(UnityAudioEffectState *state, const char *name,
                                                               float *buffer, int numsamples)
{
    if (std::strcmp(name, "Status") != 0 || numsamples < STATUS_LENGTH)
    {
        return UNITY_AUDIODSP_ERR_UNSUPPORTED;
    }
    const Effect *effect = static_cast<const Effect *>(state->effectdata);
    const bool running = effect->reverb && state->samplerate == effect->sample_rate;
    buffer[0] = running ? 1.0f : 0.0f;
    buffer[1] = static_cast<float>(effect->sample_rate);
    buffer[2] = static_cast<float>(state->samplerate);
    for (int i = 0; i < PARAMETER_COUNT; i++)
    {
        buffer[3 + i] = effect->requested[i].load(std::memory_order_relaxed);
    }
    return UNITY_AUDIODSP_OK;
}

UnityAudioEffectDefinition make_definition()
{
    UnityAudioEffectDefinition definition = {};
    definition.structsize = static_cast<UInt32>(sizeof(UnityAudioEffectDefinition));
    definition.paramstructsize = static_cast<UInt32>(sizeof(UnityAudioParameterDefinition));
    definition.apiversion = UNITY_AUDIO_PLUGIN_API_VERSION;
    definition.pluginversion = psx_reverb_version();
    definition.numparameters = PARAMETER_COUNT;
    std::memcpy(definition.name, "PSX Reverb", sizeof("PSX Reverb"));
    definition.create = create;
    definition.release = release;
    definition.process = process;
    definition.paramdefs = parameters;
    definition.setfloatparameter = set_float_parameter;
    definition.getfloatparameter = get_float_parameter;
    definition.getfloatbuffer = get_float_buffer;
    return definition;
}
} // namespace

// Unity finds a library's effects by Unity's name. iOS links every plugin into the app itself, where two functions of
// one name collide, so there the function has a name of its own, which the registration in unity/ios hands to Unity.
#ifdef PSX_REVERB_STATIC_PLUGIN
#define ENTRY_POINT psx_reverb_get_audio_effect_definitions
extern "C" UNITY_AUDIODSP_EXPORT_API int AUDIO_CALLING_CONVENTION ENTRY_POINT(UnityAudioEffectDefinition ***definitions);
#else
#define ENTRY_POINT UnityGetAudioEffectDefinitions
#endif

extern "C" UNITY_AUDIODSP_EXPORT_API int AUDIO_CALLING_CONVENTION ENTRY_POINT(UnityAudioEffectDefinition ***definitions)
{
    static UnityAudioEffectDefinition definition = make_definition();
    static UnityAudioEffectDefinition *list[] = {&definition};
    *definitions = list;
    return 1;
}
