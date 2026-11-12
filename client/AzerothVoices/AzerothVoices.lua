local AzerothVoices = CreateFrame("Frame", "AzerothVoicesEventFrame")
AzerothVoicesEventFrame = AzerothVoices

AzerothVoices.prefix = "AZEROTH_VOICES"
AzerothVoices.roster = {}
AzerothVoices.pendingRoster = nil
AzerothVoices.selectedGuid = nil
AzerothVoices.pendingProfileGuid = nil
AzerothVoices.pendingToneGuid = nil
AzerothVoices.tonePollElapsed = 0
AzerothVoices.tonePollRemaining = 0
AzerothVoices.pendingBackstoryGuid = nil
AzerothVoices.backstoryPollElapsed = 0
AzerothVoices.backstoryPollRemaining = 0
AzerothVoices.loadedSentiment = 0
AzerothVoices.requestId = 0
AzerothVoices.inboundBuffers = {}
AzerothVoices.backstoryPages = {}
AzerothVoices.sendQueue = {}
AzerothVoices.sendElapsed = 0
AzerothVoices.saveLocked = false
AzerothVoices.saveLockRemaining = 0
AzerothVoices.uploadGuid = nil
AzerothVoices.uploadState = nil
AzerothVoices.unsavedTraits = nil
AzerothVoices.loadedTraits = nil
AzerothVoices.generatingAllInfo = nil
AzerothVoices.genAllStep = nil
AzerothVoices.genAllTimer = 0
AzerothVoices.serverLanguageSummary = nil
AzerothVoices.serverLanguageShort = nil
AzerothVoices.threadState = nil
AzerothVoices.serverFeatures = nil
AzerothVoices.multiAddressedState = nil
AzerothVoices.gossipState = nil
AzerothVoices.emotesState = nil

local SEND_INTERVAL = 0.3
local SAVE_LOCK_TIMEOUT = 20

local function trim(value)
    if not value then
        return ""
    end
    local _, _, content = string.find(value, "^%s*(.-)%s*$")
    return content or ""
end

local function sanitizeInput(value)
    value = trim(value or "")
    local clean = ""
    for i = 1, string.len(value) do
        local b = string.byte(value, i)
        if b < 32 then
            clean = clean .. " "
        else
            clean = clean .. string.sub(value, i, i)
        end
    end
    local _, _, collapsed = string.find(clean, "^%s*(.-)%s*$")
    local result = ""
    local inSpace = false
    local len = string.len(collapsed or "")
    for i = 1, len do
        local ch = string.sub(collapsed, i, i)
        if ch == " " then
            if not inSpace then
                result = result .. " "
                inSpace = true
            end
        else
            result = result .. ch
            inSpace = false
        end
    end
    return trim(result)
end

function AzerothVoices:Encode(value)
    value = sanitizeInput(value)
    if value == "" then
        return "-"
    end

    return (string.gsub(value, "([^%w%-_%.~])", function(char)
        local byte = string.byte(char)
        if byte >= 240 then
            return string.format("~%02X", byte)
        end
        return string.format("%%%02X", byte)
    end))
end

function AzerothVoices:Decode(value)
    if not value or value == "-" then
        return ""
    end

    local out = string.gsub(value, "([%%~])(%x%x)", function(prefix, hex)
        return string.char(tonumber(hex, 16))
    end)
    return out
end

function AzerothVoices:SetStatus(text, r, g, b)
    if self.frame and self.frame.status then
        self.frame.status:SetText(text or "")
        self.frame.status:SetTextColor(
            r or 1, g or 0.82, b or 0
        )
    end
end

function AzerothVoices:SendCommand(command)
    self.requestId = math.mod((self.requestId or 0) + 1, 1000000)
    local reqId = tostring(self.requestId)
    local len = string.len(command)
    local chunkSize = 180
    local partCount = math.floor((len + chunkSize - 1) / chunkSize)
    if partCount < 1 then
        partCount = 1
    end

    for partIdx = 1, partCount do
        local startPos = (partIdx - 1) * chunkSize + 1
        local endPos = math.min(len, partIdx * chunkSize)
        local chunk = ""
        if len > 0 then
            chunk = string.sub(command, startPos, endPos)
        end
        local line = ".avaddon 1 " .. reqId .. " " .. partIdx .. " " .. partCount .. " " .. chunk
        SendChatMessage(line, "SAY")
    end
end

function AzerothVoices:QueueCommand(command)
    table.insert(self.sendQueue, command)
end

function AzerothVoices:FlushSendQueue()
    self.sendQueue = {}
    self.sendElapsed = 0
end

function AzerothVoices:HandleSendQueue(elapsed)
    if table.getn(self.sendQueue) == 0 then
        return
    end

    self.sendElapsed = (self.sendElapsed or 0) + elapsed
    if self.sendElapsed < SEND_INTERVAL then
        return
    end

    self.sendElapsed = 0
    local nextCmd = table.remove(self.sendQueue, 1)
    if nextCmd then
        self:SendCommand(nextCmd)
    end

    if table.getn(self.sendQueue) == 0 and self.uploadState == "sending" then
        self:BeginConfirmWait()
    end
end

function AzerothVoices:LockSave()
    self.saveLocked = true
    self.saveLockRemaining = 0
    self:SetSaveEnabled(false)
end

function AzerothVoices:BeginConfirmWait()
    self.uploadState = "awaiting"
    self.saveLockRemaining = SAVE_LOCK_TIMEOUT
end

function AzerothVoices:UnlockSave()
    self.saveLocked = false
    self.saveLockRemaining = 0
    self:UpdateSaveButton()
end

function AzerothVoices:HandleSaveLock(elapsed)
    if not self.saveLocked or self.uploadState ~= "awaiting" then
        return
    end

    self.saveLockRemaining = (self.saveLockRemaining or 0) - elapsed
    if self.saveLockRemaining <= 0 then
        self:AbortUpload(
            "The server did not answer. Your traits are still here - save again to retry.",
            1, 0.82, 0
        )
    end
end

function AzerothVoices:RestoreUnsavedTraits()
    local t = self.unsavedTraits
    if not t or type(t) ~= "table" or t.guid ~= self.selectedGuid then
        return
    end

    local p = self.frame
    if p then
        if p.trait1 and t.trait1 then p.trait1:SetText(t.trait1) end
        if p.trait2 and t.trait2 then p.trait2:SetText(t.trait2) end
        if p.trait3 and t.trait3 then p.trait3:SetText(t.trait3) end
    end
end

function AzerothVoices:AbortUpload(message, r, g, b)
    if self.uploadGuid then
        self:FlushSendQueue()
    end
    self.uploadGuid = nil
    self.uploadState = nil
    self.generatingAllInfo = nil
    self.genAllStep = nil
    self.genAllTimer = 0
    self:StopTonePoll()
    self:StopBackstoryPoll()
    self:RestoreUnsavedTraits()
    self:UnlockSave()
    if message then
        self:SetStatus(message, r, g, b)
    end
end

function AzerothVoices:StopTonePoll()
    self.pendingToneGuid = nil
    self.tonePollElapsed = 0
    self.tonePollRemaining = 0
    self:SetRegenStoryEnabled(true)
end

function AzerothVoices:StartTonePoll(guid)
    guid = tonumber(guid)
    if not guid then
        return
    end

    self.pendingToneGuid = guid
    self.tonePollElapsed = 0
    self.tonePollRemaining = 120
    self:SetRegenStoryEnabled(false)

    if self.frame and self.frame.tone then
        self.frame.tone:SetTextColor(0.5, 0.5, 0.5)
        self.frame.tone:SetText("Generating tone...")
    end
end

function AzerothVoices:HandleTonePoll(elapsed)
    if not self.pendingToneGuid then
        return
    end

    self.tonePollElapsed = (self.tonePollElapsed or 0) + elapsed
    self.tonePollRemaining = (self.tonePollRemaining or 0) - elapsed

    if self.tonePollRemaining <= 0 then
        self:SetStatus(
            "Tone generation is still pending. Use Refresh.",
            1, 0.82, 0
        )
        self.generatingAllInfo = nil
        self.genAllStep = nil
        self.genAllTimer = 0
        self:StopTonePoll()
        return
    end

    if self.tonePollElapsed >= 1.5 then
        self.tonePollElapsed = 0
        self:SendCommand("get " .. self.pendingToneGuid)
    end
