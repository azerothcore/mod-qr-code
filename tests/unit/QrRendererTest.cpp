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

#include "gtest/gtest.h"
#include "QrRenderer.h"

#include <algorithm>
#include <string>
#include <vector>

namespace
{
    std::vector<std::string> SplitRows(std::string const& text)
    {
        std::vector<std::string> rows;
        std::size_t start = 0;

        while (true)
        {
            std::size_t const end = text.find('\n', start);
            rows.push_back(text.substr(start, end - start));

            if (end == std::string::npos)
                return rows;

            start = end + 1;
        }
    }

    // Distinct textures, and a crop on only one of them, so a swapped or dropped style is
    // visible in the golden strings rather than silently passing.
    constexpr char const* DARK_TEXTURE  = "Interface/DialogFrame/UI-DialogBox-Background";
    constexpr char const* LIGHT_TEXTURE = "Interface/Buttons/WHITE8X8";
    constexpr char const* DARK_COORDS   = "100:100:45:55:45:55";

    // Anchored to the top so the offsets in the golden strings read as "row N's own
    // displacement" rather than being relative to the grid height. Bottom anchoring, the
    // shipped default, has its own test below.
    QrRenderGeometry SquareGeometry()
    {
        QrRenderGeometry geometry;
        geometry.dark.texture    = DARK_TEXTURE;
        geometry.dark.texCoords  = DARK_COORDS;
        geometry.light.texture   = LIGHT_TEXTURE;
        geometry.anchorBottom    = false;
        geometry.moduleWidth     = 10;
        geometry.moduleHeight    = 14;
        geometry.lineAdvance     = 14;
        return geometry;
    }

    std::string Dark(std::uint32_t widthPx, std::uint32_t heightPx, std::int32_t offY)
    {
        return std::string("|T") + DARK_TEXTURE + ":" + std::to_string(heightPx) + ":" + std::to_string(widthPx) +
            ":0:" + std::to_string(offY) + ":" + DARK_COORDS + "|t";
    }

    std::string Light(std::uint32_t widthPx, std::uint32_t heightPx, std::int32_t offY)
    {
        return std::string("|T") + LIGHT_TEXTURE + ":" + std::to_string(heightPx) + ":" + std::to_string(widthPx) +
            ":0:" + std::to_string(offY) + "|t";
    }

    // Four distinct paths, so picking the wrong pair style fails on the path rather than
    // happening to match another style's string.
    constexpr char const* PACK_DARK  = "Pack/Dark";
    constexpr char const* PACK_LIGHT = "Pack/Light";
    constexpr char const* PACK_DOL   = "Pack/DarkOverLight";
    constexpr char const* PACK_LOD   = "Pack/LightOverDark";

    QrRenderGeometry PackedGeometry()
    {
        QrRenderGeometry geometry = SquareGeometry();
        geometry.packRows                     = true;
        geometry.packed.dark.texture          = PACK_DARK;
        geometry.packed.light.texture         = PACK_LIGHT;
        geometry.packed.darkOverLight.texture = PACK_DOL;
        geometry.packed.lightOverDark.texture = PACK_LOD;
        return geometry;
    }

    /// A packed escape is always two modules tall, hence the doubled height throughout.
    std::string Pair(char const* texture, std::uint32_t widthPx, std::int32_t offY)
    {
        return std::string("|T") + texture + ":28:" + std::to_string(widthPx) + ":0:" +
            std::to_string(offY) + "|t";
    }
}

/// Locks the exact escape syntax: each run picks up its own colour's texture, a crop is
/// appended only when one is configured, and there is no stray separator when it is not.
/// Also pins run-length merging - four modules leave as two escapes, at double width each.
TEST(QrRendererTest, MergesRunsAndDrawsEachColourWithItsOwnTexture)
{
    std::vector<bool> const modules{ true, true, false, false };

    QrRenderResult const result = RenderModuleGrid(modules, 4, 1, SquareGeometry());

    ASSERT_EQ(QrRenderError::None, result.error);
    EXPECT_EQ(
        "|TInterface/DialogFrame/UI-DialogBox-Background:14:20:0:0:100:100:45:55:45:55|t"
        "|TInterface/Buttons/WHITE8X8:14:20:0:0|t",
        result.text);
}

