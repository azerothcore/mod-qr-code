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

    /// Reads a column's modules top to bottom into a bit index, most significant first, so
    /// the index spells the pattern the way the config names it.
    std::size_t ColumnState(std::vector<bool> const& modules, std::uint32_t width, std::uint32_t height,
        std::uint32_t firstRow, std::uint32_t rows, std::uint32_t x)
    {
        std::size_t state = 0;

        for (std::uint32_t d = 0; d < rows; ++d)
        {
            std::uint32_t const row = firstRow + d;

            // A grid that does not divide evenly leaves the last line short of rows. Reading
            // the missing ones as light pads with the quiet zone's own colour, so they land
            // as one more module of border rather than as a stray dark edge.
            bool const dark = row < height && modules[std::size_t(row) * width + x];
            state = (state << 1) | (dark ? 1u : 0u);
        }

        return state;
    }

    QrModuleStyle const& StyleFor(QrRenderGeometry const& geometry, std::uint32_t rows, std::size_t state)
    {
        if (rows == 1)
            return state ? geometry.dark : geometry.light;

        return geometry.packed[state];
    }
}

QrRenderGeometry ApplyPalette(QrRenderGeometry geometry, QrPalette const& palette)
{
    geometry.dark = palette.dark;

    if (palette.hasLight)
        geometry.light = palette.light;

    // Measured, not reasoned: a green code that would not scan at one module of quiet zone scans
    // at two. See QR_QUIET_ZONE_COLOUR_MODULES for why colour is the case that needs the margin.
    // Raised rather than set, so a config asking for the spec's four still gets four.
    geometry.quietZone = std::max(geometry.quietZone, QR_QUIET_ZONE_COLOUR_MODULES);

    if (palette.hasPacked)
    {
        geometry.packed = palette.packed;
        return geometry;
    }

    // Dropping to one row per line without touching the pixel sizes leaves each line carrying a
    // module a fraction of its height. The renderer closes that gap with a per-line offset, and
    // the offset accumulates: at the shipped 3x5 geometry the top row of a version 2 code ends up
    // 234 px below the chat line it was booked against, so the grid draws well clear of its own
    // lines and whatever is printed next lands on top of it.
    //
    // Scaling the module up by exactly the packing it gave up keeps the drawn height of a line
    // unchanged, and with it the per-line offset, so the fallback inherits the vertical behaviour
    // of the geometry that was actually tuned in-game. Width scales too, or the modules would come
    // out as many times taller than they are wide.
    if (geometry.rowsPerLine > 1)
    {
        std::uint32_t const lost = geometry.rowsPerLine;

        // Derived from lineAdvance = 2 * moduleHeight - fontAdvance, the relation the tuned chat
        // defaults satisfy: holding the font advance fixed while the module grows by (lost - 1)
        // of its own height moves lineAdvance by twice that.
        geometry.lineAdvance += 2 * std::int32_t(geometry.moduleHeight) * (std::int32_t(lost) - 1);
        geometry.moduleHeight *= lost;
        geometry.moduleWidth *= lost;
    }

    geometry.rowsPerLine = 1;
    return geometry;
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

    std::uint32_t const rows = std::clamp(geometry.rowsPerLine, 1u, QR_MAX_ROWS_PER_LINE);
    std::uint32_t const lines = (height + rows - 1) / rows;
    std::uint32_t const lineHeight = rows * geometry.moduleHeight;
    std::size_t const stateCount = std::size_t(1) << rows;

    // Worst case is no run merging at all: one escape per column on every line.
    std::size_t widest = 0;
    for (std::size_t state = 0; state < stateCount; ++state)
        widest = std::max(widest, StyleSize(StyleFor(geometry, rows, state)));

    result.text.reserve(std::size_t(width) * lines * (24 + widest));

    // The anchor line keeps its natural position and the others are displaced relative to
    // it. Anchoring to the last one pushes the reserved-but-unused height above the grid
    // instead of below it.
    std::int32_t const anchorLine = geometry.anchorBottom ? std::int32_t(lines) - 1 : 0;

    // offY shifts what is drawn without changing the height the line reserves, so the grid
    // closes up while the text block still books its full height. Positive is upward on the
    // client.
    //
    // One row per line is lifted by (moduleHeight - lineAdvance), which places the font's
    // effective advance at (2 * moduleHeight - lineAdvance). A line spanning `rows` modules
    // needs that advance minus its own height, so the step generalises to
    // ((2 - rows) * moduleHeight - lineAdvance) - which reproduces the single-row case at
    // rows 1 and cancels to -lineAdvance at rows 2, where a 14 px pair already fills the
    // advance. Reusing the single-row expression with the taller line double-counts the lift
    // and collapses the grid onto itself.
    std::int32_t const step =
        (2 - std::int32_t(rows)) * std::int32_t(geometry.moduleHeight) - geometry.lineAdvance;

    for (std::uint32_t line = 0; line < lines; ++line)
    {
        if (line)
            result.text += '\n';

        std::int32_t const offY = (std::int32_t(line) - anchorLine) * step;
        std::uint32_t const firstRow = line * rows;

        std::uint32_t runStart = 0;
        while (runStart < width)
        {
            std::size_t const state = ColumnState(modules, width, height, firstRow, rows, runStart);

            std::uint32_t runEnd = runStart + 1;
            while (runEnd < width &&
                ColumnState(modules, width, height, firstRow, rows, runEnd) == state)
                ++runEnd;

            AppendRun(result.text, StyleFor(geometry, rows, state),
                (runEnd - runStart) * geometry.moduleWidth, lineHeight, offY);
            runStart = runEnd;
        }
    }

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
