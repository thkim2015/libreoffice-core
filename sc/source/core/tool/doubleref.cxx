/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * This file incorporates work covered by the following license notice:
 *
 *   Licensed to the Apache Software Foundation (ASF) under one or more
 *   contributor license agreements. See the NOTICE file distributed
 *   with this work for additional information regarding copyright
 *   ownership. The ASF licenses this file to you under the Apache
 *   License, Version 2.0 (the "License"); you may not use this file
 *   except in compliance with the License. You may obtain a copy of
 *   the License at http://www.apache.org/licenses/LICENSE-2.0 .
 */

#include <doubleref.hxx>
#include <global.hxx>
#include <document.hxx>
#include <queryparam.hxx>
#include <queryentry.hxx>
#include <globstr.hrc>
#include <scresid.hxx>
#include <scmatrix.hxx>

#include <svl/sharedstringpool.hxx>
#include <osl/diagnose.h>
#include <unotools/charclass.hxx>
#include <unotools/transliterationwrapper.hxx>

#include <formulacell.hxx>
#include <cellvalue.hxx>
#include <memory>
#include <utility>
#include <vector>

namespace {

void lcl_uppercase(OUString& rStr)
{
    rStr = ScGlobal::getCharClass().uppercase(rStr.trim());
}

bool lcl_createStarQuery(
    const ScDocument* pDoc,
    svl::SharedStringPool& rPool, ScQueryParamBase* pParam, const ScDBRangeBase* pDBRef, const ScDBRangeBase* pQueryRef)
{
    // A valid StarQuery must be at least 4 columns wide. To be precise it
    // should be exactly 4 columns ...
    // Additionally, if this wasn't checked, a formula pointing to a valid 1-3
    // column Excel style query range immediately left to itself would result
    // in a circular reference when the field name or operator or value (first
    // to third query range column) is obtained (#i58354#). Furthermore, if the
    // range wasn't sufficiently specified data changes wouldn't flag formula
    // cells for recalculation.

    if (pQueryRef->getColSize() < 4)
        return false;

    bool bValid;
    OUString aCellStr;
    SCSIZE nIndex = 0;
    SCROW nRow = 0;
    SCROW nRows = pDBRef->getRowSize();
    SCSIZE nNewEntries = static_cast<SCSIZE>(nRows);
    pParam->Resize(nNewEntries);

    do
    {
        ScQueryEntry& rEntry = pParam->GetEntry(nIndex);

        bValid = false;

        if (nIndex > 0)
        {
            // For all entries after the first one, check the and/or connector in the first column.
            aCellStr = pQueryRef->getString(0, nRow);
            lcl_uppercase(aCellStr);
            if ( aCellStr == ScResId(STR_TABLE_AND) )
            {
                rEntry.eConnect = SC_AND;
                bValid = true;
            }
            else if ( aCellStr == ScResId(STR_TABLE_OR) )
            {
                rEntry.eConnect = SC_OR;
                bValid = true;
            }
        }

        if ((nIndex < 1) || bValid)
        {
            // field name in the 2nd column.
            aCellStr = pQueryRef->getString(1, nRow);
            SCCOL nField = pDBRef->findFieldColumn(aCellStr); // TODO: must be case insensitive comparison.
            if (pDoc->ValidCol(nField))
            {
                rEntry.nField = nField;
                bValid = true;
            }
            else
                bValid = false;
        }

        if (bValid)
        {
            // equality, non-equality operator in the 3rd column.
            aCellStr = pQueryRef->getString(2, nRow);
            lcl_uppercase(aCellStr);
            const sal_Unicode* p = aCellStr.getStr();
            if (p[0] == '<')
            {
                if (p[1] == '>')
                    rEntry.eOp = SC_NOT_EQUAL;
                else if (p[1] == '=')
                    rEntry.eOp = SC_LESS_EQUAL;
                else
                    rEntry.eOp = SC_LESS;
            }
            else if (p[0] == '>')
            {
                if (p[1] == '=')
                    rEntry.eOp = SC_GREATER_EQUAL;
                else
                    rEntry.eOp = SC_GREATER;
            }
            else if (p[0] == '=')
                rEntry.eOp = SC_EQUAL;

        }

        if (bValid)
        {
            // Finally, the right-hand-side value in the 4th column.
            rEntry.GetQueryItem().maString =
                rPool.intern(pQueryRef->getString(3, nRow));
            rEntry.bDoQuery = true;
        }
        nIndex++;
        nRow++;
    }
    while (bValid && (nRow < nRows) /* && (nIndex < MAXQUERY) */ );
    return bValid;
}

// weboffice: Excel computed criterion (criteria column whose header is not a field). The cell
// is a formula written for the first record (relative references moved down per record) or a
// constant; a record matches when it gives TRUE or a non-zero number.
std::shared_ptr<const std::vector<bool>> lcl_zetaComputedCriterion(
    ScDocument& rDoc, const ScAddress& rCrit, SCROW nFirst, SCROW nLast)
{
    auto pMask = std::make_shared<std::vector<bool>>();
    if (nLast < nFirst)
        return pMask;
    pMask->resize(nLast - nFirst + 1, false);
    const ScRefCellValue aCell = rDoc.GetRefCellValue(rCrit);
    if (aCell.getType() == CELLTYPE_FORMULA)
    {
        const ScFormulaCell& rSrc = *aCell.getFormula();
        for (SCROW k = 0; k <= nLast - nFirst; ++k)
        {
            if (rCrit.Row() + k > rDoc.MaxRow())
                break;
            ScFormulaCell aClone(rSrc, rDoc, ScAddress(rCrit.Col(), rCrit.Row() + k, rCrit.Tab()));
            aClone.SetDirtyVar();
            aClone.Interpret();
            (*pMask)[k] = aClone.GetRawError() == FormulaError::NONE && aClone.IsValue()
                          && aClone.GetRawValue() != 0.0;
        }
    }
    else
    {
        const bool bOn = aCell.getType() == CELLTYPE_VALUE && aCell.getDouble() != 0.0;
        std::fill(pMask->begin(), pMask->end(), bOn);
    }
    return pMask;
}

bool lcl_createExcelQuery(
    const ScDocument* pDoc,
    svl::SharedStringPool& rPool, ScQueryParamBase* pParam, const ScDBRangeBase* pDBRef, const ScDBRangeBase* pQueryRef)
{
    bool bValid = true;
    SCCOL nCols = pQueryRef->getColSize();
    SCROW nRows = pQueryRef->getRowSize();
    std::vector<SCCOL> aFields(nCols);
    // weboffice: computed criteria need both ranges on the sheet
    const ScDBInternalRange* pDBInt = dynamic_cast<const ScDBInternalRange*>(pDBRef);
    const ScDBInternalRange* pQueryInt = dynamic_cast<const ScDBInternalRange*>(pQueryRef);
    SCCOL nCol = 0;
    while (bValid && (nCol < nCols))
    {
        OUString aQueryStr = pQueryRef->getString(nCol, 0);
        SCCOL nField = pDBRef->findFieldColumn(aQueryStr);
        if (pDoc->ValidCol(nField))
            aFields[nCol] = nField;
        else if (pDBInt && pQueryInt)
            aFields[nCol] = -1; // weboffice: Excel computed criterion
        else
            bValid = false;
        ++nCol;
    }

    if (bValid)
    {
        // Count the number of visible cells (excluding the header row).  Each
        // visible cell corresponds with a single query.
        SCSIZE nVisible = pQueryRef->getVisibleDataCellCount();
        if ( nVisible > SCSIZE_MAX / sizeof(void*) )
        {
            OSL_FAIL("too many filter criteria");
            nVisible = 0;
        }

        SCSIZE nNewEntries = nVisible;
        pParam->Resize( nNewEntries );

        SCSIZE nIndex = 0;
        SCROW nRow = 1;
        OUString aCellStr;
        while (nRow < nRows)
        {
            nCol = 0;
            while (nCol < nCols)
            {
                aCellStr = pQueryRef->getString(nCol, nRow);
                aCellStr = ScGlobal::getCharClass().uppercase( aCellStr );
                // weboffice: a computed criterion counts when the cell has anything (an error
                // formula has an empty input string and matches no record)
                const bool bComputed = aFields[nCol] < 0
                    && pDoc->GetCellType(ScAddress(pQueryInt->getRange().aStart.Col() + nCol,
                                                   pQueryInt->getRange().aStart.Row() + nRow,
                                                   pQueryInt->getRange().aStart.Tab()))
                           != CELLTYPE_NONE;
                if (bComputed)
                {
                    if (nIndex < nNewEntries)
                    {
                        const ScRange& rDB = pDBInt->getRange();
                        const ScAddress& rQ = pQueryInt->getRange().aStart;
                        ScQueryEntry& rEntry = pParam->GetEntry(nIndex);
                        rEntry.bDoQuery = true;
                        rEntry.nField = rDB.aStart.Col();
                        rEntry.eOp = SC_EQUAL;
                        rEntry.mnZetaMaskRow = rDB.aStart.Row() + 1;
                        rEntry.mpZetaRowMask = lcl_zetaComputedCriterion(
                            *const_cast<ScDocument*>(pDoc),
                            ScAddress(rQ.Col() + nCol, rQ.Row() + nRow, rQ.Tab()),
                            rDB.aStart.Row() + 1, rDB.aEnd.Row());
                        nIndex++;
                        if (nIndex < nNewEntries)
                            pParam->GetEntry(nIndex).eConnect = SC_AND;
                    }
                    else
                        bValid = false;
                }
                else if (!aCellStr.isEmpty() && aFields[nCol] >= 0)
                {
                    if (nIndex < nNewEntries)
                    {
                        pParam->GetEntry(nIndex).nField = aFields[nCol];
                        pParam->FillInExcelSyntax(rPool, aCellStr, nIndex, nullptr);
                        nIndex++;
                        if (nIndex < nNewEntries)
                            pParam->GetEntry(nIndex).eConnect = SC_AND;
                    }
                    else
                        bValid = false;
                }
                nCol++;
            }
            nRow++;
            if (nIndex < nNewEntries)
                pParam->GetEntry(nIndex).eConnect = SC_OR;
        }
    }
    return bValid;
}

bool lcl_fillQueryEntries(
    const ScDocument* pDoc,
    svl::SharedStringPool& rPool, ScQueryParamBase* pParam, const ScDBRangeBase* pDBRef, const ScDBRangeBase* pQueryRef)
{
    SCSIZE nCount = pParam->GetEntryCount();
    for (SCSIZE i = 0; i < nCount; ++i)
        pParam->GetEntry(i).Clear();

    // Standard QueryTabelle
    bool bValid = lcl_createStarQuery(pDoc, rPool, pParam, pDBRef, pQueryRef);
    // Excel QueryTabelle
    if (!bValid)
        bValid = lcl_createExcelQuery(pDoc, rPool, pParam, pDBRef, pQueryRef);

    nCount = pParam->GetEntryCount();
    if (bValid)
    {
        //  bQueryByString must be set
        for (SCSIZE i = 0; i < nCount; ++i)
            pParam->GetEntry(i).GetQueryItem().meType = ScQueryEntry::ByString;
    }
    else
    {
        //  nothing
        for (SCSIZE i = 0; i < nCount; ++i)
            pParam->GetEntry(i).Clear();
    }
    return bValid;
}

}

