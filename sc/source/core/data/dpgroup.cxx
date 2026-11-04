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

#include <dpgroup.hxx>

#include <dpcache.hxx>
#include <document.hxx>
#include <dpfilteredcache.hxx>
#include <dputil.hxx>
#include <dptabres.hxx>
#include <tokenarray.hxx>

#include <o3tl/safeint.hxx>
#include <osl/diagnose.h>
#include <rtl/math.hxx>
#include <rtl/ustrbuf.hxx>
#include <svl/numformat.hxx>

#include <com/sun/star/sheet/DataPilotFieldGroupBy.hpp>

#include <cmath>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
#include <algorithm>

using namespace ::com::sun::star;
using ::com::sun::star::uno::Any;
using ::com::sun::star::uno::Sequence;

const sal_uInt16 SC_DP_LEAPYEAR = 1648;     // arbitrary leap year for date calculations

namespace {

class ScDPGroupNumFilter : public ScDPFilteredCache::FilterBase
{
public:
    ScDPGroupNumFilter(std::vector<ScDPItemData>&& rValues, const ScDPNumGroupInfo& rInfo);

    virtual bool match(const ScDPItemData &rCellData) const override;
    virtual std::vector<ScDPItemData> getMatchValues() const override;
private:
    std::vector<ScDPItemData> maValues;
    ScDPNumGroupInfo maNumInfo;
};

}

ScDPGroupNumFilter::ScDPGroupNumFilter( std::vector<ScDPItemData>&& rValues, const ScDPNumGroupInfo& rInfo) :
    maValues(std::move(rValues)), maNumInfo(rInfo) {}

bool ScDPGroupNumFilter::match(const ScDPItemData& rCellData) const
{
    if (rCellData.GetType() != ScDPItemData::Value)
        return false;

    for (const auto& rValue : maValues)
    {
        double fVal = rValue.GetValue();
        if (std::isinf(fVal))
        {
            if (std::signbit(fVal))
            {
                // Less than the min value.
                if (rCellData.GetValue() < maNumInfo.mfStart)
                    return true;
            }

            // Greater than the max value.
            if (maNumInfo.mfEnd < rCellData.GetValue())
                return true;

            continue;
        }

        double low = fVal;
        double high = low + maNumInfo.mfStep;
        if (maNumInfo.mbIntegerOnly)
            high += 1.0;

        if (low <= rCellData.GetValue() && rCellData.GetValue() < high)
            return true;
    }

    return false;
}

std::vector<ScDPItemData> ScDPGroupNumFilter::getMatchValues() const
{
    return std::vector<ScDPItemData>();
}

namespace {

class ScDPGroupDateFilter : public ScDPFilteredCache::FilterBase
{
public:
    ScDPGroupDateFilter(
        std::vector<ScDPItemData>&& rValues, const Date& rNullDate, const ScDPNumGroupInfo& rNumInfo);

    virtual bool match(const ScDPItemData & rCellData) const override;
    virtual std::vector<ScDPItemData> getMatchValues() const override;

private:
    std::vector<ScDPItemData> maValues;
    Date             maNullDate;
    ScDPNumGroupInfo maNumInfo;
};

}

ScDPGroupDateFilter::ScDPGroupDateFilter(
    std::vector<ScDPItemData>&& rValues, const Date& rNullDate, const ScDPNumGroupInfo& rNumInfo) :
    maValues(std::move(rValues)),
    maNullDate(rNullDate),
    maNumInfo(rNumInfo)
{
}

bool ScDPGroupDateFilter::match( const ScDPItemData & rCellData ) const
{
    using namespace ::com::sun::star::sheet;
    using ::rtl::math::approxFloor;
    using ::rtl::math::approxEqual;

    if ( !rCellData.IsValue() )
        return false;

    for (const ScDPItemData& rValue : maValues)
    {
        if (rValue.GetType() != ScDPItemData::GroupValue)
            continue;

        sal_Int32 nGroupType = rValue.GetGroupValue().mnGroupType;
        sal_Int32 nValue = rValue.GetGroupValue().mnValue;

        // Start and end dates are inclusive.  (An end date without a time value
        // is included, while an end date with a time value is not.)

        if (rCellData.GetValue() < maNumInfo.mfStart && !approxEqual(rCellData.GetValue(), maNumInfo.mfStart))
        {
            if (nValue == ScDPItemData::DateFirst)
                return true;
            continue;
        }

        if (rCellData.GetValue() > maNumInfo.mfEnd && !approxEqual(rCellData.GetValue(), maNumInfo.mfEnd))
        {
            if (nValue == ScDPItemData::DateLast)
                return true;
            continue;
        }

        if (nGroupType == DataPilotFieldGroupBy::HOURS || nGroupType == DataPilotFieldGroupBy::MINUTES ||
            nGroupType == DataPilotFieldGroupBy::SECONDS)
        {
            // handle time
            // (do as in the cell functions, ScInterpreter::ScGetHour() etc.)

            sal_uInt16 nHour, nMinute, nSecond;
            double fFractionOfSecond;
            tools::Time::GetClock( rCellData.GetValue(), nHour, nMinute, nSecond, fFractionOfSecond, 0);

            switch (nGroupType)
            {
                case DataPilotFieldGroupBy::HOURS:
                {
                    if (nHour == nValue)
                        return true;
                }
                break;
                case DataPilotFieldGroupBy::MINUTES:
                {
                    if (nMinute == nValue)
                        return true;
                }
                break;
                case DataPilotFieldGroupBy::SECONDS:
                {
                    if (nSecond == nValue)
                        return true;
                }
                break;
                default:
                    OSL_FAIL("invalid time part");
            }

            continue;
        }

        Date date = maNullDate + static_cast<sal_Int32>(approxFloor(rCellData.GetValue()));
        switch (nGroupType)
        {
            case DataPilotFieldGroupBy::YEARS:
            {
                sal_Int32 year = static_cast<sal_Int32>(date.GetYear());
                if (year == nValue)
                    return true;
            }
            break;
            case DataPilotFieldGroupBy::QUARTERS:
            {
                sal_Int32 qtr =  1 + (static_cast<sal_Int32>(date.GetMonth()) - 1) / 3;
                if (qtr == nValue)
                    return true;
            }
            break;
            case DataPilotFieldGroupBy::MONTHS:
            {
                sal_Int32 month = static_cast<sal_Int32>(date.GetMonth());
                if (month == nValue)
                    return true;
            }
            break;
            case DataPilotFieldGroupBy::DAYS:
            {
                Date yearStart(1, 1, date.GetYear());
                sal_Int32 days = (date - yearStart) + 1;       // Jan 01 has value 1
                if (days >= 60 && !date.IsLeapYear())
                {
                    // This is not a leap year.  Adjust the value accordingly.
                    ++days;
                }
                if (days == nValue)
                    return true;
            }
            break;
            default:
                OSL_FAIL("invalid date part");
        }
    }

    return false;
}

std::vector<ScDPItemData> ScDPGroupDateFilter::getMatchValues() const
{
    return std::vector<ScDPItemData>();
}

namespace {

bool isDateInGroup(const ScDPItemData& rGroupItem, const ScDPItemData& rChildItem)
{
    if (rGroupItem.GetType() != ScDPItemData::GroupValue || rChildItem.GetType() != ScDPItemData::GroupValue)
        return false;

    sal_Int32 nGroupPart = rGroupItem.GetGroupValue().mnGroupType;
    sal_Int32 nGroupValue = rGroupItem.GetGroupValue().mnValue;
    sal_Int32 nChildPart = rChildItem.GetGroupValue().mnGroupType;
    sal_Int32 nChildValue = rChildItem.GetGroupValue().mnValue;

    if (nGroupValue == ScDPItemData::DateFirst || nGroupValue == ScDPItemData::DateLast ||
        nChildValue == ScDPItemData::DateFirst || nChildValue == ScDPItemData::DateLast)
    {
        // first/last entry matches only itself
        return nGroupValue == nChildValue;
    }

    switch (nChildPart)        // inner part
    {
        case css::sheet::DataPilotFieldGroupBy::MONTHS:
            // a month is only contained in its quarter
            if (nGroupPart == css::sheet::DataPilotFieldGroupBy::QUARTERS)
                // months and quarters are both 1-based
                return (nGroupValue - 1 == (nChildValue - 1) / 3);
            break;
        case css::sheet::DataPilotFieldGroupBy::DAYS:
            // a day is only contained in its quarter or month
            if (nGroupPart == css::sheet::DataPilotFieldGroupBy::MONTHS ||
                nGroupPart == css::sheet::DataPilotFieldGroupBy::QUARTERS)
            {
                Date aDate(1, 1, SC_DP_LEAPYEAR);
                aDate.AddDays(nChildValue - 1);            // days are 1-based
                sal_Int32 nCompare = aDate.GetMonth();
                if (nGroupPart == css::sheet::DataPilotFieldGroupBy::QUARTERS)
                    nCompare = ( ( nCompare - 1 ) / 3 ) + 1;    // get quarter from date

                return nGroupValue == nCompare;
            }
            break;
        default:
            ;
    }

    return true;
}

}