end

function AzerothVoices:SetRegenStoryEnabled(enabled)
    local busy = self.generatingAllInfo or self.uploadState
    if self.frame and self.frame.regenStoryBtn then
        if enabled and not self.pendingBackstoryGuid and not busy then
            self.frame.regenStoryBtn:Enable()
        else
            self.frame.regenStoryBtn:Disable()
        end
    end
    if self.frame and self.frame.regenToneBtn then
        if enabled and not self.pendingToneGuid and not busy then
            self.frame.regenToneBtn:Enable()
        else
            self.frame.regenToneBtn:Disable()
        end
    end
    if self.frame and self.frame.regenTraitsBtn then
        if enabled and not self.pendingToneGuid and not self.pendingBackstoryGuid and not busy then
            self.frame.regenTraitsBtn:Enable()
        else
            self.frame.regenTraitsBtn:Disable()
        end
    end
    if self.frame and self.frame.regenAllBtn then
        if enabled and not self.pendingToneGuid and not self.pendingBackstoryGuid and not busy then
            self.frame.regenAllBtn:Enable()
        else
            self.frame.regenAllBtn:Disable()
        end
    end
    if self.frame and self.frame.resetSentimentBtn then
        if enabled and self.selectedGuid and not busy then
            local score = self.loadedSentiment or 0
            if score ~= 0 then
                self.frame.resetSentimentBtn:Enable()
            else
                self.frame.resetSentimentBtn:Disable()
            end
        else
            self.frame.resetSentimentBtn:Disable()
        end
    end
end

function AzerothVoices:GetSentimentTier(score)
    score = math.max(-100, math.min(100, tonumber(score) or 0))
    if score <= -40 then
        return "Hostile", 0.85, 0.2, 0.2
    elseif score <= -10 then
        return "Cold", 0.9, 0.55, 0.2
    elseif score < 10 then
        return "Neutral", 0.9, 0.82, 0.2
    elseif score < 40 then
        return "Warm", 0.4, 0.85, 0.3
    else
        return "Trusted", 0.2, 0.85, 0.5
    end
end

function AzerothVoices:UpdateSentimentBar(score)
    score = math.max(-100, math.min(100, tonumber(score) or 0))
    self.loadedSentiment = score
    local p = self.frame
    if not p or not p.sentimentBar then
        return
    end

    local tier, r, g, b = self:GetSentimentTier(score)
    local normalized = score + 100
    p.sentimentBar:SetValue(normalized)
    p.sentimentBar:SetStatusBarColor(r, g, b)

    local sign = ""
    if score > 0 then
        sign = "+"
    end
    if p.sentimentBar.text then
        p.sentimentBar.text:SetText("Score: " .. sign .. score .. " | " .. tier)
    end

    if self.selectedGuid then
        if p.resetSentimentBtn then
            if score ~= 0 then
                p.resetSentimentBtn:Enable()
            else
                p.resetSentimentBtn:Disable()
            end
        end
    else
        if p.resetSentimentBtn then
            p.resetSentimentBtn:Disable()
        end
    end
end

function AzerothVoices:ResetSentiment()
    if not self.selectedGuid then
        self:SetStatus("Select a bot first.", 1, 0.2, 0.2)
        return
    end

    local guid = self.selectedGuid
    local bName = self:GetSelectedBotName() or "bot"
    local pName = UnitName("player") or "Player"

    self:UpdateSentimentBar(0)

    AzerothVoicesDB = AzerothVoicesDB or {}
    AzerothVoicesDB.sentiments = AzerothVoicesDB.sentiments or {}
    AzerothVoicesDB.sentiments[guid] = 0

    ChatterDB = ChatterDB or {}
    ChatterDB.sentiments = ChatterDB.sentiments or {}
    ChatterDB.sentiments[guid] = 0

    if SendChatMessage then
        SendChatMessage(".av sentiment reset " .. bName .. " " .. pName, "SAY")
    end

    self:SetStatus("Reset sentiment for " .. bName .. " to Neutral (0).", 0.3, 1, 0.3)
    if DEFAULT_CHAT_FRAME and DEFAULT_CHAT_FRAME.AddMessage then
        DEFAULT_CHAT_FRAME:AddMessage("|cffffd100[AzerothVoices]|r Reset sentiment for |cff00ff00" .. bName .. "|r toward |cff00ff00" .. pName .. "|r to Neutral (0).")
    end
end

function AzerothVoices:SetSaveEnabled(enabled)
    if self.frame and self.frame.saveBtn then
        if enabled then
            self.frame.saveBtn:Enable()
        else
            self.frame.saveBtn:Disable()
        end
    end
end

function AzerothVoices:UpdateSaveButton()
    -- Save Traits button removed; traits save automatically on generation.
end

function AzerothVoices:StopBackstoryPoll()
    self.pendingBackstoryGuid = nil
    self.backstoryPollElapsed = 0
    self.backstoryPollRemaining = 0
    self:SetRegenStoryEnabled(true)
end

function AzerothVoices:StartBackstoryPoll(guid)
    guid = tonumber(guid)
    if not guid then
        return
    end

    self.pendingBackstoryGuid = guid
    self.backstoryPollElapsed = 0
    self.backstoryPollRemaining = 120
    self:SetRegenStoryEnabled(false)

    local placeholder = "Creating background story..."
    if self.frame and self.frame.backstory then
        self.frame.backstory:SetText(placeholder)
        self.frame.backstory:SetTextColor(0.5, 0.5, 0.5)
    end
end

function AzerothVoices:HandleBackstoryPoll(elapsed)
    if not self.pendingBackstoryGuid then
        return
    end

    self.backstoryPollElapsed = (self.backstoryPollElapsed or 0) + elapsed
    self.backstoryPollRemaining = (self.backstoryPollRemaining or 0) - elapsed

    if self.backstoryPollRemaining <= 0 then
        self:SetStatus(
            "Backstory generation still pending. Use Refresh.",
            1, 0.82, 0
        )
        self.generatingAllInfo = nil
        self.genAllStep = nil
        self.genAllTimer = 0
        self:StopBackstoryPoll()
        return
    end

    if self.backstoryPollElapsed >= 2.0 then
        self.backstoryPollElapsed = 0
        self:SendCommand("get " .. self.pendingBackstoryGuid)
    end
end

function AzerothVoices:SaveWindowPosition()
    if not self.frame then
        return
    end

    local point, _, relPoint, x, y = self.frame:GetPoint(1)
    AzerothVoicesDB = AzerothVoicesDB or {}
    AzerothVoicesDB.point = point
    AzerothVoicesDB.relPoint = relPoint
    AzerothVoicesDB.x = x
    AzerothVoicesDB.y = y

    ChatterDB = ChatterDB or {}
    ChatterDB.point = point
    ChatterDB.relPoint = relPoint
    ChatterDB.x = x
    ChatterDB.y = y
end

function AzerothVoices:RestoreWindowPosition()
    if not self.frame then
        return
    end

    self.frame:ClearAllPoints()
    local db = AzerothVoicesDB
    if not (db and db.point) and ChatterDB and ChatterDB.point then
        db = ChatterDB
    end

    if db and db.point then
        self.frame:SetPoint(
            db.point,
            UIParent,
            db.relPoint or db.point,
            db.x or 0,
            db.y or 0
        )
    else
        self.frame:SetPoint("CENTER", UIParent, "CENTER", 0, 0)
    end
end

function AzerothVoices:MigrateSavedVariables()
    AzerothVoicesDB = AzerothVoicesDB or {}
    if ChatterDB and not AzerothVoicesDB.migratedFromChatter then
        if ChatterDB.point then AzerothVoicesDB.point = ChatterDB.point end
        if ChatterDB.relPoint then AzerothVoicesDB.relPoint = ChatterDB.relPoint end
        if ChatterDB.x then AzerothVoicesDB.x = ChatterDB.x end
        if ChatterDB.y then AzerothVoicesDB.y = ChatterDB.y end
        if ChatterDB.selectedGuid then AzerothVoicesDB.selectedGuid = ChatterDB.selectedGuid end
        if ChatterDB.sentiments and not AzerothVoicesDB.sentiments then
            AzerothVoicesDB.sentiments = ChatterDB.sentiments
        end
        AzerothVoicesDB.migratedFromChatter = true
    end