ScDBRangeBase::ScDBRangeBase(ScDocument* pDoc) :
    mpDoc(pDoc)
{
}

ScDBRangeBase::~ScDBRangeBase()
{
}

bool ScDBRangeBase::fillQueryEntries(ScQueryParamBase* pParam, const ScDBRangeBase* pDBRef) const
{
    if (!pDBRef)
        return false;

    return lcl_fillQueryEntries(getDoc(), getDoc()->GetSharedStringPool(), pParam, pDBRef, this);
}

void ScDBRangeBase::fillQueryOptions(ScQueryParamBase* pParam)
{
    pParam->bHasHeader = true;
    pParam->bByRow = true;
    pParam->bInplace = true;
    pParam->bCaseSens = false;
    pParam->eSearchType = utl::SearchParam::SearchType::Normal;
    pParam->bDuplicate = true;
}

ScDBInternalRange::ScDBInternalRange(ScDocument* pDoc, const ScRange& rRange) :
    ScDBRangeBase(pDoc), maRange(rRange)
{
}

ScDBInternalRange::~ScDBInternalRange()
{
}

SCCOL ScDBInternalRange::getColSize() const
{
    return maRange.aEnd.Col() - maRange.aStart.Col() + 1;
}

SCROW ScDBInternalRange::getRowSize() const
{
    return maRange.aEnd.Row() - maRange.aStart.Row() + 1;
}

