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

#include "QrConfig.h"
#include "Config.h"
#include "Log.h"
#include "ScriptMgr.h"

QrConfig* QrConfig::instance()
{
    static QrConfig instance;
    return &instance;
}

QrRenderGeometry QrConfig::LoadGeometry(std::string const& prefix, QrRenderGeometry const& defaults) const
{
    QrRenderGeometry geometry;
    geometry.moduleWidth  = sConfigMgr->GetOption<uint32>(prefix + ".ModuleWidth", defaults.moduleWidth);
    geometry.moduleHeight = sConfigMgr->GetOption<uint32>(prefix + ".ModuleHeight", defaults.moduleHeight);
    geometry.lineAdvance  = sConfigMgr->GetOption<int32>(prefix + ".LineAdvance", defaults.lineAdvance);
    geometry.maxRowWidthPx = sConfigMgr->GetOption<uint32>(prefix + ".MaxRowWidth", defaults.maxRowWidthPx);

    // Forward slashes: the client accepts either, and they carry through a config file
    // without raising any question about backslash escaping.
    geometry.dark.texture = sConfigMgr->GetOption<std::string>("QRCode.DarkTexture",
        "Interface/DialogFrame/UI-DialogBox-Background");
    geometry.dark.texCoords = sConfigMgr->GetOption<std::string>("QRCode.DarkTexCoords", "100:100:45:55:45:55");
    geometry.light.texture = sConfigMgr->GetOption<std::string>("QRCode.LightTexture", "Interface/Buttons/WHITE8X8");
    geometry.light.texCoords = sConfigMgr->GetOption<std::string>("QRCode.LightTexCoords", "");

    geometry.anchorBottom = sConfigMgr->GetOption<bool>("QRCode.AnchorBottom", true);

    if (!geometry.moduleWidth || !geometry.moduleHeight)
    {
        LOG_ERROR("module.qrcode", "{}.ModuleWidth/ModuleHeight must be non-zero, falling back to {}x{}",
            prefix, defaults.moduleWidth, defaults.moduleHeight);
        geometry.moduleWidth  = defaults.moduleWidth;
        geometry.moduleHeight = defaults.moduleHeight;
    }

    return geometry;
}

void QrConfig::Load()
{
    Enabled = sConfigMgr->GetOption<bool>("QRCode.Enable", true);

    uint32 const backend = sConfigMgr->GetOption<uint32>("QRCode.Backend", 1);
    if (backend != uint32(QrBackend::Chat) && backend != uint32(QrBackend::Quest))
    {
        LOG_ERROR("module.qrcode", "QRCode.Backend = {} is not 1 (chat) or 2 (quest frame), falling back to chat",
            backend);
        Backend = QrBackend::Chat;
    }
    else
        Backend = QrBackend(backend);

    std::string const eccName = sConfigMgr->GetOption<std::string>("QRCode.ErrorCorrection", "L");
    if (std::optional<QrEcc> const parsed = ParseQrEcc(eccName))
        Ecc = *parsed;
    else
    {
        LOG_ERROR("module.qrcode", "QRCode.ErrorCorrection = '{}' is not one of L/M/Q/H, falling back to L", eccName);
        Ecc = QrEcc::Low;
    }

    MaxVersion = sConfigMgr->GetOption<uint32>("QRCode.MaxVersion", 3);
    if (MaxVersion < 1 || MaxVersion > 40)
    {
        LOG_ERROR("module.qrcode", "QRCode.MaxVersion = {} is outside 1..40, falling back to 3", MaxVersion);
        MaxVersion = 3;
    }

    MaxInputLength  = sConfigMgr->GetOption<uint32>("QRCode.MaxInputLength", 96);
    CooldownSeconds = sConfigMgr->GetOption<uint32>("QRCode.CooldownSeconds", 5);

    // Chat defaults are measured in-game: 7 px square modules, and a row offset of
    // (0 - 7) that lifts each row 7 px to cancel the chat font's row spacing. The client
    // reads a positive offset as upward, so a lower LineAdvance packs the rows tighter -
    // it is a dial, not a measurement of anything. The quest values are extrapolated from
    // the chat ones and still need confirming in the quest pane.
    QrRenderGeometry chatDefaults;
    chatDefaults.moduleWidth = 7;
    chatDefaults.moduleHeight = 7;
    chatDefaults.lineAdvance = 0;
    chatDefaults.maxRowWidthPx = 0;

    QrRenderGeometry questDefaults;
    questDefaults.moduleWidth = 7;
    questDefaults.moduleHeight = 9;
    questDefaults.lineAdvance = 2;
    questDefaults.maxRowWidthPx = 285;

    ChatGeometry  = LoadGeometry("QRCode.Chat", chatDefaults);
    QuestGeometry = LoadGeometry("QRCode.Quest", questDefaults);

    uint32 const maxPayloadBytes = sConfigMgr->GetOption<uint32>("QRCode.MaxPayloadBytes", 32000);
    ChatGeometry.maxPayloadBytes  = maxPayloadBytes;
    QuestGeometry.maxPayloadBytes = maxPayloadBytes;
}

QrRenderGeometry const& QrConfig::ActiveGeometry() const
{
    return Backend == QrBackend::Quest ? QuestGeometry : ChatGeometry;
}

class qr_code_world : public WorldScript
{
public:
    qr_code_world() : WorldScript("qr_code_world", { WORLDHOOK_ON_AFTER_CONFIG_LOAD }) { }

    void OnAfterConfigLoad(bool /*reload*/) override
    {
        sQrConfig->Load();
    }
};

void AddSC_qr_code_world()
{
    new qr_code_world();
}
