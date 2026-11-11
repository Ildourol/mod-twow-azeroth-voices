#include "AzerothVoicesChannelMembership.h"
#include "AzerothVoicesManager.h"

#include "Channel.h"
#include <map>
#include <mutex>
#include <utility>
#include "Creature.h"
#include "GameObject.h"
#include "Guild.h"
#include "GuildMgr.h"
#include "Item.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include "Opcodes.h"
#include "Player.h"
#include "QuestDef.h"
#include "ScriptObjects.h"
#include "SharedDefines.h"
#include "Spell.h"
#include "WorldPacket.h"
#include "WorldSession.h"

#include <algorithm>
#include <cctype>
#include <string>

namespace AzerothVoices
{
    namespace
    {
        std::mutex s_whisperTargetsMutex;
        std::map<uint64_t, std::pair<std::string, std::string>> s_pendingWhisperTargets;
    }

    void HandleAddonCommandFromPlayer(Player* player, std::string const& arguments);

    namespace
    {
        std::string Lower(std::string value)
        {
            std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            return value;
        }

        bool ToScope(uint32 type, ChatScope& scope)
        {
            switch (type)
            {
                case CHAT_MSG_SAY: scope = ChatScope::Say; return true;
                case CHAT_MSG_YELL: scope = ChatScope::Yell; return true;
                case CHAT_MSG_WHISPER: scope = ChatScope::Whisper; return true;
                case CHAT_MSG_PARTY: scope = ChatScope::Party; return true;
                case CHAT_MSG_RAID:
                case CHAT_MSG_RAID_LEADER:
                case CHAT_MSG_RAID_WARNING: scope = ChatScope::Raid; return true;
                case CHAT_MSG_GUILD: scope = ChatScope::Guild; return true;
                case CHAT_MSG_OFFICER: scope = ChatScope::Officer; return true;
                case CHAT_MSG_CHANNEL: scope = ChatScope::Channel; return true;
                default: return false;
            }
        }

        // `.llmc` is the stock Chatter Companion addon command channel. It is
        // consumed here when the core command parser did not already claim it
        // (that path runs first whenever PlayerCommands is enabled).
        bool IsAddonCommand(std::string const& message)
        {
            std::string const prefix = ".llmc";
            if (message.size() < prefix.size())
                return false;
            for (size_t i = 0; i < prefix.size(); ++i)
                if (std::tolower(static_cast<unsigned char>(message[i])) != prefix[i])
                    return false;
            return message.size() == prefix.size() ||
                std::isspace(static_cast<unsigned char>(message[prefix.size()])) != 0;
        }

        // `.avaddon` is the native Turtle WoW AzerothVoices addon command transport.
        // It travels as framed chunks in Say chat and is suppressed before broadcast.
        bool IsAvaddonCommand(std::string const& message)
        {
            std::string const prefix = ".avaddon";
            if (message.size() < prefix.size())
                return false;
            for (size_t i = 0; i < prefix.size(); ++i)
                if (std::tolower(static_cast<unsigned char>(message[i])) != prefix[i])
                    return false;
            return message.size() == prefix.size() ||
                std::isspace(static_cast<unsigned char>(message[prefix.size()])) != 0;
        }

        class AzerothVoicesWorldScript final : public WorldScript
        {
        public:
            AzerothVoicesWorldScript()
                : WorldScript("AzerothVoicesWorldScript", {
                    WORLDHOOK_ON_AFTER_CONFIG_LOAD,
                    WORLDHOOK_ON_UPDATE,
                    WORLDHOOK_ON_STARTUP,
                    WORLDHOOK_ON_SHUTDOWN })
            {
            }

            void OnStartup() override
            {
                Manager::Instance().Start();
            }

            void OnUpdate(uint32 diff) override
            {
                ChannelMembership::Drain();
                Manager::Instance().Update(diff);
            }

            void OnAfterConfigLoad(bool reload) override
            {
                if (reload)
                    Manager::Instance().Reload();
            }

            void OnShutdown() override
            {
                Manager::Instance().Stop();
            }
        };