SCSIZE ScDBInternalRange::getVisibleDataCellCount() const
{
    SCCOL nCols = getColSize();
    SCROW nRows = getRowSize();
    if (nRows <= 1)
        return 0;

    return (nRows-1)*nCols;
}

OUString ScDBInternalRange::getString(SCCOL nCol, SCROW nRow) const
{
    const ScAddress& s = maRange.aStart;
    // #i109200# this is used in formula calculation, use GetInputString, not GetString
    // (consistent with ScDBInternalRange::getCellString)
    // GetStringForFormula is not used here, to allow querying for date values.
    return getDoc()->GetInputString(s.Col() + nCol, s.Row() + nRow, maRange.aStart.Tab());
}

SCCOL ScDBInternalRange::getFirstFieldColumn() const
{
    return getRange().aStart.Col();
}

SCCOL ScDBInternalRange::findFieldColumn(SCCOL nIndex) const
{
    const ScRange& rRange = getRange();
    const ScAddress& s = rRange.aStart;

    SCCOL nDBCol1 = s.Col();

    // Don't handle out-of-bound condition here.  We'll do that later.
    return nIndex + nDBCol1 - 1;
}

SCCOL ScDBInternalRange::findFieldColumn(const OUString& rStr, FormulaError* pErr) const
{
    const ScAddress& s = maRange.aStart;
    const ScAddress& e = maRange.aEnd;
    OUString aUpper = rStr;
    lcl_uppercase(aUpper);

    SCCOL nDBCol1 = s.Col();
    SCROW nDBRow1 = s.Row();
    SCTAB nDBTab1 = s.Tab();
    SCCOL nDBCol2 = e.Col();

    bool bFound = false;

    OUString aCellStr;
    ScAddress aLook( nDBCol1, nDBRow1, nDBTab1 );
    while (!bFound && (aLook.Col() <= nDBCol2))
    {
        FormulaError nErr = getDoc()->GetStringForFormula( aLook, aCellStr );
        if (pErr)
            *pErr = nErr;
        lcl_uppercase(aCellStr);
        bFound = ScGlobal::GetTransliteration().isEqual(aCellStr, aUpper);
        if (!bFound)
            aLook.IncCol();
    }
    SCCOL nField = aLook.Col();

    return bFound ? nField : -1;
}

