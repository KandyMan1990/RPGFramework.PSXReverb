using System;
using UnityEditor;

namespace RPGFramework.PSXReverb.Editor
{
    // Unity matches this to the effect by Name, and draws its own sliders after it, which is where a parameter is
    // exposed to scripts.
    internal sealed class PSXReverbGUI : IAudioEffectPluginGUI
    {
        private const string PRESET        = "Preset";
        private const string STATUS        = "Status";
        private const int    STATUS_LENGTH = 7;
        private const int    ECHO          = 7;
        private const int    DELAY         = 8;

        private static readonly string[] PRESET_NAMES =
        {
            "Off", "Room", "Studio A", "Studio B", "Studio C", "Hall", "Space", "Echo", "Delay", "Pipe"
        };

        public override string Name        => "PSX Reverb";
        public override string Description => "The PlayStation's SPU reverb: its presets, rate and integer maths";
        public override string Vendor      => "RPG Framework";

        public override bool OnGUI(IAudioEffectPlugin plugin)
        {
            plugin.GetFloatParameter(PRESET, out float value);
            // Half away from zero, as the plugin rounds; Mathf.RoundToInt rounds half to even and would name 4.5 wrongly.
            int preset = Math.Clamp((int)Math.Round(value, MidpointRounding.AwayFromZero), 0, PRESET_NAMES.Length - 1);

            using (new EditorGUI.DisabledScope(!plugin.IsPluginEditableAndEnabled()))
            {
                int chosen = EditorGUILayout.Popup(PRESET, preset, PRESET_NAMES);
                if (chosen != preset)
                {
                    plugin.SetFloatParameter(PRESET, chosen);
                }
            }

            string unused = UnusedSettings(preset);
            if (unused != null)
            {
                EditorGUILayout.LabelField(unused, EditorStyles.miniLabel);
            }

            // The running effect's own report; there is none while the mixer is not running.
            if (plugin.GetFloatBuffer(STATUS, out float[] status, STATUS_LENGTH) && status[0] == 0f)
            {
                EditorGUILayout.HelpBox(SilenceReason(status[1], status[2]), MessageType.Warning);
            }

            return true;
        }

        private static string SilenceReason(float madeFor, float mixerRate)
        {
            string reason = madeFor == mixerRate
                                ? $"Silent: the reverb cannot run at {mixerRate:N0} Hz."
                                : $"Silent: made for {madeFor:N0} Hz, but the mixer now runs at {mixerRate:N0} Hz.";
            return reason;
        }

        private static string UnusedSettings(int preset)
        {
            string unused = preset == ECHO  ? null
                          : preset == DELAY ? "Feedback has no effect on delay."
                                            : "Delay and Feedback have no effect on this preset.";
            return unused;
        }
    }
}
