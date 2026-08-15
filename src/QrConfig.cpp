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

    // The defaults crop the USK age-rating badge, which is the one stock texture carrying a
    // hard black-on-white edge in both directions with no alpha anywhere near it: pure 0
    // above pure 248, and both bands flat enough to stretch across a run. Coordinates are
    // stated against the texture's real 128x128 rather than as percentages, which keeps them
    // exact and costs a third of the bytes - and every crop is charged on every escape.
    geometry.packRows = sConfigMgr->GetOption<bool>("QRCode.PackRows", true);

    geometry.packed.dark.texture = sConfigMgr->GetOption<std::string>("QRCode.Pack.DarkTexture",
        "Interface/Glues/Login/Glues-GermanRating");
    geometry.packed.dark.texCoords = sConfigMgr->GetOption<std::string>("QRCode.Pack.DarkTexCoords",
        "128:128:1:51:89:100");
    geometry.packed.light.texture = sConfigMgr->GetOption<std::string>("QRCode.Pack.LightTexture",
        "Interface/Buttons/WHITE8X8");
    geometry.packed.light.texCoords = sConfigMgr->GetOption<std::string>("QRCode.Pack.LightTexCoords", "");
    geometry.packed.darkOverLight.texture = sConfigMgr->GetOption<std::string>("QRCode.Pack.DarkOverLightTexture",
        "Interface/Glues/Login/Glues-GermanRating");
    geometry.packed.darkOverLight.texCoords = sConfigMgr->GetOption<std::string>("QRCode.Pack.DarkOverLightTexCoords",
        "128:128:2:52:88:114");
    geometry.packed.lightOverDark.texture = sConfigMgr->GetOption<std::string>("QRCode.Pack.LightOverDarkTexture",
        "Interface/Glues/Login/Glues-GermanRating");
    geometry.packed.lightOverDark.texCoords = sConfigMgr->GetOption<std::string>("QRCode.Pack.LightOverDarkTexCoords",
        "128:128:2:126:122:128");

    if (geometry.packRows && (geometry.packed.dark.texture.empty() || geometry.packed.light.texture.empty() ||
        geometry.packed.darkOverLight.texture.empty() || geometry.packed.lightOverDark.texture.empty()))
    {
        LOG_ERROR("module.qrcode", "QRCode.PackRows needs all four QRCode.Pack.*Texture paths set, "
            "drawing one row per line instead");
        geometry.packRows = false;
    }

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

    MaxVersion = sConfigMgr->GetOption<uint32>("QRCode.MaxVersion", 5);
    if (MaxVersion < 1 || MaxVersion > 40)
    {
        LOG_ERROR("module.qrcode", "QRCode.MaxVersion = {} is outside 1..40, falling back to 5", MaxVersion);
        MaxVersion = 5;
    }

    MaxInputLength  = sConfigMgr->GetOption<uint32>("QRCode.MaxInputLength", 96);
    CooldownSeconds = sConfigMgr->GetOption<uint32>("QRCode.CooldownSeconds", 5);
    TwoFAEnabled    = sConfigMgr->GetOption<bool>("QRCode.TwoFA.Enable", true);
    TwoFAIssuer     = sConfigMgr->GetOption<std::string>("QRCode.TwoFA.Issuer", "");

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

    uint32 quietZone = sConfigMgr->GetOption<uint32>("QRCode.QuietZone", QR_QUIET_ZONE_DEFAULT_MODULES);
    if (quietZone > QR_QUIET_ZONE_MODULES)
    {
        LOG_ERROR("module.qrcode", "QRCode.QuietZone = {} is above the {} modules the spec asks for, "
            "falling back to {}", quietZone, QR_QUIET_ZONE_MODULES, QR_QUIET_ZONE_MODULES);
        quietZone = QR_QUIET_ZONE_MODULES;
    }

    ChatGeometry.quietZone  = quietZone;
    QuestGeometry.quietZone = quietZone;

    uint32 const maxPayloadBytes = sConfigMgr->GetOption<uint32>("QRCode.MaxPayloadBytes", 48000);
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
