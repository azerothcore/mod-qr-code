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
#include "QrOtpAuth.h"

#include <string>

// A malformed URI fails silently - the app shows an account that generates tokens the server
// will never accept - so the shape is worth pinning down here rather than by scanning.

TEST(QrOtpAuthTest, BuildsTheKeyUriShape)
{
    EXPECT_EQ("otpauth://totp/Azeroth:HELIAS?secret=JBSWY3DPEHPK3PXP",
        BuildOtpAuthUri("Azeroth", "HELIAS", "JBSWY3DPEHPK3PXP"));
}

/// Realm names are free text, and a raw space would end the URI at the first one.
TEST(QrOtpAuthTest, PercentEncodesTheLabel)
{
    EXPECT_EQ("otpauth://totp/My%20Realm%21:HELIAS?secret=AAAA",
        BuildOtpAuthUri("My Realm!", "HELIAS", "AAAA"));
}

/// Non-ASCII has to come out as UTF-8 bytes, uppercase-hex encoded, not mangled by a
/// locale-dependent character classification.
TEST(QrOtpAuthTest, PercentEncodesNonAsciiBytes)
{
    EXPECT_EQ("otpauth://totp/Ni%C3%B1o:HELIAS?secret=AAAA",
        BuildOtpAuthUri("Ni\xC3\xB1o", "HELIAS", "AAAA"));
}

/// Without an issuer the label is the account alone - no stray leading colon, which some
/// apps read as an empty issuer and display as a blank account name.
TEST(QrOtpAuthTest, OmitsTheIssuerPrefixWhenThereIsNoIssuer)
{
    EXPECT_EQ("otpauth://totp/HELIAS?secret=AAAA", BuildOtpAuthUri("", "HELIAS", "AAAA"));
}