/// An uncropped style stops after offY. A stray trailing separator would make the client
/// read a malformed rect, so the empty case has to be omitted rather than emitted blank.
TEST(QrRendererTest, OmitsTheCropWhenNoneIsConfigured)
{
    QrRenderGeometry geometry = SquareGeometry();
    geometry.dark.texCoords.clear();

    std::vector<bool> const modules{ true };

    QrRenderResult const result = RenderModuleGrid(modules, 1, 1, geometry);

    ASSERT_EQ(QrRenderError::None, result.error);
    EXPECT_EQ("|TInterface/DialogFrame/UI-DialogBox-Background:14:10:0:0|t", result.text);
}

/// Every escape in row N carries offY = -N * (lineAdvance - moduleHeight), which is what
/// closes the seam between rows when the modules are shorter than the font's line advance.
TEST(QrRendererTest, AccumulatesRowOffsetWhenModulesAreShorterThanTheLineAdvance)
{
    QrRenderGeometry geometry = SquareGeometry();
    geometry.moduleHeight = 9;

    std::vector<bool> const modules{ true, true };

    QrRenderResult const result = RenderModuleGrid(modules, 1, 2, geometry);

    ASSERT_EQ(QrRenderError::None, result.error);
    EXPECT_EQ(Dark(10, 9, 0) + "\n" + Dark(10, 9, -5), result.text);
}

/// Bottom anchoring leaves the last row on its own line and displaces the earlier ones
/// upward from there, so the height the text block reserves but does not draw into ends up
/// above the grid rather than below it.
TEST(QrRendererTest, AnchorsTheLastRowToItsOwnLine)
{
    QrRenderGeometry geometry = SquareGeometry();
    geometry.anchorBottom = true;
    geometry.moduleHeight = 9;

    std::vector<bool> const modules{ true, true, true };

    QrRenderResult const result = RenderModuleGrid(modules, 1, 3, geometry);

    ASSERT_EQ(QrRenderError::None, result.error);

    // Same 5 px spacing as the top-anchored case, shifted so the last row lands on zero.
    EXPECT_EQ(Dark(10, 9, 10) + "\n" + Dark(10, 9, 5) + "\n" + Dark(10, 9, 0), result.text);
}

/// A single row cannot be displaced by either anchor - there is nothing to displace it
/// relative to - so both modes have to agree on it.
TEST(QrRendererTest, LeavesASingleRowUnshiftedUnderEitherAnchor)
{
    QrRenderGeometry topAnchored = SquareGeometry();
    topAnchored.moduleHeight = 9;

    QrRenderGeometry bottomAnchored = topAnchored;
    bottomAnchored.anchorBottom = true;

    std::vector<bool> const modules{ true };

    EXPECT_EQ(RenderModuleGrid(modules, 1, 1, topAnchored).text,
        RenderModuleGrid(modules, 1, 1, bottomAnchored).text);
    EXPECT_EQ(Dark(10, 9, 0), RenderModuleGrid(modules, 1, 1, bottomAnchored).text);
}

/// A negative line advance is the documented escape hatch for an inverted offY sign
/// convention, so it has to flow through to a positive offset rather than be clamped.
TEST(QrRendererTest, NegativeLineAdvanceFlipsTheRowOffset)
{
    QrRenderGeometry geometry = SquareGeometry();
    geometry.moduleHeight = 9;
    geometry.lineAdvance  = -14;

    std::vector<bool> const modules{ true, true };

    QrRenderResult const result = RenderModuleGrid(modules, 1, 2, geometry);

    ASSERT_EQ(QrRenderError::None, result.error);
    EXPECT_NE(std::string::npos, result.text.find(":9:10:0:23:"));
}

