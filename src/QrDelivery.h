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

#ifndef MOD_QR_CODE_QR_DELIVERY_H
#define MOD_QR_CODE_QR_DELIVERY_H

#include "Define.h"

#include <string>
#include <string_view>

class ChatHandler;
class Player;

namespace QrDelivery
{
    /// Sends the grid as system chat, one packet per row.
    void SendChat(ChatHandler* handler, std::string const& grid);

    /// Opens a quest-details frame carrying the grid as the quest description.
    ///
    /// The frame is server-pushed and its strings are inline, so nothing has to exist in
    /// the world DB or the client's quest cache. Accept and Decline are both safe to
    /// leave unhandled: the packet names the player as its own quest giver, which sends
    /// the accept handler down its "object == _player" early-out.
    void SendQuestFrame(Player* player, std::string const& title, std::string const& grid);

    /// Opens a player-sourced gossip menu with a "Show the QR code" option; picking it
    /// delivers the grid as system chat.
    ///
    /// SMSG_GOSSIP_MESSAGE carries no body text - only an npc_text id the client
    /// resolves against its local cache - so the menu's greeting is pushed first as an
    /// unsolicited SMSG_NPC_TEXT_UPDATE under a reserved id; each call overwrites the
    /// previous entry. The window's source is the player, so no NPC has to exist or be
    /// targeted.
    ///
    /// The grid cannot ride in the window itself: the client stops opening a gossip
    /// window somewhere between 3 and 4 KB of body text, and a full-size grid in the
    /// quest-details string crashes it outright, which is why the hand-off target is
    /// chat. The grid is parked per player until the option is picked, the window is
    /// closed, or the player logs out.
    void SendGossipQrMenu(Player* player, std::string const& grid);

    /// Doubles every '|' so a payload echo cannot inject UI escape sequences of its own.
    std::string EscapeUiSequences(std::string_view text);
}

#endif // MOD_QR_CODE_QR_DELIVERY_H
