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

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
