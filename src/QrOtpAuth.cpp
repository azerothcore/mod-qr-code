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

#include "QrOtpAuth.h"

namespace
{
    /// Percent-encodes everything outside RFC 3986's unreserved set. The label is
    /// free-form - a realm name can carry spaces, apostrophes or non-ASCII bytes - and an
    /// unencoded one gives the app a URI it either truncates at the offending character or
    /// rejects outright.
    ///
    /// The character classes are spelled out rather than taken from <cctype>, whose
    /// classification follows the process locale.
    std::string PercentEncode(std::string const& text)
    {
        static constexpr char HEX_DIGITS[] = "0123456789ABCDEF";

        std::string encoded;
        encoded.reserve(text.size());

        for (char const character : text)
        {
            unsigned char const byte = static_cast<unsigned char>(character);
            bool const unreserved = (byte >= '0' && byte <= '9') || (byte >= 'A' && byte <= 'Z')
                || (byte >= 'a' && byte <= 'z') || byte == '-' || byte == '.' || byte == '_' || byte == '~';

            if (unreserved)
                encoded.push_back(character);
            else
            {
                encoded.push_back('%');
                encoded.push_back(HEX_DIGITS[byte >> 4]);
                encoded.push_back(HEX_DIGITS[byte & 0x0F]);
            }
        }

        return encoded;
    }
}

std::string BuildOtpAuthUri(std::string const& issuer, std::string const& account, std::string const& base32Secret)
{
    // The colon separating issuer from account stays literal: RFC 3986 allows it inside a
    // path segment, the Key Uri Format accepts either spelling, and %3A costs two bytes the
    // symbol may not have to spare.
    std::string label = PercentEncode(account);
    if (!issuer.empty())
        label = PercentEncode(issuer) + ':' + label;

    return "otpauth://totp/" + label + "?secret=" + base32Secret;
}
