"""Every zone's intent, and the preset that goes for it. The source of AzerothAtmosphere's Zones.lua.

Run build_zones.py to regenerate the Lua. Edit HERE, never the generated file.

SOURCES, per zone, in `src`:
  measured  Forever footage sampled frame by frame on 29-30 Sep 2026 (Jansn Benchmarks' zone videos, and
            the 21 Sep WOIOY comparison for Duskwood, Stranglethorn, Elwynn, Stormwind). The tint is the
            measured colour of the horizon band; the darkening comes from how dark Forever renders it.
  known     a summary of how the zone is known to look, from web sources, 30 Sep 2026.
  inferred  my own reading of the zone, where neither was available. Worth your eye first.

THE CONTROLS (comfyatmosphere's own CVars, plus our wash):
  fog      comfyFogThickness 0-100. 0 is the client's own fog; 60 is comfy's default.
  rays     comfyRaysStrength 0-100 (a curve: most of the change is near the top).
  vol      comfyVolumeStrength 0-100. Volumetric light did not render on the client it was tested on (24 Sep 2026);
           authored anyway so it is ready when a comfy release fixes it.
  night    comfyNightStrength 0-100: rays and light by moonlight as % of their day strength.
  clouds   comfyClouds 1/0: 0 hides the cloud layer (hard, cloudless desert skies).
  tint     the colour the zone leans toward; `k` how far (0-0.4); `dark` how much darker (0-0.25).
           Becomes a multiply wash over the 3D world: wash = (1 - k * (1 - tint / max(tint))) * (1 - dark).
A control left out is not touched: the player's own value stands in that zone.
"""

import math

Z = {}
def zone(name, mood, src, fog=None, rays=None, vol=None, night=None, clouds=None, tint=None, k=0.0, dark=0.0,
         wash=None):
    Z[name] = dict(mood=mood, src=src, fog=fog, rays=rays, vol=vol, night=night, clouds=clouds, tint=tint, k=k,
                   dark=dark, wash=wash)

# ---------------------------------------------------------------- Eastern Kingdoms
zone("Elwynn Forest", "Sunlit, golden pastoral forest: warm light, a soft morning haze, shafts through the elms.",
     "measured", fog=30, rays=50, vol=25, night=50, tint=(255, 220, 160), k=0.12)
zone("Westfall", "Golden autumn farmland under a big sky: warm dusty haze, bright and open.",
     "measured", fog=30, rays=40, vol=20, tint=(240, 205, 140), k=0.15)
zone("Redridge Mountains", "Crisp autumn highlands around Lake Everstill: clear air, warm reds and golds.",
     "known", fog=25, rays=40, vol=20, tint=(240, 190, 150), k=0.12)
zone("Duskwood", "Dark, moody, haunted forest: thick low fog, teal gloom, pale moonlight.",
     "measured", fog=90, rays=15, vol=40, night=10, wash=(0.80, 0.90, 0.95))   # night 35 lit shafts of moonlight
                                                                            # rising out of the ground (30 Sep 2026)
zone("Stranglethorn Vale", "Steamy jungle: humid cream haze, deep shade under the canopy, sun shafts through leaves.",
     "measured", fog=55, rays=55, vol=45, tint=(215, 210, 160), k=0.20, dark=0.05)
zone("Swamp of Sorrows", "Oppressive, sickly bog: low green mist, dim light, stagnant air.",
     "known", fog=80, rays=10, vol=40, night=25, tint=(120, 150, 100), k=0.30, dark=0.15)
zone("Blasted Lands", "Scorched red wasteland under the Dark Portal: dusty red haze, harsh light.",
     "known", fog=50, rays=30, vol=30, tint=(200, 110, 80), k=0.25, dark=0.08)
zone("Deadwind Pass", "A dead grey canyon under a shrouded sky: cold, colourless dread.",
     "known", fog=70, rays=10, vol=35, night=20, tint=(150, 150, 160), k=0.15, dark=0.15)
zone("Dun Morogh", "Frozen dusk frontier: blue-white snow, a peach sky, soft blowing-snow haze.",
     "measured", fog=50, rays=35, vol=30, night=60, tint=(200, 215, 240), k=0.12, dark=0.02)
zone("Loch Modan", "Crisp, overcast highland loch: cool clean light, a light haze over the water.",
     "measured", fog=35, rays=25, vol=20, tint=(190, 205, 215), k=0.10)
zone("Wetlands", "Dank, rain-soaked marsh: a low grey sky, persistent haze, muddy greens.",
     "known", fog=70, rays=10, vol=35, night=30, tint=(140, 155, 130), k=0.25, dark=0.10)
zone("Arathi Highlands", "Bright, windswept rolling highlands: soft daylight, mild distance haze, sun through old ruins.",
     "measured", fog=25, rays=45, vol=25, tint=(220, 215, 170), k=0.08)
zone("The Hinterlands", "Lush, wild green highlands: bright light, soft forest mist in the valleys.",
     "known", fog=35, rays=45, vol=30, tint=(170, 210, 150), k=0.12)
zone("Badlands", "A harsh, exposed dust bowl: bleached light, dusty haze, a hard open sky.",
     "known", fog=40, rays=35, vol=20, clouds=0, tint=(220, 170, 120), k=0.20)
zone("Searing Gorge", "Scorching volcanic gorge: heat haze and smoke, ember-orange light.",
     "known", fog=55, rays=30, vol=40, tint=(230, 120, 60), k=0.30, dark=0.08)
zone("Burning Steppes", "A land on fire: a burnt-orange sky, ash haze to the horizon, charred ground.",
     "measured", fog=65, rays=40, vol=45, tint=(64, 35, 23), k=0.25, dark=0.10)
zone("Blackrock Mountain", "The mountain's molten heart: black rock, red lava glow, choking smoke.",
     "measured", fog=55, rays=20, vol=50, tint=(48, 18, 14), k=0.30, dark=0.18)