end

function AzerothVoices:ApplyProfileToPanel(p, profile, keepTraits)
    if not p then
        return
    end

    if p.trait1 and not keepTraits then
        p.trait1:SetText(profile.trait1 or "")
    end
    if p.trait2 and not keepTraits then
        p.trait2:SetText(profile.trait2 or "")
    end
    if p.trait3 and not keepTraits then
        p.trait3:SetText(profile.trait3 or "")
    end

    if p.tone then
        local toneText = profile.tone or ""
        local polling = (self.pendingToneGuid and self.pendingToneGuid == profile.guid)
        if not polling or toneText ~= "" then
            p.tone:SetText(toneText)
            p.tone:SetTextColor(0.8, 0.8, 0.8)
        end
    end

    if p.backstory then
        local bsText = profile.backstory or ""
        local polling = (self.pendingBackstoryGuid and self.pendingBackstoryGuid == profile.guid)
        if not polling or bsText ~= "" then
            p.backstory:SetText(bsText)
            p.backstory:SetTextColor(0.8, 0.8, 0.8)
        end
    end
end

function AzerothVoices:ApplyProfile(profile)
    if profile.guid ~= self.selectedGuid or self.forgetQueue then
        return
    end

    if self.uploadState and self.uploadGuid == profile.guid then
        return
    end

    local keepTraits = (
        type(self.unsavedTraits) == "table"
        and self.unsavedTraits.guid == profile.guid
    )

    self.pendingProfileGuid = nil
    self:SetRegenStoryEnabled(not self.pendingBackstoryGuid)
    local awaitingTone = (self.pendingToneGuid == profile.guid)
    self.selectedGuid = profile.guid

    self.loadedTraits = {
        trait1 = profile.trait1 or "",
        trait2 = profile.trait2 or "",
        trait3 = profile.trait3 or ""
    }

    self:ApplyProfileToPanel(self.frame, profile, keepTraits)

    if keepTraits then
        self:UpdateSaveButton()
    else
        self:SetSaveEnabled(false)
    end

    AzerothVoicesDB = AzerothVoicesDB or {}
    AzerothVoicesDB.selectedGuid = profile.guid
    AzerothVoicesDB.sentiments = AzerothVoicesDB.sentiments or {}
    local savedScore = AzerothVoicesDB.sentiments[profile.guid] or 0
    self:UpdateSentimentBar(savedScore)

    ChatterDB = ChatterDB or {}
    ChatterDB.selectedGuid = profile.guid
    ChatterDB.sentiments = ChatterDB.sentiments or {}
    ChatterDB.sentiments[profile.guid] = savedScore

    self:UpdateRosterViews()

    if awaitingTone then
        if profile.tone and profile.tone ~= "" then
            self:StopTonePoll()
            local bName = profile.name or self:GetSelectedBotName() or "bot"
            if self.generatingAllInfo then
                self.generatingAllInfo = nil
                self.genAllStep = nil
                self.genAllTimer = 0
                self:SetRegenStoryEnabled(true)
                self:SetStatus("All info generated for " .. bName .. "!", 0.3, 1, 0.3)
                if DEFAULT_CHAT_FRAME and DEFAULT_CHAT_FRAME.AddMessage then
                    DEFAULT_CHAT_FRAME:AddMessage("|cffffd100[AzerothVoices]|r All info generated for |cff00ff00" .. bName .. "|r (Traits -> Backstory -> Tone).")
                end
                self:SendCommand("get " .. profile.guid)
                self:UpdateRosterViews()
            else
                self:SetStatus("Generated tone for " .. bName .. ".", 0.3, 1, 0.3)
            end
        else
            self:SetStatus("Generating tone...", 1, 0.82, 0)
        end
    elseif self.pendingBackstoryGuid and self.pendingBackstoryGuid == profile.guid then
        self:SetStatus("Generating background story via LLM...", 1, 0.82, 0)
    else
        self:SetStatus("Loaded " .. (profile.name or "bot"), 0.3, 1, 0.3)
    end
end

function AzerothVoices:SelectBot(guid)
    if self.forgetQueue then return end
    guid = tonumber(guid)
    if not guid then
        return
    end

    if self.generatingAllInfo and self.selectedGuid and self.selectedGuid ~= guid then
        self:AbortUpload("Cancelled Generate All due to bot selection change.", 1, 0.4, 0.4)
    end

    if self.pendingToneGuid and self.pendingToneGuid ~= guid then
        self:StopTonePoll()
        self.generatingAllInfo = nil
    end
    if self.pendingBackstoryGuid and self.pendingBackstoryGuid ~= guid then
        self:StopBackstoryPoll()
        self.generatingAllInfo = nil
    end

    if type(self.unsavedTraits) == "table" and self.unsavedTraits.guid ~= guid then
        self.unsavedTraits = nil
    end
    if self.uploadGuid and self.uploadGuid ~= guid then
        self:AbortUpload()
    end

    self.selectedGuid = guid
    self.pendingProfileGuid = guid
    self.loadedTraits = nil
    self:SetSaveEnabled(false)
    self:SetRegenStoryEnabled(false)

    local savedScore = (AzerothVoicesDB and AzerothVoicesDB.sentiments and AzerothVoicesDB.sentiments[guid]) or 0
    self:UpdateSentimentBar(savedScore)

    local empty = { trait1 = "", trait2 = "", trait3 = "", tone = "", backstory = "" }
    self:ApplyProfileToPanel(self.frame, empty)
    self:UpdateRosterViews()
    self:SetStatus("Loading bot profile...", 1, 0.82, 0)
    self:SendCommand("get " .. guid)
end

function AzerothVoices:RequestRoster()
    if self.forgetQueue or self.pendingRoster then return end
    self.pendingRoster = {}
    self.rosterElapsed = 0
    self:UpdateRosterViews()
    self:SetStatus("Requesting roster...", 1, 0.82, 0)
    self:SendCommand("roster")
end

function AzerothVoices:SaveProfile()
    if self.pendingProfileGuid or self.forgetQueue then return end
    if not self.selectedGuid then
        self:SetStatus("Select a bot first.", 1, 0.2, 0.2)
        return
    end

    local p = self.frame
    if not p then
        self:SetStatus("No panel open.", 1, 0.2, 0.2)
        return
    end

    local trait1 = sanitizeInput(p.trait1:GetText())
    local trait2 = sanitizeInput(p.trait2:GetText())
    local trait3 = sanitizeInput(p.trait3:GetText())

    p.trait1:SetText(trait1)
    p.trait2:SetText(trait2)
    p.trait3:SetText(trait3)

    if trait1 == "" or trait2 == "" or trait3 == "" then
        self:SetStatus("All three traits are required.", 1, 0.2, 0.2)
        return
    end

    if string.len(trait1) > 64 or string.len(trait2) > 64 or string.len(trait3) > 64 then
        self:SetStatus("Traits must stay under 64 characters.", 1, 0.2, 0.2)
        return
    end

    self.pendingTraits = {
        guid = self.selectedGuid,
        trait1 = trait1,
        trait2 = trait2,
        trait3 = trait3,
    }

    local loaded = self.loadedTraits or {}
    local changed = (
        trait1 ~= (loaded.trait1 or "")
        or trait2 ~= (loaded.trait2 or "")
        or trait3 ~= (loaded.trait3 or "")
    )

    if not changed then
        self:SetStatus("Traits unchanged.", 0.3, 1, 0.3)
        return
    end

    self:DoSaveProfile()
end

function AzerothVoices:DoSaveProfile()
    local t = self.pendingTraits
    if not t or t.guid ~= self.selectedGuid or self.pendingProfileGuid or self.forgetQueue then
        return
    end

    local guid = self.selectedGuid
    if not self.generatingAllInfo then
        self:StopTonePoll()
        self:StopBackstoryPoll()
    end
    self:FlushSendQueue()

    self.unsavedTraits = {
        guid = guid,
        trait1 = t.trait1,
        trait2 = t.trait2,
        trait3 = t.trait3,
    }
    self.pendingTraits = nil

    local line = "set " .. guid .. " " ..
        self:Encode(t.trait1) .. " " ..
        self:Encode(t.trait2) .. " " ..
        self:Encode(t.trait3)

    self.uploadGuid = guid
    self.uploadState = "sending"
    self:SendCommand(line)
    self:LockSave()
    self:BeginConfirmWait()

    self:SetStatus("Saving traits...", 1, 0.82, 0)
