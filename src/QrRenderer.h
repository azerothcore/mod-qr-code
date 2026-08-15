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

#ifndef MOD_QR_CODE_QR_RENDERER_H
#define MOD_QR_CODE_QR_RENDERER_H

#include "QrEncoder.h"

#include <cstdint>
#include <string>
#include <vector>

/// Quiet-zone width mandated by the QR spec, in modules, on every side. Also the widest
/// this module will draw: past it there is nothing left to gain, only rows to lose.
constexpr std::uint32_t QR_QUIET_ZONE_MODULES = 4;

/// Quiet zone drawn by default, in modules.
///
/// Below the spec's 4 on purpose. The border is charged in chat lines like everything else -
/// each module of it is two more rows the frame has to show at once - and the frame running
/// out of height is the failure this module actually hits, while decoders in practice manage
/// on one module of margin against a clean light background.
constexpr std::uint32_t QR_QUIET_ZONE_DEFAULT_MODULES = 1;

/// How one colour of module is drawn.
///
/// The 3.3.5a client accepts the vertex-colour arguments of the |T escape but discards
/// them, so a module's colour cannot be tinted - it has to come from a texture that is
/// already that colour. Both the texture and the sub-rect are therefore data, not
/// constants: which client textures are usable is not something the server can know.
struct QrModuleStyle
{
    /// Texture path, e.g. "Interface\\Buttons\\WHITE8X8". Backslashes and forward
    /// slashes both work.
    std::string texture;

    /// Optional trailing escape arguments selecting a sub-rect of the texture, as
    /// "texWidth:texHeight:left:right:top:bottom". The client divides the edges by the
    /// stated dimensions, so "100:100:45:55:45:55" reads as "the middle 10%" whatever the
    /// texture's real size. Empty means draw the whole texture.
    ///
    /// Cropping matters for patterned textures: stretching a whole stone-effect
    /// background across a module shows the pattern, while a small flat crop gives an
    /// even block, and evenness is what a decoder needs.
    std::string texCoords;
};

/// The four vertical module pairs a packed escape has to be able to draw.
///
/// Packing puts two module rows inside one escape, so a mixed pair needs a texture whose
/// crop already contains a dark band sitting directly on a light one - it cannot be built
/// out of the solid styles, because an escape names exactly one texture. The set also has
/// to be internally consistent: if the solid dark and the dark half of a mixed pair are
/// not the same colour, the seam between pairs reads as a module edge.
struct QrPackedStyles
{
    QrModuleStyle dark;          ///< Both modules dark.
    QrModuleStyle light;         ///< Both modules light, including the quiet zone.
    QrModuleStyle darkOverLight; ///< Upper module dark, lower module light.
    QrModuleStyle lightOverDark; ///< Upper module light, lower module dark.
};

/// Pixel geometry the escape sequences are emitted with. Every field is a client-side
/// value that can only be settled by looking at the result in-game, so all of them come
/// from config and none are baked into the renderer.
struct QrRenderGeometry
{
    QrModuleStyle dark;  ///< Style for dark modules.
    QrModuleStyle light; ///< Style for light modules, including the quiet zone.

    std::uint32_t moduleWidth  = 14; ///< Drawn width of one module, in pixels.
    std::uint32_t moduleHeight = 14; ///< Drawn height of one module, in pixels.

    /// Light border drawn on every side, in modules.
    ///
    /// The spec mandates QR_QUIET_ZONE_MODULES and decoders are calibrated for it, but the
    /// border is charged in chat lines like everything else: each module of it costs two
    /// rows the frame has to show at once, and height is what this module runs out of. The
    /// default trades margin for rows; raise it if a code will not scan.
    std::uint32_t quietZone = QR_QUIET_ZONE_DEFAULT_MODULES;

    /// Drives the per-row vertical offset, applied cumulatively as
    /// -row * (lineAdvance - moduleHeight).
    ///
    /// Despite the name this is a dial rather than a measurement. The client reads a
    /// positive offset as upward, so a *lower* value packs the rows tighter, and zero or
    /// negative is the normal setting once modules are shorter than the font's row
    /// spacing. Renaming it would break every deployed config, so the meaning is
    /// documented instead.
    std::int32_t lineAdvance = 0;

    /// Which end of the grid keeps its natural position.
    ///
    /// The row offset only moves what is drawn, never the layout height the text block
    /// reserves, so a grid of short modules always leaves slack somewhere. Anchoring at
    /// the bottom (the default) puts the last row on its own line and the slack above it,
    /// where it reads as ordinary empty chat; anchoring at the top leaves the grid
    /// floating with a visible gap beneath it, which grows as modules get smaller.
    bool anchorBottom = true;

    /// Draw two module rows per text line, taking styles from @ref packed rather than from
    /// @ref dark and @ref light.
    ///
    /// Halves the number of chat lines a code occupies, which is the resource this module
    /// actually runs out of. It buys nothing in bytes - both rows' run boundaries still
    /// have to be honoured, so the escape count barely moves - and the longer texture paths
    /// a packed style needs can leave the payload slightly larger than the unpacked one.
    ///
    /// Only sound while two modules still fit inside the chat font's fixed line advance.
    /// Past that the line grows to fit the taller escape and hands the halving straight
    /// back, so it wants rechecking against the real frame whenever moduleHeight changes.
    /// The shipped config turns it on; this default stays off so a bare geometry never
    /// packs without the four styles below having been filled in.
    bool packRows = false;

    /// Styles used when @ref packRows is set. Ignored otherwise.
    QrPackedStyles packed;

    /// Widest row the target frame can show without wrapping, in pixels. 0 = no limit.
    std::uint32_t maxRowWidthPx = 0;

    /// Largest string the backend will send. 0 = no limit.
    std::uint32_t maxPayloadBytes = 0;
};

enum class QrRenderError : std::uint8_t
{
    None = 0,
    RowTooWide,      ///< A row would wrap in the target frame, destroying the grid.
    PayloadTooLarge, ///< The assembled string is over maxPayloadBytes.
};

struct QrRenderResult
{
    QrRenderError error = QrRenderError::None;

    /// One text line per module row, joined by '\n'. Empty unless error is None.
    std::string text;

    std::uint32_t rowWidthPx = 0; ///< Width one row would occupy, reported on RowTooWide.
    std::size_t   byteCount  = 0; ///< Size of the assembled string, reported on PayloadTooLarge.
};

/// Renders an arbitrary module grid, row-major, @p width * @p height entries, true = dark.
/// No quiet zone is added - the caller owns padding.
QrRenderResult RenderModuleGrid(std::vector<bool> const& modules, std::uint32_t width, std::uint32_t height,
    QrRenderGeometry const& geometry);

/// Renders a QR symbol with @p geometry's quiet zone on all four sides.
///
/// The quiet zone is drawn as real light textures rather than left blank: a blank line
/// takes the frame's own background colour, which is dark in the chat frame and would
/// break decoding there.
QrRenderResult RenderQr(QrBitmap const& bitmap, QrRenderGeometry const& geometry);

#endif // MOD_QR_CODE_QR_RENDERER_H
