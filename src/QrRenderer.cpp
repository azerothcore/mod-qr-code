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

    // Worst case is no run merging at all: one escape per module.
    std::size_t const escapeSize = 24 + std::max(geometry.dark.texture.size() + geometry.dark.texCoords.size(),
        geometry.light.texture.size() + geometry.light.texCoords.size());
    result.text.reserve(std::size_t(width) * height * escapeSize);

    // The anchor row is the one drawn on its own text line; every other row is displaced
    // relative to it. Anchoring to the last row pushes the reserved-but-unused height
    // above the grid instead of below it.
    std::int32_t const anchorRow = geometry.anchorBottom ? std::int32_t(height) - 1 : 0;

    for (std::uint32_t y = 0; y < height; ++y)
    {
        if (y)
            result.text += '\n';

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

            AppendRun(result.text, dark ? geometry.dark : geometry.light,
                (runEnd - runStart) * geometry.moduleWidth, geometry.moduleHeight, offY);
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

    std::uint32_t const padded = bitmap.size + 2 * QR_QUIET_ZONE_MODULES;

    std::vector<bool> modules(std::size_t(padded) * padded, false);

    for (std::uint32_t y = 0; y < bitmap.size; ++y)
        for (std::uint32_t x = 0; x < bitmap.size; ++x)
            modules[std::size_t(y + QR_QUIET_ZONE_MODULES) * padded + x + QR_QUIET_ZONE_MODULES] = bitmap.At(x, y);

    return RenderModuleGrid(modules, padded, padded, geometry);
}
