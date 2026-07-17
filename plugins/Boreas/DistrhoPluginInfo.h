#ifndef DISTRHO_PLUGIN_INFO_H_INCLUDED
#define DISTRHO_PLUGIN_INFO_H_INCLUDED

#ifdef BOREAS_BETA
#define DISTRHO_PLUGIN_BRAND   "Stefan"
#define DISTRHO_PLUGIN_NAME    "Boreas (Beta)"
#define DISTRHO_PLUGIN_URI     "http://boreas.local/plugins/boreas-beta"
#define DISTRHO_PLUGIN_CLAP_ID "local.boreas.boreas-beta"
#define DISTRHO_PLUGIN_BRAND_ID  BorB
#define DISTRHO_PLUGIN_UNIQUE_ID dBoB
#else
#define DISTRHO_PLUGIN_BRAND   "Stefan"
#define DISTRHO_PLUGIN_NAME    "Boreas"
#define DISTRHO_PLUGIN_URI     "http://boreas.local/plugins/boreas"
#define DISTRHO_PLUGIN_CLAP_ID "local.boreas.boreas"
#define DISTRHO_PLUGIN_BRAND_ID  Bore
#define DISTRHO_PLUGIN_UNIQUE_ID dBor
#endif

// LV2 plugin class -> the "Category" shown in MOD's plugin info/store
// (mod-ui maps lv2:SpectralPlugin to its "Spectral" category).
#define DISTRHO_PLUGIN_LV2_CATEGORY   "lv2:SpectralPlugin"

#define DISTRHO_PLUGIN_HAS_UI         0
#define DISTRHO_PLUGIN_IS_RT_SAFE     1
#define DISTRHO_PLUGIN_NUM_INPUTS     1
#define DISTRHO_PLUGIN_NUM_OUTPUTS    1

#define DISTRHO_PLUGIN_WANT_PROGRAMS                          0
#define DISTRHO_PLUGIN_WANT_STATE                             0
#define DISTRHO_PLUGIN_WANT_PARAMETER_VALUE_CHANGE_REQUEST    1

#endif // DISTRHO_PLUGIN_INFO_H_INCLUDED
