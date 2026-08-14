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

    bool      Enabled         = true;
    QrBackend Backend         = QrBackend::Chat;
    QrEcc     Ecc             = QrEcc::Low;
    uint32    MaxVersion      = 3;
    uint32    MaxInputLength  = 96;
    uint32    CooldownSeconds = 5;

    QrRenderGeometry ChatGeometry;
    QrRenderGeometry QuestGeometry;

private:
    QrRenderGeometry LoadGeometry(std::string const& prefix, QrRenderGeometry const& defaults) const;
};

#define sQrConfig QrConfig::instance()

#endif // MOD_QR_CODE_QR_CONFIG_H
