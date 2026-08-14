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

#include "QrEncoder.h"
#include "vendor/qrcodegen.hpp"

#include <algorithm>
#include <cctype>
#include <exception>

namespace
{
    qrcodegen::QrCode::Ecc ToVendorEcc(QrEcc ecc)
    {
        switch (ecc)
        {
            case QrEcc::Medium:
                return qrcodegen::QrCode::Ecc::MEDIUM;
            case QrEcc::Quartile:
                return qrcodegen::QrCode::Ecc::QUARTILE;
            case QrEcc::High:
                return qrcodegen::QrCode::Ecc::HIGH;
            case QrEcc::Low:
            default:
                return qrcodegen::QrCode::Ecc::LOW;
        }
    }

    /// Attempts a byte-mode encode of @p length filler bytes. Used only to probe capacity.
    bool ByteModeFits(std::uint32_t length, std::uint32_t maxVersion, QrEcc ecc)
    {
        try
        {
            std::vector<std::uint8_t> const filler(length, std::uint8_t('A'));
            std::vector<qrcodegen::QrSegment> const segments{ qrcodegen::QrSegment::makeBytes(filler) };

            // boostEcl is off here: a capacity probe must answer for the requested level,
            // not for whatever stronger level happened to fit.
            qrcodegen::QrCode::encodeSegments(segments, ToVendorEcc(ecc), 1, int(maxVersion), -1, false);
            return true;
        }
        catch (std::exception const&)
        {
            return false;
        }
    }
}

std::optional<QrEcc> ParseQrEcc(std::string const& name)
{
    if (name.size() != 1)
        return std::nullopt;

    switch (std::toupper(static_cast<unsigned char>(name[0])))
    {
        case 'L':
            return QrEcc::Low;
        case 'M':
            return QrEcc::Medium;
        case 'Q':
            return QrEcc::Quartile;
        case 'H':
            return QrEcc::High;
        default:
            return std::nullopt;
    }
}

char const* QrEccName(QrEcc ecc)
{
    switch (ecc)
    {
        case QrEcc::Medium:
            return "M";
        case QrEcc::Quartile:
            return "Q";
        case QrEcc::High:
            return "H";
        case QrEcc::Low:
        default:
            return "L";
    }
}

std::optional<QrBitmap> EncodeQr(std::string const& payload, std::uint32_t maxVersion, QrEcc ecc)
{
    if (maxVersion < std::uint32_t(qrcodegen::QrCode::MIN_VERSION) ||
        maxVersion > std::uint32_t(qrcodegen::QrCode::MAX_VERSION))
        return std::nullopt;

    try
    {
        // makeSegments picks numeric/alphanumeric/byte per run, so an all-digit or
        // all-uppercase payload fits a smaller version than its byte length suggests.
        std::vector<qrcodegen::QrSegment> const segments = qrcodegen::QrSegment::makeSegments(payload.c_str());

        // boostEcl raises the correction level when it costs no extra version - free
        // resilience against the residual row seams this module renders with.
        qrcodegen::QrCode const code =
            qrcodegen::QrCode::encodeSegments(segments, ToVendorEcc(ecc), 1, int(maxVersion), -1, true);

        QrBitmap bitmap;
        bitmap.size = std::uint32_t(code.getSize());
        bitmap.modules.resize(std::size_t(bitmap.size) * bitmap.size);

        for (std::uint32_t y = 0; y < bitmap.size; ++y)
            for (std::uint32_t x = 0; x < bitmap.size; ++x)
                bitmap.modules[std::size_t(y) * bitmap.size + x] = code.getModule(int(x), int(y));

        return bitmap;
    }
    catch (std::exception const&)
    {
        return std::nullopt;
    }
}

std::uint32_t MaxQrPayloadBytes(std::uint32_t maxVersion, QrEcc ecc)
{
    if (maxVersion < std::uint32_t(qrcodegen::QrCode::MIN_VERSION) ||
        maxVersion > std::uint32_t(qrcodegen::QrCode::MAX_VERSION))
        return 0;

    // 2953 is the byte-mode ceiling of version 40 at ECC L, so it bounds every input.
    std::uint32_t low = 0;
    std::uint32_t high = 2954;

    while (low + 1 < high)
    {
        std::uint32_t const mid = low + (high - low) / 2;
        if (ByteModeFits(mid, maxVersion, ecc))
            low = mid;
        else
            high = mid;
    }

    return low;
}
