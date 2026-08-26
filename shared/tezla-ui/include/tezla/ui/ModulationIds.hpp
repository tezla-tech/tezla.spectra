#pragma once

// The parameter IDs the modulation UI expects, in one place.
//
// Both plugins use the same names, which is what lets the MOD strip and the
// assignment rings be written once. Defining them here rather than twice also
// removes the way that would otherwise break: one plugin's `lfo2Smooth` quietly
// becoming `lfo2Smoothing`, and a shared component silently finding nothing.
//
// **Every one of these is frozen forever**, exactly like any other parameter
// ID -- renaming one resets that control in every project that uses it. New
// ones are appended. See CLAUDE.md section 8.

#include <cstddef>

namespace tezla::ui::modIds
{

/// Eight modulation slots: what drives it, what it drives, and how much.
inline constexpr const char* source[] {
    "modSrc1", "modSrc2", "modSrc3", "modSrc4", "modSrc5", "modSrc6", "modSrc7", "modSrc8"
};
inline constexpr const char* destination[] {
    "modDst1", "modDst2", "modDst3", "modDst4", "modDst5", "modDst6", "modDst7", "modDst8"
};
inline constexpr const char* depth[] {
    "modDepth1", "modDepth2", "modDepth3", "modDepth4",
    "modDepth5", "modDepth6", "modDepth7", "modDepth8"
};

inline constexpr int numSlots = static_cast<int> (std::size (source));

/// Three LFOs, six controls each.
inline constexpr const char* lfoWave[]     { "lfo1Wave",   "lfo2Wave",   "lfo3Wave" };
inline constexpr const char* lfoRate[]     { "lfo1Rate",   "lfo2Rate",   "lfo3Rate" };
inline constexpr const char* lfoSync[]     { "lfo1Sync",   "lfo2Sync",   "lfo3Sync" };
inline constexpr const char* lfoDivision[] { "lfo1Div",    "lfo2Div",    "lfo3Div" };
inline constexpr const char* lfoPhase[]    { "lfo1Phase",  "lfo2Phase",  "lfo3Phase" };
inline constexpr const char* lfoSmooth[]   { "lfo1Smooth", "lfo2Smooth", "lfo3Smooth" };

inline constexpr int numLfos = static_cast<int> (std::size (lfoWave));

/// The level follower. Not an envelope generator: there is no trigger and no
/// MIDI anywhere, the audio drives it.
inline constexpr auto envAttack      = "envAttack";
inline constexpr auto envRelease     = "envRelease";
inline constexpr auto envSensitivity = "envSensitivity";

} // namespace tezla::ui::modIds