end

function AzerothVoices:GetSelectedName()
    if not self.selectedGuid then
        return nil
    end
    local guid = tonumber(self.selectedGuid)
    if not guid then return nil end
    local count = table.getn(self.roster or {})
    for i = 1, count do
        local entry = self.roster[i]
        if entry and entry.guid == guid then
            return entry.name
        end
    end
    return nil
end

AzerothVoices.GetSelectedBotName = AzerothVoices.GetSelectedName

local PERSONALITY_TRAIT_POOLS = {
    "fiery", "calm", "brooding", "volatile", "serene", "melancholic", "jovial",
    "quick-tempered", "patient", "restless", "intense", "mercurial", "passionate",
    "gregarious", "reclusive", "charming", "blunt", "diplomatic", "awkward",
    "commanding", "deferential", "flirtatious", "standoffish", "nurturing", "aloof",
    "boisterous", "soft-spoken", "tactful", "abrasive", "endearing",
    "hopeful", "fatalistic", "pragmatic", "idealistic", "cynical", "wistful",
    "defiant", "resigned", "ambitious", "content", "suspicious", "trusting",
    "world-weary", "jaded", "reverent", "skeptical",
    "fearless", "cautious", "reckless", "hesitant", "bold", "calculating",
    "foolhardy", "steadfast", "skittish", "dauntless", "wary", "brash", "daring",
    "honorable", "ruthless", "merciful", "vengeful", "selfless", "self-serving",
    "just", "cunning", "compassionate", "cold-hearted", "principled", "opportunistic",
    "forgiving", "grudge-holding", "charitable", "greedy", "noble-spirited",
    "scholarly", "sharp-witted", "naive", "perceptive", "philosophical",
    "literal-minded", "inquisitive", "shrewd", "bookish", "street-smart",
    "sarcastic", "deadpan", "mirthful", "dark-humored", "witty", "dry",
    "bawdy", "whimsical", "sardonic", "teasing", "earnest", "irreverent",
    "stoic", "dramatic", "gruff", "gentle", "stern", "playful",
    "solemn", "lighthearted", "imposing", "unassuming", "eccentric", "dignified", "wild"
}

function AzerothVoices:RollRandomTraits()
    local total = table.getn(PERSONALITY_TRAIT_POOLS)
    if total < 3 then return "cunning", "bold", "stoic" end
    local idx1 = math.random(1, total)
    local idx2 = math.random(1, total)
    while idx2 == idx1 do idx2 = math.random(1, total) end
    local idx3 = math.random(1, total)
    while idx3 == idx1 or idx3 == idx2 do idx3 = math.random(1, total) end
    return PERSONALITY_TRAIT_POOLS[idx1], PERSONALITY_TRAIT_POOLS[idx2], PERSONALITY_TRAIT_POOLS[idx3]
end

function AzerothVoices:RegenTraits()
    if self.pendingProfileGuid or self.forgetQueue or self.pendingToneGuid or self.pendingBackstoryGuid then return end
    if not self.selectedGuid then
        self:SetStatus("Select a bot first.", 1, 0.2, 0.2)
        if DEFAULT_CHAT_FRAME and DEFAULT_CHAT_FRAME.AddMessage then
            DEFAULT_CHAT_FRAME:AddMessage("|cffffd100[AzerothVoices]|r |cffff2020Select a bot first from the roster to generate traits.|r")
        end
        return
    end

    local guid = self.selectedGuid
    local loaded = self.loadedTraits or {}
    local t1, t2, t3 = self:RollRandomTraits()
    local attempts = 0
    while attempts < 10 and t1 == (loaded.trait1 or "") and t2 == (loaded.trait2 or "") and t3 == (loaded.trait3 or "") do
        t1, t2, t3 = self:RollRandomTraits()
        attempts = attempts + 1
    end

    if self.frame then
        if self.frame.trait1 then self.frame.trait1:SetText(t1) end
        if self.frame.trait2 then self.frame.trait2:SetText(t2) end
        if self.frame.trait3 then self.frame.trait3:SetText(t3) end
    end

    self.unsavedTraits = nil
    self.pendingTraits = {
        guid = guid,
        trait1 = t1,
        trait2 = t2,
        trait3 = t3,
    }
    self:UpdateSaveButton()

    -- Automatically commit traits to server via 'set' so that SQL is updated without altering tone & backstory
    self:DoSaveProfile()
    self:SetStatus("Generated traits: " .. t1 .. ", " .. t2 .. ", " .. t3 .. ". Saving...", 1, 0.82, 0)
    if DEFAULT_CHAT_FRAME and DEFAULT_CHAT_FRAME.AddMessage then
        local bName = self:GetSelectedBotName() or "bot"
        DEFAULT_CHAT_FRAME:AddMessage("|cffffd100[AzerothVoices]|r Generated traits for |cff00ff00" .. bName .. "|r: " .. t1 .. ", " .. t2 .. ", " .. t3 .. ".")
    end
end

function AzerothVoices:RegenTone()
    if self.pendingProfileGuid or self.forgetQueue then return end
    if not self.selectedGuid then
        self:SetStatus("Select a bot first.", 1, 0.2, 0.2)
        if DEFAULT_CHAT_FRAME and DEFAULT_CHAT_FRAME.AddMessage then
            DEFAULT_CHAT_FRAME:AddMessage("|cffffd100[AzerothVoices]|r |cffff2020Select a bot first from the roster to generate tone.|r")
        end
        return
    end

    local guid = self.selectedGuid
    self:StopTonePoll()
    self:SendCommand("regentone " .. guid)
    self:StartTonePoll(guid)
    self:SetStatus("Generating tone via LLM endpoint...", 1, 0.82, 0)
    if DEFAULT_CHAT_FRAME and DEFAULT_CHAT_FRAME.AddMessage then
        local bName = self:GetSelectedBotName() or "bot"
        DEFAULT_CHAT_FRAME:AddMessage("|cffffd100[AzerothVoices]|r Generating tone for |cff00ff00" .. bName .. "|r via LLM...")
    end
end

function AzerothVoices:RegenBackstory()
    if self.pendingProfileGuid or self.forgetQueue then return end
    if not self.selectedGuid then
        self:SetStatus("Select a bot first.", 1, 0.2, 0.2)
        if DEFAULT_CHAT_FRAME and DEFAULT_CHAT_FRAME.AddMessage then
            DEFAULT_CHAT_FRAME:AddMessage("|cffffd100[AzerothVoices]|r |cffff2020Select a bot first from the roster to generate backstory.|r")
        end
        return
    end

    local guid = self.selectedGuid
    self:StopBackstoryPoll()
    self:SendCommand("regenbackstory " .. guid)
    self:StartBackstoryPoll(guid)
    self:SetStatus("Generating background story via LLM...", 1, 0.82, 0)
    if DEFAULT_CHAT_FRAME and DEFAULT_CHAT_FRAME.AddMessage then
        local bName = self:GetSelectedBotName() or "bot"
        DEFAULT_CHAT_FRAME:AddMessage("|cffffd100[AzerothVoices]|r Generating background story for |cff00ff00" .. bName .. "|r via LLM...")
    end
end

function AzerothVoices:RegenAll()
    if self.generatingAllInfo or self.pendingProfileGuid or self.forgetQueue or self.pendingToneGuid or self.pendingBackstoryGuid then
        return
    end
    if not self.selectedGuid then
        self:SetStatus("Select a bot first.", 1, 0.2, 0.2)
        if DEFAULT_CHAT_FRAME and DEFAULT_CHAT_FRAME.AddMessage then
            DEFAULT_CHAT_FRAME:AddMessage("|cffffd100[AzerothVoices]|r |cffff2020Select a bot first from the roster to generate all info.|r")
        end
        return
    end

    self.generatingAllInfo = true
    self.genAllStep = "traits"
    self.genAllTimer = 0
    self:SetRegenStoryEnabled(false)
    local bName = self:GetSelectedBotName() or "bot"
    self:SetStatus("Generating all info for " .. bName .. " (step 1/3: Traits)...", 1, 0.82, 0)
    if DEFAULT_CHAT_FRAME and DEFAULT_CHAT_FRAME.AddMessage then
        DEFAULT_CHAT_FRAME:AddMessage("|cffffd100[AzerothVoices]|r Generating all info for |cff00ff00" .. bName .. "|r sequentially (Traits -> Backstory -> Tone)...")
    end
    self:RegenTraits()
