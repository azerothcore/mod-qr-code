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

    // Named for the pattern each one draws, top module first, so a mis-indexed style shows
    // up as the wrong path rather than as some other style's plausible-looking string.
    constexpr char const* PACK_LL = "Pack/LL";
    constexpr char const* PACK_LD = "Pack/LD";
    constexpr char const* PACK_DL = "Pack/DL";
    constexpr char const* PACK_DD = "Pack/DD";

    QrRenderGeometry PackedGeometry()
    {
        QrRenderGeometry geometry = SquareGeometry();
        geometry.rowsPerLine          = 2;
        geometry.packed[0b00].texture = PACK_LL;
        geometry.packed[0b01].texture = PACK_LD;
        geometry.packed[0b10].texture = PACK_DL;
        geometry.packed[0b11].texture = PACK_DD;
        return geometry;
    }

    /// A two-row packed escape is always two modules tall, hence the doubled height.
    std::string Pair(char const* texture, std::uint32_t widthPx, std::int32_t offY)
    {
        return std::string("|T") + texture + ":28:" + std::to_string(widthPx) + ":0:" +
            std::to_string(offY) + "|t";
    }
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
    EXPECT_EQ(Pair(PACK_DD, 10, 0) + Pair(PACK_LL, 10, 0) + Pair(PACK_DL, 10, 0) +
        Pair(PACK_LD, 10, 0), result.text);
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
    EXPECT_EQ(Pair(PACK_DD, 10, 0) + Pair(PACK_DL, 10, 0), result.text);
}

/// An odd row count leaves the final line without a lower row. Reading the absent one as
/// light lands it as another module of quiet zone; reading it as dark would print a false
/// row of modules straight across the bottom of the symbol.
TEST(QrRendererTest, PackedRowsPairAnOddLastRowWithLight)
{
    std::vector<bool> const modules(3, true);

    QrRenderResult const result = RenderModuleGrid(modules, 1, 3, PackedGeometry());

    ASSERT_EQ(QrRenderError::None, result.error);
    EXPECT_EQ(Pair(PACK_DD, 10, 0) + "\n" + Pair(PACK_DL, 10, -14), result.text);
}

/// A packed line spans two module rows, so it needs one module less lift than a single row
/// does - the step is -lineAdvance, not the unpacked expression with the pair height
/// substituted in. Getting this wrong stacks every line onto the one below it.
TEST(QrRendererTest, PackedRowOffsetDropsOneModuleOfLiftPerLine)
{
    std::vector<bool> const modules(4, true);

    QrRenderResult const result = RenderModuleGrid(modules, 1, 4, PackedGeometry());

    ASSERT_EQ(QrRenderError::None, result.error);
    EXPECT_EQ(Pair(PACK_DD, 10, 0) + "\n" + Pair(PACK_DD, 10, -14), result.text);
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
    EXPECT_EQ(Pair(PACK_DD, 10, 14) + "\n" + Pair(PACK_DD, 10, 0), result.text);
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
    EXPECT_EQ(Pair(PACK_LL, 90, 0), rows[0]);
    EXPECT_EQ(Pair(PACK_LL, 90, -14), rows[1]);

    // Row 4 of 9 is the upper half of the third pair, so the one dark module draws as
    // dark-over-light rather than as a solid pair.
    EXPECT_EQ(Pair(PACK_LL, 40, -28) + Pair(PACK_DL, 10, -28) + Pair(PACK_LL, 40, -28), rows[2]);
}

/// Three rows per line reaches eight styles, and the index has to read the top module as
/// the high bit. Reversing it mirrors every line top to bottom, which still looks like a
/// grid and still will not scan.
TEST(QrRendererTest, IndexesPackedStylesWithTheTopModuleAsTheHighBit)
{
    QrRenderGeometry geometry = SquareGeometry();
    geometry.rowsPerLine = 3;

    for (std::size_t i = 0; i < 8; ++i)
        geometry.packed[i].texture = "Pack/" + std::to_string(i);

    // Dark, light, dark reading downward is 0b101.
    std::vector<bool> const modules{ true, false, true };

    QrRenderResult const result = RenderModuleGrid(modules, 1, 3, geometry);

    ASSERT_EQ(QrRenderError::None, result.error);
    EXPECT_EQ("|TPack/5:42:10:0:0|t", result.text);
}

/// The lift per line is (2 - rows) * moduleHeight - lineAdvance. At three rows that is one
/// module more displacement than two rows needs, not less, and getting the sign or the
/// factor wrong stacks the lines on top of each other.
TEST(QrRendererTest, ScalesTheRowOffsetWithTheRowsPerLine)
{
    QrRenderGeometry geometry = SquareGeometry();
    geometry.rowsPerLine = 3;
    geometry.packed[0b111].texture = PACK_DD;

    std::vector<bool> const modules(6, true);

    QrRenderResult const result = RenderModuleGrid(modules, 1, 6, geometry);

    ASSERT_EQ(QrRenderError::None, result.error);

    // step = (2 - 3) * 14 - 14 = -28, so the second line sits 28 px lower than the first.
    EXPECT_EQ("|TPack/DD:42:10:0:0|t\n|TPack/DD:42:10:0:-28|t", result.text);
}

/// A grid that does not divide evenly pads the final line with light, which reads as more
/// quiet zone. Padding with dark would print a false row of modules across the bottom.
TEST(QrRendererTest, PadsTheLastPackedLineWithLight)
{
    QrRenderGeometry geometry = SquareGeometry();
    geometry.rowsPerLine = 3;

    for (std::size_t i = 0; i < 8; ++i)
        geometry.packed[i].texture = "Pack/" + std::to_string(i);

    // Four dark rows over three-row lines: the second line is dark, then two missing rows.
    std::vector<bool> const modules(4, true);

    QrRenderResult const result = RenderModuleGrid(modules, 1, 4, geometry);

    ASSERT_EQ(QrRenderError::None, result.error);
    EXPECT_EQ("|TPack/7:42:10:0:0|t\n|TPack/4:42:10:0:-28|t", result.text);
}

/// Asking for more rows than there are styles would index past the array, so the renderer
/// clamps rather than trusting config it cannot satisfy.
TEST(QrRendererTest, ClampsRowsPerLineToTheSupportedMaximum)
{
    QrRenderGeometry geometry = SquareGeometry();
    geometry.rowsPerLine = QR_MAX_ROWS_PER_LINE + 7;

    for (std::size_t i = 0; i < QR_PACKED_STYLE_COUNT; ++i)
        geometry.packed[i].texture = "Pack/" + std::to_string(i);

    std::vector<bool> const modules(QR_MAX_ROWS_PER_LINE, true);

    QrRenderResult const result =
        RenderModuleGrid(modules, 1, QR_MAX_ROWS_PER_LINE, geometry);

    ASSERT_EQ(QrRenderError::None, result.error);
    EXPECT_EQ(std::size_t(1), SplitRows(result.text).size());
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
