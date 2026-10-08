-- Atmosphere Director: each zone gets its own layer of fog, light and colour, the way Blizzard authors a
-- zone's mood in WoW Forever. Every zone, city, dungeon and raid in the client has a written intent (its
-- mood: Duskwood is dark, moody and haunted) and a preset that goes for it, in Zones.lua. Where Forever
-- footage exists the preset is measured from it; elsewhere it follows the zone's known look. Storms layer on
-- top, and they are made here too, so every fog decision in the pack is made in one place: Indoor Weather's
-- DLL reports how hard it is raining, and this addon thickens the fog and dims the sun on top of the zone's
-- own values while it rains, putting them back when it stops. Indoor Weather 0.14 and older thickened the fog
-- themselves; their storm fog is kept switched off, and their "fog on|off" command sets the switch here, so
-- the two can never both add a storm.
--
-- The layer moves comfyatmosphere's own sliders (they are CVars: fog, sun rays, volumetric light, night
-- and clouds) and draws one colour wash over the 3D
-- world (a multiply, so the UI is untouched). Everything fades in and out over a few seconds. The
-- player's own slider values are kept and put back when the zone is left and at logout, so the client
-- never saves a zone's look as the player's setting. A slider the player moves while in the zone is
-- theirs and stays where they put it.
--
-- Indoors the lamps have their own values. comfy keeps every candle and torch at full night strength under a
-- roof at any hour and adds the mist of the ground outside, so an inn floods orange. While you are inside, the
-- lamp glow and the mist around lamps are set to your indoor values (5 and 10 unless you choose others, in tenths
-- if you like), and the zone's own come back when you step out. Moving comfy's two lamp sliders while inside sets
-- the indoor values; outside, they set your own, which the zone moods leave alone until the layer next starts
-- over (a zone without a mood, /atmos off and on, or logging out). A storm never touches the
-- lamps, so the indoor values hold through one. Being inside is read from IsIndoors (SuperWoW or ClassicAPI);
-- without it the lamps are never set apart indoors.
--
--   /atmos            the Atmosphere window: every switch and slider of the pack in one place
--   /atmos status     status
--   /atmos on | off   the whole layer
--   /atmos 0-100      strength: 100 is the zone as designed, 0 is the player's own settings
--   /atmos indoor     the indoor lamp values; /atmos indoor <glow 0-100> [<mist 0-200>] sets them, tenths too
--   /atmos rain 0-3   a storm's fog without waiting for rain (1 light, 2 steady, 3 heavy; 0 ends it)
--   /atmos mistfix on | off   comfy's ground mist taken out aboard ships and inside (on by default; see MistWhy)
--   /aa               the same words; any other word goes on to comfy, as under /atmos
-- comfyatmosphere (since 0.7) answers to /atmos too, for its settings window and its tuning commands. The two
-- share it: plain /atmos and this addon's own words come here, every other word goes on to comfy (/atmos options,
-- /atmos debug, /atmos stats and its tuning all still work). Without comfy, /atmos is this addon's alone.

local PROFILES = AtmosphereDirector_Zones or {}   -- Zones.lua: every zone's mood and preset

local RAMP_SECONDS = 5
local INDOOR_SECONDS = 1.0          -- stepping through a door: the lamps settle in a second, as Indoor Weather does
local INDOOR_SETTLE = 0.75          -- seconds an "outside" reading must hold first: IsIndoors flickers in a doorway
local INDOOR_IN_HOLD = 0.3          -- and an "inside" one: short, so no candle flares, but past a doorway's blips
local STEP = 0.2
-- The two lamp controls the indoor cap holds down, each with the setting that holds its cap.
local LAMPS = { comfyLampGlow = "indoorGlow", comfyMistLamps = "indoorMist" }
-- A storm, by the rain level Indoor Weather's DLL reports (1 light, 2 steady, 3 heavy): the fog thickens and
-- the sun goes behind cloud, so its light on the mist, its shafts, its shadows and its brightening all weaken.
-- The fog it adds is held back like the zone presets until storms have been set by eye in game (his rule,
-- 1 Oct 2026: nothing that keeps a player from seeing to play). The weakening is Indoor Weather 0.14's own.
local STORM = {
  { name = "comfyFogThickness",      add = { 4, 7, 10 } },
  { name = "comfyMistDensity",       add = { 5, 10, 15 }, max = 200 },
  { name = "comfyRaysStrength",      mul = { 0.70, 0.45, 0.25 } },
  { name = "comfyVolumeStrength",    mul = { 0.70, 0.45, 0.25 } },
  { name = "comfyMistSun",           mul = { 0.70, 0.50, 0.35 } },
  { name = "comfySunShadowStrength", mul = { 0.60, 0.40, 0.25 } },
  { name = "comfySunlight",          mul = { 0.60, 0.40, 0.25 } },
}
local STORM_SECONDS = 6             -- the fog thickens or clears over this when the rain changes
-- Indoor Weather's report, IndoorRain_Storm: k strike distance, l rain level, n the session (two digits, the
-- one in IndoorRain_Session) and the strike count, d the thunder's delay. Only Indoor Weather writes it.
local STORM_PATTERN = "^IRS1:k%d:l(%d):n(%d%d)%d:d%d%d$"
-- Every control any zone sets, gathered from Zones.lua so the two can never disagree, the lamps, which the
-- indoor cap needs even where no zone sets them, and the storm's. comfy 0.6 and older have comfyFogThickness;
-- 0.7 and 0.8 have the mist, shadow, lamp and night controls instead. A control the installed comfy does not
-- have reads as nil and is left alone, so one table serves every version.
local MANAGED = {}
do
  local seen = {}
  for name in pairs(LAMPS) do seen[name] = true; table.insert(MANAGED, name) end
  for i = 1, table.getn(STORM) do
    local name = STORM[i].name
    if not seen[name] then seen[name] = true; table.insert(MANAGED, name) end
  end
  for _, p in pairs(PROFILES) do
    for name in pairs(p.cvars) do
      if not seen[name] then seen[name] = true; table.insert(MANAGED, name) end
    end
  end
  table.sort(MANAGED)