/// Rows with no offset collapse to offY 0, which is the whole point of the chat backend's
/// square geometry.
TEST(QrRendererTest, RowOffsetCollapsesWhenModuleHeightMatchesLineAdvance)
{
    std::vector<bool> const modules{ true, true, true, true };

    QrRenderResult const result = RenderModuleGrid(modules, 1, 4, SquareGeometry());

    ASSERT_EQ(QrRenderError::None, result.error);
    EXPECT_EQ(std::string::npos, result.text.find(":0:-"));
}

/// The quiet zone pads all four sides and is drawn as real light textures, never left
/// blank: a blank line would take the frame's own background colour, which is dark in chat
/// and would break decoding there.
TEST(QrRendererTest, PadsTheQuietZoneOnAllFourSidesWithRealTextures)
{
    QrBitmap bitmap;
    bitmap.size = 1;
    bitmap.modules = { true };

    QrRenderGeometry geometry = SquareGeometry();
    geometry.quietZone = QR_QUIET_ZONE_MODULES;

    QrRenderResult const result = RenderQr(bitmap, geometry);

    ASSERT_EQ(QrRenderError::None, result.error);

    std::vector<std::string> const rows = SplitRows(result.text);
    ASSERT_EQ(std::size_t(9), rows.size());

    // The eight quiet rows are one merged light run spanning the full 9-module width.
    std::string const fullLightRow = Light(90, 14, 0);
    for (std::size_t i = 0; i < rows.size(); ++i)
    {
        if (i == QR_QUIET_ZONE_MODULES)
            continue;

        EXPECT_EQ(fullLightRow, rows[i]) << "row " << i << " should be entirely quiet zone";
    }

    // The data row keeps four light modules on each side of the single dark one.
    EXPECT_EQ(Light(40, 14, 0) + Dark(10, 14, 0) + Light(40, 14, 0), rows[QR_QUIET_ZONE_MODULES]);
}

/// Every module of border is two more chat lines to find room for, and frame height is what
/// this module runs out of first, so the default sits below the spec's four.
TEST(QrRendererTest, DefaultsToTheNarrowQuietZone)
{
    QrBitmap bitmap;
    bitmap.size = 1;
    bitmap.modules = { true };

    QrRenderResult const result = RenderQr(bitmap, SquareGeometry());

    ASSERT_EQ(QrRenderError::None, result.error);
    ASSERT_EQ(std::size_t(1 + 2 * QR_QUIET_ZONE_DEFAULT_MODULES), SplitRows(result.text).size());
}

/// The quiet zone is configurable because the trade it makes - decoder margin against chat
/// lines - can only be settled against a real frame.
TEST(QrRendererTest, HonoursAConfiguredQuietZone)
{
    QrBitmap bitmap;
    bitmap.size = 1;
    bitmap.modules = { true };

    QrRenderGeometry geometry = SquareGeometry();
    geometry.quietZone = 2;

    QrRenderResult const result = RenderQr(bitmap, geometry);

    ASSERT_EQ(QrRenderError::None, result.error);

    std::vector<std::string> const rows = SplitRows(result.text);
    ASSERT_EQ(std::size_t(5), rows.size());
    EXPECT_EQ(Light(50, 14, 0), rows[0]);
    EXPECT_EQ(Light(20, 14, 0) + Dark(10, 14, 0) + Light(20, 14, 0), rows[2]);
}

/// A row wider than the frame wraps, which turns the grid into noise. Refusing up front
/// beats emitting something that cannot possibly scan.
TEST(QrRendererTest, RefusesRowsWiderThanTheFrame)
{
    QrRenderGeometry geometry = SquareGeometry();
    geometry.maxRowWidthPx = 50;

    std::vector<bool> const modules(6, false);

    QrRenderResult const result = RenderModuleGrid(modules, 6, 1, geometry);

    EXPECT_EQ(QrRenderError::RowTooWide, result.error);
    EXPECT_EQ(std::uint32_t(60), result.rowWidthPx);
    EXPECT_TRUE(result.text.empty());
}