        class AzerothVoicesPlayerScript final : public PlayerScript
        {
        public:
            AzerothVoicesPlayerScript()
                : PlayerScript("AzerothVoicesPlayerScript", {
                    PLAYERHOOK_ON_PLAYER_JUST_DIED,
                    PLAYERHOOK_ON_PLAYER_RELEASED_GHOST,
                    PLAYERHOOK_ON_PLAYER_COMPLETE_QUEST,
                    PLAYERHOOK_ON_PVP_KILL,
                    PLAYERHOOK_ON_CREATURE_KILL,
                    PLAYERHOOK_ON_LEVEL_CHANGED,
                    PLAYERHOOK_ON_LEARN_SPELL,
                    PLAYERHOOK_ON_SPELL_CAST,
                    PLAYERHOOK_ON_DUEL_REQUEST,
                    PLAYERHOOK_ON_DUEL_START,
                    PLAYERHOOK_ON_DUEL_END,
                    PLAYERHOOK_ON_LOGIN,
                    PLAYERHOOK_ON_LOGOUT,
                    PLAYERHOOK_ON_UPDATE_ZONE,
                    PLAYERHOOK_ON_MAP_CHANGED,
                    PLAYERHOOK_ON_LOOT_ITEM,
                    PLAYERHOOK_ON_BEFORE_SEND_CHAT_MESSAGE,
                    PLAYERHOOK_ON_CHAT_CHANNEL,
                    PLAYERHOOK_ON_TEXT_EMOTE,
                    PLAYERHOOK_ON_CHAT_WHISPER })
            {
            }

            void OnBeforeSendChatMessage(Player* player, uint32& type, uint32& language, std::string& message) override
            {
                if (!player || language == LANG_ADDON || type == CHAT_MSG_CHANNEL || type == CHAT_MSG_WHISPER)
                    return;

                if (type == CHAT_MSG_SAY)
                {
                    if (IsAvaddonCommand(message))
                    {
                        Manager::Instance().HandleAvaddonSay(player, message);
                        message.clear();
                        return;
                    }
                    if (IsAddonCommand(message))
                    {
                        HandleAddonCommandFromPlayer(player, message.substr(5));
                        message.clear();
                        return;
                    }
                }

                ChatScope scope;
                if (ToScope(type, scope))
                    Manager::Instance().HandleChat(player, scope, message, "");
            }

            void OnPlayerJustDied(Player* player) override
            {
                Manager::Instance().HandleEvent(player, "died");
            }

            void OnPlayerCompleteQuest(Player* player, Quest const* quest) override
            {
                Manager::Instance().HandleEvent(player, "quest_completed", quest ? quest->GetTitle() : "");
            }

            void OnPVPKill(Player* killer, Player* killed) override
            {
                Manager::Instance().HandleEvent(killer, "player_defeated", killed ? killed->GetName() : "");
            }

            void OnCreatureKill(Player* killer, Creature* killed) override
            {
                Manager::Instance().HandleEvent(killer, "creature_defeated",
                    killed ? killed->GetName() : "", 0,
                    killed && killed->GetCreatureInfo() ? killed->GetCreatureInfo()->entry : 0,
                    killed && killed->GetCreatureInfo() ? killed->GetCreatureInfo()->rank : 0);
            }

            void OnPlayerReleasedGhost(Player* player) override
            {
                Manager::Instance().HandleEvent(player, "released_ghost");
            }

            void OnSpellCast(Player* player, Spell* /*spell*/, bool /*skipCheck*/) override
            {
                Manager::Instance().HandleEvent(player, "spell_cast");
            }

            void OnUpdateZone(Player* player, uint32 /*newZone*/, uint32 /*newArea*/) override
            {
                Manager::Instance().HandleEvent(player, "zone_changed");
            }

            void OnMapChanged(Player* player) override
            {
                Manager::Instance().HandlePlayerMapChanged(player);
            }

