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

#include <algorithm>
#include <cctype>
#include <map>
#include <utility>

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
    /// Band thickness matters just as much, and for a reason that is easy to miss. The client
    /// scales the crop into an escape two modules tall, so a crop with 13 px bands is scaled
    /// down and keeps a hard edge, while one with 3 px bands is scaled up and the boundary
    /// becomes a two-pixel ramp. Thresholding then eats the soft edge and the module row comes
    /// out short - which makes a row's height depend on which pattern drew it. Decoders cope
    /// with a uniform aspect stretch but not with that kind of row-by-row jitter, so every
    /// mixed pattern wants bands comfortably thicker than the drawn module.
    ///
    /// The three-row set scans in-game at the shipped 4x5 modules, which is what it is tuned
    /// for. It is the less comfortable of the two: its bands are neutral but not level, with
    /// most darks near 32 while LDL sits at 1 and DLD at 26-37, and LDL and DLD come from
    /// bands only 2-3 px tall. Those hold up at 5 px modules and are the first thing to
    /// suspect if a taller module stops scanning.
    ///
    /// Coordinates are stated against each texture's real pixel size rather than as
    /// percentages: exact, and shorter, which matters because a crop is charged on every
    /// escape.
    constexpr QrPackDefault PACK_DEFAULTS[] =
    {
        // One row per line never reads these, but a bare "L"/"D" keeps the table total.
        { "L",   "Interface/Buttons/WHITE8X8",                  ""                       },
        { "D",   "Interface/Glues/Login/Glues-GermanRating",    "128:128:1:51:89:100"    },

        { "LL",  "Interface/Buttons/WHITE8X8",                    ""                      },
        { "LD",  "Interface/Glues/Login/Glues-KoreanRating-Fear", "128:128:90:102:75:123" },
        { "DL",  "Interface/Glues/Login/Glues-GermanRating",      "128:128:2:52:88:114"   },
        { "DD",  "Interface/Glues/Login/Glues-GermanRating",      "128:128:1:51:89:100"   },

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

    constexpr char const* PALETTE_PREFIX = "QRCode.Palette.";

    struct QrPaletteDefault
    {
        char const* name;
        char const* texture;
        char const* texCoords;

        /// Light-side override, empty to keep the configured white. Set only for a colour too
        /// light to be a module, which then draws black modules on a coloured ground instead.
        char const* lightTexture;
        char const* lightTexCoords;
    };

    /// Colours `.qr color` offers with nothing in the config at all.
    ///
    /// Compiled in rather than left to qrcode.conf so a realm that never edits its config still
    /// has the command, and so an existing qrcode.conf written before palettes existed does not
    /// silently offer none. Every entry is still overridable per field: the config value wins
    /// wherever one is set, and a name that appears only in the config is added to these.
    ///
    /// "black" is the one entry confirmed against a client: it reuses the DD crop from
    /// PACK_DEFAULTS above, which the original texture search measured as flat and opaque with
    /// luminance 0. It draws the same colour the default black-and-white code does, so it is
    /// useless as a colour and valuable as a control - if ".qr color black" scans and a coloured
    /// one does not, the palette machinery is fine and the other texture is the problem.
    ///
    /// The coloured entries sample gem icons. Interface/Icons is the one directory this module has
    /// already proven resolves - PACK_DEFAULTS reaches into it for the LDL pattern - and a gem
    /// icon is a large, saturated, single-hue object filling most of its 64x64 frame, which is
    /// the closest thing to a flat colour swatch the client ships.
    ///
    /// The crop is four texels rather than the middle quarter, and the size is the point. A gem
    /// icon is faceted, so a wide crop spans a highlight and a shadow and the client stretches
    /// that gradient across a whole merged run - the modules come out as visible streaks and the
    /// code will not threshold. A window this small lands inside one facet and is flat whatever
    /// the artwork does around it.
    ///
    /// red, blue and green are confirmed scanning in-game at this crop. The gem family and the
    /// _02 variant are therefore known to exist, which is what the last two entries rest on -
    /// they are the same directory, the same naming, and the same window, so they are a far
    /// shorter reach than a new texture would be. Confirm them the same way if a realm relies on
    /// them: ".qr sweep" to find a texel, ".qr swatch" to judge it.
    ///
    /// What scanning tests settled about luminance, since the obvious argument gets it half wrong.
    /// A decoder thresholds brightness, not hue, so the question is only ever what a texel
    /// measures - naming the colour predicts nothing. Emerald is a dark saturated green and scans,
    /// which pure sRGB green at 182 of 255 never would.
    ///
    /// Where a colour lands decides which side of the code it can hold. Ruby, sapphire, emerald
    /// and the amethyst texel below are all dark enough to be modules against white. Topaz is not:
    /// gold measures around 190 and no crop of it separates from a white ground, so yellow colours
    /// the light side instead and keeps black modules. Black on gold scans as readily as red on
    /// white and reads just as yellow.
    ///
    /// The amethyst coordinates come off a ".qr sweep" of the icon rather than from a guess at its
    /// middle, which is why they look nothing like the others: the stone's centre is a highlight,
    /// and the saturated purple is off to one side.
    constexpr QrPaletteDefault PALETTE_DEFAULTS[] =
    {
        { "black",  "Interface/Glues/Login/Glues-GermanRating", "128:128:1:51:89:100",       "", "" },
        { "red",    "Interface/Icons/INV_Misc_Gem_Ruby_02",     "64:64:30:34:38:42",         "", "" },
        { "blue",   "Interface/Icons/INV_Misc_Gem_Sapphire_02", "64:64:30:34:38:42",         "", "" },
        { "green",  "Interface/Icons/INV_Misc_Gem_Emerald_02",  "64:64:30:34:38:42",         "", "" },
        { "purple", "Interface/Icons/INV_Misc_Gem_Amethyst_02", "1000:1000:809:815:809:815", "", "" },

        // Black modules on a gold ground: the dark crop is the measured flat black, so only the
        // ground is artwork, and a ground being slightly uneven costs far less than a module would.
        { "yellow", "Interface/Glues/Login/Glues-GermanRating", "128:128:1:51:89:100",
                    "Interface/Icons/INV_Misc_Gem_Topaz_02",    "64:64:30:34:38:42" },
    };

    QrPaletteDefault const& PaletteDefaultFor(std::string const& name)
    {
        for (QrPaletteDefault const& entry : PALETTE_DEFAULTS)
            if (name == entry.name)
                return entry;

        // A palette the config invented has no compiled-in fallback, so every field of it has
        // to come from the config and the caller reports a missing texture.
        static constexpr QrPaletteDefault none{ "", "", "", "", "" };
        return none;
    }

    std::string ToLower(std::string text)
    {
        std::transform(text.begin(), text.end(), text.begin(),
            [](unsigned char c) { return char(std::tolower(c)); });

        return text;
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

    geometry.rowsPerLine = RowsPerLine;

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

/// Builds the `.qr color` palettes from every QRCode.Palette.<name>.* option present.
///
/// The names are recovered from the option keys rather than declared in a list of their own,
/// so a realm adds a colour by writing one option and running ".reload config" - there is no
/// second place to keep in step, and no rebuild for a colour the module never shipped.
void QrConfig::LoadPalettes()
{
    Palettes.clear();

    std::size_t const prefixLength = std::char_traits<char>::length(PALETTE_PREFIX);

    // Lowercased name to the spelling its config keys actually use. Config lookups are
    // case-sensitive while `.qr color red` must find a palette written as RED, so the two cannot
    // be the same string: searching for the lowercased spelling would miss every override an
    // admin capitalised.
    std::map<std::string, std::string> names;

    for (QrPaletteDefault const& entry : PALETTE_DEFAULTS)
        names.emplace(entry.name, entry.name);

    for (std::string const& key : sConfigMgr->GetKeysByString(PALETTE_PREFIX))
    {
        // Guards a key that is exactly the prefix, or one naming no field after the colour:
        // both would otherwise register a palette with an empty name.
        std::size_t const separator = key.find('.', prefixLength);
        if (separator == std::string::npos || separator == prefixLength)
            continue;

        std::string spelling = key.substr(prefixLength, separator - prefixLength);
        std::string const lowered = ToLower(spelling);

        // Assigned rather than inserted so a config spelling replaces the built-in one, which is
        // what makes a capitalised override of a shipped colour resolve.
        names[lowered] = std::move(spelling);
    }

    for (auto const& entry : names)
    {
        std::string const& name = entry.first;
        std::string const base = PALETTE_PREFIX + entry.second;

        QrPaletteDefault const& fallback = PaletteDefaultFor(name);

        // Silent lookups throughout: every shipped colour resolves from PALETTE_DEFAULTS, so its
        // config keys are legitimately absent. Left logging, each one would announce a dozen
        // missing options on every reload and bury the one gap that matters, reported below.
        QrPalette palette;
        palette.dark.texture = sConfigMgr->GetOption<std::string>(base + ".DarkTexture", fallback.texture, false);
        palette.dark.texCoords =
            sConfigMgr->GetOption<std::string>(base + ".DarkTexCoords", fallback.texCoords, false);

        // A palette is its dark texture. Without one there is nothing to draw, and offering the
        // name anyway would put a blank grid in front of a player who asked for a colour. Only a
        // config-invented name can land here; the shipped ones always carry a texture.
        if (palette.dark.texture.empty())
        {
            LOG_ERROR("module.qrcode", "Palette '{}' has no {}.DarkTexture, so the colour is not offered",
                name, base);
            continue;
        }

        // Optional, and only wanted for a colour too light to be a module: setting it draws black
        // modules on a coloured ground rather than coloured modules on white.
        palette.light.texture =
            sConfigMgr->GetOption<std::string>(base + ".LightTexture", fallback.lightTexture, false);
        palette.light.texCoords =
            sConfigMgr->GetOption<std::string>(base + ".LightTexCoords", fallback.lightTexCoords, false);
        palette.hasLight = !palette.light.texture.empty();

        // A packed set is all or nothing: one missing pattern leaves holes in the grid, which
        // still looks like a code and still will not scan. An incomplete set is therefore
        // dropped whole and the palette draws one module row per line instead.
        std::size_t const styleCount = std::size_t(1) << RowsPerLine;
        std::size_t found = 0;

        for (std::size_t state = 0; state < styleCount; ++state)
        {
            std::string const pattern = PackPatternName(state, RowsPerLine);

            palette.packed[state].texture =
                sConfigMgr->GetOption<std::string>(base + ".Pack." + pattern + ".Texture", "", false);
            palette.packed[state].texCoords =
                sConfigMgr->GetOption<std::string>(base + ".Pack." + pattern + ".TexCoords", "", false);

            if (!palette.packed[state].texture.empty())
                ++found;
        }

        palette.hasPacked = RowsPerLine > 1 && found == styleCount;

        if (found && found < styleCount)
            LOG_ERROR("module.qrcode", "Palette '{}' supplies {} of the {} pack textures needed at "
                "QRCode.RowsPerLine = {}; the partial set is ignored and the colour draws one module "
                "row per line", name, found, styleCount, RowsPerLine);

        Palettes.emplace(name, std::move(palette));
    }
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

    if (sConfigMgr->GetOption<int32>("QRCode.PackRows", -1) != -1)
        LOG_ERROR("module.qrcode", "QRCode.PackRows has been replaced by QRCode.RowsPerLine "
            "(1 = one row per line, 2 = what PackRows = 1 used to do) and is being ignored");

    // Parsed before the geometries so both of them and the palettes size their style sets the
    // same way; a palette counted against a different number of rows would look complete when
    // it is not.
    RowsPerLine = sConfigMgr->GetOption<uint32>("QRCode.RowsPerLine", 3);
    if (!RowsPerLine || RowsPerLine > QR_MAX_ROWS_PER_LINE)
    {
        LOG_ERROR("module.qrcode", "QRCode.RowsPerLine = {} is outside 1..{}, falling back to 3",
            RowsPerLine, QR_MAX_ROWS_PER_LINE);
        RowsPerLine = 3;
    }

    MaxInputLength  = sConfigMgr->GetOption<uint32>("QRCode.MaxInputLength", 96);
    CooldownSeconds = sConfigMgr->GetOption<uint32>("QRCode.CooldownSeconds", 5);
    TwoFAEnabled    = sConfigMgr->GetOption<bool>("QRCode.TwoFA.Enable", true);
    TwoFAIssuer     = sConfigMgr->GetOption<std::string>("QRCode.TwoFA.Issuer", "");

    // Chat defaults are measured in-game, and they only make sense as a set: three rows per
    // line at 5 px each comes to 15 px, which still fits inside the chat font's line advance,
    // and LineAdvance -4 is what lands the lines flush at that height. Change one and the
    // other two want rechecking. The client reads a positive offset as upward, so a lower
    // LineAdvance packs the lines tighter - it is a dial, not a measurement of anything.
    // The quest values are extrapolated from the chat ones and still need confirming in the
    // quest pane.
    QrRenderGeometry chatDefaults;
    chatDefaults.moduleWidth = 4;
    chatDefaults.moduleHeight = 5;
    chatDefaults.lineAdvance = -4;
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

    LoadPalettes();
}

QrPalette const* QrConfig::FindPalette(std::string const& name) const
{
    auto const itr = Palettes.find(ToLower(name));
    return itr == Palettes.end() ? nullptr : &itr->second;
}

std::vector<std::string> QrConfig::PaletteNames() const
{
    std::vector<std::string> names;
    names.reserve(Palettes.size());

    for (auto const& entry : Palettes)
        names.push_back(entry.first);

    return names;
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
