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

#include "Chat.h"
#include "GameTime.h"
#include "ObjectGuid.h"
#include "Player.h"
#include "QrConfig.h"
#include "QrDelivery.h"
#include "QrEncoder.h"
#include "QrRenderer.h"
#include "ScriptMgr.h"

#include <optional>
#include <string>
#include <unordered_map>

using namespace Acore::ChatCommands;

namespace
{
    /// Widest checkerboard `.qr grid` will draw, so a typo cannot ask for a megabyte.
    constexpr uint32 QR_GRID_MAX_SIDE = 64;

    /// Side used when `.qr grid` is given no arguments - big enough to show a seam or an
    /// overlap across several rows, small enough to take in at a glance.
    constexpr uint32 QR_GRID_DEFAULT_SIDE = 10;

    /// Longest run `.qr probe` will draw. The point of the command is to find the
    /// client's real inbound line cap, so this only has to be comfortably above it.
    constexpr uint32 QR_PROBE_MAX_MODULES = 2048;

    /// Longest payload echo shown as the quest frame's title, in bytes.
    constexpr std::size_t QR_TITLE_MAX_LENGTH = 120;

    /// Truncates to at most @p maxBytes without splitting a UTF-8 sequence, which would
    /// put an invalid string in the quest packet.
    std::string TruncateUtf8(std::string const& text, std::size_t maxBytes)
    {
        if (text.size() <= maxBytes)
            return text;

        std::size_t length = maxBytes;
        while (length && (static_cast<unsigned char>(text[length]) & 0xC0) == 0x80)
            --length;

        return text.substr(0, length);
    }

    /// Per-player cooldown, keyed by GUID rather than Player* because the entry outlives
    /// the session it was created for.
    std::unordered_map<ObjectGuid, time_t> _lastUse;

    /// Returns 0 when the player may fire a command now, otherwise the seconds left.
    uint32 CheckCooldown(Player* player)
    {
        if (!sQrConfig->CooldownSeconds)
            return 0;

        time_t const now = GameTime::GetGameTime().count();

        // The map only ever holds live cooldowns, but nothing prunes it on logout, so
        // sweep the expired entries whenever it grows past a session-sized population.
        if (_lastUse.size() > 1000)
        {
            for (auto itr = _lastUse.begin(); itr != _lastUse.end();)
                itr = (now - itr->second >= time_t(sQrConfig->CooldownSeconds)) ? _lastUse.erase(itr) : ++itr;
        }

        auto const itr = _lastUse.find(player->GetGUID());
        if (itr != _lastUse.end())
        {
            time_t const elapsed = now - itr->second;
            if (elapsed < time_t(sQrConfig->CooldownSeconds))
                return uint32(sQrConfig->CooldownSeconds - elapsed);
        }

        _lastUse[player->GetGUID()] = now;
        return 0;
    }

    /// Runs the shared gate every `.qr` subcommand needs: module enabled, real session,
    /// off cooldown. Reports the failure to @p handler and returns nullptr when blocked.
    Player* AcquirePlayer(ChatHandler* handler)
    {
        if (!sQrConfig->Enabled)
        {
            handler->SendErrorMessage("The QR code module is disabled.");
            return nullptr;
        }

        Player* player = handler->GetPlayer();
        if (!player)
        {
            handler->SendErrorMessage("This command needs an in-game session.");
            return nullptr;
        }

        if (uint32 const remaining = CheckCooldown(player))
        {
            handler->SendErrorMessage("Wait {} more second(s) before generating another QR code.", remaining);
            return nullptr;
        }

        return player;
    }

    /// Reports a render failure in terms the admin can act on: both failure modes are
    /// geometry or config problems, not user mistakes.
    void ReportRenderError(ChatHandler* handler, QrRenderResult const& result, QrRenderGeometry const& geometry)
    {
        switch (result.error)
        {
            case QrRenderError::RowTooWide:
                handler->SendErrorMessage(
                    "The code needs {} px per row but the frame only fits {} px. Lower QRCode.MaxVersion or the "
                    "backend's ModuleWidth.", result.rowWidthPx, geometry.maxRowWidthPx);
                break;
            case QrRenderError::PayloadTooLarge:
                handler->SendErrorMessage(
                    "The rendered code is {} bytes, over the {} byte QRCode.MaxPayloadBytes limit. Shorten the "
                    "payload or lower QRCode.MaxVersion.", result.byteCount, geometry.maxPayloadBytes);
                break;
            case QrRenderError::None:
            default:
                handler->SendErrorMessage("The QR code renderer produced nothing.");
                break;
        }
    }

