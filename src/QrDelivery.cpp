/*
 * This file is part of the AzerothCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Affero General Public License as published by the
 * Free Software Foundation; either version 3 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "QrDelivery.h"
#include "Chat.h"
#include "GossipDef.h"
#include "NPCHandler.h"
#include "ObjectGuid.h"
#include "Opcodes.h"
#include "Player.h"
#include "QuestDef.h"
#include "ScriptMgr.h"
#include "WorldPacket.h"
#include "WorldSession.h"

#include <unordered_map>

namespace
{
    /// npc_text id the gossip windows publish their body text under. It has to sit in
    /// the range the client demonstrably caches - the frame is not shown until the id
    /// resolves, so an id the cache rejects leaves the window waiting forever. This one
    /// is just below DEFAULT_GOSSIP_MESSAGE (16777215), the highest id the client
    /// handles in every session, and above the rest of the world DB, which stops at
    /// 921061.
    constexpr uint32 QR_GOSSIP_TEXT_ID = 16777200;

    /// Menu id stamped on the gossip so the select handler can recognise it. The client
    /// echoes it in CMSG_GOSSIP_SELECT_OPTION, and for a player-sourced gossip the core
    /// routes that to OnPlayerGossipSelect with this id.
    constexpr uint32 QR_GOSSIP_MENU_ID = 16777200;

    constexpr uint32 QR_GOSSIP_ACTION_CLOSE = 0;
    constexpr uint32 QR_GOSSIP_ACTION_SHOW  = 1;

    /// Grids waiting for their "Show the QR code" click, keyed by GUID rather than
    /// Player* because the entry must not outlive a relog unnoticed - it is erased on
    /// select, on Close and on logout.
    std::unordered_map<ObjectGuid, std::string> _pendingQr;

    /// Publishes @p greeting to the client's npc_text cache under QR_GOSSIP_TEXT_ID.
    ///
    /// Field order mirrors HandleNpcTextQueryOpcode's reply exactly - the client
    /// accepts the packet unsolicited and overwrites its cache entry, but only reads
    /// it whole, so all eight text options have to be present. Option 0 carries the
    /// greeting at probability 1; the rest stay empty at probability 0 so they are
    /// never picked.
    void PushGossipText(Player* player, std::string const& greeting)
    {
        WorldPacket data(SMSG_NPC_TEXT_UPDATE, 64 + greeting.size() * 2);
        data << uint32(QR_GOSSIP_TEXT_ID);

        std::string const empty;
        for (uint8 i = 0; i < MAX_GOSSIP_TEXT_OPTIONS; ++i)
        {
            std::string const& text = i == 0 ? greeting : empty;

            data << float(i == 0 ? 1.0f : 0.0f);                // probability
            data << text;                                       // male text
            data << text;                                       // female text
            data << uint32(0);                                  // language

            for (uint8 j = 0; j < MAX_GOSSIP_TEXT_EMOTES; ++j)
            {
                data << uint32(0);                              // emote delay
                data << uint32(0);                              // emote id
            }
        }

        player->GetSession()->SendPacket(&data);
    }

    /// Opens the player-sourced gossip window on @p greeting, with the "Show the QR
    /// code" option ahead of the Close one.
    ///
    /// The text push has to land in the cache before the window opens, but both packets
    /// travel the same ordered stream, so sending them back to back is enough.
    void OpenGossipWindow(Player* player, std::string const& greeting)
    {
        PushGossipText(player, greeting);

        PlayerMenu* menu = player->PlayerTalkClass;
        menu->ClearMenus();
        menu->GetGossipMenu().SetMenuId(QR_GOSSIP_MENU_ID);
        menu->GetGossipMenu().AddMenuItem(0, GOSSIP_ICON_INTERACT_1, "Show the QR code", 0,
            QR_GOSSIP_ACTION_SHOW, "", 0);
        menu->GetGossipMenu().AddMenuItem(1, GOSSIP_ICON_CHAT, "Close", 0, QR_GOSSIP_ACTION_CLOSE, "", 0);
        menu->SendGossipMenu(QR_GOSSIP_TEXT_ID, player->GetGUID());
    }
}

namespace QrDelivery
{
    void SendChat(ChatHandler* handler, std::string const& grid)
    {
        // SendSysMessage already splits on '\n' and builds one CHAT_MSG_SYSTEM packet per
        // line. It is used instead of PSendSysMessage because the latter runs the string
        // through Acore::StringFormat, which would read a '{' in the grid as a placeholder.
        // escapeCharacters stays false so the '|' escapes reach the client intact.
        handler->SendSysMessage(grid);
    }

    void SendQuestFrame(Player* player, std::string const& title, std::string const& grid)
    {
        // Hand-built rather than routed through PlayerMenu::SendQuestGiverQuestDetails,
        // which needs a real Quest const* from sObjectMgr. The field order mirrors that
        // function exactly - the client reads the packet straight through, so every
        // trailing field has to be present even though they are all zero here.
        WorldPacket data(SMSG_QUESTGIVER_QUEST_DETAILS, 512);
        data << player->GetGUID();                              // quest giver: the player itself
        data << player->GetDivider();
        data << uint32(0);                                      // quest id
        data << title;
        data << grid;                                           // quest details
        data << std::string();                                  // quest objectives
        data << uint8(0);                                       // auto finish
        data << uint32(0);                                      // quest flags
        data << uint32(0);                                      // suggested players
        data << uint8(0);                                       // is finished

        data << uint32(0);                                      // reward choice item count
        data << uint32(0);                                      // reward item count
        data << uint32(0);                                      // reward money
        data << uint32(0);                                      // reward xp
        data << uint32(0);                                      // reward honor
        data << float(0.0f);                                    // honor multiplier
        data << uint32(0);                                      // reward spell
        data << int32(0);                                       // reward spell cast
        data << uint32(0);                                      // char title id
        data << uint32(0);                                      // bonus talents
        data << uint32(0);                                      // reward arena points
        data << uint32(0);                                      // unk

        for (uint32 i = 0; i < QUEST_REPUTATIONS_COUNT; ++i)
            data << uint32(0);                                  // reward faction id

        for (uint32 i = 0; i < QUEST_REPUTATIONS_COUNT; ++i)
            data << int32(0);                                   // reward faction value id

        for (uint32 i = 0; i < QUEST_REPUTATIONS_COUNT; ++i)
            data << int32(0);                                   // reward faction value id override

        data << uint32(QUEST_EMOTE_COUNT);
        for (uint32 i = 0; i < QUEST_EMOTE_COUNT; ++i)
        {
            data << uint32(0);                                  // details emote
            data << uint32(0);                                  // details emote delay
        }

        player->GetSession()->SendPacket(&data);
    }

    void SendGossipQrMenu(Player* player, std::string const& grid)
    {
        _pendingQr[player->GetGUID()] = grid;
        OpenGossipWindow(player, "A QR code is ready.\n\nPick the option below to print it in the chat frame.");
    }

    std::string EscapeUiSequences(std::string_view text)
    {
        std::string escaped;
        escaped.reserve(text.size());

        for (char const c : text)
        {
            escaped += c;
            if (c == '|')
                escaped += '|';
        }

        return escaped;
    }
}

/// Serves the QR gossip menu's clicks. Player-sourced gossip has no NPC script behind
/// it, so without this the options would do nothing.
class qr_code_player : public PlayerScript
{
public:
    qr_code_player() : PlayerScript("qr_code_player", { PLAYERHOOK_ON_GOSSIP_SELECT, PLAYERHOOK_ON_LOGOUT }) { }

    void OnPlayerGossipSelect(Player* player, uint32 menu_id, uint32 /*sender*/, uint32 action) override
    {
        if (menu_id != QR_GOSSIP_MENU_ID)
            return;

        player->PlayerTalkClass->SendCloseGossip();

        auto const itr = _pendingQr.find(player->GetGUID());
        if (itr == _pendingQr.end())
            return;

        if (action == QR_GOSSIP_ACTION_SHOW)
        {
            ChatHandler handler(player->GetSession());
            QrDelivery::SendChat(&handler, itr->second);
        }

        _pendingQr.erase(itr);
    }

    void OnPlayerLogout(Player* player) override
    {
        _pendingQr.erase(player->GetGUID());
    }
};

void AddSC_qr_code_player()
{
    new qr_code_player();
}
