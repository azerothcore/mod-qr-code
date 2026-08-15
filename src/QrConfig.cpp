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

namespace
{
    struct QrPackDefault
    {
        char const* pattern;
        char const* texture;
        char const* texCoords;
    };

    /// Default crop per packed pattern, named by its modules read top to bottom.
    ///
    /// The two-row set crops the USK age-rating badge, the one stock texture carrying a hard
    /// black-on-white edge in both directions with no alpha near it: luminance 0 above 248,
    /// both bands flat enough to stretch across a run. It is the set that has been checked
    /// in-game.
    ///
    /// A candidate has to be neutral, not merely the right brightness. Luminance alone lets a
    /// tan pixel through - RGB 240,236,152 scores about 230 and renders as a visibly yellow
    /// module - so every entry here is checked for near-zero chroma as well. That is what
    /// disqualifies most of the client's artwork, which is warm almost everywhere.
    ///
    /// The three-row set has not been checked in-game. Its bands are neutral but they do not
    /// all sit at the same levels: most darks land near 32 while LDL's is 1 and DLD's are
    /// 26-37, and LDL and DLD come from bands only 2-3 px tall, which blur when stretched to
    /// a module. Expect to retune them, and confirm a code still scans before trusting three
    /// rows.
    ///
    /// Coordinates are stated against each texture's real pixel size rather than as
    /// percentages: exact, and shorter, which matters because a crop is charged on every
    /// escape.
    constexpr QrPackDefault PACK_DEFAULTS[] =
    {
        // One row per line never reads these, but a bare "L"/"D" keeps the table total.
        { "L",   "Interface/Buttons/WHITE8X8",                  ""                       },
        { "D",   "Interface/Glues/Login/Glues-GermanRating",    "128:128:1:51:89:100"    },

        { "LL",  "Interface/Buttons/WHITE8X8",                  ""                       },
        { "LD",  "Interface/Glues/Login/Glues-GermanRating",    "128:128:2:126:122:128"  },
        { "DL",  "Interface/Glues/Login/Glues-GermanRating",    "128:128:2:52:88:114"    },
        { "DD",  "Interface/Glues/Login/Glues-GermanRating",    "128:128:1:51:89:100"    },

        { "LLL", "Interface/Buttons/WHITE8X8",                      ""                      },
        { "LLD", "Interface/Glues/Login/Glues-KoreanRating-Fear",   "128:128:93:102:51:123" },
        { "LDL", "Interface/Icons/INV_Pants_Plate_05",              "64:64:48:52:1:7"       },
        { "LDD", "Interface/Glues/Login/Glues-KoreanRating-Fear",   "128:128:59:77:87:123"  },
        { "DLL", "Interface/Glues/Login/Glues-KoreanRating-Verbal", "128:128:54:89:56:98"   },
        { "DLD", "Interface/Glues/Login/Glues-RealmSelect",         "512:512:260:264:72:81" },
        { "DDL", "Interface/Glues/Login/Glues-KoreanRating-Verbal", "64:64:27:37:11:47"     },
        { "DDD", "Interface/Glues/Login/Glues-KoreanRating-Verbal", "128:128:33:94:48:66"   },
    };

    /// Spells a style index as the pattern it draws, top module first, which is the order
    /// the config names it in and the order the renderer packs the bits.
    std::string PackPatternName(std::size_t state, uint32 rows)
    {
        std::string name;
        name.reserve(rows);

        for (uint32 d = 0; d < rows; ++d)
            name += ((state >> (rows - 1 - d)) & 1) ? 'D' : 'L';

        return name;
    }

    QrPackDefault const& PackDefaultFor(std::string const& pattern)
    {
        for (QrPackDefault const& entry : PACK_DEFAULTS)
            if (pattern == entry.pattern)
                return entry;

        // Four rows would need sixteen crops; the client does not have them, so there is
        // nothing to offer and the caller reports the gap.
        static constexpr QrPackDefault none{ "", "", "" };
        return none;
    }
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

    if (sConfigMgr->GetOption<int32>("QRCode.PackRows", -1) != -1)
        LOG_ERROR("module.qrcode", "QRCode.PackRows has been replaced by QRCode.RowsPerLine "
            "(1 = one row per line, 2 = what PackRows = 1 used to do) and is being ignored");

    geometry.rowsPerLine = sConfigMgr->GetOption<uint32>("QRCode.RowsPerLine", 2);
    if (!geometry.rowsPerLine || geometry.rowsPerLine > QR_MAX_ROWS_PER_LINE)
    {
        LOG_ERROR("module.qrcode", "QRCode.RowsPerLine = {} is outside 1..{}, falling back to 2",
            geometry.rowsPerLine, QR_MAX_ROWS_PER_LINE);
        geometry.rowsPerLine = 2;
    }

    std::size_t const styleCount = std::size_t(1) << geometry.rowsPerLine;
    bool complete = true;

    for (std::size_t state = 0; state < styleCount; ++state)
    {
        std::string const pattern = PackPatternName(state, geometry.rowsPerLine);
        QrPackDefault const& fallback = PackDefaultFor(pattern);

        geometry.packed[state].texture =
            sConfigMgr->GetOption<std::string>("QRCode.Pack." + pattern + ".Texture", fallback.texture);
        geometry.packed[state].texCoords =
            sConfigMgr->GetOption<std::string>("QRCode.Pack." + pattern + ".TexCoords", fallback.texCoords);

        if (geometry.rowsPerLine > 1 && geometry.packed[state].texture.empty())
        {
            LOG_ERROR("module.qrcode", "QRCode.Pack.{}.Texture has no value and no default, so "
                "{} rows per line cannot be drawn", pattern, geometry.rowsPerLine);
            complete = false;
        }
    }

    // Every pattern has to be drawable or the grid comes out with holes in it, which still
    // looks like a code and still will not scan. One row per line always works, so fall back
    // to it rather than emit something broken.
    if (!complete)
        geometry.rowsPerLine = 1;

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