zone("Tirisfal Glades", "Murky undead gloom: olive-grey light, damp haze, a heavy overcast.",
     "measured", fog=70, rays=15, vol=40, night=30, tint=(32, 38, 28), k=0.35, dark=0.15)
zone("Silverpine Forest", "Rain-soaked pine forest: the thickest grey-green fog, walls of mist between the trees.",
     "measured", fog=85, rays=20, vol=45, night=30, tint=(56, 68, 55), k=0.30, dark=0.07)
zone("Hillsbrad Foothills", "Pleasant green farms and hills: mild daylight, a light haze, a cool coast.",
     "inferred", fog=30, rays=40, vol=20, tint=(190, 210, 180), k=0.08)
zone("Alterac Mountains", "Cold grey mountain passes: snow, flat light, a chill mist.",
     "inferred", fog=55, rays=20, vol=30, tint=(200, 210, 225), k=0.12, dark=0.05)
zone("Western Plaguelands", "Plague-blighted autumn farmland: a sickly yellow-grey haze, dying light.",
     "inferred", fog=60, rays=20, vol=35, night=30, tint=(180, 170, 120), k=0.25, dark=0.08)
zone("Eastern Plaguelands", "Corrupted, dead country: a poisoned purple-green haze, dim and diseased.",
     "inferred", fog=70, rays=15, vol=40, night=25, tint=(150, 130, 150), k=0.25, dark=0.15)
zone("Stormwind City", "Proud, sunlit white-stone city: clean daylight, blue banners, a faint haze.",
     "measured", fog=25, rays=40, vol=25, tint=(235, 230, 215), k=0.06)
zone("Ironforge", "Vast, dark dwarven halls: deep shadow, warm pools of forge light.",
     "measured", fog=20, vol=35, tint=(44, 33, 33), k=0.20, dark=0.16)
zone("Undercity", "Cold, sickly underground: grey-green murk, ghostly light, candlelit corners.",
     "measured", fog=35, vol=30, tint=(36, 38, 30), k=0.30, dark=0.15)
# Turtle's Eastern Kingdoms
zone("Gilneas", "Gloomy werewolf kingdom: grey mist, dark pines, a rain-heavy sky.",
     "known", fog=75, rays=15, vol=40, night=30, tint=(120, 135, 130), k=0.25, dark=0.12)
zone("Balor", "An accursed isle: grey, wet and broken, cliffs and breaking waves, restless souls.",
     "turtlecraft", fog=75, rays=15, vol=40, night=25, tint=(140, 150, 155), k=0.20, dark=0.12)
zone("Northwind", "Elwynn in autumn: warm golden light, falling leaves, clear air.",
     "known", fog=25, rays=45, vol=25, tint=(245, 205, 150), k=0.15)
zone("Grim Reaches", "Muted, battle-worn highlands: dull greens, a grey sky, darker swamp to the south.",
     "known", fog=50, rays=20, vol=30, tint=(160, 165, 150), k=0.15, dark=0.08)
zone("Gillijim's Isle", "An untamed tropical island: humid jungle haze, bright sea light.",
     "known", fog=45, rays=50, vol=35, tint=(200, 215, 160), k=0.12)
zone("Lapidis Isle", "Jungle troll ruins and wrecked ships: humid haze, bright sea light.",
     "known", fog=45, rays=50, vol=35, tint=(200, 215, 160), k=0.12)
zone("Alah'Thalas", "A high elven city of light: bright gold and white, clear radiant air.",
     "known", fog=20, rays=55, vol=30, tint=(255, 235, 190), k=0.10)
zone("Thalassian Highlands", "Radiant high elf forest: golden autumn light, clear air, sun shafts.",
     "known", fog=25, rays=55, vol=30, tint=(250, 220, 160), k=0.12)
zone("Scarlet Enclave", "The Scarlet Crusade's heartland: pristine towns in pale light, a cold sea wind.",
     "known", fog=35, rays=35, vol=25, tint=(220, 215, 205), k=0.05)

# ---------------------------------------------------------------- Kalimdor
zone("Durotar", "Red-clay desert under a blazing low sun: golden-orange glare, hot haze, cracked earth.",
     "measured", fog=35, rays=60, vol=30, tint=(146, 76, 33), k=0.20)
zone("The Barrens", "Wide golden savanna: bright direct sun, a dry haze over the distance.",
     "known", fog=35, rays=35, vol=20, tint=(230, 200, 140), k=0.15)
zone("Mulgore", "Open sunlit plains: crystal-clear air, golden grass, a big blue sky.",
     "measured", fog=10, rays=40, vol=15, tint=(128, 136, 84), k=0.08)
zone("Thunder Bluff", "A warm mesa-top city: golden wood, a clear sky, open winds.",
     "measured", fog=10, rays=35, vol=15, tint=(109, 96, 57), k=0.12)
zone("Stonetalon Mountains", "Cool, diffuse mountain light: morning dew, grey rock, scarred valleys.",
     "known", fog=45, rays=30, vol=30, tint=(180, 190, 190), k=0.10)
zone("Thousand Needles", "Bleached salt flats among towering spires: glaring light, a thin heat haze.",
     "known", fog=25, rays=30, vol=15, clouds=0, tint=(240, 215, 170), k=0.10)
zone("Desolace", "A bleak grey-brown wasteland: a flat oppressive sky, heavy haze that mutes everything.",
     "known", fog=65, rays=15, vol=30, tint=(160, 150, 150), k=0.20, dark=0.08)
zone("Dustwallow Marsh", "Dim, murky swamp: green-tinted gloom, damp mist over dark water.",
     "known", fog=70, rays=15, vol=40, night=30, tint=(130, 150, 110), k=0.25, dark=0.10)
zone("Feralas", "Deep, lush jungle forest: saturated greens, dappled light, mist among giant trees.",
     "known", fog=55, rays=55, vol=45, tint=(130, 180, 130), k=0.15, dark=0.03)