end

function AzerothVoices:RequestStatus()
    self:SendCommand("status")
end

function AzerothVoices:Toggle()
    self:BuildFrame()
    if not self.minimapButton then
        self:CreateMinimapButton()
    end

    if self.frame:IsShown() then
        self.frame:Hide()
        return
    end

    self.frame:Show()
    self.frame:Raise()
    self:RequestRoster()
end

function AzerothVoices:HandleRosterEntry(guidToken, nameToken)
    local guid = tonumber(guidToken)
    local name = self:Decode(nameToken)
    if not guid or name == "" then
        return
    end

    table.insert(self.pendingRoster, {
        guid = guid,
        name = name
    })
end

function AzerothVoices:FinishRoster()
    self.roster = self.pendingRoster or {}
    self.pendingRoster = nil

    table.sort(self.roster, function(a, b)
        return string.lower(a.name) < string.lower(b.name)
    end)

    self:UpdateRosterViews()

    if table.getn(self.roster) == 0 then
        self.selectedGuid = nil
        self.pendingProfileGuid = nil
        self.loadedTraits = nil
        AzerothVoicesDB = AzerothVoicesDB or {}
        AzerothVoicesDB.selectedGuid = nil
        self:StopTonePoll()
        self:StopBackstoryPoll()
        self:UpdateSentimentBar(0)
        self:SetSaveEnabled(false)
        self:SetRegenStoryEnabled(false)
        self:UpdateRosterViews()
        local empty = {
            trait1 = "", trait2 = "", trait3 = "",
            tone = "", backstory = "",
        }
        self:ApplyProfileToPanel(self.frame, empty)
        self:SetStatus("No known bots yet.", 1, 0.82, 0)
        return
    end

    local preferredGuid = self.selectedGuid
    if not preferredGuid and AzerothVoicesDB then
        preferredGuid = AzerothVoicesDB.selectedGuid
    end

    local found = nil
    local count = table.getn(self.roster)
    for i = 1, count do
        local bot = self.roster[i]
        if bot and bot.guid == preferredGuid then
            found = bot.guid
            break
        end
    end

    if not found then
        found = self.roster[1].guid
    end

    self:SelectBot(found)
end

function AzerothVoices:HandleProfilePayload(rest)
    local _, _, guid, name, trait1, trait2, trait3, tone =
        string.find(
            rest,
            "^(%d+)%s+(%S+)%s+(%S+)%s+(%S+)%s+(%S+)%s+(%S+)$"
        )
    if not guid then
        return
    end

    self:ApplyProfile({
        guid = tonumber(guid),
        name = self:Decode(name),
        trait1 = self:Decode(trait1),
        trait2 = self:Decode(trait2),
        trait3 = self:Decode(trait3),
        tone = self:Decode(tone),
    })
end

function AzerothVoices:HandleBackstoryPayload(rest)
    local _, _, guid, encoded = string.find(
        rest, "^(%d+)%s+(.*)$"
    )
    if not guid then
        return
    end

    local numGuid = tonumber(guid)
    local text = self:Decode(encoded or "-")
    if text == "-" then
        text = ""
    end

    if self.uploadState and self.uploadGuid == numGuid and not self.pendingBackstoryGuid then
        return
    end

    if text and text ~= "" and self.selectedGuid == numGuid then
        if self.frame and self.frame.backstory then
            self.frame.backstory:SetText(text)
            self.frame.backstory:SetTextColor(0.8, 0.8, 0.8)
        end
    end

    if self.pendingBackstoryGuid == numGuid then
        if text and text ~= "" then
            self:StopBackstoryPoll()
            local bName = self:GetSelectedBotName() or "bot"
            if self.generatingAllInfo then
                self:SetStatus("Backstory generated for " .. bName .. "! Waiting before tone generation (step 3/3)...", 1, 0.82, 0)
                if DEFAULT_CHAT_FRAME and DEFAULT_CHAT_FRAME.AddMessage then
                    DEFAULT_CHAT_FRAME:AddMessage("|cffffd100[AzerothVoices]|r Generated background story for |cff00ff00" .. bName .. "|r. Preparing tone...")
                end
                self.genAllStep = "wait_tone"
                self.genAllTimer = 1.2
            else
                self:SetStatus("Backstory generated for " .. bName .. ".", 0.3, 1, 0.3)
                if DEFAULT_CHAT_FRAME and DEFAULT_CHAT_FRAME.AddMessage then
                    DEFAULT_CHAT_FRAME:AddMessage("|cffffd100[AzerothVoices]|r Generated background story for |cff00ff00" .. bName .. "|r.")
                end
            end
        end
    end
end

function AzerothVoices:HandleUpdatedPayload(rest)
    local _, _, guid, name, flag = string.find(
        rest, "^(%d+)%s+(%S+)%s+(%S+)$"
    )
    if guid and name and tonumber(guid) == self.selectedGuid and not self.forgetQueue then
        self.selectedGuid = tonumber(guid)
        self:FlushSendQueue()
        self.uploadGuid = nil
        self.uploadState = nil
        self.unsavedTraits = nil
        self:UnlockSave()

        local botName = self:Decode(name)
        if self.frame then
            local t1 = sanitizeInput(self.frame.trait1 and self.frame.trait1:GetText() or "")
            local t2 = sanitizeInput(self.frame.trait2 and self.frame.trait2:GetText() or "")
            local t3 = sanitizeInput(self.frame.trait3 and self.frame.trait3:GetText() or "")
            self.loadedTraits = { trait1 = t1, trait2 = t2, trait3 = t3 }
        end
        self:SetSaveEnabled(false)

        if self.generatingAllInfo then
            local bName = botName or "bot"
            self:SetStatus("Traits saved for " .. bName .. "! Waiting before backstory generation (step 2/3)...", 1, 0.82, 0)
            if DEFAULT_CHAT_FRAME and DEFAULT_CHAT_FRAME.AddMessage then
                DEFAULT_CHAT_FRAME:AddMessage("|cffffd100[AzerothVoices]|r Traits saved for |cff00ff00" .. bName .. "|r. Preparing background story...")
            end
            self.genAllStep = "wait_backstory"
            self.genAllTimer = 1.2
        else
            if not self.pendingToneGuid then
                self:StopTonePoll()
            end
            if not self.pendingBackstoryGuid then
                self:StopBackstoryPoll()
            end
            self:SetStatus(
                "Traits saved for " .. botName .. ".",
                0.3, 1, 0.3
            )
        end
    end
end

function AzerothVoices:HandleBackstoryRegenPayload(rest)
    local _, _, guid, name = string.find(
        rest, "^(%d+)%s+(%S+)$"
    )
    if guid and name then
        self:SetStatus(
            "Regenerating backstory for " .. self:Decode(name) .. "...",
            1, 0.82, 0
        )
    end
end

function AzerothVoices:HandleTraitsRegenPayload(rest)
    local _, _, guid, name = string.find(rest, "^(%d+)%s+(%S+)$")
    if guid and name then
        self:SetStatus("Traits generated for " .. self:Decode(name) .. ".", 0.3, 1, 0.3)
    end
end

function AzerothVoices:HandleToneRegenPayload(rest)
    local _, _, guid, name = string.find(rest, "^(%d+)%s+(%S+)$")
    if guid and name then
        self:SetStatus("Generating tone for " .. self:Decode(name) .. "...", 1, 0.82, 0)
        self:StartTonePoll(tonumber(guid))
    end
end

function AzerothVoices:HandleAllRegenPayload(rest)
    local _, _, guid, name = string.find(rest, "^(%d+)%s+(%S+)$")
    if guid and name then
        self:SetStatus("Full LLM generation running for " .. self:Decode(name) .. "...", 1, 0.82, 0)
        self:StartTonePoll(tonumber(guid))
        self:StartBackstoryPoll(tonumber(guid))
    end
end

