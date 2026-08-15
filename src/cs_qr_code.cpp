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

#include "AES.h"
#include "AccountMgr.h"
#include "Base32.h"
#include "Chat.h"
#include "CryptoGenerics.h"
#include "CryptoRandom.h"
#include "DatabaseEnv.h"
#include "GameTime.h"
#include "Language.h"
#include "Log.h"
#include "ObjectGuid.h"
#include "Player.h"
#include "QrConfig.h"
#include "QrDelivery.h"
#include "QrEncoder.h"
#include "QrOtpAuth.h"
#include "QrRenderer.h"
#include "RBAC.h"
#include "Realm.h"
#include "ScriptMgr.h"
#include "SecretMgr.h"
#include "StringFormat.h"
#include "TOTP.h"
#include "World.h" // for the `realm` global
#include "WorldSession.h"

#include <algorithm>
#include <cctype>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

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

    /// Width of each `.qr swatch` band, in modules. Wide enough to judge whether a crop is
    /// flat across a run, which is what a merged run of modules will stretch it over.
    constexpr uint32 QR_SWATCH_WIDTH_MODULES = 32;

    /// Height of each `.qr swatch` band, in module rows.
    constexpr uint32 QR_SWATCH_BAND_ROWS = 3;

    /// Cells per side `.qr sweep` divides a texture into when given no count.
    constexpr uint32 QR_SWEEP_DEFAULT_CELLS = 4;

    /// Most cells per side `.qr sweep` will accept. The sweep prints one chat line per cell, so
    /// the cap is really a cap on how much chat one command may fill.
    constexpr uint32 QR_SWEEP_MAX_CELLS = 8;

    /// Width of one `.qr sweep` bar, in modules. Wide enough to see a colour, short enough to
    /// leave the coordinates beside it readable on the same line.
    constexpr uint32 QR_SWEEP_BAR_MODULES = 12;

    /// Basis `.qr sweep` states its crop against.
    ///
    /// Deliberately far larger than any client texture. The client divides the crop edges by the
    /// stated dimensions, so quoting a basis of 1000 against a 64 px icon addresses it in
    /// sixteenths of a texel - and sub-texel precision is the whole point, because a window
    /// narrower than one texel cannot contain a gradient no matter what the artwork does. Stating
    /// the real size instead limits a window to whole texels, and four texels of a faceted gem
    /// icon is enough to straddle a highlight edge and hand every module a visible ramp.
    constexpr uint32 QR_SWEEP_SAMPLE_BASIS = 1000;

    /// Half-width of the window sampled at each cell centre, against QR_SWEEP_SAMPLE_BASIS.
    ///
    /// Six units of a thousand is about four tenths of a texel on a 64 px icon: comfortably inside
    /// one, while still wide enough that the client has a region to filter rather than a degenerate
    /// zero-width rect.
    constexpr uint32 QR_SWEEP_SAMPLE_HALF = 3;

    // The leftmost and topmost sample sits half a cell in, so the window has to be narrower than
    // that or the subtraction below wraps.
    static_assert((QR_SWEEP_SAMPLE_BASIS / 2) / QR_SWEEP_MAX_CELLS > QR_SWEEP_SAMPLE_HALF,
        "sample window must fit inside the first cell of the finest sweep");

    /// Whether @p path is safe to drop into a `|T` escape.
    ///
    /// The argument is player-typed and lands between the escape's own delimiters, so a '|' or
    /// ':' in it would close the escape early and hand the rest of the string to the client as
    /// markup. Restricting to the characters a texture path can actually contain closes that
    /// without needing to know what the client will accept.
    bool IsSafeTexturePath(std::string const& path)
    {
        if (path.empty())
            return false;

        return std::all_of(path.begin(), path.end(), [](unsigned char c)
        {
            return std::isalnum(c) || c == '/' || c == '\\' || c == '-' || c == '_' || c == '.' || c == ' ';
        });
    }

    /// Whether @p coords is a well-formed trailing crop, i.e. digits and ':' only.
    ///
    /// The client is left to judge the field count; this only keeps the escape from being
    /// broken out of, same as @ref IsSafeTexturePath.
    bool IsSafeTexCoords(std::string const& coords)
    {
        return std::all_of(coords.begin(), coords.end(), [](unsigned char c)
        {
            return std::isdigit(c) || c == ':';
        });
    }

    std::string JoinNames(std::vector<std::string> const& names)
    {
        std::string joined;

        for (std::string const& name : names)
        {
            if (!joined.empty())
                joined += ", ";

            joined += name;
        }

        return joined;
    }

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
    ///
    /// @p rateLimited is what charges the cooldown, so it belongs to calls that actually draw
    /// something. A command that only validates a token ships nothing and must not be locked
    /// out by the cooldown its own preceding draw took.
    Player* AcquirePlayer(ChatHandler* handler, bool rateLimited = true)
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

        if (rateLimited)
        {
            if (uint32 const remaining = CheckCooldown(player))
            {
                handler->SendErrorMessage("Wait {} more second(s) before generating another QR code.", remaining);
                return nullptr;
            }
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
    ///
    /// @p subject names the payload in the too-long error instead of quoting it. Callers whose
    /// payload is a secret must pass one: the player typed the input to `.qr`, but nobody
    /// wants their 2FA key echoed back across the chat frame by a config error.
    std::optional<std::string> BuildQrGrid(ChatHandler* handler, std::string const& text,
        QrRenderGeometry const& geometry, std::string_view subject = {})
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
            uint32 const capacity = MaxQrPayloadBytes(sQrConfig->MaxVersion, sQrConfig->Ecc);

            if (subject.empty())
                handler->SendErrorMessage("'{}' does not fit a version {} code at ECC {} (max {} bytes).",
                    text, sQrConfig->MaxVersion, QrEccName(sQrConfig->Ecc), capacity);
            else
                handler->SendErrorMessage("{} is {} bytes and does not fit a version {} code at ECC {} "
                    "(max {} bytes). Raise QRCode.MaxVersion.", subject, text.size(),
                    sQrConfig->MaxVersion, QrEccName(sQrConfig->Ecc), capacity);

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
            { "say",    HandleQrSayCommand,    SEC_GAMEMASTER, Console::No },
            { "color",  HandleQrColorCommand,  SEC_GAMEMASTER, Console::No },
            { "swatch", HandleQrSwatchCommand, SEC_GAMEMASTER, Console::No },
            { "sweep",  HandleQrSweepCommand,  SEC_GAMEMASTER, Console::No },
            { "",       HandleQrCommand,       SEC_GAMEMASTER, Console::No },
        };

        // The core owns `account 2fa`; naming the full path grafts this handler onto that
        // node instead of replacing it, whichever script the command loader reaches first.
        static ChatCommandTable commandTable =
        {
            { "qr", qrTable },
            { "account 2fa qrcode", HandleAccount2FAQrCodeCommand, rbac::RBAC_PERM_COMMAND_ACCOUNT, Console::No },
        };

        return commandTable;
    }

    /// Enrols two-factor authentication without ever typing the key: bare `.account 2fa qrcode`
    /// draws a new key as a code to scan, and `.account 2fa qrcode <token>` turns 2FA on once a
    /// token generated from that key proves the scan worked.
    ///
    /// Nothing is written to the account until the token checks out. A key that were stored on
    /// the draw alone would lock the player out at the next login whenever the scan silently
    /// failed, recoverable only by a GM running `.account set 2fa <account> off`.
    ///
    /// This is a self-contained alternative to `.account 2fa setup`, not a companion to it.
    /// The core holds the key that command offers in a static local to its own handler, so
    /// nothing outside it can render that key; the secret below is a second, independent one,
    /// and its token only completes this command. Both write the same column and neither runs
    /// once 2FA is on, so the two flows cannot collide - a player takes one or the other.
    static bool HandleAccount2FAQrCodeCommand(ChatHandler* handler, Optional<uint32> token)
    {
        // Refused rather than unregistered: the command tree is built once, so gating the
        // registration would need a restart to undo, and this is a setting an admin turns on
        // after checking a 35-line code fits their frame.
        if (!sQrConfig->TwoFAEnabled)
        {
            handler->SendErrorMessage("Two-factor setup by QR code is disabled on this realm. Use "
                "\".account 2fa setup\" instead.");
            return false;
        }

        // The confirming call draws nothing, so it must not be refused by the cooldown that the
        // draw it is confirming has just taken.
        Player* player = AcquirePlayer(handler, !token);
        if (!player)
            return false;

        auto const& masterKey = sSecretMgr->GetSecret(SECRET_TOTP_MASTER_KEY);
        if (!masterKey.IsAvailable())
        {
            handler->SendErrorMessage(LANG_2FA_COMMANDS_NOT_SETUP);
            return false;
        }

        uint32 const accountId = handler->GetSession()->GetAccountId();

        { // a configured account's secret is never shown again, and this command is no
          // exception: it would turn any unattended session into a cloned authenticator
            auto* stmt = LoginDatabase.GetPreparedStatement(LOGIN_SEL_ACCOUNT_TOTP_SECRET);
            stmt->SetData(0, accountId);
            PreparedQueryResult result = LoginDatabase.Query(stmt);

            if (!result)
            {
                LOG_ERROR("module.qrcode", "Account {} not found in login database when processing "
                    ".account 2fa qrcode command.", accountId);
                handler->SendErrorMessage(LANG_UNKNOWN_ERROR);
                return false;
            }

            if (!result->Fetch()->IsNull())
            {
                handler->SendErrorMessage(LANG_2FA_ALREADY_SETUP);
                return false;
            }
        }

        // Keys drawn but not yet confirmed, kept until they are used or the server restarts.
        // Redrawing has to hand out the same key: a player who scanned the first code and then
        // asked for it again would otherwise be holding an authenticator entry for a key no
        // token can ever confirm.
        static std::unordered_map<uint32, Acore::Crypto::TOTP::Secret> pendingSecrets;

        if (token)
        {
            auto const itr = pendingSecrets.find(accountId);
            if (itr == pendingSecrets.end())
            {
                handler->SendErrorMessage("There is no code waiting to be confirmed. Run "
                    "\".account 2fa qrcode\" first, scan it, then run this command with the token it shows.");
                return false;
            }

            if (!Acore::Crypto::TOTP::ValidateToken(itr->second, *token))
            {
                handler->SendErrorMessage(LANG_2FA_INVALID_TOKEN);
                return false;
            }

            if (masterKey)
                Acore::Crypto::AEEncryptWithRandomIV<Acore::Crypto::AES>(itr->second, *masterKey);

            auto* stmt = LoginDatabase.GetPreparedStatement(LOGIN_UPD_ACCOUNT_TOTP_SECRET);
            stmt->SetData(0, itr->second);
            stmt->SetData(1, accountId);
            LoginDatabase.Execute(stmt);

            pendingSecrets.erase(itr);
            handler->SendSysMessage(LANG_2FA_SETUP_COMPLETE);
            return true;
        }

        std::string accountName;
        if (!AccountMgr::GetName(accountId, accountName))
        {
            LOG_ERROR("module.qrcode", "Account {} has no name in the login database.", accountId);
            handler->SendErrorMessage(LANG_UNKNOWN_ERROR);
            return false;
        }

        // std::vector's 1-argument std::size_t constructor invokes resize, so a fresh entry comes
        // out at the right length and is then filled with the key itself.
        auto const [itr, inserted] = pendingSecrets.emplace(std::piecewise_construct,
            std::make_tuple(accountId), std::make_tuple(Acore::Crypto::TOTP::RECOMMENDED_SECRET_LENGTH));
        if (inserted)
            Acore::Crypto::GetRandomBytes(itr->second);

        std::string const key = Acore::Encoding::Base32::Encode(itr->second);
        std::string const uri = BuildOtpAuthUri(
            sQrConfig->TwoFAIssuer.empty() ? realm.Name : sQrConfig->TwoFAIssuer, accountName, key);

        std::optional<std::string> const grid = BuildQrGrid(handler, uri, sQrConfig->ActiveGeometry(),
            "The two-factor setup link");
        if (!grid)
            return false;

        // The quest title is never given the payload, which carries the key.
        Deliver(handler, player, *grid, "2FA setup");
        handler->PSendSysMessage("Scan this code with your authenticator app, then run "
            "\".account 2fa qrcode <token>\" with the token it shows to switch two-factor "
            "authentication on. Nothing changes on the account until you do.");
        handler->PSendSysMessage("If you cannot scan it, add this key by hand instead: {}", key);
        return true;
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

    /// Same pipeline as the bare `.qr`, but said aloud instead of shown to the caller alone:
    /// the code goes out as the GM's own say chat, so every client in say range draws it and
    /// anyone standing there can scan it off their own screen.
    ///
    /// The grid renders at the chat geometry whatever the configured backend is, since the
    /// chat frame is where it lands, exactly as `.qr gossip` does.
    static bool HandleQrSayCommand(ChatHandler* handler, Tail payload)
    {
        Player* player = AcquirePlayer(handler);
        if (!player)
            return false;

        std::string const text(payload);
        if (text.empty())
        {
            handler->SendErrorMessage("Usage: .qr say <text>");
            return false;
        }

        std::optional<std::string> const grid = BuildQrGrid(handler, text, sQrConfig->ChatGeometry);
        if (!grid)
            return false;

        QrDelivery::SendSay(player, *grid);
        return true;
    }

    /// Draws a code in one of the realm's configured colours, or lists them when given no name.
    ///
    /// Only the dark modules change colour: light stays white, because light is what a decoder
    /// thresholds against and colouring it spends the contrast the code is carrying.
    static bool HandleQrColorCommand(ChatHandler* handler, Optional<std::string> name, Tail payload)
    {
        // Gated before anything is reported, but without charging the cooldown: listing the
        // colours draws nothing, and asking what is available should not be refused because of
        // the draw that prompted the question.
        if (!AcquirePlayer(handler, false))
            return false;

        std::vector<std::string> const names = sQrConfig->PaletteNames();

        if (names.empty())
        {
            handler->SendErrorMessage("No colours are configured. Add a QRCode.Palette.<name>.DarkTexture "
                "to qrcode.conf, then \".reload config\". Use \".qr swatch\" to check a texture first.");
            return false;
        }

        // Listing is what an admin wants first and costs nothing to draw, so it runs before the
        // cooldown is charged.
        if (!name)
        {
            handler->PSendSysMessage("Usage: .qr color <name> <text>. Configured: {}", JoinNames(names));
            return true;
        }

        // Escaped because it is echoed straight back: the name is whatever the player typed, and
        // an unmatched one is exactly the case where that is not a colour at all.
        std::string const safeName = QrDelivery::EscapeUiSequences(*name);

        QrPalette const* palette = sQrConfig->FindPalette(*name);
        if (!palette)
        {
            handler->SendErrorMessage("No colour named '{}'. Configured: {}", safeName, JoinNames(names));
            return false;
        }

        std::string const text(payload);
        if (text.empty())
        {
            handler->SendErrorMessage("Usage: .qr color {} <text>", safeName);
            return false;
        }

        Player* player = AcquirePlayer(handler);
        if (!player)
            return false;

        QrRenderGeometry const base = sQrConfig->ActiveGeometry();
        QrRenderGeometry const geometry = ApplyPalette(base, *palette);

        std::optional<std::string> const grid = BuildQrGrid(handler, text, geometry);
        if (!grid)
            return false;

        Deliver(handler, player, *grid,
            QrDelivery::EscapeUiSequences(TruncateUtf8(text, QR_TITLE_MAX_LENGTH)));

        // Losing the packed styles multiplies the line count, and a code that suddenly needs
        // three times the frame height is worth explaining rather than leaving to be discovered.
        if (geometry.rowsPerLine < base.rowsPerLine)
            handler->PSendSysMessage("'{}' has no pack textures, so this drew one module row per line - "
                "{}x the lines the same code takes in black and white.", safeName, base.rowsPerLine);

        return true;
    }

    /// Draws a candidate texture three ways so its fitness as a module colour can be judged by
    /// eye, which is the only way available: the server cannot read the client's art.
    ///
    /// The three bands answer the three questions in order. Solid shows whether the crop is
    /// flat, since a gradient stretched across a merged run reads as a smear rather than a
    /// module. Against white is what the decoder actually sees, so an unreadable second band
    /// means the colour is too light whatever it looks like on its own. Against the configured
    /// dark says whether the colour is distinguishable from plain black - if that band looks
    /// solid, the palette buys nothing over the default.
    static bool HandleQrSwatchCommand(ChatHandler* handler, Optional<std::string> texture, Tail texCoords)
    {
        Player* player = AcquirePlayer(handler);
        if (!player)
            return false;

        if (!texture)
        {
            handler->SendErrorMessage("Usage: .qr swatch <texture> [texCoords]. Example: "
                ".qr swatch Interface/Buttons/WHITE8X8");
            return false;
        }

        // Both rejections quote what was typed, and what was typed is by definition something the
        // escape-safety check just refused - so it is escaped on the way back out rather than
        // handed to the client as markup.
        if (!IsSafeTexturePath(*texture))
        {
            handler->SendErrorMessage("'{}' is not a usable texture path: letters, digits, slashes, "
                "dots, dashes and underscores only.", QrDelivery::EscapeUiSequences(*texture));
            return false;
        }

        std::string const coords(texCoords);
        if (!IsSafeTexCoords(coords))
        {
            handler->SendErrorMessage("'{}' is not usable as texCoords: digits and colons only, as "
                "\"texWidth:texHeight:left:right:top:bottom\".", QrDelivery::EscapeUiSequences(coords));
            return false;
        }

        QrPalette candidate;
        candidate.dark.texture = *texture;
        candidate.dark.texCoords = coords;

        QrRenderGeometry const base = sQrConfig->ActiveGeometry();

        // Routed through the same call `.qr color` uses, so the bands are drawn at the module size
        // a coloured code would really use rather than at the packed size. The difference decides
        // the answer: the fallback scales the module up by the packing it gives up, and a crop
        // stretched over a module three times taller shows a gradient that was invisible at the
        // smaller size. Judging at the wrong size passes crops that then smear in the code.
        QrRenderGeometry const band = ApplyPalette(base, candidate);

        std::vector<bool> solid(std::size_t(QR_SWATCH_WIDTH_MODULES) * QR_SWATCH_BAND_ROWS, true);
        std::vector<bool> checker(std::size_t(QR_SWATCH_WIDTH_MODULES) * QR_SWATCH_BAND_ROWS);
        for (uint32 y = 0; y < QR_SWATCH_BAND_ROWS; ++y)
            for (uint32 x = 0; x < QR_SWATCH_WIDTH_MODULES; ++x)
                checker[std::size_t(y) * QR_SWATCH_WIDTH_MODULES + x] = (x + y) % 2 == 0;

        std::string text;
        std::size_t byteCount = 0;

        auto const appendBand = [&](std::vector<bool> const& modules, QrModuleStyle const& light) -> bool
        {
            QrRenderGeometry geometry = band;
            geometry.light = light;

            QrRenderResult const result =
                RenderModuleGrid(modules, QR_SWATCH_WIDTH_MODULES, QR_SWATCH_BAND_ROWS, geometry);
            if (result.error != QrRenderError::None)
            {
                ReportRenderError(handler, result, geometry);
                return false;
            }

            if (!text.empty())
                text += '\n';

            text += result.text;
            byteCount += result.byteCount;
            return true;
        };

        if (!appendBand(solid, base.light) || !appendBand(checker, base.light) ||
            !appendBand(checker, base.dark))
            return false;

        Deliver(handler, player, text, "QR swatch");

        handler->PSendSysMessage("Swatch: solid, then against white, then against the configured dark. "
            "{} modules at {}x{} px, the size a coloured code draws at, {} bytes.",
            QR_SWATCH_WIDTH_MODULES, band.moduleWidth, band.moduleHeight, byteCount);
        handler->PSendSysMessage("If band 2 reads as a clean checker and band 3 does not look solid, "
            "put it in qrcode.conf as:");
        handler->PSendSysMessage("QRCode.Palette.<name>.DarkTexture = \"{}\"", *texture);
        handler->PSendSysMessage("QRCode.Palette.<name>.DarkTexCoords = \"{}\"", coords);
        return true;
    }

    /// Samples a texture on a grid of points and draws one flat bar per point, each followed by
    /// the texCoords that produced it.
    ///
    /// This is the answer to the question `.qr swatch` leaves open. Swatch judges a crop you
    /// already have; finding one means knowing where in a texture the colour you want sits, and
    /// the server cannot read the artwork to say. Drawing every sample at once turns that into
    /// something the eye settles in one command, with the coordinates printed beside each bar
    /// ready to paste.
    ///
    /// The samples are deliberately tiny. Stretching a wider crop across a run of modules is what
    /// smears a faceted icon - the module ends up carrying the gradient between a highlight and a
    /// shadow instead of one colour - so the sweep only ever offers windows small enough to land
    /// inside a single facet, which is also what makes every bar directly usable.
    ///
    /// A texture that draws nothing at all is the other thing this catches, and the reason to
    /// reach for it first - every bar blank means the path is wrong, not the crop.
    static bool HandleQrSweepCommand(ChatHandler* handler, Optional<std::string> texture,
        Optional<uint32> cellsArg)
    {
        Player* player = AcquirePlayer(handler);
        if (!player)
            return false;

        if (!texture)
        {
            handler->SendErrorMessage("Usage: .qr sweep <texture> [cells per side, 2-{}]. Example: "
                ".qr sweep Interface/Icons/INV_Misc_Gem_Ruby_02", QR_SWEEP_MAX_CELLS);
            return false;
        }

        if (!IsSafeTexturePath(*texture))
        {
            handler->SendErrorMessage("'{}' is not a usable texture path: letters, digits, slashes, "
                "dots, dashes and underscores only.", QrDelivery::EscapeUiSequences(*texture));
            return false;
        }

        uint32 const cells = cellsArg.value_or(QR_SWEEP_DEFAULT_CELLS);
        if (cells < 2 || cells > QR_SWEEP_MAX_CELLS)
        {
            handler->SendErrorMessage("Cells per side must be 2 to {}.", QR_SWEEP_MAX_CELLS);
            return false;
        }

        // Every cell is one solid run, so the bar is a single-row all-dark grid recoloured per
        // cell, drawn through ApplyPalette at the size a coloured code really uses.
        QrRenderGeometry const base = sQrConfig->ActiveGeometry();

        std::vector<bool> const bar(QR_SWEEP_BAR_MODULES, true);

        handler->PSendSysMessage("Sweep of {} at {}x{} sample points. Every bar is a flat colour, "
            "so pick the darkest, most saturated one; every bar blank means the texture path is "
            "wrong.", *texture, cells, cells);

        for (uint32 row = 0; row < cells; ++row)
        {
            for (uint32 col = 0; col < cells; ++col)
            {
                // A small window at the cell's centre rather than the cell itself, so the bar is a
                // flat colour and the coordinates are directly usable. Stated against a 100x100
                // basis so the edges are percentages and the sweep needs no knowledge of the
                // texture's real pixel size, which the server has no way to learn.
                uint32 const centreX = (2 * col + 1) * (QR_SWEEP_SAMPLE_BASIS / 2) / cells;
                uint32 const centreY = (2 * row + 1) * (QR_SWEEP_SAMPLE_BASIS / 2) / cells;

                std::string const coords = Acore::StringFormat("{}:{}:{}:{}:{}:{}",
                    QR_SWEEP_SAMPLE_BASIS, QR_SWEEP_SAMPLE_BASIS,
                    centreX - QR_SWEEP_SAMPLE_HALF, centreX + QR_SWEEP_SAMPLE_HALF,
                    centreY - QR_SWEEP_SAMPLE_HALF, centreY + QR_SWEEP_SAMPLE_HALF);

                QrPalette sample;
                sample.dark.texture = *texture;
                sample.dark.texCoords = coords;

                QrRenderGeometry geometry = ApplyPalette(base, sample);
                geometry.maxRowWidthPx = 0;

                QrRenderResult const result = RenderModuleGrid(bar, QR_SWEEP_BAR_MODULES, 1, geometry);
                if (result.error != QrRenderError::None)
                {
                    ReportRenderError(handler, result, geometry);
                    return false;
                }

                // Bar and coordinates on one line: the swatch is only useful next to the string
                // that produced it, and a separate legend would have to be counted against it.
                handler->PSendSysMessage("{} {}", result.text, coords);
            }
        }

        handler->PSendSysMessage("Confirm one with \".qr swatch {} <coords>\".", *texture);
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