zone("Teldrassil", "Dreamlike twilight forest: purple haze, silhouetted trees, ethereal light.",
     "known", fog=55, rays=50, vol=45, night=85, tint=(170, 150, 210), k=0.25, dark=0.05)
zone("Darkshore", "A mist-choked twilight coast: shadowed woods, grey-blue gloom, crumbling ruins.",
     "known", fog=70, rays=25, vol=40, night=60, tint=(130, 150, 180), k=0.25, dark=0.10)
zone("Ashenvale", "Enchanted twilight forest: blue-violet haze, the strongest shafts of light anywhere, glowing wisps.",
     "measured", fog=55, rays=70, vol=55, night=80, tint=(15, 55, 79), k=0.18, dark=0.05)
zone("Felwood", "Corrupted fel forest: a sickly green haze, a dark canopy, glowing pools.",
     "known", fog=70, rays=20, vol=45, night=30, tint=(130, 170, 90), k=0.30, dark=0.12)
zone("Azshara", "Eternal autumn: windswept, muted reds and golds, abandoned elven grandeur.",
     "known", fog=35, rays=40, vol=25, tint=(230, 170, 130), k=0.15)
zone("Moonglade", "A glade of perpetual moonlight: silvery night, soft violet mist, serene.",
     "known", fog=45, rays=40, vol=50, night=95, tint=(170, 170, 220), k=0.20, dark=0.05)
zone("Winterspring", "Endless snow with a soft pink glow: bright, cold, idyllic.",
     "known", fog=40, rays=40, vol=25, night=60, tint=(240, 220, 235), k=0.10)
zone("Tanaris", "Blazing desert: a cloudless glaring sky, orange-gold sand, heat shimmer.",
     "known", fog=25, rays=35, vol=15, clouds=0, tint=(245, 200, 130), k=0.15)
zone("Un'Goro Crater", "A primal lost-world jungle: humid green haze, steam, towering plants.",
     "known", fog=60, rays=50, vol=45, tint=(160, 200, 130), k=0.18)
zone("Silithus", "A desolate insect wasteland: yellow sand haze, harsh dead light.",
     "known", fog=55, rays=25, vol=30, clouds=0, tint=(220, 180, 110), k=0.25, dark=0.05)
zone("Darnassus", "A moonlit city in the canopy: cool blue-green light, drifting mist.",
     "known", fog=45, rays=45, vol=45, night=85, tint=(150, 170, 210), k=0.20, dark=0.05)
zone("Orgrimmar", "A red-rock fortress in canyon shade: dusty red-orange haze, warm lamplight, a violet Cleft of Shadow.",
     "measured", fog=35, rays=40, vol=30, tint=(101, 54, 37), k=0.20, dark=0.07)
# Turtle's Kalimdor and the seas
zone("Hyjal", "The sacred mountain under the World Tree: cool misty light, Archimonde's bones, the Nightmare creeping in.",
     "turtlecraft", fog=55, rays=40, vol=40, night=50, tint=(170, 185, 200), k=0.12)
zone("Moonwhisper Coast", "A moonlit elven coast: silvery-blue light, sea mist, bewitched ruins.",
     "known", fog=50, rays=40, vol=40, night=85, tint=(160, 175, 215), k=0.20, dark=0.05)
zone("Tel'Abim", "A rainy tropical island: savanna under frequent rain, humid haze.",
     "known", fog=50, rays=30, vol=30, tint=(200, 200, 150), k=0.12)
zone("Icepoint Rock", "A frozen glacier island: blinding white ice, cold blue light, sea fog.",
     "known", fog=50, rays=35, vol=25, tint=(210, 225, 245), k=0.15)
zone("Blackstone Island", "Goblin industry: coal smoke, a grimy haze, harbour slums.",
     "known", fog=55, rays=25, vol=30, tint=(170, 160, 140), k=0.20, dark=0.08)
zone("Sunnyglade Valley", "A First War battlefield in a green valley: clear daylight.",
     "known", fog=25, rays=40, vol=20)

# ---------------------------------------------------------------- dungeons and raids
# Inside there is no sky, so rays and night are left alone. Fog stays light: comfy's fog in a small room
# reads as murk, not mood. The wash carries most of each one's colour.
def dungeon(name, mood, src, fog, tint, k, dark=0.0, vol=35):
    zone(name, mood, src, fog=fog, vol=vol, tint=tint, k=k, dark=dark)

