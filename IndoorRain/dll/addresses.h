/* Client addresses for WoW 1.12.1 build 5875 (the TWMOA 1.18.1 exe keeps them).
 * Read from the RavenCraft WoW.exe with objdump on 26 Sep 2026 (see the scout pages).
 * Every read goes through a VirtualQuery guard, and the one client function the DLL
 * calls is byte-compared first, so on a different exe the DLL logs and stays silent. */
#ifndef INDOORRAIN_ADDRESSES_H
#define INDOORRAIN_ADDRESSES_H

/* weather */
#ifndef ADDR_CMAPWEATHER_PTR
#define ADDR_CMAPWEATHER_PTR   0x00C6326Cu  /* DWORD: CMapWeather*, allocated at world init          */
#endif
#define OFF_WEATHER_INTENSITY  0x00u        /* float: target intensity 0..1                          */
#define OFF_WEATHER_TYPE       0x20u        /* int: -1 none in game, 1 rain, 2 snow, 3 sand; others = none */
#define OFF_WEATHER_RAIN_FX    0x28u        /* DWORD: rain effect object, non-null while rain renders */
#ifndef ADDR_WEATHER_SOUND_ID
#define ADDR_WEATHER_SOUND_ID  0x00B05FBCu  /* DWORD: last server weather sound id, 0 when off        */
#endif
#ifndef ADDR_WEATHER_GATE
#define ADDR_WEATHER_GATE      0x00C7B584u  /* BYTE: 1 after a forced /weather; normally 0            */
#endif
#define SOUND_RAIN_LIGHT   8533u
#define SOUND_RAIN_MEDIUM  8534u
#define SOUND_RAIN_HEAVY   8535u

/* in world: the object manager and the active player's GUID */
#ifndef ADDR_OBJMGR_PTR
#define ADDR_OBJMGR_PTR        0x00B41414u  /* DWORD: object manager, 0 outside the world             */
#endif
#define OFF_OBJMGR_PLAYER_GUID 0xC0u        /* DWORD: active player GUID low, 0 outside the world      */

/* The building you stand in (0.13 probe), read the way the client's own zone-text update does it
 * (0x005DB900 -> 0x0067E510 -> 0x0069D4F0 / 0x0069D980, read from the exe on 28 Sep 2026):
 * the active player's map entity keeps a list of the map-object groups it is in; the one flagged
 * 0x10 leads to the placed building (CMapObjDef) and its model (CMapObj), whose header holds the
 * model id WMOAreaTable.dbc is keyed by, and whose path is the model's file name. */
#define OFF_OBJMGR_FIRST_OBJ   0xACu        /* DWORD: first object in the object manager's list        */
#define OFF_OBJMGR_NEXT_LINK   0xA4u        /* DWORD: offset of the list link inside each object       */
#define OFF_OBJ_GUID           0x30u        /* 2 DWORDs: object GUID                                   */
#define OFF_UNIT_MAP_ENTITY    0xE0u        /* DWORD: the unit's map entity                            */
#define OFF_ENTITY_FLAGS       0x90u        /* DWORD: bit 0 = the entity knows what it stands in       */
#define OFF_ENTITY_LINKOFF     0x18u        /* DWORD: offset of the next pointer in each list link     */
#define OFF_ENTITY_LINKS       0x20u        /* DWORD: first link (low bit set = end of list)           */
#define OFF_LINK_OBJ           0x08u        /* DWORD: what the link points to                          */
#define OFF_GROUPDEF_FLAGS     0x08u        /* DWORD: 0x10 = a building group that contains you        */
#define OFF_GROUPDEF_ROOTLINK  0x20u        /* DWORD: link to the placed building                      */
#define OFF_GROUPDEF_INDEX     0x7Cu        /* DWORD: group index within the model                     */
#define OFF_MAPOBJDEF_MODEL    0x118u       /* DWORD: CMapObj, the model                               */
#define OFF_MAPOBJDEF_NAMESET  0x128u       /* DWORD: name set of this placement                       */
#define OFF_MAPOBJ_PATH        0x1Cu        /* char[]: the model's file name                           */
#define OFF_MAPOBJ_HEADER      0x120u       /* DWORD: the model's MOHD header                          */
#define OFF_MOHD_WMOID         0x20u        /* DWORD: model id                                         */
#define OFF_MAPOBJ_GROUPS      0x1F4u       /* DWORD[]: loaded groups by index                         */
#define OFF_GROUP_ID           0x14Cu       /* DWORD: WMOGroupID                                       */

/* CVar bridge: the addon registers IndoorRain_* CVars on the main thread; the DLL only looks
 * them up once (after the world is up) and then reads the value string at CVar+0x20. */
#define ADDR_CVAR_LOOKUP       0x0063DEC0u  /* CVar* __fastcall Lookup(const char* name)              */
#define ADDR_CVAR_READY        0x00C4EDB8u  /* DWORD hash mask, 0xFFFFFFFF until the table exists      */
#define OFF_CVAR_VALUE         0x20u        /* char*: current value string                             */
#define CVAR_LOOKUP_HEAD_LEN   12
#define CVAR_LOOKUP_HEAD_BYTES { 0x83, 0x3d, 0xb8, 0xed, 0xc4, 0x00, 0xff, 0x53, 0x56, 0x57, 0x8b, 0xf9 }

#define CVAR_ENABLED  "IndoorRain_Enabled"
#define CVAR_VOLUME   "IndoorRain_Volume"
#define CVAR_INDOORS  "IndoorRain_Indoors"
#define CVAR_MASTER   "MasterVolume"     /* the client's own sliders, 0..1: the outdoor weather */
#define CVAR_AMBIENCE "AmbienceVolume"   /* sound plays at its SoundEntries volume times this   */
#define CVAR_ENABLE_AMBIENCE "EnableAmbience"  /* "Ambient Sounds" box: 0 silences the weather outside */
#define CVAR_STORM    "IndoorRain_Storm"    /* DLL -> addon, written in place: "IRS1:k0:l0:n000:d00"      */
#define CVAR_STORMS   "IndoorRain_Storms"   /* addon -> DLL: 1 thunder and gusts while it rains, 0 none  */
#define CVAR_SESSION  "IndoorRain_Session"  /* addon -> DLL: a code 10..99, echoed in IndoorRain_Storm  */
#define CVAR_THUNDER  "IndoorRain_Thunder"  /* addon -> DLL: a counter; each change is a test strike    */

/* Harness only: instead of calling Lookup, read three char* from a fake table. */
#ifndef HARNESS_CVARS
#define HARNESS_CVARS 0u
#endif

#endif