            void OnChatChannel(Player* from, char const* channel, char const* msg) override
            {
                if (!from || !channel || !msg || !*msg)
                    return;
                std::string const channelStr(channel);
                std::string const msgStr(msg);
                ChatScope scope = Manager::Instance().IsWorldChannel(channelStr)
                    ? ChatScope::World : ChatScope::Channel;
                Manager::Instance().HandleChat(from, scope, msgStr, "", channelStr);
            }

            void OnChatWhisper(Player* from, char const* msg) override
            {
                if (!from || !msg || !*msg)
                    return;
                uint64_t const guid = from->GetObjectGuid().GetRawValue();
                std::string target;
                {
                    std::lock_guard<std::mutex> lock(s_whisperTargetsMutex);
                    auto it = s_pendingWhisperTargets.find(guid);
                    if (it != s_pendingWhisperTargets.end() && it->second.second == msg)
                    {
                        target = it->second.first;
                        s_pendingWhisperTargets.erase(it);
                    }
                }
                if (!target.empty())
                {
                    Manager::Instance().HandleChat(from, ChatScope::Whisper, msg, target);
                }
            }

            void OnLogout(Player* player) override
            {
                if (player)
                {
                    std::lock_guard<std::mutex> lock(s_whisperTargetsMutex);
                    s_pendingWhisperTargets.erase(player->GetObjectGuid().GetRawValue());
                }
                if (player) ChannelMembership::Logout(player->GetObjectGuid().GetRawValue());
                Manager::Instance().HandlePlayerLogout(player);
                Manager::Instance().HandleEvent(player, "player_logout");
            }

            void OnLevelChanged(Player* player, uint8 oldLevel) override
            {
                if (player && player->GetLevel() > oldLevel)
                    Manager::Instance().HandleEvent(player, "level_up", std::to_string(player->GetLevel()));
            }

            void OnLearnSpell(Player* player, uint32 spellId) override
            {
                Manager::Instance().HandleEvent(player, "spell_learned", std::to_string(spellId));
            }

            void OnDuelRequest(Player* target, Player* challenger) override
            {
                Manager::Instance().HandleEvent(challenger, "duel_requested", target ? target->GetName() : "");
            }

            void OnDuelStart(Player* player1, Player* player2) override
            {
                Manager::Instance().HandleEvent(player1, "duel_started", player2 ? player2->GetName() : "");
                Manager::Instance().HandleDuelSpectator(player1, player2, 0);
            }

            void OnDuelEnd(Player* winner, Player* loser, uint32 /*type*/) override
            {
                Manager::Instance().HandleEvent(winner, "duel_won", loser ? loser->GetName() : "");
                Manager::Instance().HandleDuelSpectator(winner, loser, 2, winner);
            }

            void OnLogin(Player* player) override
            {
                if (player && player->GetGuildId())
                    Manager::Instance().HandleEvent(player, "guild_login");
            }

            void OnLootItem(Player* player, Item* item, uint32 count, ObjectGuid /*lootGuid*/) override
            {
                std::string detail;
                std::string eventName = "item_looted";
                uint32_t quality = 0;
                if (item && item->GetProto())
                {
                    detail = item->GetProto()->Name1 + " x" + std::to_string(count);
                    quality = item->GetProto()->Quality;
                    if (item->GetProto()->Quality >= ITEM_QUALITY_EPIC)
                        eventName = "epic_item";
                    else if (item->GetProto()->Quality >= ITEM_QUALITY_RARE)
                        eventName = "rare_item";
                }
                Manager::Instance().HandleEvent(player, eventName, detail, 0, 0, 0, quality);
            }

            void OnTextEmote(Player* player, uint32 textEmote, uint32 emoteNum, ObjectGuid guid) override
            {
                if (!player)
                    return;
                Manager::Instance().HandlePlayerTextEmote(player, textEmote, emoteNum, guid);
            }
        };

        class AzerothVoicesServerScript final : public ServerScript
        {
        public:
            AzerothVoicesServerScript()
                : ServerScript("AzerothVoicesServerScript", { SERVERHOOK_CAN_PACKET_RECEIVE, SERVERHOOK_CAN_PACKET_SEND })
            {
            }