std::unique_ptr<ScDBQueryParamBase> ScDBInternalRange::createQueryParam(const ScDBRangeBase* pQueryRef) const
{
    std::unique_ptr<ScDBQueryParamInternal> pParam(new ScDBQueryParamInternal);

    // Set the database range first.
    const ScAddress& s = maRange.aStart;
    const ScAddress& e = maRange.aEnd;
    pParam->nCol1 = s.Col();
    pParam->nRow1 = s.Row();
    pParam->nCol2 = e.Col();
    pParam->nRow2 = e.Row();
    pParam->nTab  = s.Tab();

    fillQueryOptions(pParam.get());

    // Now construct the query entries from the query range.
    if (!pQueryRef->fillQueryEntries(pParam.get(), this))
        return nullptr;

    return std::unique_ptr<ScDBQueryParamBase>(std::move(pParam));
}

bool ScDBInternalRange::isRangeEqual(const ScRange& rRange) const
{
    return maRange == rRange;
}

ScDBExternalRange::ScDBExternalRange(ScDocument* pDoc, ScMatrixRef pMat) :
    ScDBRangeBase(pDoc), mpMatrix(std::move(pMat))
{
    SCSIZE nC, nR;
    mpMatrix->GetDimensions(nC, nR);
    mnCols = static_cast<SCCOL>(nC);
    mnRows = static_cast<SCROW>(nR);
}

ScDBExternalRange::~ScDBExternalRange()
{
}

SCCOL ScDBExternalRange::getColSize() const
{
    return mnCols;
}

SCROW ScDBExternalRange::getRowSize() const
{
    return mnRows;
}

SCSIZE ScDBExternalRange::getVisibleDataCellCount() const
{
    SCCOL nCols = getColSize();
    SCROW nRows = getRowSize();
    if (nRows <= 1)
        return 0;

    return (nRows-1)*nCols;
}

OUString ScDBExternalRange::getString(SCCOL nCol, SCROW nRow) const
{
    if (nCol >= mnCols || nRow >= mnRows)
        return OUString();

    return mpMatrix->GetString(nCol, nRow).getString();
}

SCCOL ScDBExternalRange::getFirstFieldColumn() const
{
    return 0;
}

SCCOL ScDBExternalRange::findFieldColumn(SCCOL nIndex) const
{
    return nIndex - 1;
}

SCCOL ScDBExternalRange::findFieldColumn(const OUString& rStr, FormulaError* pErr) const
{
    if (pErr)
        *pErr = FormulaError::NONE;

    OUString aUpper = rStr;
    lcl_uppercase(aUpper);
    for (SCCOL i = 0; i < mnCols; ++i)
    {
        OUString aUpperVal = mpMatrix->GetString(i, 0).getString();
        lcl_uppercase(aUpperVal);
        if (aUpper == aUpperVal)
            return i;
    }
    return -1;
}

std::unique_ptr<ScDBQueryParamBase> ScDBExternalRange::createQueryParam(const ScDBRangeBase* pQueryRef) const
{
    std::unique_ptr<ScDBQueryParamMatrix> pParam(new ScDBQueryParamMatrix);
    pParam->mpMatrix = mpMatrix;
    fillQueryOptions(pParam.get());

    // Now construct the query entries from the query range.
    if (!pQueryRef->fillQueryEntries(pParam.get(), this))
        return nullptr;

    return std::unique_ptr<ScDBQueryParamBase>(std::move(pParam));
}

bool ScDBExternalRange::isRangeEqual(const ScRange& /*rRange*/) const
{
    return false;
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
