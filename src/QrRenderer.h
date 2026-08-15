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

#include <array>
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

/// Most module rows one text line may carry.
///
/// Every extra row doubles the number of distinct crops the client has to supply, and the
/// supply is the binding constraint long before the arithmetic is: two rows need 4, three
/// need 8, four need 16, and the stock textures already run thin at three.
constexpr std::uint32_t QR_MAX_ROWS_PER_LINE = 4;

/// Number of packed styles the widest supported line needs.
constexpr std::size_t QR_PACKED_STYLE_COUNT = std::size_t(1) << QR_MAX_ROWS_PER_LINE;

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

    /// Module rows drawn per text line, 1 to QR_MAX_ROWS_PER_LINE.
    ///
    /// 1 draws one row per line from @ref dark and @ref light. Above that each line is one
    /// escape per run, @ref rowsPerLine modules tall, styled from @ref packed - which cuts
    /// the line count by that factor, and the line count is the resource the chat frame
    /// actually runs out of.
    ///
    /// It buys far less in bytes than it looks: a run has to agree on every row it spans,
    /// so merging weakens as fast as the line count falls.
    ///
    /// The ceiling is the chat font's fixed line advance, around 16 px. While
    /// rowsPerLine * moduleHeight stays under it the line keeps its normal height and the
    /// saving is real; past it the line grows to fit the taller escape and hands the saving
    /// straight back. Two 7 px modules come to 14 px and fit, which is why 2 is free at the
    /// shipped module size; three need moduleHeight around 5 to stay inside.
    std::uint32_t rowsPerLine = 1;

    /// Styles for packed lines, indexed by the column's modules read top to bottom as bits,
    /// most significant first: with rowsPerLine 2, index 0b10 is dark over light. Only the
    /// first 2^rowsPerLine entries are read. Ignored when rowsPerLine is 1.
    std::array<QrModuleStyle, QR_PACKED_STYLE_COUNT> packed;

    /// Widest row the target frame can show without wrapping, in pixels. 0 = no limit.
    std::uint32_t maxRowWidthPx = 0;

    /// Largest string the backend will send. 0 = no limit.
    std::uint32_t maxPayloadBytes = 0;
};

/// Quiet zone a coloured code is given, in modules, unless the config already asks for more.
///
/// Above QR_QUIET_ZONE_DEFAULT_MODULES because a coloured code has no confidence to spare. Its
/// crop comes from artwork rather than from the measured black-and-white set, so its modules are
/// less uniform, and the chat frame it is drawn in has a dark background - which leaves the quiet
/// zone as the only thing separating the symbol from a dark surround. One module of it is thin
/// enough for a binariser to eat; two is not, and costs two lines.
constexpr std::uint32_t QR_QUIET_ZONE_COLOUR_MODULES = 2;

/// A recolouring, picked per command by `.qr color <name>`.
///
/// A palette colours one side of the code and leaves the other alone, and which side depends
/// entirely on how dark the colour measures. A decoder thresholds brightness, so the two sides
/// have to stay far apart in luminance; the colour goes on whichever side it can hold.
///
/// Dark enough to be a module - ruby at about 54 of 255, sapphire, emerald - and it replaces
/// @ref dark against the usual white. Too light to threshold against white - gold at about 190,
/// pale amethyst higher still - and no crop rescues it, so it goes in @ref light instead and the
/// modules stay black. Black on gold scans as readily as red on white and reads just as yellow.
struct QrPalette
{
    QrModuleStyle dark; ///< Style for dark modules.

    /// Style for light modules, including the quiet zone. Read only when @ref hasLight is set;
    /// otherwise the geometry's configured light is kept.
    QrModuleStyle light;

    /// Whether this palette colours the light side rather than the dark one.
    bool hasLight = false;

    /// Packed styles, indexed exactly as QrRenderGeometry::packed. Read only when
    /// @ref hasPacked is set.
    std::array<QrModuleStyle, QR_PACKED_STYLE_COUNT> packed;

    /// Whether @ref packed holds a complete set for the configured rows per line.
    ///
    /// Rarely true. A packed crop has to carry the colour and the white as stacked bands in
    /// one texture, and the black-and-white search that found the shipped set turned up two
    /// usable textures in the whole client - a coloured equivalent is unlikely to exist. A
    /// palette without one still draws; it just costs one line per module row.
    bool hasPacked = false;
};

/// Returns @p geometry recoloured by @p palette.
///
/// A palette with no packed set falls back to one module row per line, which is the only way
/// to draw a colour the packed crops do not carry. That multiplies the line count by the rows
/// per line it gave up, so a coloured code is markedly taller than the same payload in black.
QrRenderGeometry ApplyPalette(QrRenderGeometry geometry, QrPalette const& palette);

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