dungeon("Ragefire Chasm", "A volcanic throat lit by lava: dark basalt, sulphur haze, hot dread.", "known", 35, (230, 110, 50), 0.25, 0.08)
dungeon("Wailing Caverns", "A winding burrow in dim green murk: mist and pale glowing life.", "known", 45, (120, 170, 130), 0.25, 0.08)
dungeon("The Deadmines", "A long mineshaft in warm torchlight: coal dust and abandoned-industry gloom.", "known", 30, (200, 160, 110), 0.18, 0.06)
dungeon("Shadowfang Keep", "A ruined castle in perpetual twilight: cold blue-grey stone, drifting fog.", "known", 45, (130, 140, 170), 0.22, 0.10)
dungeon("Blackfathom Deeps", "A drowned temple: sandy stone giving way to deep blue-green gloom.", "known", 45, (90, 150, 160), 0.25, 0.08)
dungeon("The Stockade", "A cramped prison under the canals: cold grey light, damp stone.", "known", 25, (170, 175, 180), 0.12, 0.08)
dungeon("Gnomeregan", "A decaying gnome city: brass and pipes in a sickly green glow.", "known", 35, (140, 190, 120), 0.22, 0.06)
dungeon("Razorfen Kraul", "Thorn-choked tunnels: earthy browns, dim totems, feral hostility.", "known", 35, (170, 140, 100), 0.20, 0.08)
dungeon("Scarlet Monastery", "Gothic halls of white stone and scarlet: warm candlelight, solemn.", "known", 20, (235, 200, 180), 0.10)
dungeon("Scarlet Monastery Graveyard", "Gothic halls of white stone and scarlet: a grey, haunted yard.", "known", 40, (180, 180, 190), 0.12, 0.06)
dungeon("Scarlet Monastery Library", "Gothic halls of white stone and scarlet: warm candlelight, solemn.", "known", 20, (235, 200, 180), 0.10)
dungeon("Scarlet Monastery Armory", "Gothic halls of white stone and scarlet: warm candlelight, solemn.", "known", 20, (235, 200, 180), 0.10)
dungeon("Scarlet Monastery Cathedral", "Gothic halls of white stone and scarlet: warm candlelight, solemn.", "known", 20, (235, 200, 180), 0.10)
dungeon("Razorfen Downs", "A dark thorn lair: muddy browns, sickly greens, the stench of decay.", "known", 40, (150, 150, 110), 0.22, 0.10)
dungeon("Uldaman", "Vast Titan vaults of warm amber stone: soft crystal light, ancient grandeur.", "known", 25, (230, 190, 130), 0.15)
dungeon("Zul'Farrak", "An open-air troll city of sandstone under a bright sky: sun-baked.", "known", 30, (240, 200, 140), 0.12)
dungeon("Maraudon", "A vast cavern of shifting colour: purple grottos, orange caverns, green falls.", "known", 35, (170, 150, 190), 0.12)
dungeon("The Temple of Atal'Hakkar", "A drowned temple in murky greens and purples: mist and dread.", "known", 55, (120, 150, 130), 0.25, 0.12)
dungeon("Blackrock Depths", "A dark dwarven city overrun by fire: magma glow, thick smoke.", "known", 40, (220, 100, 60), 0.25, 0.12)
dungeon("Blackrock Spire", "A soot-black warren lit by fire; smoke-streaked sky above the peak.", "known", 40, (210, 110, 70), 0.22, 0.10)
dungeon("Dire Maul", "A fallen Highborne city: ghostly blue-purple arcane light, spectral fog.", "known", 40, (150, 150, 210), 0.20, 0.06)
dungeon("Scholomance", "A necromantic school: sickly green candlelight on black stone, ectoplasmic haze.", "known", 45, (130, 170, 130), 0.25, 0.14)
dungeon("Stratholme", "A ruined city: crusader red-and-white against the grey-green rot, thick fog.", "known", 55, (170, 170, 150), 0.18, 0.10)
dungeon("Molten Core", "A living inferno: rivers of magma, pillars of fire, heat haze.", "known", 40, (240, 120, 50), 0.28, 0.06)
dungeon("Onyxia's Lair", "A dark vaulted cave: the red glow of a dragon's lair, coiled stillness.", "known", 35, (200, 90, 70), 0.22, 0.12)
dungeon("Blackwing Lair", "A dragonflight warren of black stone and red fire: oppressive scale.", "known", 35, (200, 90, 60), 0.22, 0.12)
dungeon("Zul'Gurub", "An open-air troll temple in hot jungle sun: sandstone, braziers, ritual.", "known", 40, (220, 200, 140), 0.15)
dungeon("Ruins of Ahn'Qiraj", "A vast sandy ruin: pale ochre, bone-white, a toxic green glow.", "known", 45, (230, 200, 140), 0.18)
dungeon("Ahn'Qiraj", "A colossal hive city: deep greens and purples, pulsing glow, spore haze.", "known", 45, (150, 160, 140), 0.22, 0.10)
dungeon("Naxxramas", "A floating citadel of black ice and bone: cold blue-white frost, necrotic green.", "known", 45, (150, 180, 200), 0.22, 0.12)
dungeon("The Upper Necropolis", "A floating citadel of black ice and bone: cold blue-white frost, necrotic green.", "known", 45, (150, 180, 200), 0.22, 0.12)
dungeon("Alterac Valley", "A snowbound battlefield: cold white valleys, grey mountain light.", "inferred", 45, (210, 220, 235), 0.10)
dungeon("Warsong Gulch", "A night-elf forest battleground: soft green light between the walls.", "inferred", 30, (170, 200, 160), 0.08)
dungeon("Arathi Basin", "A sunny highland battleground: warm fields, open light.", "inferred", 25, (225, 215, 170), 0.06)
# Turtle's dungeons and raids
dungeon("Karazhan Crypt", "Forlorn catacombs where the dead are twisted back to life: cold stone, candle glimmer.", "turtlecraft", 40, (150, 150, 170), 0.18, 0.12)
dungeon("Lower Karazhan Halls", "Long-forgotten, dust-covered halls humming with arcane power: violet light.", "turtlecraft", 35, (170, 150, 200), 0.18, 0.08)
dungeon("Tower of Karazhan", "A spire of immense, uncontrolled power: violet arcane light, a stormy sky.", "turtlecraft", 35, (170, 150, 200), 0.18, 0.08)
dungeon("The Rock of Desolation", "A fragment adrift in the Nether: violet void light, eerie calm.", "inferred", 35, (160, 140, 210), 0.22, 0.08)
dungeon("Crescent Grove", "A hidden grove under the trees, gone corrupt: dusky green-violet haze.", "turtlecraft", 45, (150, 160, 170), 0.20, 0.08)
dungeon("Hateforge Quarry", "An innocent-looking quarry over an insidious Dark Iron cavern: forge fire and soot.", "turtlecraft", 40, (220, 120, 70), 0.22, 0.10)
dungeon("Stormwind Vault", "A sealed vault under the city: weakening runes, cold stone, horrors within.", "turtlecraft", 30, (170, 170, 185), 0.14, 0.12)
dungeon("Gilneas City", "A proud city left a shell: grey gothic gloom, a dark presence, distant howls.", "turtlecraft", 60, (130, 140, 140), 0.22, 0.14)
dungeon("Emerald Sanctum", "The Emerald Dream under a fog of corruption: luminous green mist turning sickly.", "turtlecraft", 55, (140, 200, 150), 0.24, 0.05)
dungeon("The Black Morass", "A swamp in the past: dark water, a violet time-rift glow.", "inferred", 55, (130, 130, 160), 0.22, 0.10)
dungeon("Dragonmaw Retreat", "An orc hold of the Dragonmaw: smoke, red banners, stone.", "inferred", 40, (200, 120, 90), 0.18, 0.08)
dungeon("Stormwrought Ruins", "A derelict castle on wave-lashed cliffs: pitch-black halls, ghosts and cultists.", "turtlecraft", 45, (140, 145, 160), 0.20, 0.16)
dungeon("Timbermaw Hold", "Furbolg tunnels through the mountain: earth, roots, warm lamplight.", "inferred", 30, (190, 160, 120), 0.15, 0.06)
dungeon("Windhorn Canyon", "A windy canyon cave: dusty ochre stone, dim light.", "inferred", 35, (210, 170, 120), 0.15, 0.06)
dungeon("Thorn Gorge", "A briar-choked gorge: tangled browns and greens, dim shade.", "inferred", 40, (160, 150, 110), 0.18, 0.08)
dungeon("Frostmane Hollow", "An ice-troll cave: frozen blue light, cold mist.", "inferred", 45, (180, 210, 235), 0.20, 0.08)
dungeon("Moomoo Grove", "A cheerful holiday grove: soft green light.", "inferred", 25, (190, 220, 170), 0.08)
dungeon("Winter Veil Vale", "A festive snowy vale: bright white snow, warm lights.", "inferred", 30, (230, 225, 240), 0.08)
# entrance zones and in-betweens the client names as zones of their own
zone("Caverns of Time", "Timeless bronze caverns: warm sandy light, a shimmer of sand in the air.",
     "inferred", fog=30, rays=30, vol=30, tint=(230, 200, 140), k=0.12)
