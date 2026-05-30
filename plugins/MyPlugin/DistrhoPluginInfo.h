/*
 * MyPlugin — DPF plugin info header.
 *
 * Single source of truth for the plugin's LV2 identity. Change every
 * `myplugin` string here AND the matching values in the top-level
 * Makefile (PLUGIN, BRAND, LABEL, PLUGIN_URI_BASE) when forking.
 *
 * BETA build: pass -DMYPLUGIN_BETA on the command line (top-level
 * Makefile does this when BETA=1) to get a side-by-side build with a
 * distinct LV2 URI, bundle name, label, and DPF unique-id — installs
 * alongside the stable plugin for A/B testing.
 */

#ifndef DISTRHO_PLUGIN_INFO_H_INCLUDED
#define DISTRHO_PLUGIN_INFO_H_INCLUDED

#ifdef MYPLUGIN_BETA
#define DISTRHO_PLUGIN_BRAND   "myplugin-beta"
#define DISTRHO_PLUGIN_NAME    "My Plugin (Beta)"
#define DISTRHO_PLUGIN_URI     "http://myplugin.local/plugins/myplugin-beta"
#define DISTRHO_PLUGIN_CLAP_ID "local.myplugin.myplugin-beta"
// 4-char identifiers — must be unique within your set of plugins. DPF
// packs them into 32-bit ints (d_cconst). The beta gets distinct codes
// so DAWs / hosts don't confuse it with the stable plugin's state.
#define DISTRHO_PLUGIN_BRAND_ID  MyPB
#define DISTRHO_PLUGIN_UNIQUE_ID dMyB
#else
#define DISTRHO_PLUGIN_BRAND   "myplugin"
#define DISTRHO_PLUGIN_NAME    "My Plugin"
#define DISTRHO_PLUGIN_URI     "http://myplugin.local/plugins/myplugin"
#define DISTRHO_PLUGIN_CLAP_ID "local.myplugin.myplugin"
#define DISTRHO_PLUGIN_BRAND_ID  MyPl
#define DISTRHO_PLUGIN_UNIQUE_ID dMyP
#endif

// Feature flags. Adjust as your plugin grows.
#define DISTRHO_PLUGIN_HAS_UI         0    // 0 = no native UI; modgui supplies the GUI on MOD
#define DISTRHO_PLUGIN_IS_RT_SAFE     1    // 1 = no allocs / locks / I/O in run()
#define DISTRHO_PLUGIN_NUM_INPUTS     1    // mono input bus
#define DISTRHO_PLUGIN_NUM_OUTPUTS    2    // stereo output bus

#define DISTRHO_PLUGIN_WANT_PROGRAMS                          0
#define DISTRHO_PLUGIN_WANT_STATE                             0
// Enables requestParameterValueChange() — used when the DSP needs to
// echo parameter changes back to the host (e.g. for derived ports
// recomputed from other ports).
#define DISTRHO_PLUGIN_WANT_PARAMETER_VALUE_CHANGE_REQUEST    1

#endif // DISTRHO_PLUGIN_INFO_H_INCLUDED
