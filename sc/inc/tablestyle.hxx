/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4; fill-column: 100 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#pragma once

#include <memory>

#include <editeng/boxitem.hxx>
#include <editeng/brushitem.hxx>
#include <tools/json_writer.hxx>
#include "document.hxx"
#include "scdllapi.h"
#include "dbdata.hxx"

class ScPatternAttr;
namespace model
{
class ColorSet;
}

namespace tools
{
class JsonWriter;
}

enum class ScTableStyleElement
{
    WholeTable,
    FirstColumnStripe,
    SecondColumnStripe,
    FirstRowStripe,
    SecondRowStripe,
    LastColumn,
    FirstColumn,
    HeaderRow,
    TotalRow,
    FirstHeaderCell,
    LastHeaderCell,
    // pivot table style elements (OOXML ST_TableStyleType)
    PageFieldLabels,
    PageFieldValues,
    FirstSubtotalColumn,
    SecondSubtotalColumn,
    ThirdSubtotalColumn,
    BlankRow,
    FirstSubtotalRow,
    SecondSubtotalRow,
    ThirdSubtotalRow,
    FirstColumnSubheading,
    SecondColumnSubheading,
    ThirdColumnSubheading,
    FirstRowSubheading,
    SecondRowSubheading,
    ThirdRowSubheading,
};

template <class T> const T* GetItemFromPattern(ScPatternAttr* pPattern, TypedWhichId<T> nWhich)
{
    return pPattern->GetItemSet().GetItemIfSet(nWhich);
}

class SC_DLLPUBLIC ScTableStyle
{
private:
    ScTableStyle(ScTableStyle const&) = delete;
    ScTableStyle(ScTableStyle&&) = delete;
    void operator=(ScTableStyle const&) = delete;
    void operator=(ScTableStyle&&) = delete;

    std::unique_ptr<ScPatternAttr> mpTablePattern;
    std::unique_ptr<ScPatternAttr> mpFirstColumnStripePattern;
    std::unique_ptr<ScPatternAttr> mpSecondColumnStripePattern;
    std::unique_ptr<ScPatternAttr> mpFirstRowStripePattern;
    std::unique_ptr<ScPatternAttr> mpSecondRowStripePattern;
    std::unique_ptr<ScPatternAttr> mpLastColumnPattern;
    std::unique_ptr<ScPatternAttr> mpFirstColumnPattern;
    std::unique_ptr<ScPatternAttr> mpHeaderRowPattern;
    std::unique_ptr<ScPatternAttr> mpTotalRowPattern;
    std::unique_ptr<ScPatternAttr> mpFirstHeaderCellPattern;
    std::unique_ptr<ScPatternAttr> mpLastHeaderCellPattern;
    /// pivot-only elements (page fields, subtotals, subheadings, blank row)
    std::map<ScTableStyleElement, std::unique_ptr<ScPatternAttr>> maPivotPatterns;

    sal_Int32 mnFirstRowStripeSize;
    sal_Int32 mnSecondRowStripeSize;
    sal_Int32 mnFirstColStripeSize;
    sal_Int32 mnSecondColStripeSize;

    OUString maStyleName;
    std::optional<OUString> maUIName;
    bool mbIsOOXMLDefault;
    /// OOXML tableStyle table/pivot attributes: where the style is offered
    bool mbTable;
    bool mbPivot;

public:
    ScTableStyle(const OUString& rName, const std::optional<OUString>& rUIName);

    static bool HasFontAttrSet(const ScPatternAttr* pPattern);
    const SfxItemSet* GetFontItemSet(const ScDBData& rDBData, SCCOL nCol, SCROW nRow,
                                     SCROW nRowIndex) const;
    const SvxBrushItem* GetFillItem(const ScDBData& rDBData, SCCOL nCol, SCROW nRow,
                                    SCROW nRowIndex) const;
    std::unique_ptr<SvxBoxItem> GetBoxItem(const ScDBData& rDBData, SCCOL nCol, SCROW nRow,
                                           SCROW nRowIndex) const;

    void SetRowStripeSize(sal_Int32 nFirstRowStripeSize, sal_Int32 nSecondRowStripeSize);
    void SetColStripeSize(sal_Int32 nFirstColStripeSize, sal_Int32 nSecondColStripeSize);
    /// stripe size of a stripe element (1 for the other elements)
    sal_Int32 GetStripeSize(ScTableStyleElement eElement) const;

    void SetPattern(ScTableStyleElement eTableStyleElement,
                    std::unique_ptr<ScPatternAttr> pPattern);

    std::map<ScTableStyleElement, const ScPatternAttr*> GetSetPatterns() const;

    void SetOOXMLDefault(bool bDefault);
    bool IsOOXMLDefault() const;

    void SetTable(bool bTable) { mbTable = bTable; }
    bool IsTable() const { return mbTable; }
    void SetPivot(bool bPivot) { mbPivot = bPivot; }
    bool IsPivot() const { return mbPivot; }

    /// Re-resolve themed colors in custom style patterns against a new ColorSet
    void UpdateThemedColors(const model::ColorSet& rColorSet);

    const OUString& GetName() const;
    const OUString& GetUIName() const;
};

class SC_DLLPUBLIC ScTableStyles
{
    SfxBindings* mpBindings;

private:
    ScTableStyles(ScTableStyles const&) = delete;
    ScTableStyles(ScTableStyles&&) = delete;
    void operator=(ScTableStyles const&) = delete;
    void operator=(ScTableStyles&&) = delete;

    std::unordered_map<OUString, std::unique_ptr<ScTableStyle>> maTableStyles;

public:
    ScTableStyles(SfxBindings* pBindings);

    void AddTableStyle(std::unique_ptr<ScTableStyle> pTableStyle);
    const ScTableStyle* GetTableStyle(const OUString& rName) const;
    /// add or replace a style with the same name
    void SetTableStyle(std::unique_ptr<ScTableStyle> pTableStyle);
    /// remove a custom style (OOXML built-in styles stay)
    void RemoveTableStyle(const OUString& rName);
    /// styles that are not OOXML built-in ones (custom styles of the document)
    std::vector<const ScTableStyle*> GetCustomTableStyles() const;
    bool HasTableStyle() const { return !maTableStyles.empty(); }

    /// Remove all styles marked as OOXML defaults (for regeneration after theme change)
    void ClearOOXMLDefaultStyles();

    /// Update themed colors in custom (non-default) styles after a theme change
    void UpdateCustomStyleThemedColors(const model::ColorSet& rColorSet);

    void generateJSON(tools::JsonWriter& rWriter) const;
};

/* vim:set shiftwidth=4 softtabstop=4 expandtab cinoptions=b1,g0,N-s cinkeys+=0=break: */
