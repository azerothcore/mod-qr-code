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

#ifndef MOD_QR_CODE_QR_OTP_AUTH_H
#define MOD_QR_CODE_QR_OTP_AUTH_H

#include <string>

/// Builds the `otpauth://` URI an authenticator app expects for a TOTP account, following
/// the Key Uri Format (https://github.com/google/google-authenticator/wiki/Key-Uri-Format).
///
/// @p issuer and @p account become the displayed label; @p base32Secret is the shared
/// secret, already Base32-encoded.
///
/// Nothing else is emitted. AzerothCore's TOTP is 6 digits, SHA1, 30 second interval, which
/// is exactly the format's default set, and the issuer travels in the label prefix rather
/// than being repeated in an `issuer` query parameter. Both omissions are about length: the
/// URI has to stay inside a QR version the chat frame can actually draw, and every ~50 bytes
/// added costs a version, which costs four more rows of chat.
std::string BuildOtpAuthUri(std::string const& issuer, std::string const& account, std::string const& base32Secret);

#endif // MOD_QR_CODE_QR_OTP_AUTH_H
