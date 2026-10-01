-- Clean Screen: the whole UI fades from 100% to 0% over a minute of inactivity, so the world is all that is
-- left on screen. Any activity brings it straight back.
--
-- Activity: the mouse moving, the mouse resting over any part of the UI, the character moving, combat,
-- casting, a new target, a whisper, the chat box open, or any window open (map, bags, character,
-- spellbook, and every other window Escape closes), so a window being read never fades away.
--
-- At 0% the UI is hidden outright, the same as Alt+Z, because a few addons (Gatherer's minimap icons) set
-- their own transparency every update and would otherwise stay on screen. A UI the player hid with Alt+Z
-- is left hidden.
--
--   /cleanscreen            the settings window: on/off, and the fade time from 5 seconds to 2 minutes
--   /cleanscreen on | off
--   /cleanscreen <seconds>  the fade time (5 to 120)
--   /cleanscreen status

local POLL = 0.1
local FADE_IN = 0.4          -- seconds to come back after activity
local MIN_SECONDS, MAX_SECONDS, DEFAULT_SECONDS = 5, 120, 60

local db
local idle, alpha = 0, 1
local hiddenByUs = false
local lastX, lastY, lastPX, lastPY, lastPZ
local busy = false           -- combat or casting
local t = 0
local options                -- the settings window, built on first use

local function Say(msg) DEFAULT_CHAT_FRAME:AddMessage("|cff99ccffclean screen|r: " .. msg) end

local function Clamp(n) return math.max(MIN_SECONDS, math.min(MAX_SECONDS, math.floor(n + 0.5))) end

local function AnyWindowOpen()
  if WorldMapFrame and WorldMapFrame:IsVisible() then return true end
  if ChatFrameEditBox and ChatFrameEditBox:IsVisible() then return true end
  if UISpecialFrames then
    for i = 1, table.getn(UISpecialFrames) do
      local f = getglobal(UISpecialFrames[i])
      if f and f:IsVisible() then return true end
    end
  end
  for i = 1, (NUM_CONTAINER_FRAMES or 12) do
    local f = getglobal("ContainerFrame" .. i)
    if f and f:IsVisible() then return true end
  end
  return false
end

-- The mouse resting over any part of the UI: the player is looking at it.
local function MouseOverUI()
  if not GetMouseFocus then return false end
  local f = GetMouseFocus()
  return f ~= nil and f ~= WorldFrame
end

local function PlayerMoved()
  local x, y, z
  if UnitPosition then                    -- SuperWoW: world coordinates, works everywhere
    local ok, a, b, c = pcall(UnitPosition, "player")
    if ok then x, y, z = a, b, c end
  end
  if not x and GetPlayerMapPosition then
    x, y = GetPlayerMapPosition("player"); z = 0
  end
  if not x then return false end
  local moved = lastPX ~= nil and (math.abs(x - lastPX) > 0.0001 or math.abs(y - lastPY) > 0.0001
    or math.abs((z or 0) - (lastPZ or 0)) > 0.0001)
  lastPX, lastPY, lastPZ = x, y, z
  return moved
end

local function MouseMoved()
  local x, y = GetCursorPosition()
  local moved = lastX ~= nil and (math.abs(x - lastX) > 0.5 or math.abs(y - lastY) > 0.5)
  lastX, lastY = x, y
  return moved
end

local function Apply(a)
  if a <= 0 then
    alpha = 0
    UIParent:SetAlpha(0)
    if UIParent:IsShown() then UIParent:Hide(); hiddenByUs = true end
    return
  end
  if hiddenByUs then UIParent:Show(); hiddenByUs = false end
  alpha = a
  UIParent:SetAlpha(a)
end

local function Restore()
  if hiddenByUs then UIParent:Show(); hiddenByUs = false end
  alpha = 1
  UIParent:SetAlpha(1)
end

local function BuildOptions()
  local f = CreateFrame("Frame", "CleanScreenOptions", UIParent)
  f:SetWidth(300); f:SetHeight(140)
  f:SetPoint("CENTER", UIParent, "CENTER", 0, 120)
  f:SetFrameStrata("DIALOG")
  f:SetBackdrop({ bgFile = "Interface\\DialogFrame\\UI-DialogBox-Background",
    edgeFile = "Interface\\DialogFrame\\UI-DialogBox-Border", tile = true, tileSize = 32, edgeSize = 32,
    insets = { left = 11, right = 12, top = 12, bottom = 11 } })
  f:SetMovable(true); f:EnableMouse(true); f:RegisterForDrag("LeftButton")
  f:SetScript("OnDragStart", function() this:StartMoving() end)
  f:SetScript("OnDragStop", function() this:StopMovingOrSizing() end)

  local title = f:CreateFontString(nil, "ARTWORK", "GameFontNormal")
  title:SetPoint("TOP", f, "TOP", 0, -18); title:SetText("Clean Screen")

  local close = CreateFrame("Button", "CleanScreenOptionsClose", f, "UIPanelCloseButton")
  close:SetPoint("TOPRIGHT", f, "TOPRIGHT", -6, -6)

  local check = CreateFrame("CheckButton", "CleanScreenOptionsEnable", f, "UICheckButtonTemplate")
  check:SetPoint("TOPLEFT", f, "TOPLEFT", 20, -38)
  getglobal("CleanScreenOptionsEnableText"):SetText("Fade the UI away when idle")
  check:SetScript("OnClick", function()
    db.enabled = this:GetChecked() and true or false
    idle = 0
    if not db.enabled then Restore() end
  end)

  local slider = CreateFrame("Slider", "CleanScreenOptionsSeconds", f, "OptionsSliderTemplate")
  slider:SetPoint("TOPLEFT", f, "TOPLEFT", 26, -86)
  slider:SetWidth(248)
  slider:SetMinMaxValues(MIN_SECONDS, MAX_SECONDS)
  slider:SetValueStep(5)
  getglobal("CleanScreenOptionsSecondsLow"):SetText("5 s")
  getglobal("CleanScreenOptionsSecondsHigh"):SetText("2 min")
  slider:SetScript("OnValueChanged", function()
    db.seconds = Clamp(this:GetValue())
    getglobal("CleanScreenOptionsSecondsText"):SetText("Fade time: " .. db.seconds .. " s")
    idle = 0
  end)

  f.check, f.slider = check, slider
  table.insert(UISpecialFrames, "CleanScreenOptions")   -- Escape closes it; while open, nothing fades
  f:Hide()
  return f
end

local function ShowOptions()
  options = options or BuildOptions()
  options.check:SetChecked(db.enabled and 1 or nil)
  options.slider:SetValue(db.seconds)
  getglobal("CleanScreenOptionsSecondsText"):SetText("Fade time: " .. db.seconds .. " s")
  if options:IsShown() then options:Hide() else options:Show() end
end

local frame = CreateFrame("Frame", "CleanScreenFrame", WorldFrame)
frame:RegisterEvent("VARIABLES_LOADED")
frame:RegisterEvent("PLAYER_REGEN_DISABLED")
frame:RegisterEvent("PLAYER_REGEN_ENABLED")
frame:RegisterEvent("SPELLCAST_START")
frame:RegisterEvent("SPELLCAST_STOP")
frame:RegisterEvent("SPELLCAST_FAILED")
frame:RegisterEvent("SPELLCAST_INTERRUPTED")
frame:RegisterEvent("SPELLCAST_CHANNEL_START")
frame:RegisterEvent("SPELLCAST_CHANNEL_STOP")
frame:RegisterEvent("PLAYER_TARGET_CHANGED")
frame:RegisterEvent("CHAT_MSG_WHISPER")
frame:RegisterEvent("PLAYER_LOGOUT")
frame:SetScript("OnEvent", function()
  if event == "VARIABLES_LOADED" then
    CleanScreenDB = CleanScreenDB or {}
    db = CleanScreenDB
    if db.enabled == nil then db.enabled = true end
    db.seconds = Clamp(db.seconds or DEFAULT_SECONDS)
  elseif event == "PLAYER_REGEN_DISABLED" or event == "SPELLCAST_START" or event == "SPELLCAST_CHANNEL_START" then
    busy = true; idle = 0
  elseif event == "PLAYER_REGEN_ENABLED" or event == "SPELLCAST_STOP" or event == "SPELLCAST_FAILED"
      or event == "SPELLCAST_INTERRUPTED" or event == "SPELLCAST_CHANNEL_STOP" then
    busy = UnitAffectingCombat and UnitAffectingCombat("player") and true or false; idle = 0
  elseif event == "PLAYER_TARGET_CHANGED" or event == "CHAT_MSG_WHISPER" then
    idle = 0
  elseif event == "PLAYER_LOGOUT" then
    Restore()
  end
end)

-- This frame hangs off WorldFrame, so it keeps running while UIParent is faded or hidden.
frame:SetScript("OnUpdate", function()
  t = t + arg1
  if t < POLL then return end
  local dt = t; t = 0
  if not db then return end
  if not db.enabled then if alpha ~= 1 or hiddenByUs then Restore() end; return end
  if not hiddenByUs and not UIParent:IsShown() then return end   -- the player hid the UI (Alt+Z)
  local active = busy or MouseMoved() or PlayerMoved() or MouseOverUI() or AnyWindowOpen() or idle == 0
  if active then idle = 0.0001 else idle = idle + dt end   -- an event sets 0 to mean "just now"
  -- Where the UI should be: all the way back while active, otherwise fading by the idle time. Coming back
  -- is quick (FADE_IN); going away follows the fade time.
  local target = active and 1 or math.max(0, 1 - idle / db.seconds)
  if alpha < target then Apply(math.min(target, alpha + dt / FADE_IN))
  elseif alpha > target then Apply(target) end
end)

SLASH_CLEANSCREEN1 = "/cleanscreen"
SlashCmdList["CLEANSCREEN"] = function(msg)
  if not db then return end
  msg = string.lower(msg or "")
  local n = tonumber(msg)
  if msg == "" then ShowOptions()
  elseif msg == "on" then db.enabled = true; idle = 0; Say("on: the UI fades over " .. db.seconds .. " s of inactivity.")
  elseif msg == "off" then db.enabled = false; Restore(); Say("off.")
  elseif n then db.seconds = Clamp(n); idle = 0; Say("fade over " .. db.seconds .. " s.")
  else Say((db.enabled and "on" or "off") .. ", fade over " .. db.seconds .. " s, UI at "
    .. math.floor(alpha * 100 + 0.5) .. "%" .. (hiddenByUs and " (hidden)" or "") .. ".") end
end
