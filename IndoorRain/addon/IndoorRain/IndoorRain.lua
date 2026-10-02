-- Indoor Rain, the addon half. The DLL (IndoorRain.dll, loaded from dlls.txt) plays the
-- muffled rain, snow or sandstorm through the game's own sound engine. This addon does the two things only
-- the game's own thread can do safely: it tells the DLL whether you are under a roof
-- (IsIndoors from SuperWoW or ClassicAPI, checked four times a second, written to the
-- CVar IndoorRain_Indoors only when it changes) and it holds the settings as CVars, which
-- the client saves for you.
--
-- Storms (0.5, with IndoorRain.dll 0.13): the DLL plays thunder and gusts while it rains and
-- reports each lightning strike in IndoorRain_Storm, a CVar it overwrites in place. This addon
-- flashes the sky when a strike lands (the thunder follows by the strike's distance). It must never
-- SetCVar IndoorRain_Storm once the DLL may be writing it. Off (/indoorrain off) means off: no flash.
--
-- Since 0.14.1 the storm's fog is made by Azeroth Atmosphere, which reads the rain level from the same
-- CVar, so every fog decision in the pack is made in one place. This addon no longer touches comfy's
-- sliders; it only puts back a player's own values that a 0.14 storm was holding at the last logout.
--
-- Since 0.14 this folder holds no sound files: the DLL builds every sound it plays from the
-- client's own archives, the rain loops when the game starts and the rest when first wanted
-- (the storm's when it comes on, a snow or sand loop the first time it is wanted indoors).
-- Nothing here ever named one. GPL-3.0.

local POLL = 0.25
local since = 0
local lastIndoors = nil
local ready = false

local STORM_TEMPLATE = "IRS1:k0:l0:n000:d00"
local STORM_PATTERN = "^IRS1:k(%d):l(%d):n(%d%d)(%d):d(%d%d)$"
local STORM_POLL = 0.05
local stormSince = 0
local session = nil      -- our code; the DLL writes it back once it is running
local trusted = false    -- the DLL has answered: the rain level in IndoorRain_Storm is current
local lastStrike = nil
local rainLevel = 0

local function Say(msg)
  DEFAULT_CHAT_FRAME:AddMessage("|cff7fb2ffIndoor Rain:|r " .. msg)
end

local function Indoors()
  if not IsIndoors then return nil end
  if IsIndoors() then return 1 else return 0 end
end

-- The mod's own switch, read the way the DLL reads it: "0" is off.
local function On()
  return tonumber(GetCVar("IndoorRain_Enabled")) ~= 0
end

local function RegisterSettings()
  RegisterCVar("IndoorRain_Enabled", "1")
  RegisterCVar("IndoorRain_Volume", "70")
  RegisterCVar("IndoorRain_Indoors", "0")
  RegisterCVar("IndoorRain_Storms", "1")
  RegisterCVar("IndoorRain_Session", "0")
  RegisterCVar("IndoorRain_Thunder", "0")
  RegisterCVar("IndoorRain_Storm", STORM_TEMPLATE)
end

local function Push()
  local ind = Indoors()
  if ind == nil then ind = 0 end
  if ind ~= lastIndoors then
    SetCVar("IndoorRain_Indoors", tostring(ind))
    lastIndoors = ind
  end
end

-- Lightning: a screen-wide flash over the world and under the interface, blue-white and brighter
-- towards the sky, in two or three short pulses. Closer strikes are brighter; indoors it is the light
-- through the windows. Everything drawn by Lua lies over the finished picture, so this lightens sky
-- and ground alike.
local FLASH_PEAK = { 0.55, 0.45, 0.32, 0.20, 0.13 }   -- by distance: close, near, mid, far, farther
local flash = CreateFrame("Frame", nil, WorldFrame)
flash:SetFrameStrata("BACKGROUND")
flash:SetFrameLevel(0)
flash:SetAllPoints(WorldFrame)
local flashTex = flash:CreateTexture(nil, "BACKGROUND")
flashTex:SetAllPoints(flash)
flashTex:SetTexture(1, 1, 1)
flashTex:SetBlendMode("ADD")
flashTex:SetGradientAlpha("VERTICAL", 0.72, 0.80, 1.0, 0.30, 0.90, 0.94, 1.0, 1.0)
flash:Hide()
local flashT, flashPeak, flashPulses = 0, 0, {}

local function Flash(k, indoors)
  local peak = FLASH_PEAK[k] or 0.2
  if indoors then peak = peak * 0.35 end
  local p = { { 0, 0.07 + math.random() * 0.05, 1.0 } }
  local t = p[1][2] + 0.04 + math.random() * 0.06
  p[2] = { t, 0.05 + math.random() * 0.04, 0.45 + math.random() * 0.25 }
  if k <= 2 or math.random() < 0.35 then
    t = t + p[2][2] + 0.05 + math.random() * 0.12
    p[3] = { t, 0.04 + math.random() * 0.05, 0.25 + math.random() * 0.25 }
  end
  flashPulses, flashPeak, flashT = p, peak, 0
  flash:SetAlpha(0)
  flash:Show()
end

flash:SetScript("OnUpdate", function()
  flashT = flashT + arg1
  local a, last = 0, 0
  for i = 1, table.getn(flashPulses) do
    local s, len, str = flashPulses[i][1], flashPulses[i][2], flashPulses[i][3]
    if flashT >= s and flashT < s + len then a = str * (1 - 0.5 * (flashT - s) / len) end
    last = s + len
  end
  if flashT >= last then flash:Hide(); return end
  flash:SetAlpha(a * flashPeak)
end)

-- A storm under 0.14 held comfy's sliders and kept the player's own values in IndoorRainDB until the rain was
-- over. If one was still on at the last logout, those values go back once comfyatmosphere is up; nothing else
-- here writes comfy's sliders any more (Azeroth Atmosphere makes the storm's fog now).
local repairUntil = nil

local function CVarNum(name)
  local ok, v = pcall(GetCVar, name)
  if not ok or v == nil then return nil end
  return tonumber(v)
end

local function RestoreNow()
  local db = IndoorRainDB
  if not db then return end
  if db.active and db.base then
    for name, b in pairs(db.base) do pcall(SetCVar, name, b.s) end
  end
  db.active, db.base = false, nil
end

local function RepairTick()
  if not repairUntil then return end
  local up = false
  for name in pairs(IndoorRainDB.base or {}) do if CVarNum(name) then up = true; break end end
  if up or GetTime() > repairUntil then repairUntil = nil; RestoreNow() end
end

local function ReadStorm()
  local v = GetCVar("IndoorRain_Storm")
  -- read twice: the DLL writes single digits from another thread, and two equal reads are never half of a write
  if not v or GetCVar("IndoorRain_Storm") ~= v then return end
  local _, _, k, l, e, n = string.find(v, STORM_PATTERN)
  if not k then return end
  if tonumber(e) ~= session then trusted = false; return end
  rainLevel = tonumber(l)
  if not trusted then trusted = true; lastStrike = n; return end
  if n ~= lastStrike then
    lastStrike = n
    if IndoorRainDB.flash and On() then Flash(tonumber(k), lastIndoors == 1) end
  end
end

local function StartSession()
  -- IndoorRain_Storm must have the template's exact width for the DLL to write it. Only a value of the
  -- wrong shape is reset, and the DLL never writes one of those, so this cannot race it.
  local v = GetCVar("IndoorRain_Storm")
  local old = nil
  if not v or not string.find(v, STORM_PATTERN) then SetCVar("IndoorRain_Storm", STORM_TEMPLATE)
  else local _, _, _, _, e = string.find(v, STORM_PATTERN); old = tonumber(e) end
  session = math.mod(math.floor(GetTime() * 1000), 90) + 10
  if session == old then session = math.mod(session - 9, 90) + 10 end
  trusted, lastStrike = false, nil
  SetCVar("IndoorRain_Session", tostring(session))
end

local frame = CreateFrame("Frame")
frame:RegisterEvent("VARIABLES_LOADED")
frame:RegisterEvent("PLAYER_ENTERING_WORLD")
frame:RegisterEvent("PLAYER_LOGOUT")
frame:SetScript("OnEvent", function()
  if event == "VARIABLES_LOADED" then
    RegisterSettings()
    if type(IndoorRainDB) ~= "table" then IndoorRainDB = {} end
    if IndoorRainDB.flash == nil then IndoorRainDB.flash = true end
    if IndoorRainDB.active then repairUntil = GetTime() + 60 end
    StartSession()
    ready = true
    if not IsIndoors then
      Say("IsIndoors is missing, so SuperWoW or ClassicAPI is not loaded. Indoor Rain cannot tell when you are inside.")
    end
  elseif event == "PLAYER_LOGOUT" then
    RestoreNow()
  end
  lastIndoors = nil
end)
frame:SetScript("OnUpdate", function()
  if not ready then return end
  stormSince = stormSince + arg1
  if stormSince >= STORM_POLL then stormSince = 0; ReadStorm() end
  RepairTick()
  since = since + arg1
  if since < POLL then return end
  since = 0
  Push()
end)

local function OnOff(word, current)
  if word == "on" then return true elseif word == "off" then return false end
  return current
end

SLASH_INDOORRAIN1 = "/indoorrain"
SlashCmdList["INDOORRAIN"] = function(msg)
  msg = string.lower(msg or "")
  local _, _, vol = string.find(msg, "^volume%s+(%d+)")
  local _, _, what, word = string.find(msg, "^(%a+)%s+(%a+)$")
  if msg == "on" then
    SetCVar("IndoorRain_Enabled", "1"); Say("on.")
  elseif msg == "off" then
    SetCVar("IndoorRain_Enabled", "0"); flash:Hide(); Say("off.")
  elseif vol then
    vol = math.max(0, math.min(100, tonumber(vol)))
    SetCVar("IndoorRain_Volume", tostring(vol)); Say("volume " .. vol .. "% of the weather outside.")
  elseif what == "storm" and (word == "on" or word == "off") then
    SetCVar("IndoorRain_Storms", word == "on" and "1" or "0"); Say("thunder and wind " .. word .. ".")
  elseif what == "fog" then
    Say("the storm's fog is made by Azeroth Atmosphere now: /atmos (or /aa) for its window.")
  elseif what == "flash" and (word == "on" or word == "off") then
    IndoorRainDB.flash = OnOff(word, IndoorRainDB.flash); Say("lightning flashes " .. word .. ".")
  elseif msg == "thunder" then
    local n = math.mod((tonumber(GetCVar("IndoorRain_Thunder")) or 0) + 1, 1000)
    SetCVar("IndoorRain_Thunder", tostring(n)); Say("a test strike.")
  elseif msg == "status" then
    Say("enabled=" .. tostring(GetCVar("IndoorRain_Enabled")) .. " volume=" .. tostring(GetCVar("IndoorRain_Volume")) .. "%"
      .. " indoors=" .. tostring(GetCVar("IndoorRain_Indoors")) .. " IsIndoors=" .. tostring(Indoors())
      .. " storm=" .. tostring(GetCVar("IndoorRain_Storms"))
      .. " flash=" .. (IndoorRainDB.flash and "on" or "off") .. " rain=" .. tostring(rainLevel)
      .. (trusted and "" or " (the DLL has not answered: is IndoorRain.dll 0.14 loaded?)")
      .. ". The DLL writes Logs\\IndoorRain.log.")
  else
    Say("/indoorrain on | off | volume 0-100 (percent of the weather outside, default 70) | storm on | storm off"
      .. " | flash on | flash off | thunder (a test strike) | status")
  end
end