zone("Gates of Ahn'Qiraj", "The scarab wall in the desert: yellow sand haze, a hard alien sky.",
     "inferred", fog=55, rays=25, vol=30, clouds=0, tint=(220, 180, 110), k=0.25, dark=0.05)
zone("Timbermaw Tunnels", "Furbolg tunnels through the mountain: earth, roots, warm lamplight.",
     "inferred", fog=30, vol=30, tint=(190, 160, 120), k=0.15, dark=0.06)
dungeon("Windhorn Caverns", "A windy canyon cave: dusty ochre stone, dim light.", "inferred", 35, (210, 170, 120), 0.15, 0.06)

# ---------------------------------------------------------------- comfyatmosphere 0.7 and 0.8
# comfy 0.7 and 0.8 added sun shadows, glowing lamps, darker nights and a volumetric mist that lies on the ground,
# and the mist replaced the old fog control. Each zone's intent reaches those through a few traits and one set of
# rules (derive_v8), so 118 zones stay consistent with each other instead of being 118 separate guesses.
# Feature switches and quality settings (Volumetric Light on or off, shadow resolution, mist reach) are never
# touched: they cost frame rate, and that is the player's call. Only how each enabled effect looks is set.
#
# traits: forest jungle swamp lake coast plains hills mountains snow desert volcanic plague fel haunted twilight
#         city indoor windy sunny overcast morning lamps
T = {}
def traits(names, *ts):
    for n in names.split(";"):
        n = n.strip()
        assert n in Z, n
        T.setdefault(n, set()).update(ts)

# Eastern Kingdoms
traits("Elwynn Forest", "forest", "sunny", "morning", "lamps")
traits("Westfall", "plains", "sunny", "windy", "morning")
traits("Redridge Mountains", "hills", "lake", "sunny", "morning")
traits("Duskwood", "forest", "haunted", "lamps", "overcast")
traits("Stranglethorn Vale", "jungle", "coast", "sunny", "morning")
traits("Swamp of Sorrows", "swamp", "overcast", "haunted")
traits("Blasted Lands", "desert", "windy", "volcanic")
traits("Deadwind Pass", "haunted", "mountains", "windy", "overcast")
traits("Dun Morogh", "snow", "mountains", "morning")
traits("Loch Modan", "lake", "hills", "overcast", "morning")
traits("Wetlands", "swamp", "lake", "overcast")
traits("Arathi Highlands", "plains", "hills", "windy", "sunny")
traits("The Hinterlands", "forest", "hills", "morning")
traits("Badlands", "desert", "windy", "sunny")
traits("Searing Gorge", "volcanic", "mountains")
traits("Burning Steppes", "volcanic", "windy")
traits("Blackrock Mountain", "volcanic", "mountains", "lamps")
traits("Tirisfal Glades", "forest", "haunted", "overcast", "plague")
traits("Silverpine Forest", "forest", "lake", "haunted", "overcast")
traits("Hillsbrad Foothills", "plains", "hills", "coast", "morning")
traits("Alterac Mountains", "snow", "mountains", "overcast")
traits("Western Plaguelands", "plains", "plague", "overcast")
traits("Eastern Plaguelands", "plague", "haunted", "forest")
traits("Stormwind City", "city", "lamps", "sunny", "lake")
traits("Ironforge", "city", "indoor", "lamps")
traits("Undercity", "city", "indoor", "lamps", "haunted")
traits("Gilneas", "forest", "haunted", "overcast", "lamps")
traits("Balor", "coast", "haunted", "overcast", "windy")
traits("Northwind", "plains", "forest", "sunny", "morning")
traits("Grim Reaches", "hills", "lake", "overcast", "swamp")
traits("Gillijim's Isle; Lapidis Isle", "jungle", "coast", "sunny")
traits("Alah'Thalas", "city", "sunny", "lamps", "coast")
traits("Thalassian Highlands", "forest", "sunny", "morning")
traits("Scarlet Enclave", "coast", "plains", "windy")
# Kalimdor
traits("Durotar", "desert", "sunny", "windy")
traits("The Barrens", "plains", "sunny", "windy")
traits("Mulgore", "plains", "sunny", "windy", "morning")
traits("Thunder Bluff", "city", "sunny", "windy")
traits("Stonetalon Mountains", "mountains", "forest", "overcast")
traits("Thousand Needles", "desert", "sunny", "windy")
traits("Desolace", "desert", "overcast", "windy", "haunted")
traits("Dustwallow Marsh", "swamp", "coast", "overcast")
traits("Feralas", "jungle", "forest", "coast", "morning")
traits("Teldrassil", "forest", "twilight", "morning")
traits("Darkshore", "coast", "forest", "twilight", "overcast")
traits("Ashenvale", "forest", "twilight", "lake")
traits("Felwood", "forest", "fel", "haunted", "overcast")
traits("Azshara", "coast", "forest", "windy")
traits("Moonglade", "forest", "twilight", "lake")
traits("Winterspring", "snow", "mountains")
traits("Tanaris", "desert", "sunny", "windy")
traits("Un'Goro Crater", "jungle", "volcanic", "morning")
traits("Silithus", "desert", "windy", "overcast")
traits("Darnassus", "city", "twilight", "lamps", "lake")
traits("Orgrimmar", "city", "desert", "lamps")
traits("Hyjal", "forest", "mountains", "twilight", "fel")
traits("Moonwhisper Coast", "coast", "twilight", "forest")
traits("Tel'Abim", "jungle", "coast", "overcast")
traits("Icepoint Rock", "snow", "coast", "windy")
traits("Blackstone Island", "coast", "volcanic", "lamps")
traits("Sunnyglade Valley", "plains", "sunny")
traits("Caverns of Time", "desert", "indoor", "lamps")
traits("Gates of Ahn'Qiraj", "desert", "windy")
traits("Timbermaw Tunnels; Windhorn Caverns", "indoor", "lamps")
# dungeons, raids and battlegrounds
for n in Z:
    if n not in T:
        T[n] = {"indoor", "lamps"}