ScDPGroupItem::ScDPGroupItem( const ScDPItemData& rName ) :
    aGroupName( rName )
{
}

void ScDPGroupItem::AddElement( const ScDPItemData& rName )
{
    aElements.push_back( rName );
}

bool ScDPGroupItem::HasElement( const ScDPItemData& rData ) const
{
    return std::any_of(aElements.begin(), aElements.end(),
        [&rData](const ScDPItemData& rElement) { return rElement.IsCaseInsEqual(rData); });
}

bool ScDPGroupItem::HasCommonElement( const ScDPGroupItem& rOther ) const
{
    return std::any_of(aElements.begin(), aElements.end(),
        [&rOther](const ScDPItemData& rElement) { return rOther.HasElement(rElement); });
}

void ScDPGroupItem::FillGroupFilter( ScDPFilteredCache::GroupFilter& rFilter ) const
{
    for (const auto& rElement : aElements)
        rFilter.addMatchItem(rElement);
}

ScDPGroupDimension::ScDPGroupDimension( tools::Long nSource, OUString aNewName ) :
    nSourceDim( nSource ),
    nGroupDim( -1 ),
    aGroupName(std::move( aNewName )),
    mbDateDimension(false)
{
}

ScDPGroupDimension::~ScDPGroupDimension()
{
    maMemberEntries.clear();
}

ScDPGroupDimension::ScDPGroupDimension( const ScDPGroupDimension& rOther ) :
    nSourceDim( rOther.nSourceDim ),
    nGroupDim( rOther.nGroupDim ),
    aGroupName( rOther.aGroupName ),
    aItems( rOther.aItems ),
    mbDateDimension(rOther.mbDateDimension)
{
}

ScDPGroupDimension& ScDPGroupDimension::operator=( const ScDPGroupDimension& rOther )
{
    nSourceDim = rOther.nSourceDim;
    nGroupDim  = rOther.nGroupDim;
    aGroupName = rOther.aGroupName;
    aItems     = rOther.aItems;
    mbDateDimension = rOther.mbDateDimension;
    return *this;
}

void ScDPGroupDimension::AddItem( const ScDPGroupItem& rItem )
{
    aItems.push_back( rItem );
}

void ScDPGroupDimension::SetGroupDim( tools::Long nDim )
{
    nGroupDim = nDim;
}

const std::vector<SCROW>& ScDPGroupDimension::GetColumnEntries(
    const ScDPFilteredCache& rCacheTable) const
{
    if (!maMemberEntries.empty())
        return maMemberEntries;

    rCacheTable.getCache().GetGroupDimMemberIds(nGroupDim, maMemberEntries);
    return maMemberEntries;
}

const ScDPGroupItem* ScDPGroupDimension::GetGroupForData( const ScDPItemData& rData ) const
{
    auto aIter = std::find_if(aItems.begin(), aItems.end(),
        [&rData](const ScDPGroupItem& rItem) { return rItem.HasElement(rData); });
    if (aIter != aItems.end())
        return &*aIter;

    return nullptr;
}

const ScDPGroupItem* ScDPGroupDimension::GetGroupForName( const ScDPItemData& rName ) const
{
    auto aIter = std::find_if(aItems.begin(), aItems.end(),
        [&rName](const ScDPGroupItem& rItem) { return rItem.GetName().IsCaseInsEqual(rName); });
    if (aIter != aItems.end())
        return &*aIter;

    return nullptr;
}

const ScDPGroupItem* ScDPGroupDimension::GetGroupByIndex( size_t nIndex ) const
{
    if (nIndex >= aItems.size())
        return nullptr;

    return &aItems[nIndex];
}

void ScDPGroupDimension::DisposeData()
{
    maMemberEntries.clear();
}

void ScDPGroupDimension::SetDateDimension()
{
    mbDateDimension = true;
}

ScDPNumGroupDimension::ScDPNumGroupDimension() : mbDateDimension(false) {}

ScDPNumGroupDimension::ScDPNumGroupDimension( const ScDPNumGroupInfo& rInfo ) :
    aGroupInfo(rInfo), mbDateDimension(false) {}

ScDPNumGroupDimension::ScDPNumGroupDimension( const ScDPNumGroupDimension& rOther ) :
    aGroupInfo(rOther.aGroupInfo), mbDateDimension(rOther.mbDateDimension) {}

ScDPNumGroupDimension& ScDPNumGroupDimension::operator=( const ScDPNumGroupDimension& rOther )
{
    aGroupInfo = rOther.aGroupInfo;
    mbDateDimension = rOther.mbDateDimension;
    return *this;
}

void ScDPNumGroupDimension::DisposeData()
{
    aGroupInfo = ScDPNumGroupInfo();
    maMemberEntries.clear();
}

ScDPNumGroupDimension::~ScDPNumGroupDimension()
{
}

void ScDPNumGroupDimension::SetDateDimension()
{
    aGroupInfo.mbEnable = true;   //TODO: or query both?
    mbDateDimension = true;
}

const std::vector<SCROW>& ScDPNumGroupDimension::GetNumEntries(
    SCCOL nSourceDim, const ScDPCache* pCache) const
{
    if (!maMemberEntries.empty())
        return maMemberEntries;

    pCache->GetGroupDimMemberIds(nSourceDim, maMemberEntries);
    return maMemberEntries;
}

ScDPGroupTableData::ScDPGroupTableData( const std::shared_ptr<ScDPTableData>& pSource, ScDocument* pDocument ) :
    ScDPTableData(pDocument),
    pSourceData( pSource ),
    pDoc( pDocument )
{
    OSL_ENSURE( pSource, "ScDPGroupTableData: pSource can't be NULL" );

    CreateCacheTable();
    // nSourceCount is the raw source column count (excluding calculated fields).
    // Group dimensions are placed right after source columns at index nSourceCount+i,
    // which matches the cache's group field index space (mnColumnCount+i).
    // Calculated fields come after group dimensions in this proxy's index space.
    nSourceCount = pSource->GetColumnCount() - pSource->GetCalculatedColumnCount();
    pNumGroups.reset( new ScDPNumGroupDimension[nSourceCount] );
}

ScDPGroupTableData::~ScDPGroupTableData()
{
}

void ScDPGroupTableData::AddGroupDimension( const ScDPGroupDimension& rGroup )
{
    ScDPGroupDimension aNewGroup( rGroup );
    // Group dimensions are placed right after source columns: [source 0..N-1] [groups N..N+G-1]
    // This ensures nGroupDim = N+i matches the cache's group field index space.
    aNewGroup.SetGroupDim( nSourceCount + aGroups.size() );
    aGroups.push_back( aNewGroup );
}

void ScDPGroupTableData::SetNumGroupDimension( sal_Int32 nIndex, const ScDPNumGroupDimension& rGroup )
{
    if ( nIndex < nSourceCount )
    {
        pNumGroups[nIndex] = rGroup;

        // automatic minimum / maximum is handled in GetNumEntries
    }
}

sal_Int32 ScDPGroupTableData::GetDimensionIndex( std::u16string_view rName )
{
    for (tools::Long i = 0; i < nSourceCount; ++i)                         // nSourceCount excludes data layout
        if (pSourceData->getDimensionName(i) == rName)        //TODO: ignore case?
            return i;
    return -1;  // none
}

sal_Int32 ScDPGroupTableData::GetColumnCount()
{
    // Dimension layout: [source 0..N-1] [groups N..N+G-1] [calc fields N+G..N+G+C-1]
    // DataLayout is at N+G+C (one past the end, not counted here).
    return nSourceCount + aGroups.size() + pSourceData->GetCalculatedColumnCount();
}

sal_Int32 ScDPGroupTableData::GetCalculatedColumnCount()
{
    return pSourceData->GetCalculatedColumnCount();
}

bool ScDPGroupTableData::IsNumGroupDimension( tools::Long nDimension ) const
{
    return ( nDimension < nSourceCount && pNumGroups[nDimension].GetInfo().mbEnable );
}

void ScDPGroupTableData::GetNumGroupInfo(tools::Long nDimension, ScDPNumGroupInfo& rInfo)
{
    if ( nDimension < nSourceCount )
        rInfo = pNumGroups[nDimension].GetInfo();
}
sal_Int32  ScDPGroupTableData::GetMembersCount( sal_Int32 nDim )
{
    const std::vector< SCROW >&  members = GetColumnEntries( nDim );
    return members.size();
}
void ScDPGroupTableData::ZetaSetCalcItems( sal_Int32 nDim, const std::vector<std::pair<OUString, OUString>>& rItems )
{
    if ( nDim < 0 || nDim >= nSourceCount )
        return;
    std::vector<ZetaCalcItem>& rList = maZetaCalcItems[nDim];
    rList.clear();
    for ( const auto& rItem : rItems )
    {
        ZetaCalcItem aItem;
        aItem.aName = rItem.first;
        aItem.aFormula = rItem.second;
        rList.push_back( aItem );
    }
    mbZetaCalcResolved = false;
}

