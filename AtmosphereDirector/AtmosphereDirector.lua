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
--   /atmos            the Atmosphere window: every switch and slider of the pack in one place
--   /atmos status     status
--   /atmos on | off   the whole layer
--   /atmos 0-100      strength: 100 is the zone as designed, 0 is the player's own settings
--   /aa               the same, and always this addon's
-- comfyatmosphere (since 0.7) answers to /atmos too, for its settings window and its tuning commands. The two
-- share it: plain /atmos and this addon's own words come here, every other word goes on to comfy (/atmos options,
-- /atmos debug, /atmos stats and its tuning all still work). Without comfy, /atmos is this addon's alone.

local PROFILES = AtmosphereDirector_Zones or {}   -- Zones.lua: every zone's mood and preset

local RAMP_SECONDS = 5
local STEP = 0.2
-- Every control any zone sets, gathered from Zones.lua so the two can never disagree. comfy 0.6 and older have
-- comfyFogThickness; 0.7 and 0.8 have the mist, shadow, lamp and night controls instead. A control the installed
-- comfy does not have reads as nil and is left alone, so one table serves every version.
local MANAGED = {}
do
  local seen = {}
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
local repairUntil

local function Say(msg) DEFAULT_CHAT_FRAME:AddMessage("|cff88bbccatmosphere|r: " .. msg) end

local function CVarNum(name)
  local ok, v = pcall(GetCVar, name)
  if not ok or v == nil then return nil end
  return tonumber(v)
end

local function SetNum(name, v)
  local s = tostring(math.floor(v + 0.5))
  if pcall(SetCVar, name, s) then written[name] = tonumber(s); return true end
  return false
end

local function StormActive()
  return IndoorRainDB and IndoorRainDB.active and true or false
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

-- Where the layer wants each slider now: between the player's own value and the zone's, by strength.
local function Targets()
  local t, w = {}, { 1, 1, 1 }
  local k = (db.enabled and profile) and Strength() or 0
  for i = 1, table.getn(MANAGED) do
    local name = MANAGED[i]
    local base = db.base[name]
    if base then
      local zv = profile and profile.cvars[name]
      t[name] = zv and (base + (zv - base) * k) or base
    end
  end
  if profile and profile.wash then
    for i = 1, 3 do w[i] = 1 - (1 - profile.wash[i]) * k end
  end
  return t, w
end

local function StartRamp()
  -- a slider the player moved since this addon last wrote it is theirs: leave it and never put it back
  for i = 1, table.getn(MANAGED) do
    local name = MANAGED[i]
    local now = CVarNum(name)
    if now and written[name] and math.abs(now - written[name]) > 0.5 then
      db.base[name] = nil; written[name] = nil
    end
  end
  local t, w = Targets()
  from, to = {}, {}
  for name, v in pairs(t) do from[name] = CVarNum(name) or v; to[name] = v end
  for i = 1, 3 do washFrom[i] = washTo[i] and (washFrom[i] + (washTo[i] - washFrom[i]) * rampT) or 1 end
  washTo = w
  rampT, stepT = 0, 0
end

-- Take the player's own values the first time a layer starts (never while a storm has moved them).
local function TakeBase()
  if db.active then return true end
  local any = false
  db.base = {}
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
  db.active, db.base = false, {}
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
  ZoneCheck()
  if rampT >= 1 then
    if db.active and not profile and not StormActive() then RestoreNow() end
    return
  end
  if StormActive() or PanelOpen() then return end
  rampT = math.min(1, rampT + dt / RAMP_SECONDS)
  for name, v in pairs(to) do
    local now = CVarNum(name)
    if now and written[name] and math.abs(now - written[name]) > 0.5 then
      -- the player moved it: theirs from now on, and not put back
      to[name] = nil; db.base[name] = nil
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
  f:SetWidth(360); f:SetHeight(580)
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

  -- Clean screen
  Heading(f, "Clean screen", -362)
  if CleanScreenDB then
    add(Check(f, "AtmosphereOptClean", "Fade the UI away when idle", 20, -378,
      function() return CleanScreenDB.enabled end, function(v) CleanScreenDB.enabled = v end))
    add(Slide(f, "AtmosphereOptCleanTime", "Fade time", 26, -424, 5, 120, 5, "5 s", "2 min",
      function() return CleanScreenDB.seconds or 60 end,
      function(v) CleanScreenDB.seconds = math.max(5, math.min(120, v)) end, " s"))
  else
    Note(f, "Clean Screen is not installed.", -384)
  end

  Heading(f, "Fog, light and shadow", -462)
  Note(f, "comfyatmosphere's own settings. Zone moods move them for you.", -482)
  local cs = CreateFrame("Button", "AtmosphereOptComfy", f, "UIPanelButtonTemplate")
  cs:SetWidth(150); cs:SetHeight(22)
  cs:SetPoint("TOPLEFT", f, "TOPLEFT", 24, -500)
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
  if not OwnCommand(msg) then Say("/aa: the window. /aa on | off | status | 0-100.") end
end
