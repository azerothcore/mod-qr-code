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

#ifndef MOD_QR_CODE_QR_ENCODER_H
#define MOD_QR_CODE_QR_ENCODER_H

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

/// QR error correction level, mirroring the four levels defined by the QR spec.
enum class QrEcc : std::uint8_t
{
    Low      = 0,
    Medium   = 1,
    Quartile = 2,
    High     = 3,
};

/// Parses a single-letter ECC name ("L", "M", "Q", "H", case-insensitive).
/// Returns std::nullopt for anything else so the caller can report a config error.
std::optional<QrEcc> ParseQrEcc(std::string const& name);

/// Short display name of an ECC level ("L", "M", "Q", "H").
char const* QrEccName(QrEcc ecc);

/// A rendered QR symbol as a square grid of light/dark modules, with no quiet zone.
/// This is the only shape the rest of the module sees; the vendored generator's
/// types never leave QrEncoder.cpp.
struct QrBitmap
{
    std::uint32_t     size = 0; ///< Modules per side, excluding the quiet zone.
    std::vector<bool> modules;  ///< Row-major, size*size entries, true = dark.

    bool At(std::uint32_t x, std::uint32_t y) const { return modules[y * size + x]; }
};

/// Encodes @p payload into the smallest QR version that fits within @p maxVersion.
/// Returns std::nullopt when the payload does not fit, or when maxVersion is out of
/// the 1..40 range accepted by the QR spec.
std::optional<QrBitmap> EncodeQr(std::string const& payload, std::uint32_t maxVersion, QrEcc ecc);

/// Largest byte-mode payload that still fits in @p maxVersion at @p ecc.
///
/// The encoder is the authority on capacity, so this is measured by probing it rather
/// than read from a table: any capacity claim made elsewhere could contradict the real
/// answer. Byte mode is the conservative case — a payload that happens to be all
/// digits or all uppercase encodes denser than this.
std::uint32_t MaxQrPayloadBytes(std::uint32_t maxVersion, QrEcc ecc);

#endif // MOD_QR_CODE_QR_ENCODER_H
