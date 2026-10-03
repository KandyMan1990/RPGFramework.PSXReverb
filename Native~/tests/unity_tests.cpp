// Plays Unity's part: loads the built plugin as Unity does, finds its effect and drives it through its callbacks,
// checking what comes out against the C interface run directly on the same input.
#include "check.h"
#include "psx_reverb.h"
#include "signal.h"

#include "AudioPluginInterface.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace
{
using GetDefinitions = int(AUDIO_CALLING_CONVENTION *)(UnityAudioEffectDefinition ***);

#ifdef _WIN32
HMODULE library;
#else
void *library;
#endif

UnityAudioEffectDefinition *effect;

bool open_library(const char *path)
{
#ifdef _WIN32
    library = LoadLibraryA(path);
#else
    library = dlopen(path, RTLD_NOW | RTLD_LOCAL);
#endif
    const bool opened = library != nullptr;
    return opened;
}

GetDefinitions find(const char *name)
{
#ifdef _WIN32
    const FARPROC address = GetProcAddress(library, name);
#else
    void *address = dlsym(library, name);
#endif
    GetDefinitions function;
    static_assert(sizeof(function) == sizeof(address), "a symbol's address must hold a function pointer");
    std::memcpy(&function, &address, sizeof(function));
    return function;
}

// An effect with Unity's state around it, as the mixer holds one.
struct Instance
{
    UnityAudioEffectState state;

    explicit Instance(uint32_t rate)
    {
        std::memset(&state, 0, sizeof(state));
        state.structsize = static_cast<UInt32>(sizeof(state));
        state.samplerate = rate;
        state.dspbuffersize = 1024;
        state.hostapiversion = UNITY_AUDIO_PLUGIN_API_VERSION;
        CHECK(effect->create(&state) == UNITY_AUDIODSP_OK);
    }

    ~Instance()
    {
        CHECK(effect->release(&state) == UNITY_AUDIODSP_OK);
    }

    Instance(const Instance &) = delete;
    Instance &operator=(const Instance &) = delete;

    void set(int index, float value)
    {
        CHECK(effect->setfloatparameter(&state, index, value) == UNITY_AUDIODSP_OK);
    }

    float get(int index)
    {
        float value = -1.0f;
        CHECK(effect->getfloatparameter(&state, index, &value, nullptr) == UNITY_AUDIODSP_OK);
        return value;
    }

    void process(float *input, float *output, size_t frames, int channels)
    {
        CHECK(effect->process(&state, input, output, static_cast<unsigned int>(frames), channels, channels) ==
              UNITY_AUDIODSP_OK);
    }
};

enum
{
    PRESET,
    DEPTH,
    DELAY,
    FEEDBACK
};

// Noise through the effect and the same noise through the interface, block by block. The interface hears the first two
// channels, or one channel on both sides; what the effect gives must be its answer, the two sides' average for one
// channel, and silence on any further channels, to the bit.
struct Stream
{
    uint32_t seed = 7;
    bool same = true;

    void run(Instance &instance, psx_reverb *expected, int channels, size_t frames, bool in_place = false)
    {
        const size_t width = static_cast<size_t>(channels);
        std::vector<float> input(frames * width), output(frames * width), stereo(frames * 2), answer(frames * 2);
        for (float &sample : input)
        {
            sample = noise(&seed);
        }
        for (size_t i = 0; i < frames; i++)
        {
            stereo[i * 2] = input[i * width];
            stereo[i * 2 + 1] = input[i * width + (channels == 1 ? 0 : 1)];
        }
        psx_reverb_process(expected, stereo.data(), answer.data(), frames);
        float *out = in_place ? input.data() : output.data();
        instance.process(input.data(), out, frames, channels);
        for (size_t i = 0; i < frames; i++)
        {
            for (size_t c = 0; c < width; c++)
            {
                const float want = channels == 1 ? (answer[i * 2] + answer[i * 2 + 1]) * 0.5f
                                   : c < 2       ? answer[i * 2 + c]
                                                 : 0.0f;
                same = same && out[i * width + c] == want;
            }
        }
    }

    // A second, in blocks cycling through the sizes given.
    void second(Instance &instance, psx_reverb *expected, uint32_t rate, int channels,
                const std::vector<size_t> &blocks, bool in_place = false)
    {
        size_t done = 0;
        for (size_t b = 0; done < rate; b++)
        {
            const size_t frames = blocks[b % blocks.size()];
            run(instance, expected, channels, frames, in_place);
            done += frames;
        }
    }
};

// A second of noise, long enough for any preset's first reflection to come back if the reverb were running.
bool silent(Instance &instance, int channels)
{
    uint32_t seed = 7;
    bool quiet = true;
    std::vector<float> input(1024 * static_cast<size_t>(channels)), output(input.size());
    for (int block = 0; block < 48; block++)
    {
        for (float &sample : input)
        {
            sample = noise(&seed);
        }
        std::fill(output.begin(), output.end(), 1.0f);
        instance.process(input.data(), output.data(), 1024, channels);
        for (float sample : output)
        {
            quiet = quiet && sample == 0.0f;
        }
    }
    return quiet;
}

void only_the_definitions_are_exported()
{
    CHECK(find("UnityGetAudioEffectDefinitions") != nullptr);
    CHECK(find("psx_reverb_create") == nullptr);
}

void it_defines_one_effect()
{
    UnityAudioEffectDefinition **definitions = nullptr;
    CHECK_EQ(1, find("UnityGetAudioEffectDefinitions")(&definitions));
    effect = definitions[0];
    CHECK(std::strcmp(effect->name, "PSX Reverb") == 0);
    CHECK_EQ(sizeof(UnityAudioEffectDefinition), effect->structsize);
    CHECK_EQ(sizeof(UnityAudioParameterDefinition), effect->paramstructsize);
    CHECK_EQ(UNITY_AUDIO_PLUGIN_API_VERSION, effect->apiversion);
    CHECK_EQ(psx_reverb_version(), effect->pluginversion);
    CHECK_EQ(0, effect->channels);
    CHECK_EQ(0, effect->flags);
    CHECK(effect->create && effect->release && effect->process && effect->setfloatparameter &&
          effect->getfloatparameter && effect->getfloatbuffer);
}

void its_parameters_are_the_consoles()
{
    struct
    {
        const char *name;
        float min, max, defaultval;
    } expected[] = {{"Preset", 0.0f, 9.0f, 4.0f},
                    {"Depth", 0.0f, 127.0f, 40.0f},
                    {"Delay", 1.0f, 127.0f, 127.0f},
                    {"Feedback", 0.0f, 127.0f, 127.0f}};
    CHECK_EQ(4, effect->numparameters);
    for (int i = 0; i < 4; i++)
    {
        const UnityAudioParameterDefinition &parameter = effect->paramdefs[i];
        CHECK(std::strcmp(parameter.name, expected[i].name) == 0);
        CHECK(parameter.min == expected[i].min && parameter.max == expected[i].max);
        CHECK(parameter.defaultval == expected[i].defaultval);
        CHECK(parameter.description != nullptr);
    }
}

void a_new_effect_reports_its_defaults()
{
    Instance instance(48000);
    for (int i = 0; i < 4; i++)
    {
        CHECK(instance.get(i) == effect->paramdefs[i].defaultval);
    }
    char text[64] = "untouched";
    float value = 0.0f;
    CHECK(effect->getfloatparameter(&instance.state, PRESET, &value, text) == UNITY_AUDIODSP_OK);
    CHECK(text[0] == '\0');
}

void parameters_it_does_not_have_are_refused()
{
    Instance instance(48000);
    float value = 0.0f;
    CHECK(effect->setfloatparameter(&instance.state, 4, 1.0f) == UNITY_AUDIODSP_ERR_UNSUPPORTED);
    CHECK(effect->setfloatparameter(&instance.state, -1, 1.0f) == UNITY_AUDIODSP_ERR_UNSUPPORTED);
    CHECK(effect->getfloatparameter(&instance.state, 4, &value, nullptr) == UNITY_AUDIODSP_ERR_UNSUPPORTED);
}

void stereo_is_the_interfaces_at_every_rate()
{
    for (uint32_t rate : {44100u, 48000u, 22050u, 96000u})
    {
        Instance instance(rate);
        psx_reverb *expected = psx_reverb_create(rate);
        Stream stream;
        stream.second(instance, expected, rate, 2, {512});
        CHECK(stream.same);
        psx_reverb_destroy(expected);
    }
}

void any_block_size_and_channel_count_gives_the_same()
{
    const std::vector<size_t> blocks = {1, 7, 255, 256, 257, 1000, 4096};
    for (int channels : {2, 1, 6})
    {
        for (bool in_place : {false, true})
        {
            Instance instance(48000);
            psx_reverb *expected = psx_reverb_create(48000);
            Stream stream;
            stream.second(instance, expected, 48000, channels, blocks, in_place);
            CHECK(stream.same);
            psx_reverb_destroy(expected);
        }
    }
}

// Settings arrive between blocks and hold from the next, as the interface's do from the next frame.
void settings_take_hold_at_the_next_block()
{
    Instance instance(48000);
    psx_reverb *expected = psx_reverb_create(48000);
    Stream stream;
    stream.second(instance, expected, 48000, 2, {480});
    instance.set(PRESET, 5);
    psx_reverb_set_preset(expected, PSX_REVERB_HALL);
    stream.second(instance, expected, 48000, 2, {480});
    instance.set(PRESET, 7);
    instance.set(DELAY, 64);
    instance.set(FEEDBACK, 90);
    instance.set(DEPTH, 100);
    psx_reverb_set_preset(expected, PSX_REVERB_ECHO);
    psx_reverb_set_delay(expected, 64);
    psx_reverb_set_feedback(expected, 90);
    psx_reverb_set_depth(expected, 100);
    stream.second(instance, expected, 48000, 6, {480});
    CHECK(stream.same);
    psx_reverb_destroy(expected);
}

// Unity may send a value again unchanged; the tail must ring on.
void the_same_preset_again_keeps_the_tail()
{
    Instance instance(44100);
    psx_reverb *expected = psx_reverb_create(44100);
    instance.set(PRESET, 8);
    psx_reverb_set_preset(expected, PSX_REVERB_DELAY);
    Stream stream;
    stream.second(instance, expected, 44100, 2, {441});
    instance.set(PRESET, 8);
    stream.second(instance, expected, 44100, 2, {441});
    CHECK(stream.same);
    psx_reverb_destroy(expected);
}

void values_round_to_whole_and_hold_to_their_range()
{
    Instance instance(48000);
    psx_reverb *expected = psx_reverb_create(48000);
    instance.set(PRESET, 8.6f);
    instance.set(DEPTH, 300.0f);
    instance.set(DELAY, -4.0f);
    instance.set(FEEDBACK, 63.5f);
    CHECK(instance.get(PRESET) == 8.6f);
    CHECK(instance.get(DEPTH) == 127.0f);
    CHECK(instance.get(DELAY) == 1.0f);
    psx_reverb_set_preset(expected, PSX_REVERB_PIPE);
    psx_reverb_set_depth(expected, 127);
    psx_reverb_set_delay(expected, 1);
    psx_reverb_set_feedback(expected, 64);
    Stream stream;
    stream.second(instance, expected, 48000, 2, {512});
    CHECK(stream.same);
    psx_reverb_destroy(expected);
}

void a_rate_it_cannot_run_at_is_silent()
{
    Instance instance(4000);
    CHECK(silent(instance, 2));
    CHECK(silent(instance, 6));
}

void a_rate_changed_under_it_is_silent()
{
    Instance instance(48000);
    instance.state.samplerate = 44100;
    CHECK(silent(instance, 2));
}

// What the editor reads to say whether the effect is running, and what it holds.
void its_status_says_whether_it_runs_and_what_it_holds()
{
    float status[7] = {};
    Instance running(48000);
    running.set(PRESET, 8.0f);
    running.set(DEPTH, 300.0f);
    CHECK(effect->getfloatbuffer(&running.state, "Status", status, 7) == UNITY_AUDIODSP_OK);
    CHECK(status[0] == 1.0f && status[1] == 48000.0f && status[2] == 48000.0f);
    CHECK(status[3] == 8.0f && status[4] == 127.0f && status[5] == 127.0f && status[6] == 127.0f);

    running.state.samplerate = 44100;
    CHECK(effect->getfloatbuffer(&running.state, "Status", status, 7) == UNITY_AUDIODSP_OK);
    CHECK(status[0] == 0.0f && status[1] == 48000.0f && status[2] == 44100.0f);

    Instance unreachable(4000);
    CHECK(effect->getfloatbuffer(&unreachable.state, "Status", status, 7) == UNITY_AUDIODSP_OK);
    CHECK(status[0] == 0.0f);

    CHECK(effect->getfloatbuffer(&running.state, "Status", status, 6) == UNITY_AUDIODSP_ERR_UNSUPPORTED);
    CHECK(effect->getfloatbuffer(&running.state, "Spectrum", status, 7) == UNITY_AUDIODSP_ERR_UNSUPPORTED);
}

void a_rate_of_0_is_silent()
{
    Instance instance(0);
    instance.state.samplerate = 48000;
    CHECK(silent(instance, 2));
}
} // namespace

int main(int argc, char **argv)
{
    if (argc != 2)
    {
        std::fprintf(stderr, "usage: psxreverb_unity_tests PLUGIN\n");
        return 2;
    }
    if (!open_library(argv[1]))
    {
        std::fprintf(stderr, "could not load %s\n", argv[1]);
        return 1;
    }

    only_the_definitions_are_exported();
    it_defines_one_effect();
    its_parameters_are_the_consoles();
    a_new_effect_reports_its_defaults();
    parameters_it_does_not_have_are_refused();
    stereo_is_the_interfaces_at_every_rate();
    any_block_size_and_channel_count_gives_the_same();
    settings_take_hold_at_the_next_block();
    the_same_preset_again_keeps_the_tail();
    values_round_to_whole_and_hold_to_their_range();
    a_rate_it_cannot_run_at_is_silent();
    a_rate_changed_under_it_is_silent();
    a_rate_of_0_is_silent();
    its_status_says_whether_it_runs_and_what_it_holds();

    const int result = check_failures() == 0 ? 0 : 1;
    std::printf("%s\n", result == 0 ? "All checks passed" : "Checks failed");
    return result;
}
