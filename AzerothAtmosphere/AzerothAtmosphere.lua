-- Atmosphere Director: each zone gets its own layer of fog, light and colour, the way Blizzard authors a
-- zone's mood in WoW Forever. Every zone, city, dungeon and raid in the client has a written intent (its
-- mood: Duskwood is dark, moody and haunted) and a preset that goes for it, in Zones.lua. Where Forever
-- footage exists the preset is measured from it; elsewhere it follows the zone's known look. Weather
-- layers on top: while IndoorRain's storm is thickening the fog, this addon waits, so the two never fight
-- over a slider.
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
--   /aa               the same, and always this addon's
-- comfyatmosphere (since 0.7) answers to /atmos too, for its settings window and its tuning commands. The two
-- share it: plain /atmos and this addon's own words come here, every other word goes on to comfy (/atmos options,
-- /atmos debug, /atmos stats and its tuning all still work). Without comfy, /atmos is this addon's alone.

local PROFILES = AtmosphereDirector_Zones or {}   -- Zones.lua: every zone's mood and preset

local RAMP_SECONDS = 5
local INDOOR_SECONDS = 1.0          -- stepping through a door: the lamps settle in a second, as Indoor Weather does
local INDOOR_SETTLE = 0.75          -- seconds the reading must hold first: IsIndoors flickers in a doorway
local STEP = 0.2
-- The two lamp controls the indoor cap holds down, each with the setting that holds its cap.
local LAMPS = { comfyLampGlow = "indoorGlow", comfyMistLamps = "indoorMist" }
-- Every control any zone sets, gathered from Zones.lua so the two can never disagree, and the lamps, which the
-- indoor cap needs even where no zone sets them. comfy 0.6 and older have comfyFogThickness; 0.7 and 0.8 have
-- the mist, shadow, lamp and night controls instead. A control the installed comfy does not have reads as nil
-- and is left alone, so one table serves every version.
local MANAGED = {}
do
  local seen = {}
  for name in pairs(LAMPS) do seen[name] = true; table.insert(MANAGED, name) end
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

-- Where the layer wants each slider now: between the player's own value and the zone's, by strength. Indoors
-- the two lamp controls go no higher than their caps.
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
  if profile and profile.wash then
    for i = 1, 3 do w[i] = 1 - (1 - profile.wash[i]) * k end
  end
  return t, w
end