// ZetaOffice: compiled calculated item formula. Excel evaluates it on the aggregated
// values of the items in each cell, so any worksheet-like expression is allowed.
struct ZetaCalcExpr
{
    enum Kind { Num, Ref, Neg, Bin, Func };
    struct Node
    {
        Kind eKind = Num;
        double fNum = 0.0;
        sal_Unicode cOp = 0;            // + - * / ^ = < > and l (<=) g (>=) n (<>)
        OUString aName;                 // item name (Ref) or upper case function name (Func)
        sal_Int32 nSlot = -1;           // Ref: index into ZetaCalcItem::aSlots
        std::vector<sal_Int32> aArgs;   // child nodes
    };
    std::vector<Node> aNodes;
    sal_Int32 nRoot = -1;
};

namespace {

bool zetaIsOpChar( sal_Unicode c )
{
    return c == '+' || c == '-' || c == '*' || c == '/' || c == '(' || c == ')' || c == '[' || c == ']'
           || c == '\'' || c == ' ' || c == '\t' || c == '=' || c == '^' || c == '&' || c == ','
           || c == ';' || c == '<' || c == '>';
}

bool zetaIsNumber( std::u16string_view aName )
{
    bool bDot = false;
    bool bDigit = false;
    for ( sal_Unicode c : aName )
    {
        if ( c >= '0' && c <= '9' )
            bDigit = true;
        else if ( c == '.' && !bDot )
            bDot = true;
        else
            return false;
    }
    return bDigit;
}

// function name -> argument count range
struct ZetaFuncInfo
{
    const char* pName;
    sal_Int32 nMin;
    sal_Int32 nMax;
};

const ZetaFuncInfo aZetaFuncs[] = {
    { "SUM", 1, 255 }, { "AVERAGE", 1, 255 }, { "MAX", 1, 255 }, { "MIN", 1, 255 },
    { "PRODUCT", 1, 255 }, { "ABS", 1, 1 }, { "INT", 1, 1 }, { "TRUNC", 1, 2 },
    { "ROUND", 2, 2 }, { "ROUNDUP", 2, 2 }, { "ROUNDDOWN", 2, 2 }, { "SQRT", 1, 1 },
    { "POWER", 2, 2 }, { "MOD", 2, 2 }, { "EXP", 1, 1 }, { "LN", 1, 1 }, { "LOG10", 1, 1 },
    { "SIGN", 1, 1 }, { "IF", 2, 3 }, { "AND", 1, 255 }, { "OR", 1, 255 }, { "NOT", 1, 1 },
};

const ZetaFuncInfo* zetaFindFunc( const OUString& rName )
{
    for ( const ZetaFuncInfo& r : aZetaFuncs )
        if ( rName.equalsAscii( r.pName ) )
            return &r;
    return nullptr;
}

class ZetaCalcCompiler
{
    const OUString& mrText;
    const OUString& mrField;
    ZetaCalcExpr& mrExpr;
    sal_Int32 mnPos = 0;
    bool mbError = false;