end
local SOURCES = { ["measured"] = "measured from WoW Forever", ["known"] = "from the zone's known look",
  ["turtlecraft"] = "from turtlecraft.gg", ["inferred"] = "a first guess" }
local UpdateMood                    -- the window's line about the zone you are in
local ShareAtmos                    -- shares /atmos with comfyatmosphere; defined with the commands, below
local ShareIndoorRain               -- takes /indoorrain fog on|off for the storm fog here; the same place

local db
local zone, profile                 -- the layer being shown (nil = none)
local from, to = {}, {}             -- ramp endpoints per CVar
local washFrom, washTo = { 1, 1, 1 }, { 1, 1, 1 }
local written = {}                  -- what this addon last wrote, to notice a player's own change
local rampT, stepT = 1, 0
local rampLen, lampRamp = RAMP_SECONDS, false   -- the running fade's length, and whether it moves only the lamps
local capped = nil                  -- the indoor cap is on: you are inside and the layer is on (nil: look again now)
local seenInside, seenAt = nil, 0   -- the last indoor reading, and since when it has held
local repairUntil
local rain = 0                      -- the storm being shown: Indoor Weather's rain level, 0 when none or off
local testRain                      -- /atmos rain 1-3: a storm's fog shown without rain, until /atmos rain 0 or a reload
local mistOff, mistWhy = false, nil   -- comfy's ground mist taken out here, and why (see MistWhy)
local groundSince, pendingWhy, pendingAt = 0, nil, nil

local function Say(msg) DEFAULT_CHAT_FRAME:AddMessage("|cff88bbccatmosphere|r: " .. msg) end

local function CVarNum(name)
  local ok, v = pcall(GetCVar, name)
  if not ok or v == nil then return nil end
  return tonumber(v)
end

local function SetNum(name, v)
  -- whole numbers, but tenths for the lamps: comfy reads its CVars as decimals, and indoor lamps want the detail
  local s = tostring(LAMPS[name] and math.floor(v * 10 + 0.5) / 10 or math.floor(v + 0.5))
  if pcall(SetCVar, name, s) then written[name] = tonumber(s); return true end
  return false
end

local function StormActive()
  return IndoorRainDB and IndoorRainDB.active and true or false
end

-- A slider Indoor Weather's storm is holding: it moved it, not the player, and puts it back when the storm ends.
local function StormHolds(name)
  return StormActive() and IndoorRainDB.base and IndoorRainDB.base[name] ~= nil
end

-- A slider's value under any storm: the one the storm will put back, or the slider itself.
local function Under(name)
  if StormHolds(name) then return IndoorRainDB.base[name].v end
  return CVarNum(name)
end

-- Sets a slider, or, while a storm holds it, the value the storm keeps for it: the storm works from that value
-- every tick, so the zone's fog is thickened rather than fought over, and the storm lands on it at the end.
local function Put(name, v)
  if StormHolds(name) then
    local r = math.floor(v + 0.5)
    IndoorRainDB.base[name] = { v = r, s = tostring(r) }
    written[name] = r
    return true
  end
  return SetNum(name, v)
end

-- Whether you are under a roof, from IsIndoors (SuperWoW or ClassicAPI). nil without it: then there is no indoor
-- cap, never a guess. (Indoor Weather's own copy is no help: it reads the same IsIndoors, and says "0" without it.)
local function Indoors()
  if IsIndoors then return (IsIndoors() and true or false), "IsIndoors" end
  return nil
end

local function PanelOpen()
  return (OptionsFrame and OptionsFrame:IsVisible()) or (SoundOptionsFrame and SoundOptionsFrame:IsVisible())
    or (UIOptionsFrame and UIOptionsFrame:IsVisible())
end

-- the colour wash over the 3D world
local washFrame = CreateFrame("Frame", nil, WorldFrame)
washFrame:SetAllPoints(WorldFrame)
washFrame:SetFrameStrata("BACKGROUND")
washFrame:SetFrameLevel(0)
local washTex = washFrame:CreateTexture(nil, "BACKGROUND")
washTex:SetAllPoints(washFrame)
washTex:SetTexture(1, 1, 1)
washTex:SetBlendMode("MOD")
washFrame:Hide()

local function SetWash(r, g, b)
  if r > 0.995 and g > 0.995 and b > 0.995 then washFrame:Hide(); return end
  washTex:SetTexture(r, g, b)
  washFrame:Show()
end

local function Strength() return (db and db.strength or 100) / 100 end

-- Where the layer wants each slider now: between the player's own value and the zone's, by strength, with the
-- storm on top while it rains. Indoors the two lamp controls go no higher than their caps.
local function Targets()
  local t, w = {}, { 1, 1, 1 }
  local k = (db.enabled and profile) and Strength() or 0
  for i = 1, table.getn(MANAGED) do
    local name = MANAGED[i]
    local base = db.base[name]
    if base then
      local zv = profile and not db.mine[name] and profile.cvars[name]
      t[name] = zv and (base + (zv - base) * k) or base
      if capped and db.enabled and LAMPS[name] then t[name] = db[LAMPS[name]] end
    end
  end
  if rain > 0 then
    local l = math.min(3, rain)
    for i = 1, table.getn(STORM) do
      local c, v = STORM[i], t[STORM[i].name]
      if v then
        if c.add then t[c.name] = math.min(c.max or 100, v + c.add[l]) else t[c.name] = v * c.mul[l] end
      end
    end
  end
  if mistOff and t.comfyMistDensity then   -- see MistWhy: comfy's ground mist goes wrong here
    local hadMist = t.comfyMistDensity > 0
    t.comfyMistDensity = 0
    -- comfy runs its volume pass while Volumetric Light Strength is above 0 or its mist is on (volume.cpp
    -- VolumeActive), and sun shadows and lamp glow need that pass: with the mist out, a strength of 0 would put
    -- them out at every door and on every ship. 1 keeps the pass with next to no light of its own, and only where
    -- the mist was keeping it: a player with both at 0 has the pass off everywhere and keeps it off (review, 1 Oct).
    if hadMist and t.comfyVolumeStrength and t.comfyVolumeStrength < 1 then t.comfyVolumeStrength = 1 end
  end
  if profile and profile.wash then
    for i = 1, 3 do w[i] = 1 - (1 - profile.wash[i]) * k end
  end
  return t, w
end

-- WHERE COMFY'S GROUND MIST GOES WRONG. comfyatmosphere works out the ground under its mist from the map's terrain
-- around you, and fills the view wherever that guess is wrong. Two kinds of place, both found on 1 Oct 2026:
--   aboard a ship or a zeppelin: comfy reads your place on the deck, measured from the ship's middle, as your place
--     in the world (/atmos stats: 6577.5, 768.9 on the Auberdine pier, -7.7, -2.5 on the deck; 6420.3, 816.5 and
--     1.5, 1.6 for the Stormwind ship), so its ground is near the map's origin. It walled the deck in at strength 0,
--     with stock comfy 0.8.2 and nothing of ours;
--   inside: comfy 0.8.2 has no indoor rule, so under a roof, in a mine or a city under the land its ground is the
--     land above you, and you are buried in its mist (the Deadmines tunnel under Moonbrook, Undercity, Ironforge).
--     A dungeon built as one building reads as inside too (every room of the Deadmines did, in Indoor Weather's log).
-- Both are read from things that keep coming while the mist is off: comfy's position (written every second, fog or
-- no fog) and IsIndoors. Not comfy's count of ground cells with no map tile: comfy stops measuring the ground while
-- its mist is 0 (FogOn is density > 0), so that count froze at the Feathermoon ferry's and the mist never came back
-- (found in game, 1 Oct 2026). Aboard, the ground mist is taken out at once; inside, on two readings in a row (half
-- a second); and it fades back over a few seconds once you have left, after a second of being out, since IsIndoors
-- flickers in a doorway. Every zone at once, nothing to tune per zone. db.mistFix ("/atmos mistfix off", or the window) leaves comfy's mist alone.
-- A spot on land within ABOARD_YARDS of a continent's origin loses its mist too, which costs nothing.
local ABOARD_YARDS = 60
local SETTLE = 1.0
local INSIDE_HOLD = 0.4
-- comfy's position, as it writes it: x, y and z, then (since comfy's release of 4 Oct 2026) gz, the ground's height
-- under you, empty until comfy has read it, then pos=1 while it knows where you are.
local STATS_PATTERNS = {
  "x=(%-?[%d%.]+);y=(%-?[%d%.]+);z=%-?[%d%.]+;gz=%-?[%d%.]*;pos=1;",
  "x=(%-?[%d%.]+);y=(%-?[%d%.]+);z=%-?[%d%.]+;pos=1;",
}