function AzerothVoices:HandleStatusPayload(rest)
    local _, _, enabled, apiConfigured, endpoint, model = string.find(rest, "^(%S+)%s+(%S+)%s+(%S+)%s*(%S*)$")
    if enabled then
        self.serverStatus = {
            enabled = (enabled == "enabled"),
            apiConfigured = (apiConfigured == "configured"),
            endpoint = self:Decode(endpoint or "-"),
            model = self:Decode(model or "-"),
        }
        if self.minimapButton and GameTooltip and GameTooltip:IsOwned(self.minimapButton) then
            self:UpdateMinimapTooltip(self.minimapButton)
        end
    end
end

function AzerothVoices:HandleForgottenPayload(rest)
    local _, _, guid = string.find(rest, "^(%d+)")
    if guid then
        self:HandleForgotten(tonumber(guid))
    end
end

function AzerothVoices:HandleErrorPayload(rest)
    if self.forgetQueue then
        local _, _, errText = string.find(rest, "^%S+%s*(.*)$")
        self:FinishForgetBatch("Stopped: " .. self:Decode(errText or rest))
        return
    end
    self.pendingRoster = nil
    self.generatingAllInfo = nil
    self.genAllStep = nil
    self.genAllTimer = 0
    self:StopTonePoll()
    self:StopBackstoryPoll()
    local _, _, _, encoded = string.find(
        rest, "^(%S+)%s*(.*)$"
    )
    local text = self:Decode(encoded or rest)
    if self.uploadState then
        self:AbortUpload(text, 1, 0.2, 0.2)
    else
        self:UnlockSave()
        self:SetStatus(text, 1, 0.2, 0.2)
    end
end

function AzerothVoices:UpdateLanguageUI()
    local text = self.serverLanguageSummary or "Waiting for server language settings..."
    local short = self.serverLanguageShort or "|cffffd100Lang:|r |cff888888Waiting for server...|r"
    if self.frame then
        if self.frame.languageSourceText then
            self.frame.languageSourceText:SetText(text)
        end
        if self.frame.langHeaderButton and self.frame.langHeaderButton.label then
            self.frame.langHeaderButton.label:SetText(short)
        end
    end
end

function AzerothVoices:UpdateThreadUI()
    local p = self.frame
    if not p then return end

    local s = self.threadState
    if not s then
        if p.threadEnergyBar then
            p.threadEnergyBar:SetValue(0)
            p.threadEnergyBar:SetStatusBarColor(0.5, 0.5, 0.5)
            if p.threadEnergyBar.text then
                p.threadEnergyBar.text:SetText("Threads: Idle / Ready")
            end
        end
        if p.threadTopicText then
            p.threadTopicText:SetText("|cffffd100Active Topic:|r None (Idle banter)")
        end
        if p.threadOpenPointText then
            p.threadOpenPointText:SetText("|cffffd100Open Question / Point:|r None")
        end
        if p.threadCallbacksText then
            p.threadCallbacksText:SetText("|cffffd100Recent Discussion History (Callbacks):|r None")
        end
        return
    end

    if not s.enabled then
        if p.threadEnergyBar then
            p.threadEnergyBar:SetValue(0)
            p.threadEnergyBar:SetStatusBarColor(0.5, 0.5, 0.5)
            if p.threadEnergyBar.text then
                p.threadEnergyBar.text:SetText("Threads: Disabled on Server")
            end
        end
        if p.threadTopicText then
            p.threadTopicText:SetText("|cffffd100Active Topic:|r |cff888888Disabled|r")
        end
        if p.threadOpenPointText then
            p.threadOpenPointText:SetText("|cffffd100Open Question / Point:|r |cff888888None|r")
        end
        if p.threadCallbacksText then
            p.threadCallbacksText:SetText("|cffffd100Recent Discussion History (Callbacks):|r |cff888888None|r")
        end
        return
    end

    local energy = math.max(0, math.min(100, tonumber(s.energy) or 0))
    if p.threadEnergyBar then
        p.threadEnergyBar:SetValue(energy)
        local r, g, b = 0.2, 0.85, 0.5
        if energy <= 30 then
            r, g, b = 0.85, 0.35, 0.2
        elseif energy <= 60 then
            r, g, b = 0.9, 0.82, 0.2
        end
        p.threadEnergyBar:SetStatusBarColor(r, g, b)

        local moveDisplay = s.move or "ContinueTopic"
        if moveDisplay == "ContinueTopic" then
            moveDisplay = "Continue Topic"
        elseif moveDisplay == "DriftTopic" then
            moveDisplay = "Topic Drift"
        elseif moveDisplay == "CallbackToPrior" then
            moveDisplay = "Callback to Prior"
        elseif moveDisplay == "StartNewTopic" then
            moveDisplay = "Wrap-Up / New Topic"
        end

        local scopePrefix = (s.scope and s.scope ~= "-") and (s.scope .. ": ") or ""
        local statusText = s.status or "Active"
        if p.threadEnergyBar.text then
            p.threadEnergyBar.text:SetText(scopePrefix .. "Energy " .. energy .. "% | " .. moveDisplay .. " (" .. (s.exchanges or 0) .. " turns) | " .. statusText)
        end
    end

    if p.threadTopicText then
        local topicStr = (s.topic and s.topic ~= "-" and s.topic ~= "") and s.topic or "None (Idle banter)"
        p.threadTopicText:SetText("|cffffd100Active Topic:|r " .. topicStr)
    end

    if p.threadOpenPointText then
        local openStr = (s.openPoint and s.openPoint ~= "-" and s.openPoint ~= "") and s.openPoint or "None"
        p.threadOpenPointText:SetText("|cffffd100Open Question / Point:|r " .. openStr)
    end

    if p.threadCallbacksText then
        local cbStr = (s.callbacks and s.callbacks ~= "-" and s.callbacks ~= "") and s.callbacks or "None"
        p.threadCallbacksText:SetText("|cffffd100Recent Discussion History (Callbacks):|r " .. cbStr)
    end
end

function AzerothVoices:RequestThreadStatus()
    self:SetStatus("Refreshing conversation threads...", 1, 0.82, 0)
    self:SendCommand("status")
end

function AzerothVoices:UpdateFeaturesUI()
    -- Subsections below Conversation Threads have been removed for a clean, lightweight UI
end

