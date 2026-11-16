/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#pragma once

#include <sal/types.h>

/// Word's "- 1 -, - 2 -, - 3 -, ..." number format (OOXML numberInDash, MS-DOC nfc 57).
/// css::style::NumberingType has no constant for it: this value is formatted by
/// i18npool's DefaultNumberingProvider (kept out of the IDL to avoid a full rebuild).
constexpr sal_Int16 SW_NUMBERING_NUMBER_IN_DASH = 76;

/// More Word number formats without a css::style::NumberingType constant, formatted the same way
/// (values as Word 2021 shows them in list numbers):
/// decimalEnclosedFullstop U+2488.. (1-20, then 21, 22, ...), MS-DOC nfc 26
constexpr sal_Int16 SW_NUMBERING_ENCLOSED_FULLSTOP = 77;
/// decimalEnclosedParen U+2474.. (1-20, then 21, 22, ...), nfc 27
constexpr sal_Int16 SW_NUMBERING_ENCLOSED_PAREN = 78;
/// hex: 1, ..., 9, A, ..., F, 10, ..., nfc 8
constexpr sal_Int16 SW_NUMBERING_HEX = 79;
/// decimalHalfWidth: 1, 2, 3 (kept apart from decimal for the round trip), nfc 15
constexpr sal_Int16 SW_NUMBERING_DECIMAL_HALF_WIDTH = 80;
/// thaiNumbers: Thai digits, nfc 54
constexpr sal_Int16 SW_NUMBERING_THAI_NUMBERS = 81;
/// taiwaneseDigital: ideographic digits, zero U+25CB, nfc 36
constexpr sal_Int16 SW_NUMBERING_TAIWANESE_DIGITAL = 82;
/// japaneseDigitalTenThousand: ideographic digits, zero U+3007, nfc 17
constexpr sal_Int16 SW_NUMBERING_JAPANESE_DIGITAL = 83;
/// ideographZodiacTraditional: the 60 stem-branch pairs, nfc 32
constexpr sal_Int16 SW_NUMBERING_ZODIAC_TRADITIONAL = 84;

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
