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

// ZETA: Excel 2010 'show values as' types that css::sheet::DataPilotFieldReferenceType
// has no constant for (xlsx x14:dataField pivotShowAs). They travel in
// DataPilotFieldReference::ReferenceType like the API constants.
namespace ZetaDPRef
{
constexpr sal_Int32 PARENT_ROW_PERCENTAGE = 101; // percentOfParentRow
constexpr sal_Int32 PARENT_COLUMN_PERCENTAGE = 102; // percentOfParentCol
constexpr sal_Int32 PARENT_PERCENTAGE = 103; // percentOfParent (base field)
constexpr sal_Int32 RUNNING_TOTAL_PERCENTAGE = 104; // percentOfRunningTotal (base field)
constexpr sal_Int32 RANK_ASCENDING = 105; // rankAscending (base field)
constexpr sal_Int32 RANK_DESCENDING = 106; // rankDescending (base field)

inline bool isZeta(sal_Int32 n) { return n >= PARENT_ROW_PERCENTAGE && n <= RANK_DESCENDING; }
// types that need the base field's orientation
inline bool needsBaseField(sal_Int32 n) { return n >= PARENT_PERCENTAGE && n <= RANK_DESCENDING; }
inline bool isPercent(sal_Int32 n) { return n >= PARENT_ROW_PERCENTAGE && n <= RUNNING_TOTAL_PERCENTAGE; }
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