    sal_Int32 add( ZetaCalcExpr::Node&& rNode )
    {
        mrExpr.aNodes.push_back( std::move( rNode ) );
        return mrExpr.aNodes.size() - 1;
    }
    sal_Int32 bin( sal_Unicode cOp, sal_Int32 nLeft, sal_Int32 nRight )
    {
        ZetaCalcExpr::Node aNode;
        aNode.eKind = ZetaCalcExpr::Bin;
        aNode.cOp = cOp;
        aNode.aArgs = { nLeft, nRight };
        return add( std::move( aNode ) );
    }
    void skip()
    {
        while ( mnPos < mrText.getLength() && ( mrText[mnPos] == ' ' || mrText[mnPos] == '\t' ) )
            ++mnPos;
    }
    bool peek( sal_Unicode c )
    {
        skip();
        return mnPos < mrText.getLength() && mrText[mnPos] == c;
    }
    // a name: 'quoted' ('' = ') or a run of name characters
    bool name( OUString& rOut, bool& rQuoted )
    {
        skip();
        rQuoted = false;
        if ( mnPos >= mrText.getLength() )
            return false;
        if ( mrText[mnPos] == '\'' )
        {
            rQuoted = true;
            OUStringBuffer aBuf;
            ++mnPos;
            while ( mnPos < mrText.getLength() )
            {
                if ( mrText[mnPos] == '\'' )
                {
                    if ( mnPos + 1 < mrText.getLength() && mrText[mnPos + 1] == '\'' )
                    {
                        aBuf.append( u'\'' );
                        mnPos += 2;
                        continue;
                    }
                    ++mnPos;
                    rOut = aBuf.makeStringAndClear();
                    return true;
                }
                aBuf.append( mrText[mnPos++] );
            }
            return false;
        }
        sal_Int32 nStart = mnPos;
        while ( mnPos < mrText.getLength() && !zetaIsOpChar( mrText[mnPos] ) )
            ++mnPos;
        rOut = mrText.copy( nStart, mnPos - nStart );
        return !rOut.isEmpty();
    }
    sal_Int32 primary()
    {
        if ( peek( '(' ) )
        {
            ++mnPos;
            sal_Int32 n = compare();
            if ( !peek( ')' ) )
                mbError = true;
            else
                ++mnPos;
            return n;
        }
        OUString aName;
        bool bQuoted = false;
        if ( !name( aName, bQuoted ) )
        {
            mbError = true;
            return -1;
        }
        ZetaCalcExpr::Node aNode;
        if ( !bQuoted && zetaIsNumber( aName ) )
        {
            aNode.eKind = ZetaCalcExpr::Num;
            aNode.fNum = aName.toDouble();
            return add( std::move( aNode ) );
        }
        if ( !bQuoted && peek( '(' ) )
        {
            // a function
            ++mnPos;
            aNode.eKind = ZetaCalcExpr::Func;
            aNode.aName = aName.toAsciiUpperCase();
            const ZetaFuncInfo* pInfo = zetaFindFunc( aNode.aName );
            if ( !pInfo )
            {
                mbError = true;
                return -1;
            }
            if ( !peek( ')' ) )
            {
                while ( !mbError )
                {
                    aNode.aArgs.push_back( compare() );
                    if ( peek( ',' ) || peek( ';' ) )
                    {
                        ++mnPos;
                        continue;
                    }
                    break;
                }
            }
            if ( mbError || !peek( ')' ) )
            {
                mbError = true;
                return -1;
            }
            ++mnPos;
            const sal_Int32 nArgs = aNode.aArgs.size();
            if ( nArgs < pInfo->nMin || nArgs > pInfo->nMax )
            {
                mbError = true;
                return -1;
            }
            return add( std::move( aNode ) );
        }
        if ( peek( '[' ) )
        {
            // Field[Item]: only this field's items
            ++mnPos;
            OUString aItem;
            bool bItemQuoted = false;
            if ( aName != mrField || !name( aItem, bItemQuoted ) || !peek( ']' ) )
            {
                mbError = true;
                return -1;
            }
            ++mnPos;
            aName = aItem;
        }
        aNode.eKind = ZetaCalcExpr::Ref;
        aNode.aName = aName;
        return add( std::move( aNode ) );
    }
    sal_Int32 unary()
    {
        if ( peek( '-' ) || peek( '+' ) )
        {
            const bool bMinus = mrText[mnPos] == '-';
            ++mnPos;
            sal_Int32 n = unary();
            if ( !bMinus || mbError )
                return n;
            ZetaCalcExpr::Node aNode;
            aNode.eKind = ZetaCalcExpr::Neg;
            aNode.aArgs = { n };
            return add( std::move( aNode ) );
        }
        return primary();
    }
    // Excel: unary minus binds tighter than ^, and ^ goes left to right
    sal_Int32 power()
    {
        sal_Int32 n = unary();
        while ( !mbError && peek( '^' ) )
        {
            ++mnPos;
            n = bin( '^', n, unary() );
        }
        return n;
    }
    sal_Int32 term()
    {
        sal_Int32 n = power();
        while ( !mbError && ( peek( '*' ) || peek( '/' ) ) )
        {
            const sal_Unicode c = mrText[mnPos++];
            n = bin( c, n, power() );
        }
        return n;
    }
    sal_Int32 additive()
    {
        sal_Int32 n = term();
        while ( !mbError && ( peek( '+' ) || peek( '-' ) ) )
        {
            const sal_Unicode c = mrText[mnPos++];
            n = bin( c, n, term() );
        }
        return n;
    }
    sal_Int32 compare()
    {
        sal_Int32 n = additive();
        while ( !mbError )
        {
            skip();
            if ( mnPos >= mrText.getLength() )
                break;
            const sal_Unicode c = mrText[mnPos];
            const sal_Unicode cNext = mnPos + 1 < mrText.getLength() ? mrText[mnPos + 1] : 0;
            sal_Unicode cOp = 0;
            if ( c == '=' )
                cOp = '=';
            else if ( c == '<' && cNext == '=' )
                cOp = 'l';
            else if ( c == '>' && cNext == '=' )
                cOp = 'g';
            else if ( c == '<' && cNext == '>' )
                cOp = 'n';
            else if ( c == '<' || c == '>' )
                cOp = c;
            else
                break;
            mnPos += ( cOp == c ) ? 1 : 2;
            n = bin( cOp, n, additive() );
        }
        return n;
    }

public:
    ZetaCalcCompiler( const OUString& rText, const OUString& rField, ZetaCalcExpr& rExpr )
        : mrText( rText ), mrField( rField ), mrExpr( rExpr ) {}
    bool compile()
    {
        skip();
        if ( mnPos < mrText.getLength() && mrText[mnPos] == '=' )
            ++mnPos;
        mrExpr.nRoot = compare();
        skip();
        return !mbError && mrExpr.nRoot >= 0 && mnPos == mrText.getLength();
    }
};

struct ZetaVal
{
    double f = 0.0;
    bool bErr = false;
};

ZetaVal zetaEval( const ZetaCalcExpr& rExpr, sal_Int32 nNode, const std::vector<ZetaVal>& rSlots )
{
    const ZetaCalcExpr::Node& rNode = rExpr.aNodes[nNode];
    ZetaVal aErr;
    aErr.bErr = true;
    auto ok = []( double f ) {
        ZetaVal v;
        v.f = f;
        v.bErr = !std::isfinite( f );
        return v;
    };
    switch ( rNode.eKind )
    {
        case ZetaCalcExpr::Num:
            return ok( rNode.fNum );
        case ZetaCalcExpr::Ref:
            return rNode.nSlot >= 0 && o3tl::make_unsigned( rNode.nSlot ) < rSlots.size() ? rSlots[rNode.nSlot] : aErr;
        case ZetaCalcExpr::Neg:
        {
            ZetaVal v = zetaEval( rExpr, rNode.aArgs[0], rSlots );
            v.f = -v.f;
            return v;
        }
        case ZetaCalcExpr::Bin:
        {
            const ZetaVal a = zetaEval( rExpr, rNode.aArgs[0], rSlots );
            if ( a.bErr )
                return a;
            const ZetaVal b = zetaEval( rExpr, rNode.aArgs[1], rSlots );
            if ( b.bErr )
                return b;
            switch ( rNode.cOp )
            {
                case '+': return ok( a.f + b.f );
                case '-': return ok( a.f - b.f );
                case '*': return ok( a.f * b.f );
                case '/': return b.f == 0.0 ? aErr : ok( a.f / b.f );
                case '^': return ( a.f == 0.0 && b.f == 0.0 ) ? aErr : ok( pow( a.f, b.f ) );
                case '=': return ok( a.f == b.f ? 1.0 : 0.0 );
                case 'n': return ok( a.f != b.f ? 1.0 : 0.0 );
                case '<': return ok( a.f < b.f ? 1.0 : 0.0 );
                case '>': return ok( a.f > b.f ? 1.0 : 0.0 );
                case 'l': return ok( a.f <= b.f ? 1.0 : 0.0 );
                case 'g': return ok( a.f >= b.f ? 1.0 : 0.0 );
            }
            return aErr;
        }
        case ZetaCalcExpr::Func:
            break;
    }

    const OUString& rName = rNode.aName;
    if ( rName == "IF" )
    {
        const ZetaVal c = zetaEval( rExpr, rNode.aArgs[0], rSlots );
        if ( c.bErr )
            return c;
        if ( c.f != 0.0 )
            return zetaEval( rExpr, rNode.aArgs[1], rSlots );
        return rNode.aArgs.size() > 2 ? zetaEval( rExpr, rNode.aArgs[2], rSlots ) : ok( 0.0 );
    }
    std::vector<double> aArgs;
    for ( sal_Int32 nArg : rNode.aArgs )
    {
        const ZetaVal v = zetaEval( rExpr, nArg, rSlots );
        if ( v.bErr )
            return v;
        aArgs.push_back( v.f );
    }
    const double x = aArgs[0];
    const double y = aArgs.size() > 1 ? aArgs[1] : 0.0;
    if ( rName == "SUM" || rName == "AVERAGE" )
    {
        double f = 0.0;
        for ( double d : aArgs )
            f += d;
        return ok( rName == "SUM" ? f : f / aArgs.size() );
    }
    if ( rName == "MAX" )
        return ok( *std::max_element( aArgs.begin(), aArgs.end() ) );
    if ( rName == "MIN" )
        return ok( *std::min_element( aArgs.begin(), aArgs.end() ) );
    if ( rName == "PRODUCT" )
    {
        double f = 1.0;
        for ( double d : aArgs )
            f *= d;
        return ok( f );
    }
    if ( rName == "AND" || rName == "OR" )
    {
        const bool bAnd = rName == "AND";
        bool b = bAnd;
        for ( double d : aArgs )
            b = bAnd ? ( b && d != 0.0 ) : ( b || d != 0.0 );
        return ok( b ? 1.0 : 0.0 );
    }
    if ( rName == "NOT" )
        return ok( x == 0.0 ? 1.0 : 0.0 );
    if ( rName == "ABS" )
        return ok( fabs( x ) );
    if ( rName == "INT" )
        return ok( floor( x ) );
    if ( rName == "TRUNC" )
        return ok( rtl::math::round( x, static_cast<int>( y ), rtl_math_RoundingMode_Down ) );
    if ( rName == "ROUND" )
        return ok( rtl::math::round( x, static_cast<int>( y ) ) );
    if ( rName == "ROUNDUP" )
        return ok( rtl::math::round( x, static_cast<int>( y ), rtl_math_RoundingMode_Up ) );
    if ( rName == "ROUNDDOWN" )
        return ok( rtl::math::round( x, static_cast<int>( y ), rtl_math_RoundingMode_Down ) );
    if ( rName == "SQRT" )
        return x < 0.0 ? aErr : ok( sqrt( x ) );
    if ( rName == "POWER" )
        return ( x == 0.0 && y == 0.0 ) ? aErr : ok( pow( x, y ) );
    if ( rName == "MOD" )
        return y == 0.0 ? aErr : ok( x - y * floor( x / y ) );
    if ( rName == "EXP" )
        return ok( exp( x ) );
    if ( rName == "LN" )
        return x <= 0.0 ? aErr : ok( log( x ) );
    if ( rName == "LOG10" )
        return x <= 0.0 ? aErr : ok( log10( x ) );
    if ( rName == "SIGN" )
        return ok( x > 0.0 ? 1.0 : ( x < 0.0 ? -1.0 : 0.0 ) );
    return aErr;
}

// aggregate of one item's records in one leaf (a combination of the other row/column items)
struct ZetaLeafAgg
{
    double fSum = 0.0;
    double fMax = 0.0;
    double fMin = 0.0;
    double fProd = 0.0;
    sal_Int64 nNum = 0;     // numbers
    sal_Int64 nAll = 0;     // all non-empty values
    bool bErr = false;

    void update( const ScDPValue& rValue )
    {
        if ( rValue.meType == ScDPValue::Empty )
            return;
        ++nAll;
        if ( rValue.meType == ScDPValue::Error )
        {
            bErr = true;
            return;
        }
        if ( rValue.meType != ScDPValue::Value )
            return;
        const double f = rValue.mfValue;
        if ( nNum == 0 )
            fMax = fMin = fProd = f;
        else
        {
            fMax = std::max( fMax, f );
            fMin = std::min( fMin, f );
            fProd *= f;
        }
        fSum += f;
        ++nNum;
    }
    ZetaVal result( ScSubTotalFunc eFunc ) const
    {
        ZetaVal v;
        if ( eFunc == SUBTOTAL_FUNC_CNT2 )
        {
            v.f = nAll;
            return v;
        }
        if ( bErr )
        {
            v.bErr = true;
            return v;
        }
        switch ( eFunc )
        {
            case SUBTOTAL_FUNC_CNT: v.f = nNum; break;
            case SUBTOTAL_FUNC_MAX: v.f = fMax; break;
            case SUBTOTAL_FUNC_MIN: v.f = fMin; break;
            case SUBTOTAL_FUNC_PROD: v.f = fProd; break;
            case SUBTOTAL_FUNC_AVE:
                // Excel does not allow averages with calculated items
                if ( nNum > 0 )
                    v.f = fSum / nNum;
                else
                    v.bErr = true;
                break;
            default: v.f = fSum; break;
        }
        return v;
    }
};

}

bool ScDPGroupTableData::ZetaParseCalcFormula( const OUString& rFormula, const OUString& rFieldName,
                                               std::vector<std::pair<OUString, double>>& rTerms )
{
    ZetaCalcExpr aExpr;
    ZetaCalcCompiler aCompiler( rFormula, rFieldName, aExpr );
    if ( !aCompiler.compile() )
        return false;
    rTerms.clear();
    for ( const ZetaCalcExpr::Node& rNode : aExpr.aNodes )
        if ( rNode.eKind == ZetaCalcExpr::Ref
             && std::none_of( rTerms.begin(), rTerms.end(), [&rNode]( const auto& r ) { return r.first == rNode.aName; } ) )
            rTerms.emplace_back( rNode.aName, 1.0 );
    return true;
}

