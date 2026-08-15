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

#include "QrRenderer.h"

#include <algorithm>

namespace
{
    /// Appends one inline-texture escape spanning @p widthPx pixels.
    ///
    /// Both colours use the same form; only the texture differs, because the client
    /// ignores the vertex-colour arguments. offX is always 0 - runs are laid out by the
    /// text flow - but it still has to be written, since offY cannot be reached without it.
    void AppendRun(std::string& out, QrModuleStyle const& style, std::uint32_t widthPx, std::uint32_t heightPx,
        std::int32_t offY)
    {
        out += "|T";
        out += style.texture;
        out += ':';
        out += std::to_string(heightPx);
        out += ':';
        out += std::to_string(widthPx);
        out += ":0:";
        out += std::to_string(offY);

        if (!style.texCoords.empty())
        {
            out += ':';
            out += style.texCoords;
        }

        out += "|t";
    }

    std::size_t StyleSize(QrModuleStyle const& style)
    {
        return style.texture.size() + style.texCoords.size();
    }

    QrModuleStyle const& PairStyle(QrPackedStyles const& styles, bool top, bool bottom)
    {
        if (top == bottom)
            return top ? styles.dark : styles.light;

        return top ? styles.darkOverLight : styles.lightOverDark;
    }

    /// Draws one module row per text line.
    void AppendSingleRows(std::string& out, std::vector<bool> const& modules, std::uint32_t width,
        std::uint32_t height, QrRenderGeometry const& geometry)
    {
        // Worst case is no run merging at all: one escape per module.
        std::size_t const escapeSize = 24 + std::max(StyleSize(geometry.dark), StyleSize(geometry.light));
        out.reserve(std::size_t(width) * height * escapeSize);

        // The anchor row is the one drawn on its own text line; every other row is displaced
        // relative to it. Anchoring to the last row pushes the reserved-but-unused height
        // above the grid instead of below it.
        std::int32_t const anchorRow = geometry.anchorBottom ? std::int32_t(height) - 1 : 0;

        for (std::uint32_t y = 0; y < height; ++y)
        {
            if (y)
                out += '\n';

            // offY shifts the drawn texture without shrinking the line's layout advance, so
            // the grid closes up while the text block still reserves its full height.
            // Positive is upward on the client, which is why a lower lineAdvance packs the
            // rows tighter.
            std::int32_t const offY =
                -(std::int32_t(y) - anchorRow) * (geometry.lineAdvance - std::int32_t(geometry.moduleHeight));

            std::uint32_t runStart = 0;
            while (runStart < width)
            {
                bool const dark = modules[std::size_t(y) * width + runStart];

                std::uint32_t runEnd = runStart + 1;
                while (runEnd < width && modules[std::size_t(y) * width + runEnd] == dark)
                    ++runEnd;

                AppendRun(out, dark ? geometry.dark : geometry.light,
                    (runEnd - runStart) * geometry.moduleWidth, geometry.moduleHeight, offY);
                runStart = runEnd;
            }
        }
    }

    /// Draws two module rows per text line, one escape of double height per run.
    ///
    /// A run now has to agree on both rows at once, so merging is weaker than the unpacked
    /// case and the escape count barely falls - the saving is the line count, which halves.
    void AppendPackedRows(std::string& out, std::vector<bool> const& modules, std::uint32_t width,
        std::uint32_t height, QrRenderGeometry const& geometry)
    {
        std::uint32_t const lines = (height + 1) / 2;
        std::int32_t const pairHeight = 2 * std::int32_t(geometry.moduleHeight);

        std::size_t const escapeSize = 24 + std::max({ StyleSize(geometry.packed.dark),
            StyleSize(geometry.packed.light), StyleSize(geometry.packed.darkOverLight),
            StyleSize(geometry.packed.lightOverDark) });
        out.reserve(std::size_t(width) * lines * escapeSize);

        std::int32_t const anchorLine = geometry.anchorBottom ? std::int32_t(lines) - 1 : 0;

        // An odd row count leaves the last line with no lower row. Reading the missing one
        // as light pairs it with the quiet zone's own colour, so it lands as one more
        // module of border rather than as a stray dark edge.
        auto at = [&](std::uint32_t row, std::uint32_t x)
        {
            return row < height && modules[std::size_t(row) * width + x];
        };

        for (std::uint32_t line = 0; line < lines; ++line)
        {
            if (line)
                out += '\n';

            // A single row is lifted by (moduleHeight - lineAdvance), which puts the font's
            // effective advance at (2 * moduleHeight - lineAdvance). Two rows now sit
            // between consecutive lines, so the lift each one needs is that advance minus
            // the pair height - which reduces to -lineAdvance, one module less than the
            // unpacked case. Reusing the unpacked expression with pairHeight double-counts
            // it and collapses the grid onto itself.
            //
            // At the chat default of 0 it cancels outright: a 14 px pair already fills the
            // advance, which is why packing costs nothing at 7 px modules.
            std::int32_t const offY = -(std::int32_t(line) - anchorLine) * geometry.lineAdvance;

            std::uint32_t const topRow    = line * 2;
            std::uint32_t const bottomRow = topRow + 1;

            std::uint32_t runStart = 0;
            while (runStart < width)
            {
                bool const top    = at(topRow, runStart);
                bool const bottom = at(bottomRow, runStart);

                std::uint32_t runEnd = runStart + 1;
                while (runEnd < width && at(topRow, runEnd) == top && at(bottomRow, runEnd) == bottom)
                    ++runEnd;

                AppendRun(out, PairStyle(geometry.packed, top, bottom),
                    (runEnd - runStart) * geometry.moduleWidth, std::uint32_t(pairHeight), offY);
                runStart = runEnd;
            }
        }
    }
}

QrRenderResult RenderModuleGrid(std::vector<bool> const& modules, std::uint32_t width, std::uint32_t height,
    QrRenderGeometry const& geometry)
{
    QrRenderResult result;
    result.rowWidthPx = width * geometry.moduleWidth;

    if (geometry.maxRowWidthPx && result.rowWidthPx > geometry.maxRowWidthPx)
    {
        result.error = QrRenderError::RowTooWide;
        return result;
    }

    if (!width || !height || modules.size() < std::size_t(width) * height)
        return result;

    if (geometry.packRows)
        AppendPackedRows(result.text, modules, width, height, geometry);
    else
        AppendSingleRows(result.text, modules, width, height, geometry);

    result.byteCount = result.text.size();

    if (geometry.maxPayloadBytes && result.byteCount > geometry.maxPayloadBytes)
    {
        result.error = QrRenderError::PayloadTooLarge;
        result.text.clear();
    }

    return result;
}

QrRenderResult RenderQr(QrBitmap const& bitmap, QrRenderGeometry const& geometry)
{
    QrRenderResult result;

    if (!bitmap.size)
        return result;

    std::uint32_t const quietZone = geometry.quietZone;
    std::uint32_t const padded = bitmap.size + 2 * quietZone;

    std::vector<bool> modules(std::size_t(padded) * padded, false);

    for (std::uint32_t y = 0; y < bitmap.size; ++y)
        for (std::uint32_t x = 0; x < bitmap.size; ++x)
            modules[std::size_t(y + quietZone) * padded + x + quietZone] = bitmap.At(x, y);

    return RenderModuleGrid(modules, padded, padded, geometry);
}