    void Deliver(ChatHandler* handler, Player* player, std::string const& grid, std::string const& title)
    {
        if (sQrConfig->Backend == QrBackend::Quest)
            QrDelivery::SendQuestFrame(player, title, grid);
        else
            QrDelivery::SendChat(handler, grid);
    }

    /// Validates, encodes and renders one payload against @p geometry. An empty optional
    /// means the failure has already been reported to @p handler.
    std::optional<std::string> BuildQrGrid(ChatHandler* handler, std::string const& text,
        QrRenderGeometry const& geometry)
    {
        if (text.size() > sQrConfig->MaxInputLength)
        {
            handler->SendErrorMessage("Input is {} bytes, over the {} byte QRCode.MaxInputLength limit.",
                text.size(), sQrConfig->MaxInputLength);
            return std::nullopt;
        }

        std::optional<QrBitmap> const bitmap = EncodeQr(text, sQrConfig->MaxVersion, sQrConfig->Ecc);
        if (!bitmap)
        {
            // The encoder owns capacity, so the limit quoted here is measured from it
            // rather than from a second table that could drift out of agreement with it.
            handler->SendErrorMessage("'{}' does not fit a version {} code at ECC {} (max {} bytes).",
                text, sQrConfig->MaxVersion, QrEccName(sQrConfig->Ecc),
                MaxQrPayloadBytes(sQrConfig->MaxVersion, sQrConfig->Ecc));
            return std::nullopt;
        }

        QrRenderResult const result = RenderQr(*bitmap, geometry);
        if (result.error != QrRenderError::None)
        {
            ReportRenderError(handler, result, geometry);
            return std::nullopt;
        }

        return result.text;
    }
}

class qr_code_commandscript : public CommandScript
{
public:
    qr_code_commandscript() : CommandScript("qr_code_commandscript") { }

    ChatCommandTable GetCommands() const override
    {
        static ChatCommandTable qrTable =
        {
            { "probe",  HandleQrProbeCommand,  SEC_GAMEMASTER, Console::No },
            { "grid",   HandleQrGridCommand,   SEC_GAMEMASTER, Console::No },
            { "gossip", HandleQrGossipCommand, SEC_GAMEMASTER, Console::No },
            { "",       HandleQrCommand,       SEC_GAMEMASTER, Console::No },
        };

        static ChatCommandTable commandTable =
        {
            { "qr", qrTable },
        };

        return commandTable;
    }

    static bool HandleQrCommand(ChatHandler* handler, Tail payload)
    {
        Player* player = AcquirePlayer(handler);
        if (!player)
            return false;

        std::string const text(payload);
        if (text.empty())
        {
            handler->SendErrorMessage("Usage: .qr <text>");
            return false;
        }

        std::optional<std::string> const grid = BuildQrGrid(handler, text, sQrConfig->ActiveGeometry());
        if (!grid)
            return false;

        Deliver(handler, player, *grid, QrDelivery::EscapeUiSequences(TruncateUtf8(text, QR_TITLE_MAX_LENGTH)));
        return true;
    }

    /// Same pipeline as the bare `.qr`, but delivered through the gossip menu: a gossip
    /// window opens with a "Show the QR code" option, and picking it prints the code in
    /// the chat frame. The window needs no NPC - the player is its own gossip source.
    /// The indirection exists because the gossip body text itself caps out between 3
    /// and 4 KB client-side, far below what a QR grid needs; the grid therefore renders
    /// at the chat geometry, since chat is where it ends up.
    static bool HandleQrGossipCommand(ChatHandler* handler, Tail payload)
    {
        Player* player = AcquirePlayer(handler);
        if (!player)
            return false;

        std::string const text(payload);
        if (text.empty())
        {
            handler->SendErrorMessage("Usage: .qr gossip <text>");
            return false;
        }

        std::optional<std::string> const grid = BuildQrGrid(handler, text, sQrConfig->ChatGeometry);
        if (!grid)
            return false;

        QrDelivery::SendGossipQrMenu(player, *grid);
        return true;
    }

