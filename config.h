#define PLUG_NAME "FirstSynth"
#define PLUG_MFR "EASYANDNICE"
#define PLUG_VERSION_HEX 0x00010000
#define PLUG_VERSION_STR "1.0.0"
#define PLUG_UNIQUE_ID 'hnve'
#define PLUG_MFR_ID 'EZAN'
#define PLUG_URL_STR "https://easyandnicewaki.com/"
#define PLUG_EMAIL_STR "easyandnice@ymail.ne.jp"
#define PLUG_COPYRIGHT_STR "Copyright 2025 EASYANDNICE"
#define PLUG_CLASS_NAME FirstSynth

#define BUNDLE_NAME "FirstSynth"
#define BUNDLE_MFR "EASYANDNICE"
#define BUNDLE_DOMAIN "com"

#define SHARED_RESOURCES_SUBPATH "FirstSynth"

#define PLUG_CHANNEL_IO "0-2"

#define PLUG_LATENCY 0
#define PLUG_TYPE 1
#define PLUG_DOES_MIDI_IN 1
#define PLUG_DOES_MIDI_OUT 1
#define PLUG_DOES_MPE 1
#define PLUG_DOES_STATE_CHUNKS 0
#define PLUG_HAS_UI 1
// The Matrix page's 2-column layout made the previous 1352x694 too small (caused
// scroll arrows to appear). Earlier GetClientRect-based measurements (1395x750,
// 1402x780) were taken while AlignMeterToContentEdge() had a real CSS bug
// (writing marginRight onto .meter-container, nested inside the auto-sized
// .header-right-group, inflated that ancestor's own intrinsic width and forced a
// scrollbar no matter the window size - see progress.md's "Horizontal
// scrollbar..." entries for the full trail). That bug was fixed 2026-07-29 by
// moving the margin to .header-right-group itself. This value (1387x780) is a
// fresh GetClientRect measurement taken AFTER that fix, with the user manually
// narrowing the window until the scrollbar and the excess right-side empty
// space both vanished. No artificial safety buffer added this time - if a
// scrollbar reappears, it means real content changed, not that this number was
// too tight.
#define PLUG_WIDTH 1387
#define PLUG_HEIGHT 780
#define PLUG_FPS 60
#define PLUG_SHARED_RESOURCES 0
// Tried disabling this 2026-07-28 to fix Renoise/BespokeSynth opening the editor
// too small (see the long comment that used to be here) - didn't fix the cropping
// (both hosts still opened cropped) AND it cost the one workaround that did exist:
// with this off, canResize() reports false, and Renoise's own window resize-grip
// stopped working too, leaving the user stuck at the wrong size with no way to
// drag it bigger anymore. Reverted same session - net regression, not a fix. The
// actual root cause (these hosts opening the editor at some default size instead
// of respecting getSize()'s declared PLUG_WIDTH/PLUG_HEIGHT) is still unsolved -
// see progress.md's "PLUG_HOST_RESIZE experiment" entry before trying this again.
#define PLUG_HOST_RESIZE 1

#define AUV2_ENTRY FirstSynth_Entry
#define AUV2_ENTRY_STR "FirstSynth_Entry"
#define AUV2_FACTORY FirstSynth_Factory
#define AUV2_VIEW_CLASS FirstSynth_View
#define AUV2_VIEW_CLASS_STR "FirstSynth_View"

#define AAX_TYPE_IDS 'IPS1', 'IPS2'
#define AAX_PLUG_MFR_STR "EASYANDNICE"
#define AAX_PLUG_NAME_STR "FirstSynth\nIPIS"
#define AAX_DOES_AUDIOSUITE 0
#define AAX_PLUG_CATEGORY_STR "Synth"

#define VST3_SUBCATEGORY "Instrument|Synth"
#define CLAP_FEATURES "instrument"
#define CLAP_DESCRIPTION "FirstSynth - a simple polyphonic synth"

#define APP_NUM_CHANNELS 2
#define APP_N_VECTOR_WAIT 0
#define APP_MULT 1
#define APP_COPY_AUV3 0
#define APP_SIGNAL_VECTOR_SIZE 64