OUString ScDPGroupTableData::ZetaCalcFormulaToExcel( const OUString& rFormula, const OUString& rFieldName )
{
    // a bare name: starts with a letter or '_' (any non-ASCII counts as a letter), then no operators
    auto quote = []( const OUString& rName ) -> OUString {
        bool bBare = !rName.isEmpty() && !( rName[0] >= '0' && rName[0] <= '9' ) && rName[0] != '.';
        for ( sal_Int32 i = 0; bBare && i < rName.getLength(); ++i )
            bBare = !zetaIsOpChar( rName[i] );
        return bBare ? rName : u"'"_ustr + rName.replaceAll( u"'", u"''" ) + u"'";
    };
    const OUString aField = quote( rFieldName );
    OUStringBuffer aOut;
    sal_Int32 i = 0;
    const sal_Int32 n = rFormula.getLength();
    while ( i < n && ( rFormula[i] == ' ' || rFormula[i] == '\t' ) )
        ++i;
    if ( i < n && rFormula[i] == '=' )
        ++i;
    while ( i < n )
    {
        const sal_Unicode c = rFormula[i];
        if ( c == ' ' || c == '\t' )
        {
            ++i;
            continue;
        }
        if ( zetaIsOpChar( c ) && c != '\'' )
        {
            // Excel stores ',' as the argument separator
            aOut.append( c == ';' ? u',' : c );
            ++i;
            continue;
        }
        // a name, a number or a function
        OUString aName;
        bool bQuoted = false;
        if ( c == '\'' )
        {
            bQuoted = true;
            OUStringBuffer aBuf;
            ++i;
            while ( i < n )
            {
                if ( rFormula[i] == '\'' )
                {
                    if ( i + 1 < n && rFormula[i + 1] == '\'' )
                    {
                        aBuf.append( u'\'' );
                        i += 2;
                        continue;
                    }
                    ++i;
                    break;
                }
                aBuf.append( rFormula[i++] );
            }
            aName = aBuf.makeStringAndClear();
        }
        else
        {
            sal_Int32 nStart = i;
            while ( i < n && !zetaIsOpChar( rFormula[i] ) )
                ++i;
            aName = rFormula.copy( nStart, i - nStart );
        }
        sal_Int32 k = i;
        while ( k < n && ( rFormula[k] == ' ' || rFormula[k] == '\t' ) )
            ++k;
        if ( k < n && rFormula[k] == '[' )
        {
            // already Field[Item]: copy the item part as it is
            sal_Int32 nClose = rFormula.indexOf( ']', k );
            if ( nClose < 0 )
                nClose = n - 1;
            aOut.append( quote( aName ) + rFormula.subView( k, nClose - k + 1 ) );
            i = nClose + 1;
            continue;
        }
        if ( !bQuoted && k < n && rFormula[k] == '(' )
            aOut.append( aName.toAsciiUpperCase() );     // a function
        else if ( !bQuoted && zetaIsNumber( aName ) )
            aOut.append( aName );
        else
            aOut.append( aField + "[" + quote( aName ) + "]" );
    }
    return aOut.makeStringAndClear();
}

void ScDPGroupTableData::ZetaResolveCalcItems()
{
    if ( mbZetaCalcResolved )
        return;
    mbZetaCalcResolved = true;
    maZetaEntries.clear();
    const ScDPCache& rCache = GetCacheTable().getCache();
    for ( auto& [nDim, rList] : maZetaCalcItems )
    {
        const OUString aField = rCache.GetDimensionName( nDim );
        // item name -> id of the source items (as they are shown)
        std::unordered_map<OUString, SCROW> aIds;
        const tools::Long nCount = rCache.GetDimMemberCount( nDim );
        for ( SCROW nId = 0; nId < nCount; ++nId )
            if ( const ScDPItemData* pItem = rCache.GetItemDataById( nDim, nId ) )
                aIds.emplace( rCache.GetFormattedString( nDim, *pItem, false ), nId );
        // a calculated item can use the items before it (Excel: no circular references)
        for ( size_t i = 0; i < rList.size(); ++i )
        {
            ZetaCalcItem& rItem = rList[i];
            rItem.nId = rCache.GetIdByItemData( nDim, ScDPItemData( rItem.aName ) );
            rItem.aSlots.clear();
            auto pExpr = std::make_shared<ZetaCalcExpr>();
            ZetaCalcCompiler aCompiler( rItem.aFormula, aField, *pExpr );
            if ( rItem.nId < 0 || !aCompiler.compile() )
            {
                rItem.nId = -1;
                rItem.pExpr.reset();
                continue;
            }
            bool bOk = true;
            for ( ZetaCalcExpr::Node& rNode : pExpr->aNodes )
            {
                if ( rNode.eKind != ZetaCalcExpr::Ref )
                    continue;
                std::pair<SCROW, sal_Int32> aSlot( -1, -1 );
                auto it = aIds.find( rNode.aName );
                if ( it != aIds.end() )
                    aSlot.first = it->second;
                else
                {
                    auto itCalc = std::find_if( rList.begin(), rList.begin() + i,
                                                [&rNode]( const ZetaCalcItem& r ) { return r.aName == rNode.aName; } );
                    if ( itCalc == rList.begin() + i || itCalc->nId < 0 )
                    {
                        bOk = false;
                        break;
                    }
                    aSlot.second = itCalc - rList.begin();
                }
                auto itSlot = std::find( rItem.aSlots.begin(), rItem.aSlots.end(), aSlot );
                rNode.nSlot = itSlot - rItem.aSlots.begin();
                if ( itSlot == rItem.aSlots.end() )
                    rItem.aSlots.push_back( aSlot );
            }
            if ( bOk )
                rItem.pExpr = std::move( pExpr );
            else
            {
                rItem.nId = -1;
                rItem.pExpr.reset();
            }
        }
    }
}

bool ScDPGroupTableData::ZetaIsCalcItem( sal_Int32 nDim, SCROW nId ) const
{
    auto it = maZetaCalcItems.find( nDim );
    if ( it == maZetaCalcItems.end() )
        return false;
    return std::any_of( it->second.begin(), it->second.end(), [nId]( const ZetaCalcItem& r ) { return r.nId >= 0 && r.nId == nId; } );
}

void ScDPGroupTableData::ZetaCalcResults( CalcInfo& rInfo, bool bAutoShow )
{
    // Excel: a cell of a calculated item is its formula over the aggregated values of the
    // items it uses in the same leaf (the same items of all other row and column fields).
    // Each leaf goes into the results as one already aggregated record, so the subtotals
    // take the data function over the leaves (the counts add up) as in Excel. Every leaf
    // gets a record, so the calculated item lists all inner items, also with 0 ('B 0').
    ZetaResolveCalcItems();
    const size_t nColCount = rInfo.aColLevelDims.size();
    const size_t nRowCount = rInfo.aRowLevelDims.size();
    const size_t nMeasures = rInfo.aDataSrcCols.size();
    const ScDPResultData* pResultData = rInfo.pRowRoot ? rInfo.pRowRoot->GetResultData() : nullptr;
    std::vector<ScSubTotalFunc> aFuncs( nMeasures, SUBTOTAL_FUNC_SUM );
    if ( pResultData )
        for ( size_t m = 0; m < nMeasures && o3tl::make_unsigned( pResultData->GetMeasureCount() ) > m; ++m )
            aFuncs[m] = pResultData->GetMeasureFunction( m );

    struct DimLeaves
    {
        const std::vector<ZetaCalcItem>* pItems = nullptr;
        size_t nSlot = 0;                   // position of the field in col data + row data
        std::unordered_set<SCROW> aUsed;    // source items used by the formulas
        std::map<std::vector<SCROW>, std::unordered_map<SCROW, std::vector<ZetaLeafAgg>>> aLeaves;
    };
    std::vector<DimLeaves> aDims;
    for ( size_t nLevel = 0; nLevel < nColCount + nRowCount; ++nLevel )
    {
        const sal_Int32 nDim = nLevel < nColCount ? rInfo.aColLevelDims[nLevel] : rInfo.aRowLevelDims[nLevel - nColCount];
        auto it = maZetaCalcItems.find( nDim );
        if ( it == maZetaCalcItems.end() )
            continue;
        DimLeaves aDim;
        aDim.pItems = &it->second;
        aDim.nSlot = nLevel;
        bool bAny = false;
        for ( const ZetaCalcItem& rItem : it->second )
        {
            if ( rItem.nId < 0 )
                continue;
            bAny = true;
            for ( const auto& rSlot : rItem.aSlots )
                if ( rSlot.second < 0 )
                    aDim.aUsed.insert( rSlot.first );
        }
        if ( bAny )
            aDims.push_back( std::move( aDim ) );
    }
    if ( aDims.empty() )
        return;

    const ScDPFilteredCache& rCacheTable = pSourceData->GetCacheTable();
    const sal_Int32 nRowSize = rCacheTable.getRowSize();
    for ( sal_Int32 nRow = 0; nRow < nRowSize; ++nRow )
    {
        sal_Int32 nLastRow;
        if ( !rCacheTable.isRowActive( nRow, &nLastRow ) )
        {
            nRow = nLastRow;
            continue;
        }
        CalcRowData aData;
        FillRowDataFromCacheTable( nRow, rCacheTable, rInfo, aData );
        if ( !rInfo.aColLevelDims.empty() )
            FillGroupValues( aData.aColData, rInfo.aColLevelDims );
        if ( !rInfo.aRowLevelDims.empty() )
            FillGroupValues( aData.aRowData, rInfo.aRowLevelDims );

        std::vector<SCROW> aKey( aData.aColData );
        aKey.insert( aKey.end(), aData.aRowData.begin(), aData.aRowData.end() );
        if ( aKey.size() != nColCount + nRowCount )
            continue;
        for ( DimLeaves& rDim : aDims )
        {
            const SCROW nItem = aKey[rDim.nSlot];
            aKey[rDim.nSlot] = -2;
            auto& rItems = rDim.aLeaves[aKey];
            if ( rDim.aUsed.count( nItem ) )
            {
                std::vector<ZetaLeafAgg>& rAggs = rItems[nItem];
                rAggs.resize( nMeasures );
                for ( size_t m = 0; m < nMeasures && m < aData.aValues.size(); ++m )
                    rAggs[m].update( aData.aValues[m] );
            }
            aKey[rDim.nSlot] = nItem;
        }
    }

    for ( const DimLeaves& rDim : aDims )
    {
        const std::vector<ZetaCalcItem>& rList = *rDim.pItems;
        for ( const auto& [rKey, rItems] : rDim.aLeaves )
        {
            // per calculated item, per measure: the value in this leaf
            std::vector<std::vector<ZetaVal>> aCalcValues( rList.size() );
            for ( size_t i = 0; i < rList.size(); ++i )
            {
                const ZetaCalcItem& rItem = rList[i];
                if ( rItem.nId < 0 || !rItem.pExpr )
                    continue;
                CalcRowData aRow;
                aRow.aColData.assign( rKey.begin(), rKey.begin() + nColCount );
                aRow.aRowData.assign( rKey.begin() + nColCount, rKey.end() );
                ( rDim.nSlot < nColCount ? aRow.aColData[rDim.nSlot] : aRow.aRowData[rDim.nSlot - nColCount] ) = rItem.nId;
                aRow.aValues.resize( nMeasures );
                aCalcValues[i].resize( nMeasures );
                for ( size_t m = 0; m < nMeasures; ++m )
                {
                    std::vector<ZetaVal> aSlots;
                    for ( const auto& [nId, nCalc] : rItem.aSlots )
                    {
                        if ( nCalc >= 0 )
                        {
                            const auto& rPrev = aCalcValues[nCalc];
                            ZetaVal aErr;
                            aErr.bErr = true;
                            aSlots.push_back( m < rPrev.size() ? rPrev[m] : aErr );
                            continue;
                        }
                        auto it = rItems.find( nId );
                        aSlots.push_back( it != rItems.end() && m < it->second.size() ? it->second[m].result( aFuncs[m] ) : ZetaVal() );
                    }
                    const ZetaVal v = zetaEval( *rItem.pExpr, rItem.pExpr->nRoot, aSlots );
                    aCalcValues[i][m] = v;
                    aRow.aValues[m].Set( v.f, v.bErr ? ScDPValue::Error : ScDPValue::ZetaAggregated );
                }
                ProcessRowData( rInfo, aRow, bAutoShow );
            }
        }
    }
}