TEST(QrRendererTest, AllowsRowsExactlyAtTheFrameWidth)
{
    QrRenderGeometry geometry = SquareGeometry();
    geometry.maxRowWidthPx = 60;

    std::vector<bool> const modules(6, false);

    QrRenderResult const result = RenderModuleGrid(modules, 6, 1, geometry);

    EXPECT_EQ(QrRenderError::None, result.error);
}

/// Oversized strings are rejected rather than sent, because what an over-long line does to
/// the client is exactly the unknown this module has to stay on the safe side of.
TEST(QrRendererTest, RefusesPayloadsOverTheByteCap)
{
    QrRenderGeometry geometry = SquareGeometry();
    geometry.maxPayloadBytes = 16;

    std::vector<bool> const modules{ true, false };

    QrRenderResult const result = RenderModuleGrid(modules, 2, 1, geometry);

    EXPECT_EQ(QrRenderError::PayloadTooLarge, result.error);
    EXPECT_GT(result.byteCount, std::size_t(16));
    EXPECT_TRUE(result.text.empty());
}

/// Each of the four vertical pairs has to reach its own style. Getting this wrong inverts
/// or blanks half the grid, which still looks like a QR code and still will not scan.
TEST(QrRendererTest, PackedRowsPickAStylePerVerticalPair)
{
    // Columns, top over bottom: dark/dark, light/light, dark/light, light/dark.
    std::vector<bool> const modules{ true, false, true, false,
                                     true, false, false, true };

    QrRenderResult const result = RenderModuleGrid(modules, 4, 2, PackedGeometry());

    ASSERT_EQ(QrRenderError::None, result.error);
    EXPECT_EQ(Pair(PACK_DARK, 10, 0) + Pair(PACK_LIGHT, 10, 0) + Pair(PACK_DOL, 10, 0) +
        Pair(PACK_LOD, 10, 0), result.text);
}

/// The whole point: the grid occupies half the chat lines. Frame height is what this module
/// runs out of first, so this is the number that decides whether a code fits at all.
TEST(QrRendererTest, PackedRowsHalveTheLineCount)
{
    std::vector<bool> const modules(8, true);

    QrRenderResult const result = RenderModuleGrid(modules, 1, 8, PackedGeometry());

    ASSERT_EQ(QrRenderError::None, result.error);
    EXPECT_EQ(std::size_t(4), SplitRows(result.text).size());
}

/// A run has to agree on both rows at once, so a row that is uniform on its own still
/// splits where its partner changes. This is why packing saves lines and not bytes.
TEST(QrRendererTest, PackedRunsMergeOnlyWhereBothRowsAgree)
{
    std::vector<bool> const modules{ true, true,
                                     true, false };

    QrRenderResult const result = RenderModuleGrid(modules, 2, 2, PackedGeometry());

    ASSERT_EQ(QrRenderError::None, result.error);
    EXPECT_EQ(Pair(PACK_DARK, 10, 0) + Pair(PACK_DOL, 10, 0), result.text);
}

/// An odd row count leaves the final line without a lower row. Reading the absent one as
/// light lands it as another module of quiet zone; reading it as dark would print a false
/// row of modules straight across the bottom of the symbol.
TEST(QrRendererTest, PackedRowsPairAnOddLastRowWithLight)
{
    std::vector<bool> const modules(3, true);

    QrRenderResult const result = RenderModuleGrid(modules, 1, 3, PackedGeometry());

    ASSERT_EQ(QrRenderError::None, result.error);
    EXPECT_EQ(Pair(PACK_DARK, 10, 0) + "\n" + Pair(PACK_DOL, 10, -14), result.text);
}