traits("Alterac Valley", "snow", "mountains")
traits("Warsong Gulch", "forest", "twilight")
traits("Arathi Basin", "plains", "sunny", "windy")
for n in ("Alterac Valley", "Warsong Gulch", "Arathi Basin", "Zul'Farrak", "Zul'Gurub", "Ruins of Ahn'Qiraj"):
    T[n].discard("indoor")
traits("Zul'Farrak; Ruins of Ahn'Qiraj", "desert", "sunny")
traits("Zul'Gurub", "jungle", "sunny")
traits("Molten Core; Ragefire Chasm; Blackrock Depths; Blackrock Spire; Hateforge Quarry; Blackwing Lair", "volcanic")
traits("Scholomance; Stratholme; Naxxramas; The Upper Necropolis; Razorfen Downs; Karazhan Crypt; Shadowfang Keep; Gilneas City", "haunted")
traits("Wailing Caverns; The Temple of Atal'Hakkar; The Black Morass; Blackfathom Deeps", "swamp")

# Lamps (30 Sep 2026: the Darkshire glare). comfy 0.8.2 adds each lamp's glow on top of the picture with no cap
# (lampglow.cpp:389-428): glow ~ Lamp Glow x (1 + Lamps in Mist/100 x T), dimmed by the mist between, where T, the
# mist's thickness at the lamp (volume.cpp:1574-1592), grows with Mist in Low Ground (hollows), Mist over Water and
# a low Mist Height, and below the ground under a city's or a cave's rock. The two settings MULTIPLY, and Duskwood's
# 55 x 120 turned Darkshire's torches into a wall of orange. So each zone is held to comfy's own defaults, which
# the player is happy with: no lamp, at any of the spots below, glows more than LAMP_CEILING times what comfy's defaults
# give at that same spot.
LAMP_DEFAULT = dict(comfyLampGlow=20, comfyMistLamps=50, comfyMistDensity=25, comfyMistLow=150,
                    comfyMistWater=210, comfyMistHeight=25, comfyMistMorning=100)
LAMP_CEILING = 1.5
# Spots: (valley yards under the ground around it, over water, lamp yards above the ground, negative = under
# rock; at dawn, when Mist at Dawn thickens the mist; the lamp's distance in yards). Every zone is held to the open
# ground ones, in hollows up to the 25 yards comfy's Mist in Low Ground works over. Cities and dungeons, where lamps
# really do hang under rock (Undercity, Ironforge, the Cleft of Shadow), are also held under rock, every 5 yards
# down to 100: the excess peaks where the zone's own mist stops thickening (4 Mist Heights down), so sparse depths
# miss it. A cave or a ravine deeper than 25 yards in an open zone is not held: comfy's own defaults are already
# far brighter there than on a street, and holding to them cost swamps their glow over water.
LAMP_OPEN = [(v, w, z, dawn, d) for v in (0, 10, 25) for w in (0, 1) for z in (0.5, 1, 2, 4)
             for dawn in (0, 1) for d in (1, 3, 6)]
LAMP_ROCK = [(v, 0, -r, dawn, d) for v in (0, 25) for r in range(5, 105, 5) for dawn in (0, 1) for d in (1, 3, 6)]
LAMP_STREET = (0, 0, 3, 0, 6)
# Lamps the player has set by eye in game, which win over the rule.
# Lamp Distance is how far into the fog a lamp still glows: a lamp is at full glow out to half of it and gone at
# it, in hundredths of the game's own fog (comfy's 240 reaches 2.4 times that fog; 50, the least, half of it).
# Duskwood's fog ends at about 100 yards, so 50 keeps a lamp whole to 25 yards and gone by 50: a torch lights
# the fog around its guard without glowing at you from hundreds of feet off.
# Lamp Reach (Azeroth Atmosphere's comfy) sizes each lamp's globe: a torch's glow fades to nothing at 17 yards,
# and 30 percent of that is the 15 feet he asked for. Without that comfy the control is not there and is left be.
LAMP_PICKS = {
    # Darkshire at night, the Night Watch's torches: "good"; not far off; a globe of about 15 feet (30 Sep 2026)
    "Duskwood": dict(comfyLampGlow=40, comfyMistLamps=40, comfyLampDistance=50, comfyLampReach=30),
}

