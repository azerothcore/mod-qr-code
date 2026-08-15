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

#ifndef MOD_QR_CODE_QR_CONFIG_H
#define MOD_QR_CODE_QR_CONFIG_H

#include "Define.h"
#include "QrEncoder.h"
#include "QrRenderer.h"

#include <map>
#include <string>
#include <vector>

enum class QrBackend : uint8
{
    Chat  = 1,
    Quest = 2,
};

/// Cached view of qrcode.conf, refreshed by WorldScript::OnAfterConfigLoad.
///
/// Every client-side pixel value in here can only be settled by looking at the result
/// in-game, so `.reload config` has to be enough to retune the whole module - no restart,
/// no rebuild. Nothing may be read straight from sConfigMgr at command time.
class QrConfig
{
public:
    static QrConfig* instance();

    void Load();

    /// Geometry for the backend currently selected by QRCode.Backend.
    QrRenderGeometry const& ActiveGeometry() const;

    /// Palette named @p name, matched case-insensitively, or nullptr if the realm has no
    /// such colour configured.
    QrPalette const* FindPalette(std::string const& name) const;

    /// Configured palette names, in the order `.qr color` lists them.
    std::vector<std::string> PaletteNames() const;

    bool      Enabled         = true;
    QrBackend Backend         = QrBackend::Chat;
    QrEcc     Ecc             = QrEcc::Low;
    uint32    MaxVersion      = 5;
    uint32    MaxInputLength  = 96;
    uint32    CooldownSeconds = 5;

    /// Whether `.account 2fa qrcode` is offered at all.
    ///
    /// The command is only usable where the whole code fits on screen at once, and a 2FA
    /// payload needs a version 4 symbol - 35 module rows. Row packing draws those in 18
    /// chat lines, which a default frame shows; without it they need 35, which it does
    /// not. So this follows QRCode.RowsPerLine: on at 2 or more, and worth turning off at 1,
    /// since a code the player can only see two thirds of is worse than no command.
    bool TwoFAEnabled = true;

    /// Issuer shown by the authenticator app for `.account 2fa qrcode`. Empty means the
    /// realm name, which is the sensible label but not always a short one - and the label
    /// is part of a payload that has very little room left.
    std::string TwoFAIssuer;

    /// Module rows drawn per line, validated once so the geometries and the palettes agree
    /// on how many packed styles a complete set needs.
    uint32 RowsPerLine = 3;

    QrRenderGeometry ChatGeometry;
    QrRenderGeometry QuestGeometry;

    /// Colours offered by `.qr color`, keyed by lowercased name.
    std::map<std::string, QrPalette> Palettes;

private:
    QrRenderGeometry LoadGeometry(std::string const& prefix, QrRenderGeometry const& defaults) const;
    void LoadPalettes();
};

#define sQrConfig QrConfig::instance()

#endif // MOD_QR_CODE_QR_CONFIG_H