            bool CanPacketSend(WorldSession* session, WorldPacket const& original) override
            {
                if (!session || session->IsHeadless() || !session->GetPlayer() ||
                    original.GetOpcode() != SMSG_CHANNEL_NOTIFY)
                    return true;
                try
                {
                    WorldPacket packet(original);
                    packet.rpos(0);
                    uint8 notification = 0;
                    std::string channel;
                    packet >> notification >> channel;
                    uint64_t guid = session->GetPlayer()->GetObjectGuid().GetRawValue();
                    if (notification == CHAT_YOU_JOINED_NOTICE)
                        ChannelMembership::Observe(guid, channel, true);
                    else if (notification == CHAT_YOU_LEFT_NOTICE || notification == CHAT_NOT_MEMBER_NOTICE)
                        ChannelMembership::Observe(guid, channel, false);
                    else if (notification == CHAT_PLAYER_KICKED_NOTICE || notification == CHAT_PLAYER_BANNED_NOTICE)
                    {
                        ObjectGuid target;
                        packet >> target;
                        ChannelMembership::Observe(target.GetRawValue(), channel, false);
                    }
                }
                catch (ByteBufferException const&) { }
                return true;
            }

            bool CanPacketReceive(WorldSession* session, WorldPacket const& original) override
            {
                if (!session || !session->GetPlayer())
                    return true;

                if (original.GetOpcode() == CMSG_MESSAGECHAT)
                {
                    try
                    {
                        WorldPacket packet(original);
                        packet.rpos(0);
                        uint32 type = 0;
                        uint32 language = 0;
                        packet >> type >> language;
                        if (language == LANG_ADDON)
                            return true;

                        if (type == CHAT_MSG_WHISPER)
                        {
                            std::string to;
                            std::string message;
                            packet >> to >> message;
                            if (to.empty() || message.empty())
                                return true;

                            if (Player* sender = session->GetPlayer())
                            {
                                std::lock_guard<std::mutex> lock(s_whisperTargetsMutex);
                                s_pendingWhisperTargets[sender->GetObjectGuid().GetRawValue()] = { to, message };
                            }
                        }
                    }
                    catch (ByteBufferException const&)
                    {
                        // Malformed chat was already rejected by the core handler.
                    }
                    return true;
                }

                if (original.GetOpcode() == CMSG_GAMEOBJ_USE)
                {
                    try
                    {
                        WorldPacket packet(original);
                        packet.rpos(0);
                        ObjectGuid goGuid;
                        packet >> goGuid;
                        Player* player = session->GetPlayer();
                        if (player && player->IsSelfMover() && player->GetMap())
                        {
                            GameObject* obj = player->GetMap()->GetGameObject(goGuid);
                            if (obj && !obj->IsDeleted() && obj->isSpawned() &&
                                obj->GetGoType() != GAMEOBJECT_TYPE_GENERIC &&
                                obj->GetGoType() != GAMEOBJECT_TYPE_QUESTGIVER &&
                                !obj->HasFlag(GAMEOBJECT_FLAGS, GO_FLAG_NO_INTERACT) &&
                                obj->IsAtInteractDistance(player) &&
                                obj->PlayerCanUse(player) &&
                                obj->GetGOInfo())
                            {
                                Manager::Instance().HandleEvent(player, "used_object", obj->GetGOInfo()->name);
                            }
                        }
                    }
                    catch (ByteBufferException const&)
                    {
                    }
                    return true;
                }

                if (original.GetOpcode() == CMSG_GUILD_PROMOTE || original.GetOpcode() == CMSG_GUILD_DEMOTE)
                {
                    try
                    {
                        WorldPacket packet(original);
                        packet.rpos(0);
                        std::string targetName;
                        packet >> targetName;
                        Player* promoter = session->GetPlayer();
                        if (promoter && !targetName.empty())
                        {
                            uint32 const guildId = promoter->GetGuildId();
                            Guild* guild = sGuildMgr.GetGuildById(guildId);
                            uint32 const requiredRight = (original.GetOpcode() == CMSG_GUILD_PROMOTE)
                                ? GR_RIGHT_PROMOTE : GR_RIGHT_DEMOTE;
                            if (guild && guild->HasRankRight(promoter->GetObjectGuid(), requiredRight) &&
                                guild->GetMemberSlot(targetName))
                            {
                                Player* targetPlayer = ObjectAccessor::FindPlayerByName(targetName.c_str());
                                std::string const eventName = (original.GetOpcode() == CMSG_GUILD_PROMOTE) ? "guild_promotion" : "guild_demotion";
                                if (targetPlayer)
                                    Manager::Instance().HandleEvent(targetPlayer, eventName, targetName, guildId);
                                else
                                    Manager::Instance().HandleEvent(promoter, eventName, targetName, guildId);
                            }
                        }
                    }
                    catch (ByteBufferException const&)
                    {
                    }
                    return true;
                }

                return true;
            }
        };