function AzerothVoices:HandlePayload(payload)
    local _, _, command, rest = string.find(payload, "^(%S+)%s*(.*)$")
    if not command then
        return
    end

    if command == "LANGUAGE_INFO" then
        local fields = {}
        for value in string.gfind(rest, "%S+") do table.insert(fields, value) end
        if table.getn(fields) < 2 then return end

        local enabled = (fields[1] == "1")
        local rawLang = fields[2] or "NONE"
        local upperLang = string.upper(rawLang)
        local worldEnabled = (fields[3] == "1")

        local names = {
            GR = "Greeklish", GREEKLISH = "Greeklish",
            DE = "German", GERMAN = "German",
            FR = "French", FRENCH = "French",
            ES = "Spanish", SPANISH = "Spanish",
            IT = "Italian", ITALIAN = "Italian",
            PT = "Portuguese", PORTUGUESE = "Portuguese",
            PL = "Polish", POLISH = "Polish",
            RU = "Russian", RUSSIAN = "Russian",
            TR = "Turkish", TURKISH = "Turkish",
            NL = "Dutch", DUTCH = "Dutch"
        }

        local langName = names[upperLang] or rawLang

        if not enabled or upperLang == "NONE" or upperLang == "0" or upperLang == "" then
            self.serverLanguageSummary = "Secondary language disabled"
            self.serverLanguageShort = "|cffffd100Lang:|r |cffaaaaaaEnglish Only|r"
        else
            if table.getn(fields) >= 10 then
                for i = 4, 10 do
                    local n = tonumber(fields[i] or "")
                    if not n or n > 100 or n < 0 then return end
                end
            end
            local wPct = fields[4] or "50"
            local gPct = fields[5] or "100"
            local pPct = fields[6] or "100"
            local rPct = fields[7] or "100"
            local whPct = fields[8] or "100"
            local sPct = fields[9] or "100"
            local yPct = fields[10] or "100"

            local worldActive = worldEnabled and (wPct ~= "0")
            self.serverLanguageSummary = "English + " .. langName .. " (all bots)\n" ..
                "World: " .. (worldActive and (wPct .. "%") or "English only") ..
                " | Guild: " .. gPct .. "% | Party: " .. pPct .. "% | Raid: " .. rPct .. "%\n" ..
                "Whisper: " .. whPct .. "% | Say: " .. sPct .. "% | Yell: " .. yPct .. "%"
            self.serverLanguageShort = "|cffffd100Lang:|r |cff00ff00English + " .. langName .. "|r"
        end
        self:UpdateLanguageUI()
        return
    end

    if command == "THREAD_INFO" then
        local fields = {}
        for value in string.gfind(rest, "%S+") do table.insert(fields, value) end
        if table.getn(fields) < 6 then return end

        local scope = fields[1] or "Party"
        local enabled = (fields[2] == "1")
        local energy = tonumber(fields[3] or "0") or 0
        local move = fields[4] or "ContinueTopic"
        local exchanges = tonumber(fields[5] or "0") or 0
        local status = fields[6] or "Active"
        local rawTopic = fields[7] or "-"
        local rawOpenPoint = fields[8] or "-"
        local rawCallbacks = fields[9] or "-"

        local topic = (rawTopic ~= "-") and self:Decode(rawTopic) or "None (Idle banter)"
        local openPoint = (rawOpenPoint ~= "-") and self:Decode(rawOpenPoint) or "None"
        local callbacks = (rawCallbacks ~= "-") and self:Decode(rawCallbacks) or "None"

        if status ~= "-" then
            status = self:Decode(status)
        end

        self.threadState = {
            scope = scope,
            enabled = enabled,
            energy = energy,
            move = move,
            exchanges = exchanges,
            status = status,
            topic = topic,
            openPoint = openPoint,
            callbacks = callbacks
        }

        self:UpdateThreadUI()
        self:SetStatus("Conversation thread status updated.", 0.2, 0.85, 0.5)
        return
    end
    if command == "ROSTER_BEGIN" then
        self.pendingRoster = {}
        return
    end

    if command == "ROSTER" then
        local _, _, guid, name = string.find(rest, "^(%d+)%s+(%S+)$")
        if guid and name and self.pendingRoster then
            self:HandleRosterEntry(guid, name)
        end
        return
    end

    if command == "ROSTER_END" then
        self:FinishRoster()
        return
    end

    if command == "PROFILE" then
        self:HandleProfilePayload(rest)
        return
    end

    if command == "BACKSTORY" then
        self:HandleBackstoryPayload(rest)
        return
    end

    if command == "UPDATED" then
        self:HandleUpdatedPayload(rest)
        return
    end

    if command == "BACKSTORY_REGEN" then
        self:HandleBackstoryRegenPayload(rest)
        return
    end

    if command == "TRAITS_REGEN" then
        self:HandleTraitsRegenPayload(rest)
        return
    end

    if command == "TONE_REGEN" then
        self:HandleToneRegenPayload(rest)
        return
    end

    if command == "ALL_REGEN" then
        self:HandleAllRegenPayload(rest)
        return
    end

    if command == "FEATURES_INFO" then
        local fields = {}
        for value in string.gfind(rest, "%S+") do table.insert(fields, value) end
        if table.getn(fields) >= 10 then
            self.serverFeatures = {
                multiAddressed = (fields[1] == "1"),
                gossipRelay = (fields[2] == "1"),
                emotesBotTalk = (fields[3] == "1"),
                emotesNpcTalk = (fields[4] == "1"),
                emotesNpcFace = (fields[5] == "1"),
                emotesFaceDuration = tonumber(fields[6] or "5") or 5,
                emotesGeneral = (fields[7] == "1"),
                emotesInteractive = (fields[8] == "1"),
                npcChatter = (fields[9] == "1"),
                eventChat = (fields[10] == "1")
            }
            self:UpdateFeaturesUI()
        end
        return
    end

    if command == "MULTIADDRESSED_INFO" then
        local fields = {}
        for value in string.gfind(rest, "%S+") do table.insert(fields, value) end
        if table.getn(fields) >= 7 then
            local function parseScope(s)
                local _, _, en, ch, maxB = string.find(s or "", "^(%d+):(%d+):(%d+)$")
                if en and en == "1" then
                    return (ch or "50") .. "% (max " .. (maxB or "2") .. ")"
                else
                    return "Off"
                end
            end
            self.multiAddressedState = {
                enabled = (fields[1] == "1"),
                party = parseScope(fields[2]),
                say = parseScope(fields[3]),
                guild = parseScope(fields[4]),
                world = parseScope(fields[5]),
                staggerMin = tonumber(fields[6] or "3") or 3,
                staggerMax = tonumber(fields[7] or "6") or 6
            }
            self:UpdateFeaturesUI()
        end
        return
    end

    if command == "GOSSIP_INFO" then
        local fields = {}
        for value in string.gfind(rest, "%S+") do table.insert(fields, value) end
        if table.getn(fields) >= 5 then
            local function parseGossip(s)
                local _, _, en, ch = string.find(s or "", "^(%d+):(%d+)$")
                if en and en == "1" then
                    return (ch or "20") .. "%"
                else
                    return "Off"
                end
            end
            self.gossipState = {
                enabled = (fields[1] == "1"),
                party = parseGossip(fields[2]),
                say = parseGossip(fields[3]),
                guild = parseGossip(fields[4]),
                cooldown = tonumber(fields[5] or "600") or 600
            }
            self:UpdateFeaturesUI()
        end
        return
    end

    if command == "EMOTES_INFO" then
        local fields = {}
        for value in string.gfind(rest, "%S+") do table.insert(fields, value) end
        if table.getn(fields) >= 14 then
            self.emotesState = {
                botTalk = (fields[1] == "1"),
                npcTalk = (fields[2] == "1"),
                npcFace = (fields[3] == "1"),
                faceDuration = tonumber(fields[4] or "5") or 5,
                general = (fields[5] == "1"),
                listener = (fields[6] == "1"),
                roleplay = (fields[7] == "1"),
                interactive = (fields[8] == "1"),
                botMirrorCh = tonumber(fields[9] or "80") or 80,
                botMirrorCd = tonumber(fields[10] or "15") or 15,
                moodSpreadCh = tonumber(fields[11] or "50") or 50,
                npcMirrorEn = (fields[12] == "1"),
                npcMirrorCd = tonumber(fields[13] or "15") or 15,
                pvpSpectator = (fields[14] == "1")
            }
            self:UpdateFeaturesUI()
        end
        return
    end

    if command == "STATUS" then
        self:HandleStatusPayload(rest)
        return
    end

    if command == "FORGOTTEN" then
        self:HandleForgottenPayload(rest)
        return
    end

    if command == "BACKSTORY_PAGE" then
        local _, _, guid, pageIdxStr, pageCountStr, chunk =
            string.find(rest, "^(%d+)%s+(%d+)%s+(%d+)%s*(.*)$")
        if guid and pageIdxStr and pageCountStr then
            local pageIdx = tonumber(pageIdxStr)
            local pageCount = tonumber(pageCountStr)
            local pageKey = (self.currentReqId or "0") .. ":" .. guid
            self.backstoryPages = self.backstoryPages or {}
            if not self.backstoryPages[pageKey] then
                self.backstoryPages[pageKey] = { pages = {}, count = 0 }
            end
            local buf = self.backstoryPages[pageKey]
            if not buf.pages[pageIdx] then
                buf.pages[pageIdx] = chunk or ""
                buf.count = buf.count + 1
            end
            if buf.count >= pageCount then
                local full = ""
                for p = 1, pageCount do
                    full = full .. (buf.pages[p] or "")
                end
                self.backstoryPages[pageKey] = nil
                self:HandleBackstoryPayload(guid .. " " .. full)
            end
        end
        return
    end

    if command == "ERROR" then
        self:HandleErrorPayload(rest)
    end
end

