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
#include "QrEncoder.h"

#include <string>

// These tests check that the vendored generator is wired up correctly - version selection,
// ECC plumbing, module extraction. They deliberately do not re-test the library itself.

/// A version 1 symbol is 21 modules per side; anything else means the version cap or the
/// module extraction is off.
TEST(QrEncoderTest, EncodesAShortPayloadAtVersionOne)
{
    std::optional<QrBitmap> const bitmap = EncodeQr("HELLO WORLD", 1, QrEcc::Quartile);

    ASSERT_TRUE(bitmap.has_value());
    EXPECT_EQ(std::uint32_t(21), bitmap->size);
    EXPECT_EQ(std::size_t(21 * 21), bitmap->modules.size());
}

/// The three finder patterns are fixed by the spec, so they are the cheapest proof that
/// the row-major copy out of the generator is not transposed or shifted.
TEST(QrEncoderTest, PlacesTheFinderPatterns)
{
    std::optional<QrBitmap> const bitmap = EncodeQr("HELLO WORLD", 1, QrEcc::Quartile);
    ASSERT_TRUE(bitmap.has_value());

    std::uint32_t const last = bitmap->size - 1;

    for (std::uint32_t corner = 0; corner < 3; ++corner)
    {
        std::uint32_t const originX = (corner == 1) ? last - 6 : 0;
        std::uint32_t const originY = (corner == 2) ? last - 6 : 0;

        // Outer 7x7 ring dark, inner 5x5 ring light, 3x3 core dark.
        EXPECT_TRUE(bitmap->At(originX, originY));
        EXPECT_TRUE(bitmap->At(originX + 6, originY + 6));
        EXPECT_FALSE(bitmap->At(originX + 1, originY + 1));
        EXPECT_FALSE(bitmap->At(originX + 5, originY + 5));
        EXPECT_TRUE(bitmap->At(originX + 3, originY + 3));

        // Separator: the light row and column between the finder and the data area.
        EXPECT_FALSE(bitmap->At(originX == 0 ? 7 : last - 7, originY == 0 ? 7 : last - 7));
    }
}

/// A payload that needs more room than the cap allows has to be refused, not silently
/// grown into a version the target frame cannot show.
TEST(QrEncoderTest, RefusesPayloadsOverTheVersionCap)
{
    EXPECT_TRUE(EncodeQr(std::string(17, 'a'), 1, QrEcc::Low).has_value());
    EXPECT_FALSE(EncodeQr(std::string(18, 'a'), 1, QrEcc::Low).has_value());

    // The same payload fits once the cap allows a larger version.
    std::optional<QrBitmap> const larger = EncodeQr(std::string(18, 'a'), 2, QrEcc::Low);
    ASSERT_TRUE(larger.has_value());
    EXPECT_EQ(std::uint32_t(25), larger->size);
}

TEST(QrEncoderTest, RefusesVersionsOutsideTheSpecRange)
{
    EXPECT_FALSE(EncodeQr("test", 0, QrEcc::Low).has_value());
    EXPECT_FALSE(EncodeQr("test", 41, QrEcc::Low).has_value());
    EXPECT_EQ(std::uint32_t(0), MaxQrPayloadBytes(0, QrEcc::Low));
    EXPECT_EQ(std::uint32_t(0), MaxQrPayloadBytes(41, QrEcc::Low));
}

/// The reported capacity has to match the published byte-mode table, because it is what
/// the "does not fit" message quotes back to the player.
TEST(QrEncoderTest, ReportsThePublishedByteModeCapacities)
{
    EXPECT_EQ(std::uint32_t(17), MaxQrPayloadBytes(1, QrEcc::Low));
    EXPECT_EQ(std::uint32_t(32), MaxQrPayloadBytes(2, QrEcc::Low));
    EXPECT_EQ(std::uint32_t(53), MaxQrPayloadBytes(3, QrEcc::Low));
    EXPECT_EQ(std::uint32_t(78), MaxQrPayloadBytes(4, QrEcc::Low));

    EXPECT_EQ(std::uint32_t(14), MaxQrPayloadBytes(1, QrEcc::Medium));
    EXPECT_EQ(std::uint32_t(26), MaxQrPayloadBytes(2, QrEcc::Medium));
    EXPECT_EQ(std::uint32_t(42), MaxQrPayloadBytes(3, QrEcc::Medium));

    EXPECT_EQ(std::uint32_t(11), MaxQrPayloadBytes(1, QrEcc::Quartile));
    EXPECT_EQ(std::uint32_t(7),  MaxQrPayloadBytes(1, QrEcc::High));
}

TEST(QrEncoderTest, ParsesErrorCorrectionNames)
{
    ASSERT_TRUE(ParseQrEcc("L").has_value());
    ASSERT_TRUE(ParseQrEcc("m").has_value());
    ASSERT_TRUE(ParseQrEcc("Q").has_value());
    ASSERT_TRUE(ParseQrEcc("h").has_value());

    EXPECT_EQ(QrEcc::Low,      *ParseQrEcc("L"));
    EXPECT_EQ(QrEcc::Medium,   *ParseQrEcc("m"));
    EXPECT_EQ(QrEcc::Quartile, *ParseQrEcc("Q"));
    EXPECT_EQ(QrEcc::High,     *ParseQrEcc("h"));

    EXPECT_FALSE(ParseQrEcc("").has_value());
    EXPECT_FALSE(ParseQrEcc("X").has_value());
    EXPECT_FALSE(ParseQrEcc("LOW").has_value());

    EXPECT_STREQ("L", QrEccName(QrEcc::Low));
    EXPECT_STREQ("H", QrEccName(QrEcc::High));
}