-- A fade from where every slider is now to Targets(), over the given seconds (the zone's five by default).
-- lampsOnly moves the two lamp controls and nothing else; it is only started once every other slider has
-- arrived, and it may run through a storm, since a storm never touches the lamps.
-- A lamp slider the player moved: inside, it sets the indoor value; outside, it becomes their own, kept in
-- every zone. Returns false for any other slider, which the caller lets go as before.
local function Moved(name, now)
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
    local now = (not lampsOnly or LAMPS[name]) and not StormHolds(name) and CVarNum(name)
    if now and written[name] and math.abs(now - written[name]) > 0.5 then
      if not Moved(name, now) then db.base[name] = nil; written[name] = nil end
    end
  end
  local t, w = Targets()
  from, to = {}, {}
  for name, v in pairs(t) do
    if not lampsOnly or LAMPS[name] then from[name] = CVarNum(name) or v; to[name] = v end
  end
  for i = 1, 3 do washFrom[i] = washTo[i] and (washFrom[i] + (washTo[i] - washFrom[i]) * rampT) or 1 end
  washTo = w
  rampT, stepT = 0, 0
  rampLen, lampRamp = seconds or RAMP_SECONDS, lampsOnly and true or false
end

-- Take the player's own values the first time a layer starts (never while a storm has moved them).
local function TakeBase()
  if db.active then return true end
  local any = false
  db.base, db.mine = {}, {}
  for i = 1, table.getn(MANAGED) do
    local v = CVarNum(MANAGED[i])
    if v then db.base[MANAGED[i]] = v; written[MANAGED[i]] = v; any = true end
  end
  if not any then db.base = {}; return false end   -- no comfyatmosphere: the wash still works
  db.active = true
  return true
end

local function RestoreNow()
  if not db then return end
  if db.active then
    for name, v in pairs(db.base or {}) do pcall(SetCVar, name, tostring(v)) end
  end
  db.active, db.base, db.mine = false, {}, {}
  written = {}
end

local function ZoneCheck()
  if not db then return end
  local z = GetRealZoneText and GetRealZoneText() or nil
  local p = db.enabled and z and PROFILES[z] or nil
  if p == profile then return end
  if StormActive() or PanelOpen() then return end   -- try again on the next tick
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
  if capped ~= nil and GetTime() - seenAt < INDOOR_SETTLE then return end   -- nil: the caps changed, act now
  if rampT < 1 and not lampRamp and StormActive() then return end   -- try again on the next tick
  if inside and not db.active then
    if StormActive() then return end   -- as for a zone: never take the player's values while a storm has moved them
    TakeBase()
  end
  capped = inside
  if not db.active then return end    -- no comfyatmosphere: nothing to hold down
  if rampT >= 1 or lampRamp then StartRamp(INDOOR_SECONDS, true) else StartRamp() end
end

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
    db.mine = db.mine or {}
    db.base = db.base or {}
    if db.active then repairUntil = GetTime() + 30 end   -- a layer was on at the last logout or crash
  elseif event == "PLAYER_LOGOUT" then
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

frame:SetScript("OnUpdate", function()
  if not db then return end
  if repairUntil then
    local any = false
    for i = 1, table.getn(MANAGED) do if CVarNum(MANAGED[i]) then any = true; break end end
    if any or GetTime() > repairUntil then repairUntil = nil; RestoreNow() end
    return
  end
  stepT = stepT + arg1
  if stepT < STEP then return end
  local dt = stepT; stepT = 0
  if db.active then SweepLamps() end
  IndoorCheck()
  ZoneCheck()
  if rampT >= 1 then
    if db.active and not profile and capped == false and not StormActive() then RestoreNow() end
    return
  end
  if (StormActive() and not lampRamp) or PanelOpen() then return end   -- the lamps alone may move in a storm
  rampT = math.min(1, rampT + dt / rampLen)
  for name, v in pairs(to) do
    local now = CVarNum(name)
    if now and written[name] and math.abs(now - written[name]) > 0.5 and not StormHolds(name) then
      -- the player moved it: theirs from now on, and not put back
      to[name] = nil
      if not Moved(name, now) then db.base[name] = nil end
    elseif now then
      SetNum(name, from[name] + (v - from[name]) * rampT)
    end
  end
  SetWash(washFrom[1] + (washTo[1] - washFrom[1]) * rampT,
          washFrom[2] + (washTo[2] - washFrom[2]) * rampT,
          washFrom[3] + (washTo[3] - washFrom[3]) * rampT)
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
      function() return IndoorRainDB.fog end, function(v) IndoorRainDB.fog = v end))
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
  add(Slide(f, "AtmosphereOptIndoorMist", "Lamps in mist inside", 26, -442, 0, 50, 1, "none", "50 (comfy's)",
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
  elseif msg == "off" then db.enabled = false; profile = nil; zone = nil; StartRamp(); Say("off: fading back to your own settings.")
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
      .. (StormActive() and " (a storm layer is on; waiting)" or "") .. ". " .. table.concat(parts, " "))
    local inside, source = Indoors()
    Say("indoor lamps: glow " .. db.indoorGlow .. ", mist " .. db.indoorMist .. ". "
      .. (not source and "No indoor detection (it needs SuperWoW or ClassicAPI), so lamps are never set apart indoors."
        or ((capped and db.active and "Inside, lamps at the indoor values now" or (inside and "Inside" or "Outside"))
          .. " (from " .. source .. ")."))
      .. (StormActive() and " Lamps have nothing to do with the storm, so the indoor values hold through it." or ""))
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

SLASH_AZATMOS1 = "/aa"
SlashCmdList["AZATMOS"] = function(msg)
  if not OwnCommand(msg) then Say("/aa: the window. /aa on | off | status | 0-100 | indoor <glow> [<mist>].") end
end
