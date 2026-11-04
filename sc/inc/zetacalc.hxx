/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#pragma once

#include "scdllapi.h"
#include "types.hxx"

class ScDocument;

// ZetaOffice: Excel calculation mode "Automatic except for data tables"
// (xlsx calcMode="autoNoTable"). While it is on, automatic recalculation
// keeps the last result of data table cells (TABLE/MULTIPLE.OPERATIONS);
// F9, hard recalc and sheet calculation calculate them under CalcTablesGuard.
namespace sc::zeta
{
SC_DLLPUBLIC void SetNoTableAutoCalc(const ScDocument* pDoc, bool bSet);
SC_DLLPUBLIC bool IsNoTableAutoCalc(const ScDocument* pDoc);
SC_DLLPUBLIC bool IsCalcTables();

class SC_DLLPUBLIC CalcTablesGuard
{
public:
    CalcTablesGuard();
    ~CalcTablesGuard();
};

// Recalculate every data table cell of the document now.
SC_DLLPUBLIC void CalcTables(ScDocument& rDoc);

// Excel "Calculate Sheet" (Shift+F9): recalculate the formulas of one sheet,
// data tables included, even in manual mode. Other sheets are not recalculated.
SC_DLLPUBLIC void CalcSheet(ScDocument& rDoc, SCTAB nTab);
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