/// A packed line spans two module rows, so it needs one module less lift than a single row
/// does - the step is -lineAdvance, not the unpacked expression with the pair height
/// substituted in. Getting this wrong stacks every line onto the one below it.
TEST(QrRendererTest, PackedRowOffsetDropsOneModuleOfLiftPerLine)
{
    std::vector<bool> const modules(4, true);

    QrRenderResult const result = RenderModuleGrid(modules, 1, 4, PackedGeometry());

    ASSERT_EQ(QrRenderError::None, result.error);
    EXPECT_EQ(Pair(PACK_DARK, 10, 0) + "\n" + Pair(PACK_DARK, 10, -14), result.text);
}

/// The shipped chat geometry lifts nothing at all once packed: two 7 px modules already
/// fill the font's own advance, which is the reason packing is free at that module size.
TEST(QrRendererTest, PackedRowsNeedNoLiftAtTheChatDefaults)
{
    QrRenderGeometry geometry = PackedGeometry();
    geometry.moduleWidth  = 7;
    geometry.moduleHeight = 7;
    geometry.lineAdvance  = 0;

    std::vector<bool> const modules(6, true);

    QrRenderResult const result = RenderModuleGrid(modules, 1, 6, geometry);

    ASSERT_EQ(QrRenderError::None, result.error);
    EXPECT_EQ(std::string::npos, result.text.find(":0:-")) << "no line should be displaced";
    EXPECT_EQ(std::size_t(3), SplitRows(result.text).size());
}

/// Bottom anchoring still has to land the final line on zero once the lines are pairs.
TEST(QrRendererTest, PackedRowsHonourBottomAnchoring)
{
    QrRenderGeometry geometry = PackedGeometry();
    geometry.anchorBottom = true;

    std::vector<bool> const modules(4, true);

    QrRenderResult const result = RenderModuleGrid(modules, 1, 4, geometry);

    ASSERT_EQ(QrRenderError::None, result.error);
    EXPECT_EQ(Pair(PACK_DARK, 10, 14) + "\n" + Pair(PACK_DARK, 10, 0), result.text);
}

/// Packing has to leave the quiet zone intact, drawn as real light textures like the
/// unpacked path - a blank line would take the chat frame's own dark background.
TEST(QrRendererTest, PackedRowsKeepTheQuietZone)
{
    QrBitmap bitmap;
    bitmap.size = 1;
    bitmap.modules = { true };

    QrRenderGeometry geometry = PackedGeometry();
    geometry.quietZone = QR_QUIET_ZONE_MODULES;

    QrRenderResult const result = RenderQr(bitmap, geometry);

    ASSERT_EQ(QrRenderError::None, result.error);

    // 9 padded rows pair down to 5 lines, the last of them half quiet zone and half absent.
    std::vector<std::string> const rows = SplitRows(result.text);
    ASSERT_EQ(std::size_t(5), rows.size());
    EXPECT_EQ(Pair(PACK_LIGHT, 90, 0), rows[0]);
    EXPECT_EQ(Pair(PACK_LIGHT, 90, -14), rows[1]);

    // Row 4 of 9 is the upper half of the third pair, so the one dark module draws as
    // dark-over-light rather than as a solid pair.
    EXPECT_EQ(Pair(PACK_LIGHT, 40, -28) + Pair(PACK_DOL, 10, -28) + Pair(PACK_LIGHT, 40, -28), rows[2]);
}

/// Sanity bound on the size estimate the whole design rests on: a version 1 code is the
/// smallest thing this module can draw, and it is already tens of kilobytes.
TEST(QrRendererTest, ProducesOneLinePerPaddedRow)
{
    QrBitmap bitmap;
    bitmap.size = 21;
    bitmap.modules.resize(21 * 21);

    for (std::size_t i = 0; i < bitmap.modules.size(); ++i)
        bitmap.modules[i] = (i % 2) == 0;

    QrRenderResult const result = RenderQr(bitmap, SquareGeometry());

    ASSERT_EQ(QrRenderError::None, result.error);
    EXPECT_EQ(std::size_t(29), SplitRows(result.text).size());
    EXPECT_EQ(std::size_t(28), std::count(result.text.begin(), result.text.end(), '\n'));
}
