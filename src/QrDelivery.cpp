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
#include "Opcodes.h"
#include "Player.h"
#include "QuestDef.h"
#include "WorldPacket.h"
#include "WorldSession.h"

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