# HELD BACK until each place has been set by eye. His rule, 1 Oct 2026: "Any other such settings that would lead to
# fog, oversaturation, player not being able to see to play, should also be dialed way back until we've had a chance
# to set them by hand. And not just in auberdine but across all maps, dungeons and zones." Players had reported the
# Deadmines as a gas chamber (green fog, screen-filling lanterns) and the Auberdine ship walled in navy fog.
# Every control that can hide the world, glare, oversaturate or darken it keeps a share of its zone's value
# (SAFE_KEEP) and can never pass a ceiling (SAFE_CEILING), each well under comfy's own default (in the comments).
# Nothing here ever raises a value. What he has set by eye in game (LAMP_PICKS) is kept as he set it.
# The mist's height is left as it is: it shapes the fog rather than thinning it. comfy's mist at depth d under its
# ground is density * exp(min(d / height, 4)) (volume.cpp FogAt), so a lower height thickens caves and hollows,
# up to 3.7 times at 80 yards in the Badlands when this table first capped it at 20 (caught in review, 1 Oct).
# build_zones.py checks the thickness itself, at every depth, never just the numbers.
# To give a zone its full preset back once it has been set by hand, add it to SET_BY_HAND.
SAFE_KEEP = dict(comfyFogThickness=0.4, comfyMistDensity=0.4, comfyRaysStrength=0.5, comfyVolumeStrength=0.5,
                 comfyVolumeDensity=0.5)
SAFE_CEILING = dict(
    comfyFogThickness=25,        # comfy 0.6's distance fog (0.7 and 0.8 do not have it)
    comfyMistDensity=15,         # comfy 25: how thick the fog is at the ground
    comfyMistPatches=50,         # comfy 50: the thickest patches 1.5 times the plain fog, not up to 1.75
    comfyMistLow=75,             # comfy 150: extra mist in valleys
    comfyMistWater=75,           # comfy 210: extra mist over rivers, lakes and the sea (the harbours)
    comfyMistMorning=50,         # comfy 100: extra mist at dawn
    comfyMistBrightness=100,     # comfy 120: 100 is the game's own fog colour
    comfyMistSun=30,             # how brightly the sun lights the fog
    comfyMistLamps=20,           # comfy 50: how much brighter lamps glow in thick mist
    comfyLampGlow=20,            # comfy 20
    comfyRaysStrength=25,        # comfy 40
    comfyVolumeStrength=20,      # comfy 25: how bright the lit fog is
    comfyVolumeDensity=10,       # comfy 16: how thick the air is
    comfySunlight=15,            # comfy 35: what the sun reaches made brighter, in percent
    comfySunTint=50,             # comfy 95: sunlit ground takes a warm colour
    comfyShadeTint=40,           # comfy 65: shade takes the sky's blue
    comfySunShadowStrength=20,   # comfy 20: how dark the shadows are
    comfyNightStrength=25,       # comfy 25: rays and volumetric light at night
    comfyNightDarkness=15,       # comfy 20: how much darker the world is at night
    comfyMoonlight=65,           # comfy 65: how blue the night is
)
SAFE_WASH = 0.25                 # a colour wash keeps a quarter of its darkening
SET_BY_HAND = set()              # zones whose full preset has been checked by eye in game


def safe(name, cv):
    """The zone's controls held back (see SAFE_KEEP). Never raises a value; keeps what was set by eye."""
    if name in SET_BY_HAND:
        return dict(cv)
    by_eye = LAMP_PICKS.get(name, {})
    out = {}
    for c, v in cv.items():
        if c in by_eye:
            out[c] = v
            continue
        if c in SAFE_KEEP:
            v = 5 * round(v * SAFE_KEEP[c] / 5.0)
        if c in SAFE_CEILING:
            v = min(v, SAFE_CEILING[c])
        out[c] = int(min(v, cv[c]))
    return out


def safe_wash(name, w):
    """A zone's colour wash with most of its darkening taken off (see SAFE_WASH)."""
    if w is None or name in SET_BY_HAND:
        return w
    return tuple(round(1 - (1 - c) * SAFE_WASH, 2) for c in w)


def lamp_glow(c, valley, wet, z, dawn, d):
    """comfy's glow from a lamp at that spot, up to a constant that cancels."""
    def thick(z):
        mult = (1 + c["comfyMistLow"] / 100.0 * min(valley / 25.0, 1.0)) * (1 + c["comfyMistWater"] / 100.0 * wet)
        return mult * math.exp(min((valley / 2.0 - z) / c["comfyMistHeight"], 4.0))
    density = c["comfyMistDensity"] * 1e-4 * (1 + c["comfyMistMorning"] / 100.0 * dawn)
    return c["comfyLampGlow"] * math.exp(-density * thick(z + 0.5) * d) * (1 + c["comfyMistLamps"] / 100.0 * thick(z))

def lamp_ratio(c, spots):
    return max(lamp_glow(c, *s) / lamp_glow(LAMP_DEFAULT, *s) for s in spots)

def r5(v, lo, hi):
    """Rounded to comfy's own slider step of 5, inside its range."""
    return int(max(lo, min(hi, 5 * round(v / 5.0))))