    /// Draws a single row of alternating modules capped by a three-module black sentinel.
    ///
    /// Alternating defeats run merging, so the row costs one escape per module and grows
    /// predictably. If the sentinel arrives intact the whole line survived the trip, which
    /// makes binary-searching @p modules the way to find the client's real inbound cap and
    /// therefore the right value for QRCode.MaxPayloadBytes.
    static bool HandleQrProbeCommand(ChatHandler* handler, uint32 modules)
    {
        Player* player = AcquirePlayer(handler);
        if (!player)
            return false;

        if (!modules || modules > QR_PROBE_MAX_MODULES)
        {
            handler->SendErrorMessage("Usage: .qr probe <1-{}>", QR_PROBE_MAX_MODULES);
            return false;
        }

        std::vector<bool> row;
        row.reserve(modules + 4);

        for (uint32 i = 0; i < modules; ++i)
            row.push_back(i % 2 == 0);

        // A light gap keeps the sentinel from merging into a trailing dark module.
        row.push_back(false);
        row.push_back(true);
        row.push_back(true);
        row.push_back(true);

        // The row-width cap is deliberately dropped: a probe row is meant to be far wider
        // than any frame, and the point is whether the string survives, not how it looks.
        QrRenderGeometry geometry = sQrConfig->ActiveGeometry();
        geometry.maxRowWidthPx = 0;
        geometry.maxPayloadBytes = 0;

        QrRenderResult const result = RenderModuleGrid(row, uint32(row.size()), 1, geometry);
        if (result.error != QrRenderError::None)
        {
            ReportRenderError(handler, result, geometry);
            return false;
        }

        handler->PSendSysMessage("Probe: {} modules, {} bytes. The line ends in three black squares.",
            row.size(), result.byteCount);
        Deliver(handler, player, result.text, "QR probe");
        return true;
    }

    /// Draws a plain checkerboard at the active geometry, with no QR encoding involved.
    ///
    /// Seams, overlapping rows and an inverted offY sign are all obvious at a glance here,
    /// which separates "the geometry is wrong" from "the encoder is wrong" while tuning
    /// LineAdvance blind.
    /// Both sides are optional: bare `.qr grid` draws QR_GRID_DEFAULT_SIDE squared, and a
    /// single argument means a square of that size.
    static bool HandleQrGridCommand(ChatHandler* handler, Optional<uint32> rowsArg, Optional<uint32> colsArg)
    {
        Player* player = AcquirePlayer(handler);
        if (!player)
            return false;

        uint32 const rows = rowsArg.value_or(QR_GRID_DEFAULT_SIDE);
        uint32 const cols = colsArg.value_or(rows);

        if (!rows || !cols || rows > QR_GRID_MAX_SIDE || cols > QR_GRID_MAX_SIDE)
        {
            handler->SendErrorMessage("Usage: .qr grid [rows] [cols], each 1-{}.", QR_GRID_MAX_SIDE);
            return false;
        }

        std::vector<bool> modules(std::size_t(rows) * cols);
        for (uint32 y = 0; y < rows; ++y)
            for (uint32 x = 0; x < cols; ++x)
                modules[std::size_t(y) * cols + x] = (x + y) % 2 == 0;

        QrRenderGeometry const& geometry = sQrConfig->ActiveGeometry();
        QrRenderResult const result = RenderModuleGrid(modules, cols, rows, geometry);
        if (result.error != QrRenderError::None)
        {
            ReportRenderError(handler, result, geometry);
            return false;
        }

        handler->PSendSysMessage("Grid: {}x{} modules at {}x{} px, line advance {}, {} bytes.",
            cols, rows, geometry.moduleWidth, geometry.moduleHeight, geometry.lineAdvance, result.byteCount);
        Deliver(handler, player, result.text, "QR grid");
        return true;
    }
};

void AddSC_qr_code_commandscript()
{
    new qr_code_commandscript();
}