const std::vector< SCROW >& ScDPGroupTableData::GetColumnEntries( sal_Int32  nColumn )
{
    // ZetaOffice: the calculated items come after the source items
    auto itCalc = maZetaCalcItems.find( nColumn );
    if ( itCalc != maZetaCalcItems.end() )
    {
        ZetaResolveCalcItems();
        std::vector<SCROW>& rEntries = maZetaEntries[nColumn];
        rEntries = pSourceData->GetColumnEntries( nColumn );
        for ( const ZetaCalcItem& rItem : itCalc->second )
            if ( rItem.nId >= 0 && std::find( rEntries.begin(), rEntries.end(), rItem.nId ) == rEntries.end() )
                rEntries.push_back( rItem.nId );
        return rEntries;
    }

    sal_Int32 nGroupCount = aGroups.size();
    if ( nColumn >= nSourceCount && nColumn < nSourceCount + nGroupCount )
    {
        // Group dimension zone [N, N+G-1]
        const ScDPGroupDimension& rGroupDim = aGroups[nColumn - nSourceCount];
        return rGroupDim.GetColumnEntries( GetCacheTable() );
    }

    if ( IsNumGroupDimension( nColumn ) )
    {
        // dimension number is unchanged for numerical groups
        return pNumGroups[nColumn].GetNumEntries(
            static_cast<SCCOL>(nColumn), &GetCacheTable().getCache());
    }

    return pSourceData->GetColumnEntries( nColumn );
}

const ScDPItemData* ScDPGroupTableData::GetMemberById( sal_Int32 nDim, sal_Int32 nId )
{
    return pSourceData->GetMemberById( nDim, nId );
}

OUString ScDPGroupTableData::getDimensionName(sal_Int32 nColumn)
{
    sal_Int32 nGroupCount = aGroups.size();
    if ( nColumn >= nSourceCount && nColumn < nSourceCount + nGroupCount )
    {
        // Group dimension zone [N, N+G-1]
        return aGroups[nColumn - nSourceCount].GetName();
    }

    if ( nColumn >= nSourceCount + nGroupCount )
    {
        // Calc fields or DataLayout zone [N+G, ...].
        // Cannot delegate directly to source — the source's getIsDataLayoutDimension
        // checks nColumn == N+C, which collides with our calc field indices when groups exist.
        if ( getIsDataLayoutDimension(nColumn) )
        {
            // DataLayout: remap to source's DataLayout position (N+C = nColumn - G)
            return pSourceData->getDimensionName( nColumn - nGroupCount );
        }
        // Calc field: query cache directly, bypassing the source's
        // getIsDataLayoutDimension check which would misidentify this index.
        // Use CalcFieldToCacheIndex to map proxy position to actual OOXML mnIndex,
        // since calc fields can appear before groups in OOXML ordering.
        return GetCacheTable().getCache().GetDimensionName( CalcFieldToCacheIndex(nColumn) );
    }

    // Source columns [0, N-1]
    return pSourceData->getDimensionName( nColumn );
}

bool ScDPGroupTableData::getIsDataLayoutDimension(sal_Int32 nColumn)
{
    // DataLayout is at position N+G+C (after source, groups, and calc fields).
    return ( nColumn == sal::static_int_cast<tools::Long>( nSourceCount + aGroups.size()
             + pSourceData->GetCalculatedColumnCount() ) );
}

bool ScDPGroupTableData::IsDateDimension(sal_Int32 nDim)
{
    sal_Int32 nGroupCount = aGroups.size();
    if ( nDim >= nSourceCount && nDim < nSourceCount + nGroupCount )
    {
        // Group dimension — look at original source dimension
        nDim = aGroups[nDim - nSourceCount].GetSourceDim();
    }
    else if ( nDim >= nSourceCount + nGroupCount )
    {
        // Calc fields and DataLayout are not date dimensions.
        // Do not delegate to source — its getIsDataLayoutDimension check
        // would collide with our calc field indices when groups exist.
        return false;
    }

    return pSourceData->IsDateDimension( nDim );
}

bool ScDPGroupTableData::IsCalculatedDimension(sal_Int32 nDim)
{
    sal_Int32 nGroupCount = aGroups.size();
    if ( nDim >= nSourceCount && nDim < nSourceCount + nGroupCount )
        return false;   // group dimensions are not calculated fields
    // Remap calc field proxy index to actual cache mnIndex, since source's
    // isCalculatedField() matches by mnIndex which depends on OOXML ordering.
    return pSourceData->IsCalculatedDimension( CalcFieldToCacheIndex(nDim) );
}

OUString ScDPGroupTableData::GetCalculation(sal_Int32 nDim)
{
    sal_Int32 nGroupCount = aGroups.size();
    if ( nDim >= nSourceCount && nDim < nSourceCount + nGroupCount )
        return OUString();   // group dimensions have no calculation
    // Remap calc field proxy index to actual cache mnIndex.
    return pSourceData->GetCalculation( CalcFieldToCacheIndex(nDim) );
}

const ScTokenArray* ScDPGroupTableData::GetCalculationToken(sal_Int32 nDim)
{
    sal_Int32 nGroupCount = aGroups.size();
    if ( nDim >= nSourceCount && nDim < nSourceCount + nGroupCount )
        return nullptr;   // group dimensions have no calculation token
    // Remap calc field proxy index to actual cache mnIndex.
    return pSourceData->GetCalculationToken( CalcFieldToCacheIndex(nDim) );
}

sal_uInt32 ScDPGroupTableData::GetNumberFormat(sal_Int32 nDim)
{
    sal_Int32 nGroupCount = aGroups.size();
    if ( nDim >= nSourceCount && nDim < nSourceCount + nGroupCount )
    {
        // Group dimension — use the original source dimension's format
        nDim = aGroups[nDim - nSourceCount].GetSourceDim();
    }
    else if ( nDim >= nSourceCount + nGroupCount )
    {
        // Calc fields and DataLayout — return 0 (general format).
        // Do not delegate to source — its getIsDataLayoutDimension check
        // would collide with our calc field indices when groups exist.
        return 0;
    }

    return pSourceData->GetNumberFormat( nDim );
}

void ScDPGroupTableData::DisposeData()
{
    for ( auto& rGroup : aGroups )
        rGroup.DisposeData();

    for ( tools::Long i=0; i<nSourceCount; i++ )
        pNumGroups[i].DisposeData();

    pSourceData->DisposeData();
}