def derive_v8(name):
    """comfy 0.8's controls for one zone, from its intent (fog, rays, night, dark) and its traits."""
    z, t = Z[name], T[name]
    has = t.__contains__
    out = {}
    fog = z["fog"] or 0
    # The mist: our fog intent, 0 to 100, as ground density in ten-thousandths a yard (comfy's default is 25).
    out["comfyMistDensity"] = r5(40 * (fog / 60.0) ** 1.6, 5, 140)
    height = 25
    for tr, h in (("swamp", 12), ("forest", 18), ("jungle", 20), ("lake", 18), ("coast", 20), ("haunted", 20), ("city", 15),
                  ("indoor", 10), ("mountains", 35), ("snow", 30), ("desert", 40), ("volcanic", 45), ("plague", 25)):
        if has(tr):
            height = h
            break
    out["comfyMistHeight"] = r5(height + (10 if fog >= 80 else 0), 5, 200)
    patches = 50
    for tr, p in (("haunted", 75), ("swamp", 70), ("forest", 65), ("jungle", 55), ("mountains", 55), ("snow", 45),
                  ("volcanic", 40), ("plains", 35), ("city", 30), ("indoor", 25), ("desert", 20)):
        if has(tr):
            patches = p
            break
    out["comfyMistPatches"] = patches
    low = 150
    for tr, v in (("indoor", 0), ("swamp", 250), ("haunted", 220), ("forest", 200), ("jungle", 180), ("mountains", 180),
                  ("plains", 120), ("city", 80), ("desert", 60)):
        if has(tr):
            low = v
            break
    out["comfyMistLow"] = low
    out["comfyMistWater"] = 300 if (has("lake") or has("coast")) else 280 if has("swamp") else 50 if has("desert") else 150
    morning = 100
    if has("indoor") or has("volcanic"):
        morning = 0
    elif has("desert"):
        morning = 20
    elif has("morning"):
        morning = 250
    elif has("lake") or has("forest") or has("plains"):
        morning = 150
    elif has("city"):
        morning = 80
    out["comfyMistMorning"] = morning
    out["comfyMistSun"] = r5(60 if has("sunny") else 20 if has("haunted") else 25 if has("overcast") else 35 if has("twilight") else 40, 0, 200)
    bright = 120 - 150 * z["dark"]
    if has("haunted"): bright = min(bright, 85)
    if has("snow"): bright = 150
    if has("desert"): bright = 140
    out["comfyMistBrightness"] = r5(bright, 0, 200)
    out["comfyMistLamps"] = 50   # set with Lamp Glow below, from the mist above
    out["comfyMistWind"] = 0 if has("indoor") else 45 if has("windy") else 5 if has("swamp") else 10 if (has("forest") or has("jungle")) else 15
    # Lamps: the glow around lamps, candles and torches, and how much brighter it is in thick mist. Lamps belong to
    # cities and lamp-lit zones, so their street lamps glow a quarter over comfy's; elsewhere as comfy's. The mist
    # boost is the largest (never above comfy's own 50) that keeps the zone's worst spot under LAMP_CEILING.
    want = 1.25 if (has("city") or has("lamps")) and not has("indoor") else 1.0
    spots = LAMP_OPEN + (LAMP_ROCK if (has("city") or has("indoor")) else [])
    c = {**LAMP_DEFAULT, **out}   # Mist Height, Low Ground, Water, Dawn and Density are set above
    for mist in range(50, -1, -10):
        c["comfyMistLamps"], c["comfyLampGlow"] = mist, 20
        street, worst = lamp_ratio(c, [LAMP_STREET]), lamp_ratio(c, spots)
        if want * worst / street <= LAMP_CEILING:
            break
    out["comfyMistLamps"] = mist
    # On comfy's slider step of 5: the nearest step to the street target, or the step below if that breaks the ceiling.
    top = 20 * LAMP_CEILING / worst
    glow = 5 * round(min(20 * want / street, top) / 5.0)
    out["comfyLampGlow"] = int(max(5, glow if glow <= top else 5 * int(top / 5)))
    if name in LAMP_PICKS:   # set by eye in game: wins over the rule
        out.update(LAMP_PICKS[name])
    # Night.
    if not has("indoor"):
        dark = 35
        for tr, v in (("haunted", 70), ("plague", 65), ("fel", 60), ("swamp", 55), ("forest", 45), ("twilight", 35), ("city", 25),
                      ("desert", 30), ("snow", 30), ("volcanic", 25)):
            if has(tr):
                dark = v
                break
        out["comfyNightDarkness"] = r5(dark, 0, 90)
        moon = 65
        for tr, v in (("twilight", 90), ("snow", 80), ("haunted", 50), ("desert", 55), ("volcanic", 20), ("plague", 40)):
            if has(tr):
                moon = v
                break
        out["comfyMoonlight"] = moon
        if has("twilight") or has("snow"):
            out["comfySunShadowsNight"] = 60
        # Sun shadows: strong and crisp under a clear sun, weak and soft under cloud and fog.
        foggy = fog >= 70
        out["comfySunShadowStrength"] = 15 if (has("overcast") or has("haunted") or foggy) else 55 if has("desert") else 45 if has("sunny") else 35 if has("snow") else 25
        out["comfyShadowSoftness"] = 5 if has("haunted") else 4 if (has("overcast") or foggy) else 1 if (has("sunny") or has("desert")) else 2
        out["comfySunlight"] = 45 if has("desert") else 40 if has("sunny") else 10 if has("haunted") else 15 if has("overcast") else 30
        out["comfySunTint"] = 95 if (has("desert") or has("volcanic") or (has("sunny") and has("plains"))) else 40 if has("snow") else 25 if has("haunted") else 45 if has("overcast") else 50 if has("twilight") else 80
        out["comfyShadeTint"] = 85 if has("snow") else 80 if has("twilight") else 70 if has("coast") else 45 if has("desert") else 50 if has("haunted") else 30 if has("volcanic") else 65
    # The air: denser where shafts should stand out through trees and smoke.
    vd = 16
    for tr, v in (("volcanic", 24), ("jungle", 22), ("twilight", 22), ("forest", 20), ("swamp", 18), ("haunted", 18), ("indoor", 18),
                  ("city", 14), ("plains", 12), ("mountains", 12), ("snow", 12), ("desert", 8)):
        if has(tr):
            vd = v
            break
    out["comfyVolumeDensity"] = vd
    return out