function AzerothVoices:HandleAddonMessage(prefix, message, channel, sender)
    if prefix ~= self.prefix or channel ~= "GUILD" or sender ~= UnitName("player") then
        return
    end

    local _, _, version, reqId, msgIdxStr, partIdxStr, partCountStr, chunk =
        string.find(message, "^(%d+)\t([^\t]+)\t(%d+)\t(%d+)\t(%d+)\t?(.*)$")

    if version ~= "1" or not reqId or not msgIdxStr or not partIdxStr or not partCountStr then
        return
    end

    local msgIdx = tonumber(msgIdxStr)
    local partIdx = tonumber(partIdxStr)
    local partCount = tonumber(partCountStr)

    if not msgIdx or not partIdx or not partCount or
        msgIdx < 1 or partIdx < 1 or partCount < 1 or partIdx > partCount or partCount > 128 then
        return
    end

    local bufferKey = reqId .. ":" .. msgIdx
    local now = GetTime and GetTime() or 0

    for key, buf in pairs(self.inboundBuffers) do
        if buf.timestamp and (now - buf.timestamp > 10) then
            self.inboundBuffers[key] = nil
        end
    end

    local buf = self.inboundBuffers[bufferKey]
    if not buf or buf.partCount ~= partCount then
        buf = {
            partCount = partCount,
            parts = {},
            receivedCount = 0,
            totalBytes = 0,
            timestamp = now,
        }
        self.inboundBuffers[bufferKey] = buf
    end

    chunk = chunk or ""
    if not buf.parts[partIdx] then
        if buf.totalBytes + string.len(chunk) > 32768 then
            self.inboundBuffers[bufferKey] = nil
            return
        end
        buf.parts[partIdx] = chunk
        buf.receivedCount = buf.receivedCount + 1
        buf.totalBytes = buf.totalBytes + string.len(chunk)
    end

    if buf.receivedCount >= partCount then
        local payload = ""
        for i = 1, partCount do
            payload = payload .. (buf.parts[i] or "")
        end
        self.inboundBuffers[bufferKey] = nil
        self.currentReqId = reqId
        self:HandlePayload(payload)
    end
end

StaticPopupDialogs["AZEROTHVOICES_CONFIRM_SAVE_TRAITS"] = {
    text = "Save the new personality traits for this bot?",
    button1 = "Save",
    button2 = "Cancel",
    OnAccept = function()
        AzerothVoices:DoSaveProfile()
    end,
    timeout = 0,
    whileDead = false,
    hideOnEscape = true,
}

StaticPopupDialogs["CHATTER_CONFIRM_SAVE_TRAITS"] = StaticPopupDialogs["AZEROTHVOICES_CONFIRM_SAVE_TRAITS"]

SLASH_AZEROTHVOICES1 = "/azerothvoices"
SLASH_AZEROTHVOICES2 = "/avvoices"
SLASH_AZEROTHVOICES3 = "/chatter"
SLASH_AZEROTHVOICES4 = "/llmc"
SLASH_AZEROTHVOICES5 = "/av"
SLASH_AZEROTHVOICES6 = "/botchat"

SlashCmdList["AZEROTHVOICES"] = function(msg)
    local cmd = ""
    if msg then
        local _, _, first = string.find(msg, "^(%S+)")
        if first then
            cmd = string.lower(first)
        end
    end
    if cmd == "minimap" then
        AzerothVoices:ToggleMinimapButton()
    elseif cmd == "reset" then
        AzerothVoices:ResetMinimapPosition()
    elseif cmd == "help" then
        if DEFAULT_CHAT_FRAME and DEFAULT_CHAT_FRAME.AddMessage then
            DEFAULT_CHAT_FRAME:AddMessage("|cffffd100[Azeroth Voices Slash Commands]|r")
            DEFAULT_CHAT_FRAME:AddMessage("  |cff00ff00/azerothvoices|r or |cff00ff00/av|r - Toggle main editor window")
            DEFAULT_CHAT_FRAME:AddMessage("  |cff00ff00/av minimap|r - Toggle minimap quick access button")
            DEFAULT_CHAT_FRAME:AddMessage("  |cff00ff00/av reset|r - Reset window & minimap position")
        end
    else
        AzerothVoices:Toggle()
    end
end

SlashCmdList["CHATTER"] = SlashCmdList["AZEROTHVOICES"]

function AzerothVoices:HandleGenerateAll(elapsed)
    if not self.generatingAllInfo or not self.genAllStep then
        return
    end

    elapsed = elapsed or 0
    if self.genAllTimer and self.genAllTimer > 0 then
        self.genAllTimer = self.genAllTimer - elapsed
        if self.genAllTimer > 0 then
            return
        end
        self.genAllTimer = 0
    end

    if self.genAllStep == "wait_backstory" then
        self.genAllStep = "backstory"
        local bName = self:GetSelectedBotName() or "bot"
        self:SetStatus("Generating background story for " .. bName .. " via LLM (step 2/3)...", 1, 0.82, 0)
        self:RegenBackstory()
    elseif self.genAllStep == "wait_tone" then
        self.genAllStep = "tone"
        local bName = self:GetSelectedBotName() or "bot"
        self:SetStatus("Generating tone for " .. bName .. " via LLM (step 3/3)...", 1, 0.82, 0)
        self:RegenTone()
    end
end

AzerothVoices:SetScript("OnUpdate", function()
    local self = this
    local elapsed = arg1 or 0
    self:HandleSendQueue(elapsed)
    self:HandleSaveLock(elapsed)
    if self.pendingRoster then
        self.rosterElapsed = (self.rosterElapsed or 0) + elapsed
        if self.rosterElapsed >= 10 then
            self.pendingRoster = nil
            self:UpdateRosterViews()
            self:SetStatus("Roster request timed out. Use Refresh.", 1, 0.4, 0.3)
        end
    end
    self:HandleForgetQueue(elapsed)
    self:HandleTonePoll(elapsed)
    self:HandleBackstoryPoll(elapsed)
    self:HandleGenerateAll(elapsed)
end)

function AzerothVoices:HandleSystemMessage(msg)
    if not msg then return end
    local _, _, actor, score = string.find(msg, "^Sentiment%s+(%S+)%s+%->%s+%S+:%s+score=([%-%d]+)")
    if actor and score then
        local currentBot = self:GetSelectedBotName()
        if currentBot and string.lower(currentBot) == string.lower(actor) then
            local numScore = tonumber(score) or 0
            self:UpdateSentimentBar(numScore)
            if self.selectedGuid then
                AzerothVoicesDB = AzerothVoicesDB or {}
                AzerothVoicesDB.sentiments = AzerothVoicesDB.sentiments or {}
                AzerothVoicesDB.sentiments[self.selectedGuid] = numScore
            end
        end
    end
    local _, _, resetActor = string.find(msg, "^Sentiment%s+(%S+)%s+%->%s+%S+%s+reset to neutral")
    if resetActor then
        local currentBot = self:GetSelectedBotName()
        if currentBot and string.lower(currentBot) == string.lower(resetActor) then
            self:UpdateSentimentBar(0)
            if self.selectedGuid then
                AzerothVoicesDB = AzerothVoicesDB or {}
                AzerothVoicesDB.sentiments = AzerothVoicesDB.sentiments or {}
                AzerothVoicesDB.sentiments[self.selectedGuid] = 0
            end
        end
    end
end

AzerothVoices:SetScript("OnEvent", function()
    local self = this
    local currentEvent = event
    if currentEvent == "PLAYER_LOGIN" then
        self:MigrateSavedVariables()
        self:BuildFrame()
        self:CreateMinimapButton()
        self:RequestStatus()
    elseif currentEvent == "PLAYER_LOGOUT" then
        self:SaveWindowPosition()
    elseif currentEvent == "CHAT_MSG_ADDON" then
        self:HandleAddonMessage(arg1, arg2, arg3, arg4)
    elseif currentEvent == "CHAT_MSG_SYSTEM" then
        self:HandleSystemMessage(arg1)
    elseif currentEvent == "PARTY_MEMBERS_CHANGED" or currentEvent == "RAID_ROSTER_UPDATE" then
        if self.rosterFilter == "party" and self.frame and self.frame:IsShown() then
            self:UpdateRosterViews()
        end
    end
end)

AzerothVoices:RegisterEvent("PLAYER_LOGIN")
AzerothVoices:RegisterEvent("PLAYER_LOGOUT")
AzerothVoices:RegisterEvent("CHAT_MSG_ADDON")
AzerothVoices:RegisterEvent("CHAT_MSG_SYSTEM")
AzerothVoices:RegisterEvent("PARTY_MEMBERS_CHANGED")
AzerothVoices:RegisterEvent("RAID_ROSTER_UPDATE")