void ScDPGroupTableData::SetEmptyFlags( bool bIgnoreEmptyRows, bool bRepeatIfEmpty )
{
    pSourceData->SetEmptyFlags( bIgnoreEmptyRows, bRepeatIfEmpty );
}

bool ScDPGroupTableData::IsRepeatIfEmpty()
{
    return pSourceData->IsRepeatIfEmpty();
}

void ScDPGroupTableData::CreateCacheTable()
{
    mbZetaCalcResolved = false;
    pSourceData->CreateCacheTable();
}

namespace {

class FindCaseInsensitive
{
    ScDPItemData maValue;
public:
    explicit FindCaseInsensitive(const ScDPItemData& rVal) : maValue(rVal) {}

    bool operator() (const ScDPItemData& rItem) const
    {
        return maValue.IsCaseInsEqual(rItem);
    }
};

}

void ScDPGroupTableData::ModifyFilterCriteria(std::vector<ScDPFilteredCache::Criterion>& rCriteria)
{
    // Build dimension ID to object map for group dimensions.
    typedef std::unordered_map<tools::Long, const ScDPGroupDimension*> GroupFieldMapType;
    GroupFieldMapType aGroupFieldIds;

    for (const auto& rGroup : aGroups)
    {
        aGroupFieldIds.emplace(rGroup.GetGroupDim(), &rGroup);
    }

    std::vector<ScDPFilteredCache::Criterion> aNewCriteria;
    aNewCriteria.reserve(rCriteria.size() + aGroups.size());

    // Go through all the filtered field names and process them appropriately.

    const ScDPCache& rCache = GetCacheTable().getCache();
    GroupFieldMapType::const_iterator itrGrpEnd = aGroupFieldIds.end();
    for (const auto& rCriterion : rCriteria)
    {
        std::vector<ScDPItemData> aMatchValues = rCriterion.mpFilter->getMatchValues();

        GroupFieldMapType::const_iterator itrGrp = aGroupFieldIds.find(rCriterion.mnFieldIndex);
        if (itrGrp == itrGrpEnd)
        {
            if (IsNumGroupDimension(rCriterion.mnFieldIndex))
            {
                // internal number group field
                const ScDPNumGroupInfo* pNumInfo = rCache.GetNumGroupInfo(rCriterion.mnFieldIndex);
                if (!pNumInfo)
                    // Number group dimension without num info?  Something is wrong...
                    continue;

                ScDPFilteredCache::Criterion aCri;
                aCri.mnFieldIndex = rCriterion.mnFieldIndex;
                const ScDPNumGroupDimension& rNumGrpDim = pNumGroups[rCriterion.mnFieldIndex];

                if (rNumGrpDim.IsDateDimension())
                {
                    // grouped by dates.
                    aCri.mpFilter =
                        std::make_shared<ScDPGroupDateFilter>(
                            std::move(aMatchValues), pDoc->GetFormatTable()->GetNullDate(), *pNumInfo);
                }
                else
                {
                    // This dimension is grouped by numeric ranges.
                    aCri.mpFilter =
                        std::make_shared<ScDPGroupNumFilter>(std::move(aMatchValues), *pNumInfo);
                }

                aNewCriteria.push_back(std::move(aCri));
            }
            else
            {
                // This is a regular source field.
                aNewCriteria.push_back(rCriterion);
            }
        }
        else
        {
            // This is an ordinary group field or external number group field.

            const ScDPGroupDimension* pGrpDim = itrGrp->second;
            tools::Long nSrcDim = pGrpDim->GetSourceDim();
            tools::Long nGrpDim = pGrpDim->GetGroupDim();
            const ScDPNumGroupInfo* pNumInfo = rCache.GetNumGroupInfo(nGrpDim);

            if (pGrpDim->IsDateDimension() && pNumInfo)
            {
                // external number group
                ScDPFilteredCache::Criterion aCri;
                aCri.mnFieldIndex = nSrcDim;  // use the source dimension, not the group dimension.
                aCri.mpFilter =
                    std::make_shared<ScDPGroupDateFilter>(
                        std::move(aMatchValues), pDoc->GetFormatTable()->GetNullDate(), *pNumInfo);

                aNewCriteria.push_back(std::move(aCri));
            }
            else
            {
                // normal group

                ScDPFilteredCache::Criterion aCri;
                aCri.mnFieldIndex = nSrcDim;
                aCri.mpFilter = std::make_shared<ScDPFilteredCache::GroupFilter>();
                ScDPFilteredCache::GroupFilter* pGrpFilter =
                    static_cast<ScDPFilteredCache::GroupFilter*>(aCri.mpFilter.get());

                size_t nGroupItemCount = pGrpDim->GetItemCount();
                for (size_t i = 0; i < nGroupItemCount; ++i)
                {
                    const ScDPGroupItem* pGrpItem = pGrpDim->GetGroupByIndex(i);
                    if (!pGrpItem)
                        continue;

                    // Make sure this group name equals one of the match values.
                    if (std::none_of(aMatchValues.begin(), aMatchValues.end(), FindCaseInsensitive(pGrpItem->GetName())))
                        continue;

                    pGrpItem->FillGroupFilter(*pGrpFilter);
                }

                aNewCriteria.push_back(std::move(aCri));
            }
        }
    }
    rCriteria.swap(aNewCriteria);
}

void ScDPGroupTableData::FilterCacheTable(std::vector<ScDPFilteredCache::Criterion>&& rCriteria, std::unordered_set<sal_Int32>&& rCatDims)
{
    ModifyFilterCriteria(rCriteria);
    pSourceData->FilterCacheTable(std::move(rCriteria), std::move(rCatDims));
}

void ScDPGroupTableData::GetDrillDownData(std::vector<ScDPFilteredCache::Criterion>&& rCriteria, std::unordered_set<sal_Int32>&&rCatDims, Sequence< Sequence<Any> >& rData)
{
    ModifyFilterCriteria(rCriteria);
    pSourceData->GetDrillDownData(std::move(rCriteria), std::move(rCatDims), rData);
}

void ScDPGroupTableData::CalcResults(CalcInfo& rInfo, bool bAutoShow)
{
    // #i111435# Inside FillRowDataFromCacheTable/GetItemData, virtual methods
    // getIsDataLayoutDimension and GetSourceDim are used, so it has to be called
    // with original rInfo, containing dimension indexes of the grouped data.

    const ScDPFilteredCache& rCacheTable = pSourceData->GetCacheTable();
    sal_Int32 nRowSize = rCacheTable.getRowSize();
    for (sal_Int32 nRow = 0; nRow < nRowSize; ++nRow)
    {
        sal_Int32 nLastRow;
        if (!rCacheTable.isRowActive(nRow, &nLastRow))
        {
            nRow = nLastRow;
            continue;
        }

        CalcRowData aData;
        FillRowDataFromCacheTable(nRow, rCacheTable, rInfo, aData);

        if ( !rInfo.aColLevelDims.empty() )
            FillGroupValues(aData.aColData, rInfo.aColLevelDims);
        if ( !rInfo.aRowLevelDims.empty() )
            FillGroupValues(aData.aRowData, rInfo.aRowLevelDims);
        if ( !rInfo.aPageDims.empty() )
            FillGroupValues(aData.aPageData, rInfo.aPageDims);

        ProcessRowData(rInfo, aData, bAutoShow);
    }
    // ZetaOffice: the leaves of the calculated items
    if ( !maZetaCalcItems.empty() )
        ZetaCalcResults( rInfo, bAutoShow );
}

const ScDPFilteredCache& ScDPGroupTableData::GetCacheTable() const
{
    return pSourceData->GetCacheTable();
}

void ScDPGroupTableData::ReloadCacheTable()
{
    pSourceData->ReloadCacheTable();
}

