using System;
using System.Linq;
using System.Reflection;
using UnityEditor;
using UnityEngine;

namespace RPGFramework.PSXReverb.Editor
{
    // Unity lists the mixer's effects when scripts load, which on a fresh start is before it loads this plugin, and
    // lists them again only when scripts reload or the project changes; until then PSX Reverb is missing from Add Effect
    // and its sliders from the Inspector. So once the plugin has loaded, this has Unity list them again. The list is
    // internal to Unity, hence reflection.
    [InitializeOnLoad]
    internal static class EffectListRefresher
    {
        private const string EFFECT_NAME          = "PSX Reverb";
        private const double GIVE_UP_AFTER_SECONDS = 30.0;

        private static readonly MethodInfo s_Refresh;
        private static readonly MethodInfo s_ListedEffects;
        private static readonly MethodInfo s_LoadedEffects;

        private static double s_StartTime;

        static EffectListRefresher()
        {
            const BindingFlags STATIC = BindingFlags.Static | BindingFlags.Public | BindingFlags.NonPublic;
            Type definitions = typeof(AudioImporter).Assembly.GetType("UnityEditor.Audio.MixerEffectDefinitions");
            s_Refresh       = definitions?.GetMethod("Refresh", STATIC);
            s_ListedEffects = definitions?.GetMethod("GetEffectList", STATIC);
            s_LoadedEffects = definitions?.GetMethod("GetAudioEffectNames", STATIC);

            if (s_Refresh == null || s_ListedEffects == null || s_LoadedEffects == null)
            {
                Debug.LogWarning("PSX Reverb: this version of Unity's mixer effect list is not the one expected, so PSX Reverb may be missing from Add Effect until scripts next reload.");
                return;
            }

            s_StartTime = EditorApplication.timeSinceStartup;
            EditorApplication.update += WaitForPlugin;
        }

        private static void WaitForPlugin()
        {
            bool loaded = ((string[])s_LoadedEffects.Invoke(null, null)).Contains(EFFECT_NAME);
            if (loaded && !((string[])s_ListedEffects.Invoke(null, null)).Contains(EFFECT_NAME))
            {
                s_Refresh.Invoke(null, null);
            }

            if (loaded || EditorApplication.timeSinceStartup - s_StartTime > GIVE_UP_AFTER_SECONDS)
            {
                EditorApplication.update -= WaitForPlugin;
            }
        }
    }
}