        class AzerothVoicesGuildScript final : public GuildScript
        {
        public:
            AzerothVoicesGuildScript() : GuildScript("AzerothVoicesGuildScript") {}

            void OnAddMember(Guild* guild, Player* player, uint8& /*rank*/) override
            {
                Manager::Instance().HandleEvent(player, "guild_join", "", guild ? guild->GetId() : 0);
            }

            void OnRemoveMember(Guild* guild, Player* player, bool /*isDisbanding*/, bool /*isKicked*/) override
            {
                Manager::Instance().HandleEvent(player, "guild_leave", "", guild ? guild->GetId() : 0);
            }
        };

        class AzerothVoicesUnitScript final : public UnitScript
        {
        public:
            AzerothVoicesUnitScript()
                : UnitScript("AzerothVoicesUnitScript", { UNITHOOK_ON_UNIT_DEATH })
            {
            }

            void OnUnitDeath(Unit* unit, Unit* killer) override
            {
                if (!unit || !killer)
                    return;

                if (unit->GetTypeId() == TYPEID_UNIT && static_cast<Creature*>(unit)->IsPet())
                {
                    Player* killerPlayer = killer->GetCharmerOrOwnerPlayerOrPlayerItself();
                    if (killerPlayer)
                        Manager::Instance().HandleEvent(killerPlayer, "pet_defeated", unit->GetName());
                }
            }
        };

        class AzerothVoicesGameObjectScript final : public AllGameObjectScript
        {
        public:
            AzerothVoicesGameObjectScript() : AllGameObjectScript("AzerothVoicesGameObjectScript") {}

            bool CanGameObjectGossipHello(Player* player, GameObject* go) override
            {
                if (player && go && go->GetGOInfo())
                    Manager::Instance().HandleEvent(player, "used_object", go->GetGOInfo()->name);
                return false;
            }
        };

        class AzerothVoicesGameEventScript final : public GameEventScript
        {
        public:
            AzerothVoicesGameEventScript() : GameEventScript("AzerothVoicesGameEventScript") {}

            bool IsDatabaseBound() const override { return false; }

            void OnStart(uint16 eventId) override
            {
                Manager::Instance().HandleGameEventState(eventId, true);
            }

            void OnStop(uint16 eventId) override
            {
                Manager::Instance().HandleGameEventState(eventId, false);
            }
        };
    }

    void RegisterAzerothVoicesCommand();
    void RegisterAzerothVoicesCombatScripts();

    void RegisterAzerothVoicesScripts()
    {
        new AzerothVoicesWorldScript();
        new AzerothVoicesPlayerScript();
        new AzerothVoicesServerScript();
        new AzerothVoicesGuildScript();
        new AzerothVoicesUnitScript();
        new AzerothVoicesGameObjectScript();
        new AzerothVoicesGameEventScript();
        RegisterAzerothVoicesCombatScripts();
    }
}

void Addmod_azeroth_voicesScripts()
{
    AzerothVoices::RegisterAzerothVoicesScripts();
    AzerothVoices::RegisterAzerothVoicesCommand();
}

void Addmod_twow_azeroth_voicesScripts()
{
    Addmod_azeroth_voicesScripts();
}