void ScDPGroupTableData::FillGroupValues(std::vector<SCROW>& rItems, const std::vector<sal_Int32>& rDims)
{
    sal_Int32 nGroupedColumns = aGroups.size();

    const ScDPCache& rCache = GetCacheTable().getCache();
    size_t i = 0;
    for (tools::Long nColumn : rDims)
    {
        bool bDateDim = false;

        sal_Int32 nSourceDim = nColumn;
        if ( nColumn >= nSourceCount && nColumn < nSourceCount + nGroupedColumns )
        {
            const ScDPGroupDimension& rGroupDim = aGroups[nColumn - nSourceCount];
            nSourceDim= rGroupDim.GetSourceDim();
            bDateDim = rGroupDim.IsDateDimension();
            if (!bDateDim)                         // date is handled below
            {
                const ScDPItemData& rItem = *GetMemberById(nSourceDim, rItems[i]);
                const ScDPGroupItem* pGroupItem = rGroupDim.GetGroupForData(rItem);
                if (pGroupItem)
                {
                    rItems[i] =
                        rCache.GetIdByItemData(nColumn, pGroupItem->GetName());
                }
                else
                    rItems[i] = rCache.GetIdByItemData(nColumn, rItem);
            }
        }
        else if ( IsNumGroupDimension( nColumn ) )
        {
            bDateDim = pNumGroups[nColumn].IsDateDimension();
            if (!bDateDim)                         // date is handled below
            {
                const ScDPItemData* pData = rCache.GetItemDataById(nSourceDim, rItems[i]);
                if (pData->GetType() == ScDPItemData::Value)
                {
                    ScDPNumGroupInfo aNumInfo;
                    GetNumGroupInfo(nColumn, aNumInfo);
                    double fGroupValue = ScDPUtil::getNumGroupStartValue(pData->GetValue(), aNumInfo);
                    ScDPItemData aItemData;
                    aItemData.SetRangeStart(fGroupValue);
                    rItems[i] = rCache.GetIdByItemData(nSourceDim, aItemData);
                }
                // else (textual) keep original value
            }
        }

        const ScDPNumGroupInfo* pNumInfo = rCache.GetNumGroupInfo(nColumn);

        if (bDateDim && pNumInfo)
        {
            // This is a date group dimension.
            sal_Int32 nDatePart = rCache.GetGroupType(nColumn);
            const ScDPItemData* pData = rCache.GetItemDataById(nSourceDim, rItems[i]);
            if (pData->GetType() == ScDPItemData::Value)
            {
                SvNumberFormatter* pFormatter = pDoc->GetFormatTable();
                sal_Int32 nPartValue = ScDPUtil::getDatePartValue(
                    pData->GetValue(), pNumInfo, nDatePart, pFormatter);

                ScDPItemData aItem(nDatePart, nPartValue);
                rItems[i] = rCache.GetIdByItemData(nColumn, aItem);
            }
        }

        ++i;
    }
}

bool ScDPGroupTableData::IsBaseForGroup(sal_Int32 nDim) const
{
    return std::any_of(aGroups.begin(), aGroups.end(),
        [&nDim](const ScDPGroupDimension& rDim) { return rDim.GetSourceDim() == nDim; });
}

sal_Int32 ScDPGroupTableData::GetGroupBase(sal_Int32 nGroupDim) const
{
    auto aIter = std::find_if(aGroups.begin(), aGroups.end(),
        [&nGroupDim](const ScDPGroupDimension& rDim) { return rDim.GetGroupDim() == nGroupDim; });
    if (aIter != aGroups.end())
        return aIter->GetSourceDim();

    return -1;      // none
}

bool ScDPGroupTableData::IsNumOrDateGroup(sal_Int32 nDimension) const
{
    // Virtual method from ScDPTableData, used in result data to force text labels.

    if ( nDimension < nSourceCount )
    {
        return pNumGroups[nDimension].GetInfo().mbEnable ||
               pNumGroups[nDimension].IsDateDimension();
    }

    auto aIter = std::find_if(aGroups.begin(), aGroups.end(),
        [&nDimension](const ScDPGroupDimension& rDim) { return rDim.GetGroupDim() == nDimension; });
    if (aIter != aGroups.end())
        return aIter->IsDateDimension();

    return false;
}

bool ScDPGroupTableData::IsInGroup( const ScDPItemData& rGroupData, sal_Int32 nGroupIndex,
                                    const ScDPItemData& rBaseData, sal_Int32 nBaseIndex ) const
{
    auto aIter = std::find_if(aGroups.begin(), aGroups.end(),
        [&nGroupIndex, &nBaseIndex](const ScDPGroupDimension& rDim) {
            return rDim.GetGroupDim() == nGroupIndex && rDim.GetSourceDim() == nBaseIndex; });
    if (aIter != aGroups.end())
    {
        const ScDPGroupDimension& rDim = *aIter;
        if (rDim.IsDateDimension())
        {
            return isDateInGroup(rGroupData, rBaseData);
        }
        else
        {
            // If the item is in a group, only that group is valid.
            // If the item is not in any group, its own name is valid.

            const ScDPGroupItem* pGroup = rDim.GetGroupForData( rBaseData );
            return pGroup ? pGroup->GetName().IsCaseInsEqual( rGroupData ) :
                            rGroupData.IsCaseInsEqual( rBaseData );
        }
    }

    OSL_FAIL("IsInGroup: no group dimension found");
    return true;
}

bool ScDPGroupTableData::HasCommonElement( const ScDPItemData& rFirstData, sal_Int32 nFirstIndex,
                                         const ScDPItemData& rSecondData, sal_Int32 nSecondIndex ) const
{
    const ScDPGroupDimension* pFirstDim = nullptr;
    const ScDPGroupDimension* pSecondDim = nullptr;
    for ( const auto& rDim : aGroups )
    {
        const ScDPGroupDimension* pDim = &rDim;
        if ( pDim->GetGroupDim() == nFirstIndex )
            pFirstDim = pDim;
        else if ( pDim->GetGroupDim() == nSecondIndex )
            pSecondDim = pDim;
    }
    if ( pFirstDim && pSecondDim )
    {
        bool bFirstDate = pFirstDim->IsDateDimension();
        bool bSecondDate = pSecondDim->IsDateDimension();
        if (bFirstDate || bSecondDate)
        {
            // If one is a date group dimension, the other one must be, too.
            if (!bFirstDate || !bSecondDate)
            {
                OSL_FAIL( "mix of date and non-date groups" );
                return true;
            }

            return isDateInGroup(rFirstData, rSecondData);
        }

        const ScDPGroupItem* pFirstItem = pFirstDim->GetGroupForName( rFirstData );
        const ScDPGroupItem* pSecondItem = pSecondDim->GetGroupForName( rSecondData );
        if ( pFirstItem && pSecondItem )
        {
            // two existing groups -> sal_True if they have a common element
            return pFirstItem->HasCommonElement( *pSecondItem );
        }
        else if ( pFirstItem )
        {
            // "automatic" group contains only its own name
            return pFirstItem->HasElement( rSecondData );
        }
        else if ( pSecondItem )
        {
            // "automatic" group contains only its own name
            return pSecondItem->HasElement( rFirstData );
        }
        else
        {
            // no groups -> sal_True if equal
            return rFirstData.IsCaseInsEqual( rSecondData );
        }
    }

    OSL_FAIL("HasCommonElement: no group dimension found");
    return true;
}

sal_Int32 ScDPGroupTableData::GetSourceDim( sal_Int32 nDim )
{
    sal_Int32 nGroupCount = aGroups.size();
    if ( nDim >= nSourceCount && nDim < nSourceCount + nGroupCount )
    {
        // Group dimension — return the original source dimension
        const ScDPGroupDimension& rGroupDim = aGroups[nDim - nSourceCount];
        return  rGroupDim.GetSourceDim();
    }
    // Source columns, calc fields, DataLayout — return as-is.
    // Calc field indices match OOXML numbering (already includes groups).
    return nDim;
}

sal_Int32 ScDPGroupTableData::CalcFieldToCacheIndex( sal_Int32 nDim ) const
{
    sal_Int32 nGroupCount = aGroups.size();
    sal_Int32 nCalcStart = nSourceCount + nGroupCount;
    sal_Int32 nCalcCount = pSourceData->GetCalculatedColumnCount();
    if ( nDim >= nCalcStart && nDim < nCalcStart + nCalcCount )
    {
        // Calc field at proxy position k — look up its actual OOXML field index
        // (mnIndex) from the cache. The proxy layout [source][groups][calc] doesn't
        // necessarily match OOXML ordering where calc fields can appear before groups.
        sal_Int32 k = nDim - nCalcStart;
        const auto& rCalcFields = GetCacheTable().getCache().GetCalculatedFields();
        if (k >= 0 && k < sal::static_int_cast<sal_Int32>(rCalcFields.size()))
            return rCalcFields[k]->mnIndex;
    }
    return nDim;
}

sal_Int32 ScDPGroupTableData::Compare(sal_Int32 nDim, sal_Int32 nDataId1, sal_Int32 nDataId2)
{
    if ( getIsDataLayoutDimension(nDim) )
        return 0;
    // ZetaOffice: calculated items sort after the source items, in the order they were added
    {
        const bool bCalc1 = ZetaIsCalcItem( nDim, nDataId1 );
        const bool bCalc2 = ZetaIsCalcItem( nDim, nDataId2 );
        if ( bCalc1 || bCalc2 )
        {
            if ( bCalc1 && bCalc2 )
                return nDataId1 < nDataId2 ? -1 : ( nDataId1 > nDataId2 ? 1 : 0 );
            return bCalc1 ? 1 : -1;
        }
    }
    const ScDPItemData* rItem1 = GetMemberById(nDim, nDataId1);
    const ScDPItemData* rItem2 = GetMemberById(nDim, nDataId2);
    if (rItem1 == nullptr || rItem2 == nullptr)
        return 0;
    return ScDPItemData::Compare( *rItem1,*rItem2);
}

#if DUMP_PIVOT_TABLE

void ScDPGroupTableData::Dump() const
{
    std::cout << "--- ScDPGroupTableData" << std::endl;
    for (tools::Long i = 0; i < nSourceCount; ++i)
    {
        std::cout << "* dimension: " << i << std::endl;
        const ScDPNumGroupDimension& rGrp = pNumGroups[i];
        rGrp.GetInfo().Dump();
    }
    std::cout << "---" << std::endl;
}
#endif

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