-- comfy writes those figures into comfyStats only while the CVar holds exactly as many characters as its DLL was built
-- for, spaces to begin with, and its own addon sets that up only when its stats window is opened: after a reload it
-- comes back empty and comfy stops writing (found 1 Oct 2026, when the fix went quiet after a /reload). So this addon
-- arms it the same way. How many: 600 for comfy up to 6 Oct 2026, 1000 since its release of 7 Oct (its kStatsLen),
-- and setting the other one stopped comfy writing (and blanked comfy's own stats window) while this addon was on. So
-- both are tried: one is set, and if comfy has written nothing into it after STATS_WAIT seconds the other is; the length
-- comfy writes into is kept for the session, whatever it is. Spaces of another length, set by comfy's own stats window
-- (which knows its DLL's length, should it change again), are given the same time to fill before one of ours goes in.
-- At login it is armed afresh, so a line saved by a crash is never read as comfy's. At logout it is emptied: comfy then
-- stops writing, and the game saves a short line instead of hundreds of characters, which overflow the line Config.wtf
-- is written with and lose the next setting.
local STATS_LENS = { 1000, 600 }
local STATS_WAIT = 3
local statsLen, statsTry = nil, 1   -- the length comfy writes into (nil: not found yet), and which of ours to try next
local statsAt, statsSeen = nil, nil -- when the spaces now in it were first seen, and how many
local function ArmStats()
  local ok, v = pcall(GetCVar, "comfyStats")
  if not ok or not v then return end
  local n, now = string.len(v), GetTime()
  if statsAt and string.find(v, "%S") then statsLen = n; return end   -- comfy has written into it: that length is its own
  if statsLen and n == statsLen then return end                        -- comfy's length, set: comfy's to fill
  if statsAt and n > 0 and n ~= statsSeen then statsAt, statsSeen = now, n end   -- set anew, by comfy's window: wait
  if statsAt and n > 0 and now - statsAt < STATS_WAIT then return end
  local want = statsLen or STATS_LENS[statsTry]
  if statsAt and n == want then statsTry = 3 - statsTry; want = STATS_LENS[statsTry] end   -- ours, and nothing came
  pcall(SetCVar, "comfyStats", string.rep(" ", want))
  statsAt, statsSeen = now, want
end

local function MistWhy()
  if not db.mistFix or not CVarNum("comfyMistDensity") then return nil end   -- off, or no comfy: no mist to take out
  ArmStats()
  local ok, stats = pcall(GetCVar, "comfyStats")
  stats = ok and stats or ""
  local x, y
  for i = 1, table.getn(STATS_PATTERNS) do
    if not x then
      local _, _, px, py = string.find(stats, STATS_PATTERNS[i])
      x, y = px, py
    end
  end
  x, y = tonumber(x), tonumber(y)
  if Indoors() then return "inside" end   -- first: a dungeon near its map's origin (the Stockade) is inside, not a ship
  if x and y and math.abs(x) < ABOARD_YARDS and math.abs(y) < ABOARD_YARDS then return "aboard a ship" end
  return nil
end

-- How hard it is raining, from Indoor Weather's report: 0 without Indoor Weather, while it is switched off, before
-- its DLL has answered this session, or with storm fog switched off here. Read twice: the DLL writes it from another
-- thread, and two equal reads are never half of a write; a reading caught mid-write keeps the last one.
local function RainLevel()
  if testRain then return testRain end   -- a preview the player asked for, with or without Indoor Weather
  if not db.stormFog then return 0 end
  local ok, v = pcall(GetCVar, "IndoorRain_Storm")
  if not ok or not v then return 0 end
  local ok2, again = pcall(GetCVar, "IndoorRain_Storm")
  if not ok2 or again ~= v then return rain end
  local _, _, l, session = string.find(v, STORM_PATTERN)
  if not l then return 0 end
  local okS, s = pcall(GetCVar, "IndoorRain_Session")
  if not okS or tonumber(session) ~= tonumber(s) then return 0 end
  local okE, on = pcall(GetCVar, "IndoorRain_Enabled")
  if okE and tonumber(on) == 0 then return 0 end   -- /indoorrain off means off: no storm either
  return math.max(0, math.min(3, tonumber(l) or 0))
end

-- Indoor Weather 0.14 and older thickened the fog in storms themselves. That is this addon's job now, so theirs is
-- kept switched off (their own setting, which they read every tick), and the two can never both add a storm. It is
-- set to nil, which 0.14 reads as off while it runs and turns back into its default at its next load, when this is
-- run again before any storm (at login and every tick). false is left alone: that was the player's own choice, made
-- before this addon took the job over, and it is what the switch here starts from.
local function OwnStormFog()
  if IndoorRainDB and IndoorRainDB.fog then IndoorRainDB.fog = nil end
end

-- A fade from where every slider is now to Targets(), over the given seconds (the zone's five by default).
-- lampsOnly moves the two lamp controls and nothing else; it is only started once every other slider has
-- arrived, and it may run through a storm, since a storm never touches the lamps.
-- A lamp slider the player moved: inside, it sets the indoor value; outside, it becomes their own, kept in
-- every zone. Returns false for any other slider, which the caller lets go as before.
local function Moved(name, now)
  -- The mist the player moves is theirs (the zone leaves it alone from now on), and it stays managed, so the mist
  -- fix can still take it out aboard and inside: dropping it, as other sliders are, switched the fix off for the
  -- session while the status still claimed it (caught in review, 1 Oct 2026).
  if name == "comfyMistDensity" then
    -- In a storm what the player sets includes the storm's extra, and it is kept as their own value: taking the
    -- storm's part off here could save a 0 and missed the common path (tried and dropped after review, 1 Oct 2026).
    -- So while that storm lasts, a later fade can add the storm's extra on top again; it is gone when the rain stops.
    db.base[name] = now; db.mine[name] = true; written[name] = now
    return true
  end
  if not LAMPS[name] then return false end
  if capped then db[LAMPS[name]] = now else db.base[name] = now; db.mine[name] = true end
  written[name] = now
  return true
end

-- A lamp slider the player moved is filed on the side of the door where they moved it, so the two are looked
-- at every tick, before the indoor check can step through the door (a fade only notices the others).
local function SweepLamps()
  for name in pairs(LAMPS) do
    local now = CVarNum(name)
    if now and written[name] and math.abs(now - written[name]) > 0.5 then
      Moved(name, now); to[name] = nil   -- and a fade under way stops moving it
    end
  end
end

-- The indoor lamp values, in tenths, from /atmos indoor or the window. Inside now: the next tick fades to them.
local function SetIndoor(glow, mist)
  local function tenths(v, hi) return math.max(0, math.min(hi, math.floor(v * 10 + 0.5) / 10)) end
  if db.active then SweepLamps() end   -- a comfy slider moved just before is filed first, then this wins
  if glow then db.indoorGlow = tenths(glow, 100) end
  if mist then db.indoorMist = tenths(mist, 200) end
  if capped then capped = nil end
end

local function StartRamp(seconds, lampsOnly)
  -- a slider the player moved since this addon last wrote it is theirs: leave it and never put it back
  for i = 1, table.getn(MANAGED) do
    local name = MANAGED[i]
    local now = (not lampsOnly or LAMPS[name]) and Under(name)
    if now and written[name] and math.abs(now - written[name]) > 0.5 then
      if not Moved(name, now) then db.base[name] = nil; written[name] = nil end
    end
  end
  local t, w = Targets()
  from, to = {}, {}
  for name, v in pairs(t) do
    if not lampsOnly or LAMPS[name] then from[name] = Under(name) or v; to[name] = v end
  end
  for i = 1, 3 do washFrom[i] = washTo[i] and (washFrom[i] + (washTo[i] - washFrom[i]) * rampT) or 1 end
  washTo = w
  rampT, stepT = 0, 0
  rampLen, lampRamp = seconds or RAMP_SECONDS, lampsOnly and true or false
end

-- A mist the player moves while the fix holds it at 0 aboard or inside: theirs from now on, and the fix takes it out
-- again at once, whether or not a fade is running (it only did during one; caught in review, 1 Oct 2026).
local function SweepMist()
  if not mistOff then return end
  local now, w = CVarNum("comfyMistDensity"), written.comfyMistDensity
  if now and w and math.abs(now - w) > 0.5 then Moved("comfyMistDensity", now); StartRamp(0.4) end
end

-- Take the player's own values the first time a layer starts (under any storm, never the storm's).
local function TakeBase()
  if db.active then return true end
  local any = false
  db.base, db.mine = {}, {}
  for i = 1, table.getn(MANAGED) do
    local v = Under(MANAGED[i])
    if v then db.base[MANAGED[i]] = v; written[MANAGED[i]] = v; any = true end
  end
  if not any then db.base = {}; return false end   -- no comfyatmosphere: the wash still works
  db.active = true
  return true
end

local function RestoreNow()
  if not db then return end
  if db.active then
    for name, v in pairs(db.base or {}) do
      -- a slider the player moved since this addon last wrote it is theirs: it stays where they put it
      local now = Under(name)
      if not (now and written[name] and math.abs(now - written[name]) > 0.5) then Put(name, v) end
    end
  end
  db.active, db.base, db.mine = false, {}, {}
  written = {}
end

local function ZoneCheck()
  if not db then return end
  local z = GetRealZoneText and GetRealZoneText() or nil
  local p = db.enabled and z and PROFILES[z] or nil
  if p == profile then return end
  if PanelOpen() then return end   -- try again on the next tick
  if p and not db.active then TakeBase() end
  zone, profile = p and z or nil, p
  StartRamp()
  if UpdateMood then UpdateMood() end
end

-- Stepping in or out of a building: cap the lamps or let them go. Alone and quickly once every other slider
-- has arrived; inside the zone's fade while that is still under way. A zone's fade that a storm has paused
-- waits, and the lamps wait with it. capped is nil after the caps change: the next tick looks again.
local function IndoorCheck()
  if not db.enabled then capped = false; return end   -- switching off has already faded every slider back
  local inside = Indoors() and true or false
  if inside ~= seenInside then seenInside, seenAt = inside, GetTime() end
  if inside == capped or PanelOpen() then return end
  -- Going in acts within about half a second (the 0.3 s hold, checked on the 0.2 s tick): comfy lights every candle
  -- at full strength under a roof, so the old
  -- settle and fade flared the candles for a second and a half (seen at the Scarlet Raven, 1 Oct 2026), while acting
  -- on the first reading flapped on a doorway's brief blips (review). Coming out waits for the reading to hold for
  -- INDOOR_SETTLE; nil: the caps changed, act now.
  if capped ~= nil and GetTime() - seenAt < (inside and INDOOR_IN_HOLD or INDOOR_SETTLE) then return end
  if inside and not db.active then TakeBase() end
  capped = inside
  if not db.active then return end    -- no comfyatmosphere: nothing to hold down
  if inside then   -- the indoor values at once, so no candle burns at the outdoor ones on the way in
    for name, key in pairs(LAMPS) do if db.base[name] then Put(name, db[key]) end end
  end
  if rampT >= 1 or lampRamp then StartRamp(INDOOR_SECONDS, true) else StartRamp() end
end

-- Twice a second, since comfy's figures change once a second (see MistWhy).
local function GroundCheck(dt)
  groundSince = groundSince + dt
  if groundSince < 0.5 then return end
  groundSince = 0
  local why = MistWhy()
  if (why ~= nil) == mistOff then mistWhy = why or mistWhy; pendingAt = nil; return end
  if pendingAt == nil or (pendingWhy ~= nil) ~= (why ~= nil) then pendingWhy, pendingAt = why, GetTime() end
  -- aboard at once; inside on two readings in a row (a doorway's brief blips are one); back on after SETTLE
  local hold = (why == "aboard a ship") and 0 or (why and INSIDE_HOLD or SETTLE)
  if GetTime() - pendingAt < hold then return end
  if PanelOpen() then return end
  pendingAt = nil
  if why and not db.active and not TakeBase() then mistOff, mistWhy = true, why; return end   -- no comfy: nothing to do
  mistOff, mistWhy = why ~= nil, why
  if db.active then StartRamp(mistOff and 0.4 or 3) end
end

-- The rain starting, changing or stopping: the fog thickens or clears over STORM_SECONDS, on top of whatever
-- the zone is doing, in a zone with no mood too. Not while an options panel is open: the next tick tries again.
local function StormCheck()
  local r = RainLevel()
  if r == rain or PanelOpen() then return end
  if r > 0 and not db.active and not TakeBase() then rain = r; return end   -- no comfyatmosphere: nothing to thicken,
                                                                             -- and no asking again until the rain changes
  rain = r
  if db.active then StartRamp(STORM_SECONDS) end
end

-- An older copy of this addon from before its rename (the AtmosphereDirector folder, in the 0.1.0 zip) would run
-- beside this one: two layers moving the same sliders and two colour washes over the world. The launcher never
-- deletes a folder, so it is silenced here for this session, switched off for the next login, and the player is
-- told once that its folder can go. The two share their saved settings, so nothing of the player's is lost.
-- Its folder name sorts first, so it has normally loaded by now; the login looks again in case it had not.
local oldCopy = false
local function SilenceOldCopy(own)
  if not (IsAddOnLoaded and IsAddOnLoaded("AtmosphereDirector")) then return end
  local f = getglobal("AtmosphereDirectorFrame")
  if f and f ~= own then
    f:UnregisterAllEvents()
    f:SetScript("OnEvent", nil)
    f:SetScript("OnUpdate", nil)
    f:Hide()
  end
  if DisableAddOn then DisableAddOn("AtmosphereDirector") end
  oldCopy = true
end
SilenceOldCopy(nil)

local frame = CreateFrame("Frame", "AtmosphereDirectorFrame")
frame:RegisterEvent("VARIABLES_LOADED")
frame:RegisterEvent("PLAYER_ENTERING_WORLD")
frame:RegisterEvent("ZONE_CHANGED_NEW_AREA")
frame:RegisterEvent("PLAYER_LOGOUT")
frame:SetScript("OnEvent", function()
  if event == "VARIABLES_LOADED" then
    ShareAtmos()
    AtmosphereDirectorDB = AtmosphereDirectorDB or {}
    db = AtmosphereDirectorDB
    if db.enabled == nil then db.enabled = true end
    db.strength = db.strength or 100
    if db.indoorV ~= 2 then db.indoorGlow, db.indoorMist, db.indoorV = nil, nil, 2 end   -- the first draft's 10 and 0
    db.indoorGlow = db.indoorGlow or 5    -- the indoor values: comfy's lamp glow and its mist around lamps
    db.indoorMist = db.indoorMist or 10
    -- storm fog, on unless the player had switched Indoor Weather's off (read once, before this addon owns it)
    if db.stormFog == nil then db.stormFog = not (IndoorRainDB and IndoorRainDB.fog == false) end
    if db.mistFix == nil then db.mistFix = true end   -- see MistWhy
    ShareIndoorRain()
    OwnStormFog()                         -- before Indoor Weather 0.14 can run a storm tick of its own
    SilenceOldCopy(frame)
    if oldCopy then
      Say("an older copy of this addon was also installed (Interface\\AddOns\\AtmosphereDirector, from before its rename). "
        .. "It is switched off now and does nothing; you can delete that folder.")
    end
    db.mine = db.mine or {}
    db.base = db.base or {}
    if db.active then repairUntil = GetTime() + 30 end   -- a layer was on at the last logout or crash
  elseif event == "PLAYER_LOGOUT" then
    pcall(SetCVar, "comfyStats", "")   -- see ArmStats: comfy stops writing, and no long line of its is saved
    if db and db.active then
      -- IndoorRain puts its own snapshot back at logout too; make that snapshot the player's values.
      if IndoorRainDB and IndoorRainDB.active and IndoorRainDB.base then
        for name, v in pairs(db.base) do
          if IndoorRainDB.base[name] then IndoorRainDB.base[name] = { v = v, s = tostring(v) } end
        end
      end
      RestoreNow()
      db.active = false
    end
  else
    ZoneCheck()
  end
end)

-- The tick is three functions, not one: the 1.12 client runs Lua 5.0, which allows a function 32 upvalues, and the
-- whole tick in one closure had reached 30 (caught in review, 1 Oct 2026; a 5.1 harness allows 60 and never notices).

-- A layer still on from the last logout or a crash: put the player's values back once comfyatmosphere is up.
-- Returns true while that is still waiting.
local function RepairTick()
  if not repairUntil then return false end
  local any = false
  for i = 1, table.getn(MANAGED) do if CVarNum(MANAGED[i]) then any = true; break end end
  if any or GetTime() > repairUntil then
    repairUntil = nil; RestoreNow()
    -- the zone's fade started at login was set up from the old state: start the zone over from the player's own
    to = {}; rampT = 1; profile = nil; zone = nil; capped = nil
  end
  return true
end

-- One step of the running fade.
local function RampTick(dt)
  if rampT >= 1 then
    if db.active and not profile and capped == false and rain == 0 and not mistOff then RestoreNow() end
    return
  end
  if PanelOpen() then return end
  rampT = math.min(1, rampT + dt / rampLen)
  local again = false
  for name, v in pairs(to) do
    local now = Under(name)
    if now and written[name] and math.abs(now - written[name]) > 0.5 then
      -- the player moved it: theirs from now on, and not put back
      to[name] = nil
      if not Moved(name, now) then db.base[name] = nil
      elseif name == "comfyMistDensity" and mistOff then again = true end   -- the mist fix still holds it at 0
    elseif now then
      Put(name, from[name] + (v - from[name]) * rampT)
    end
  end
  SetWash(washFrom[1] + (washTo[1] - washFrom[1]) * rampT,
          washFrom[2] + (washTo[2] - washFrom[2]) * rampT,
          washFrom[3] + (washTo[3] - washFrom[3]) * rampT)
  if again then StartRamp(0.4) end
end

frame:SetScript("OnUpdate", function()
  if not db or RepairTick() then return end
  stepT = stepT + arg1
  if stepT < STEP then return end
  local dt = stepT; stepT = 0
  OwnStormFog()
  if db.active then SweepLamps(); SweepMist() end
  IndoorCheck()
  ZoneCheck()
  StormCheck()
  GroundCheck(dt)
  RampTick(dt)
end)

-- The Atmosphere window: every live switch and slider of the pack in one place. /atmos opens it.
local win, building

local function CV(name) local ok, v = pcall(GetCVar, name); if ok then return v end end
local function SetCV(name, v) pcall(SetCVar, name, tostring(v)) end

local function Check(parent, name, label, x, y, getter, setter)
  local c = CreateFrame("CheckButton", name, parent, "UICheckButtonTemplate")
  c:SetPoint("TOPLEFT", parent, "TOPLEFT", x, y)
  getglobal(name .. "Text"):SetText(label)
  c:SetScript("OnClick", function() if not building then setter(this:GetChecked() and true or false) end end)
  c.refresh = function() c:SetChecked(getter() and 1 or nil) end
  return c
end

local function Slide(parent, name, label, x, y, lo, hi, step, lowText, highText, getter, setter, unit)
  local s = CreateFrame("Slider", name, parent, "OptionsSliderTemplate")
  s:SetPoint("TOPLEFT", parent, "TOPLEFT", x, y)
  s:SetWidth(300)
  s:SetMinMaxValues(lo, hi)
  s:SetValueStep(step)
  getglobal(name .. "Low"):SetText(lowText)
  getglobal(name .. "High"):SetText(highText)
  local function Label(v) getglobal(name .. "Text"):SetText(label .. ": " .. v .. unit) end
  s:SetScript("OnValueChanged", function()
    local v = math.floor(this:GetValue() / step + 0.5) * step
    Label(v)
    if not building then setter(v) end
  end)
  s.refresh = function() local v = getter(); s:SetValue(v); Label(v) end
  return s
end

local function Heading(parent, text, y)
  local h = parent:CreateFontString(nil, "ARTWORK", "GameFontNormal")
  h:SetPoint("TOPLEFT", parent, "TOPLEFT", 22, y)
  h:SetText(text)
  return h
end

local function Note(parent, text, y)
  local n = parent:CreateFontString(nil, "ARTWORK", "GameFontHighlightSmall")
  n:SetPoint("TOPLEFT", parent, "TOPLEFT", 26, y)
  n:SetText(text)
  return n
end

local function BuildWindow()
  local f = CreateFrame("Frame", "AtmosphereOptions", UIParent)
  f:SetWidth(360); f:SetHeight(690)
  f:SetPoint("CENTER", UIParent, "CENTER", 0, 40)
  f:SetFrameStrata("DIALOG")
  f:SetBackdrop({ bgFile = "Interface\\DialogFrame\\UI-DialogBox-Background",
    edgeFile = "Interface\\DialogFrame\\UI-DialogBox-Border", tile = true, tileSize = 32, edgeSize = 32,
    insets = { left = 11, right = 12, top = 12, bottom = 11 } })
  f:SetMovable(true); f:EnableMouse(true); f:RegisterForDrag("LeftButton")
  f:SetScript("OnDragStart", function() this:StartMoving() end)
  f:SetScript("OnDragStop", function() this:StopMovingOrSizing() end)
  local title = f:CreateFontString(nil, "ARTWORK", "GameFontNormalLarge")
  title:SetPoint("TOP", f, "TOP", 0, -18); title:SetText("Atmosphere")
  local close = CreateFrame("Button", "AtmosphereOptionsClose", f, "UIPanelCloseButton")
  close:SetPoint("TOPRIGHT", f, "TOPRIGHT", -6, -6)
  f.controls = {}
  local function add(c) table.insert(f.controls, c); return c end

  -- Weather (IndoorRain)
  Heading(f, "Weather", -48)
  if IndoorRainDB then
    add(Check(f, "AtmosphereOptRain", "Muffled rain indoors", 20, -64,
      function() return tonumber(CV("IndoorRain_Enabled") or "1") ~= 0 end,
      function(v) SetCV("IndoorRain_Enabled", v and 1 or 0) end))
    add(Slide(f, "AtmosphereOptRainVol", "Indoor loudness", 26, -110, 0, 100, 5, "0%", "100%",
      function() return tonumber(CV("IndoorRain_Volume") or "70") or 70 end,
      function(v) SetCV("IndoorRain_Volume", v) end, "% of outside"))
    add(Check(f, "AtmosphereOptStorms", "Thunder and wind", 20, -138,
      function() return tonumber(CV("IndoorRain_Storms") or "1") ~= 0 end,
      function(v) SetCV("IndoorRain_Storms", v and 1 or 0) end))
    add(Check(f, "AtmosphereOptFlash", "Lightning flash", 20, -162,
      function() return IndoorRainDB.flash end, function(v) IndoorRainDB.flash = v end))
    add(Check(f, "AtmosphereOptFog", "Thicker fog in storms", 20, -186,
      function() return db.stormFog end, function(v) db.stormFog = v end))
    local test = CreateFrame("Button", "AtmosphereOptThunder", f, "UIPanelButtonTemplate")
    test:SetWidth(110); test:SetHeight(22)
    test:SetPoint("TOPLEFT", f, "TOPLEFT", 222, -164)
    test:SetText("Test thunder")
    test:SetScript("OnClick", function() if SlashCmdList["INDOORRAIN"] then SlashCmdList["INDOORRAIN"]("thunder") end end)
  else
    Note(f, "Indoor Weather is not installed.", -70)
  end

  -- Zone moods (this addon)
  Heading(f, "Zone moods", -222)
  add(Check(f, "AtmosphereOptZones", "Zone moods: every zone as it is meant to feel", 20, -238,
    function() return db.enabled end,
    function(v)
      db.enabled = v; profile = nil
      if v then ZoneCheck() else zone = nil; StartRamp() end
    end))
  add(Slide(f, "AtmosphereOptStrength", "Strength", 26, -284, 0, 100, 5, "your settings", "as designed",
    function() return db.strength end,
    function(v) db.strength = v; StartRamp() end, "%"))
  local mood = f:CreateFontString(nil, "ARTWORK", "GameFontHighlightSmall")
  mood:SetPoint("TOPLEFT", f, "TOPLEFT", 26, -318)
  mood:SetWidth(308); mood:SetJustifyH("LEFT")
  UpdateMood = function()
    local here = GetRealZoneText and GetRealZoneText() or ""
    local p = PROFILES[here]
    if not db.enabled then mood:SetText("Zone moods are off.")
    elseif p then mood:SetText("|cffffd100" .. here .. "|r: " .. p.mood .. " |cff999999(" .. (SOURCES[p.src] or p.src) .. ")|r")
    else mood:SetText("|cffffd100" .. here .. "|r: no mood written for this zone yet.") end
  end
  add({ refresh = UpdateMood })

  -- Indoor lamps: finer than comfy's own sliders, which step by 5 and 10
  Heading(f, "Lamps indoors", -362)
  add(Slide(f, "AtmosphereOptIndoorGlow", "Lamp glow inside", 26, -398, 0, 20, 0.5, "none", "20 (comfy's)",
    function() return db.indoorGlow end, function(v) SetIndoor(v, nil) end, ""))
  add(Slide(f, "AtmosphereOptIndoorMist", "Lamps in mist inside (only with the mist fix off)", 26, -442, 0, 50, 1, "none", "50 (comfy's)",
    function() return db.indoorMist end, function(v) SetIndoor(nil, v) end, ""))

  -- Clean screen
  Heading(f, "Clean screen", -472)
  if CleanScreenDB then
    add(Check(f, "AtmosphereOptClean", "Fade the UI away when idle", 20, -488,
      function() return CleanScreenDB.enabled end, function(v) CleanScreenDB.enabled = v end))
    add(Slide(f, "AtmosphereOptCleanTime", "Fade time", 26, -534, 5, 120, 5, "5 s", "2 min",
      function() return CleanScreenDB.seconds or 60 end,
      function(v) CleanScreenDB.seconds = math.max(5, math.min(120, v)) end, " s"))
  else
    Note(f, "Clean Screen is not installed.", -494)
  end

  Heading(f, "Fog, light and shadow", -572)
  Note(f, "comfyatmosphere's own settings. Zone moods move them for you.", -592)
  local cs = CreateFrame("Button", "AtmosphereOptComfy", f, "UIPanelButtonTemplate")
  cs:SetWidth(150); cs:SetHeight(22)
  cs:SetPoint("TOPLEFT", f, "TOPLEFT", 24, -610)
  cs:SetText("comfy's settings")
  cs:SetScript("OnClick", function() if not ComfyOptions() then Say("comfyatmosphere is not installed.") end end)
  add(Check(f, "AtmosphereOptMistFix", "Clear comfy's mist where it misjudges the ground", 20, -638,
    function() return db.mistFix end, function(v) db.mistFix = v end))

  table.insert(UISpecialFrames, "AtmosphereOptions")   -- Escape closes it
  f:Hide()
  return f
end

local function ToggleWindow()
  win = win or BuildWindow()
  if win:IsShown() then win:Hide(); return end
  building = true
  for i = 1, table.getn(win.controls) do win.controls[i].refresh() end
  building = false
  win:Show()
end

local comfyAtmos                    -- comfy's own /atmos handler, once the two share it

-- This addon's own words. Returns false for anything else, which then goes on to comfy.
local function OwnCommand(msg)
  if not db then return true end
  msg = string.lower(string.gsub(msg or "", "^%s*(.-)%s*$", "%1"))
  local n = tonumber(msg)
  if msg == "" then ToggleWindow()
  elseif msg == "on" then db.enabled = true; profile = nil; ZoneCheck(); Say("on.")
  elseif msg == "off" then db.enabled = false; profile = nil; zone = nil; StartRamp(); Say("zone moods off: fading back to your own settings." .. (db.stormFog and IndoorRainDB and " Storms still thicken the fog; switch that off in the window." or ""))
  elseif n then
    db.strength = math.max(0, math.min(100, n)); StartRamp(); Say("strength " .. db.strength .. "%.")
  elseif msg == "status" then
    local here = GetRealZoneText and GetRealZoneText() or ""
    if PROFILES[here] then Say(here .. ": " .. PROFILES[here].mood) end
    local parts = {}
    for i = 1, table.getn(MANAGED) do
      local v = CVarNum(MANAGED[i])
      if v then table.insert(parts, MANAGED[i] .. "=" .. tostring(v)) end
    end
    Say((db.enabled and "on" or "off") .. ", strength " .. db.strength .. "%, layer: " .. (zone or "none")
      .. ", storm fog " .. (not IndoorRainDB and "needs Indoor Weather" or (db.stormFog and "on" or "off"))
      .. (rain > 0 and (testRain and (" (storm preview, level " .. rain .. ")") or (" (raining, level " .. rain .. ")")) or "")
      .. (mistOff and (", mist fix on (" .. (mistWhy or "") .. ": comfy misjudges the ground here; its mist now "
        .. tostring(CVarNum("comfyMistDensity")) .. ")") or "")
      .. (StormActive() and " (Indoor Weather's own storm is still fading out underneath)" or "") .. ". "
      .. table.concat(parts, " "))
    local inside, source = Indoors()
    Say("indoor lamps: glow " .. db.indoorGlow .. ", mist " .. db.indoorMist .. ". "
      .. (not source and "No indoor detection (it needs SuperWoW or ClassicAPI), so lamps are never set apart indoors."
        or ((capped and db.active and "Inside, lamps at the indoor values now" or (inside and "Inside" or "Outside"))
          .. " (from " .. source .. ")."))
      .. (StormActive() and " Lamps have nothing to do with the storm, so the indoor values hold through it." or ""))
  elseif msg == "mistfix on" or msg == "mistfix off" then
    db.mistFix = (msg == "mistfix on")
    Say(db.mistFix and "mist fix on: comfy's ground mist is taken out aboard ships and inside."
      or "mist fix off: comfy's ground mist is left alone everywhere, walls and all.")
  elseif string.find(msg, "^rain") then
    local _, _, n = string.find(msg, "^rain%s+(%d)$")
    n = tonumber(n)
    if n and n <= 3 then
      testRain = (n > 0) and n or nil
      Say(testRain and ("storm preview: the fog of rain level " .. n .. " (1 light, 2 steady, 3 heavy), until /atmos rain 0 or a reload.")
        or "storm preview off: the real weather again.")
    else
      Say("/atmos rain 0-3: preview a storm's fog (1 light, 2 steady, 3 heavy); 0 ends the preview.")
    end
  elseif string.find(msg, "^indoor") then
    local _, _, glow, mist = string.find(msg, "^indoor%s*(%S*)%s*(%S*)$")
    glow, mist = tonumber(glow or ""), tonumber(mist or "")
    if glow then
      SetIndoor(glow, mist)
    elseif msg ~= "indoor" then
      Say("/atmos indoor <glow 0-100> [<mist 0-200>], tenths too (2.5): how bright lamps are inside. comfy's own are 20 and 50.")
    end
    Say("indoor lamps: glow " .. db.indoorGlow .. ", mist " .. db.indoorMist .. ".")
  else
    return false
  end
  return true
end

-- comfy's settings window, through its own command.
function ComfyOptions()
  local theirs = comfyAtmos or SlashCmdList["COMFYATMOS"]
  if not theirs then return false end
  theirs("options")
  return true
end

-- Share /atmos with comfy (see the top of this file). Run once every addon has loaded; safe to run again.
ShareAtmos = function()
  local theirs = SlashCmdList["COMFYATMOS"]
  if theirs and not comfyAtmos then
    comfyAtmos = theirs
    SlashCmdList["COMFYATMOS"] = function(msg) if not OwnCommand(msg) then comfyAtmos(msg) end end
  elseif not theirs then
    SLASH_AZATMOS2 = "/atmos"           -- no comfy: /atmos is this addon's alone
  end
end

-- Indoor Weather's "/indoorrain fog on|off" sets the storm fog switch here, since the storm's fog is made here now;
-- every other word goes on to Indoor Weather, and its status gains a line saying so. Safe to run again.
local indoorRainSlash
ShareIndoorRain = function()
  local theirs = SlashCmdList["INDOORRAIN"]
  if not theirs or indoorRainSlash then return end
  indoorRainSlash = theirs
  SlashCmdList["INDOORRAIN"] = function(msg)
    local m = string.lower(string.gsub(msg or "", "^%s*(.-)%s*$", "%1"))
    local _, _, word = string.find(m, "^fog%s+(%a+)$")
    if db and (word == "on" or word == "off") then
      db.stormFog = (word == "on")
      Say("storm fog " .. word .. ". Azeroth Atmosphere makes the storm's fog now (/atmos for its window).")
      return
    end
    indoorRainSlash(msg)
    if db and m == "status" then
      Say("storm fog " .. (db.stormFog and "on" or "off") .. ", made by Azeroth Atmosphere"
        .. (rain > 0 and (", raining at level " .. rain) or "") .. ".")
    end
  end
end

SLASH_AZATMOS1 = "/aa"
SlashCmdList["AZATMOS"] = function(msg)
  if OwnCommand(msg) then return end
  -- every other word goes on to comfy, as it does under /atmos (/aa stats, /aa options, /aa debug and its tuning)
  if comfyAtmos then comfyAtmos(msg); return end
  Say("/aa: the window. /aa on | off | status | 0-100 | indoor <glow> [<mist>] | rain 0-3 (a storm preview) | mistfix on | off.")
end
