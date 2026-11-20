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

#include "vbafont.hxx"
#include <com/sun/star/awt/FontUnderline.hpp>
#include <com/sun/star/beans/XPropertySet.hpp>
#include <com/sun/star/container/XIndexAccess.hpp>
#include <ooo/vba/word/WdUnderline.hpp>
#include <sal/macros.h>
#include <com/sun/star/container/XEnumerationAccess.hpp>
#include <com/sun/star/text/XTextRange.hpp>
#include <ooo/vba/word/WdConstants.hpp>
#include <algorithm>
#include <cmath>
#include <iterator>
#include <unordered_map>

using namespace ::ooo::vba;
using namespace ::com::sun::star;

const uno::Any aLongAnyTrue( sal_Int16(-1) );
const uno::Any aLongAnyFalse( sal_Int16( 0 ) );

namespace {

struct MapPair
{
    sal_Int32 nMSOConst;
    sal_Int32 nOOOConst;
};

}

MapPair const UnderLineTable[] = {
        { word::WdUnderline::wdUnderlineNone, css::awt::FontUnderline::NONE },
        { word::WdUnderline::wdUnderlineSingle, css::awt::FontUnderline::SINGLE },
        { word::WdUnderline::wdUnderlineWords, css::awt::FontUnderline::SINGLE },
        { word::WdUnderline::wdUnderlineDouble, css::awt::FontUnderline::DOUBLE },
        { word::WdUnderline::wdUnderlineDotted, css::awt::FontUnderline::DOTTED },
        { word::WdUnderline::wdUnderlineThick, css::awt::FontUnderline::BOLDDASH },
        { word::WdUnderline::wdUnderlineDash, css::awt::FontUnderline::DASH },
        { word::WdUnderline::wdUnderlineDotDash, css::awt::FontUnderline::DASHDOT },
        { word::WdUnderline::wdUnderlineDotDotDash, css::awt::FontUnderline::DASHDOTDOT },
        { word::WdUnderline::wdUnderlineWavy, css::awt::FontUnderline::WAVE },
        { word::WdUnderline::wdUnderlineDottedHeavy, css::awt::FontUnderline::BOLDDOTTED },
        { word::WdUnderline::wdUnderlineDashHeavy, css::awt::FontUnderline::BOLDDASH },
        { word::WdUnderline::wdUnderlineDotDashHeavy, css::awt::FontUnderline::BOLDDASHDOT },
        { word::WdUnderline::wdUnderlineDotDotDashHeavy, css::awt::FontUnderline::BOLDDASHDOTDOT },
        { word::WdUnderline::wdUnderlineWavyHeavy, css::awt::FontUnderline::BOLDWAVE },
        { word::WdUnderline::wdUnderlineDashLong, css::awt::FontUnderline::LONGDASH },
        { word::WdUnderline::wdUnderlineWavyDouble, css::awt::FontUnderline::DOUBLEWAVE },
        { word::WdUnderline::wdUnderlineDashLongHeavy, css::awt::FontUnderline::BOLDLONGDASH },
};

typedef std::unordered_map< sal_Int32, sal_Int32 > ConstToConst;

namespace {

class UnderLineMapper
{
    ConstToConst m_MSO2OOO;
    ConstToConst m_OOO2MSO;
private:
    UnderLineMapper()
    {
        for ( auto const & index: UnderLineTable )
        {
            m_MSO2OOO[ index.nMSOConst ] = index.nOOOConst;
            m_OOO2MSO[ index.nOOOConst ] = index.nMSOConst;
        }
    }
public:
    static OUString propName()
    {
        return u"CharUnderline"_ustr;
    }

    static UnderLineMapper& instance()
    {
        static  UnderLineMapper theMapper;
        return theMapper;
    }

    /// @throws lang::IllegalArgumentException
    sal_Int32 getOOOFromMSO( sal_Int32 nMSOConst )
    {
        ConstToConst::iterator it = m_MSO2OOO.find( nMSOConst );
        if ( it == m_MSO2OOO.end() )
            throw lang::IllegalArgumentException();
        return it->second;
    }
    /// @throws lang::IllegalArgumentException
    sal_Int32 getMSOFromOOO( sal_Int32 nOOOConst )
    {
        ConstToConst::iterator it = m_OOO2MSO.find( nOOOConst );
        if ( it == m_OOO2MSO.end() )
            throw lang::IllegalArgumentException();
        return it->second;
    }
};

}

SwVbaFont::SwVbaFont( const uno::Reference< XHelperInterface >& xParent, const uno::Reference< uno::XComponentContext >& xContext, const uno::Reference< container::XIndexAccess >& xPalette, uno::Reference< css::beans::XPropertySet > const & xPropertySet )
    : SwVbaFont_BASE( xParent, xContext, xPalette, xPropertySet, Component::WORD )
{
}

uno::Any SAL_CALL
SwVbaFont::getUnderline()
{
    sal_Int32 nOOVal = 0;
    mxFont->getPropertyValue(  UnderLineMapper::propName() ) >>= nOOVal;
    return uno::Any( UnderLineMapper::instance().getMSOFromOOO( nOOVal ) );
}

void SAL_CALL
SwVbaFont::setUnderline( const uno::Any& _underline )
{
    sal_Int32 nMSOVal = 0;

    if ( _underline >>= nMSOVal )
    {
        sal_Int32 nOOVal =  UnderLineMapper::instance().getOOOFromMSO( nMSOVal );
        mxFont->setPropertyValue(  UnderLineMapper::propName(), uno::Any( nOOVal ) );
    }
}

OUString
SwVbaFont::getServiceImplName()
{
    return u"SwVbaFont"_ustr;
}

uno::Any SAL_CALL
SwVbaFont::getColorIndex()
{
    sal_Int32 nColor = 0;

    getColor() >>= nColor;
    sal_Int32 nElems = mxPalette->getCount();
    sal_Int32 nIndex = 0;
    for ( sal_Int32 count=0; count<nElems; ++count )
    {
        sal_Int32 nPaletteColor = 0;
        mxPalette->getByIndex( count ) >>= nPaletteColor;
        if ( nPaletteColor == nColor )
        {
            nIndex = count;
            break;
        }
    }
    return uno::Any( nIndex );
}
namespace
{
// Word Grow Font / Shrink Font steps (as the web office ribbon: 8..72 list, below 8 by 1, above 72 by 10)
float lcl_WordFontStep(float fSize, bool bGrow)
{
    static constexpr float aSteps[] = { 8, 9, 10, 11, 12, 14, 16, 18, 20, 22, 24, 26, 28, 36, 48, 72 };
    if (!(fSize > 0))
        return 0;
    if (bGrow)
    {
        if (fSize < 8)
            return std::min(8.0f, std::floor(fSize) + 1);
        if (fSize >= 72)
            return std::min(1638.0f, std::floor(fSize / 10) * 10 + 10);
        for (float f : aSteps)
            if (f > fSize)
                return f;
        return fSize;
    }
    if (fSize <= 8)
        return std::max(1.0f, std::ceil(fSize) - 1);
    if (fSize > 72)
        return std::max(72.0f, std::ceil(fSize / 10) * 10 - 10);
    for (auto it = std::rbegin(aSteps); it != std::rend(aSteps); ++it)
        if (*it < fSize)
            return *it;
    return fSize;
}

void lcl_StepHeights(const uno::Reference<beans::XPropertySet>& xProps, bool bGrow)
{
    for (const OUString& sName : { u"CharHeight"_ustr, u"CharHeightAsian"_ustr, u"CharHeightComplex"_ustr })
    {
        float fSize = 0;
        if (!(xProps->getPropertyValue(sName) >>= fSize))
            continue;
        float fNext = lcl_WordFontStep(fSize, bGrow);
        if (fNext > 0 && fNext != fSize)
            xProps->setPropertyValue(sName, uno::Any(fNext));
    }
}

// each text portion of the range by its own size (Word steps mixed sizes run by run)
void lcl_StepFont(const uno::Reference<beans::XPropertySet>& xRange, bool bGrow)
{
    bool bDone = false;
    uno::Reference<container::XEnumerationAccess> xParas(xRange, uno::UNO_QUERY);
    uno::Reference<text::XTextRange> xRangeText(xRange, uno::UNO_QUERY);
    if (xParas.is() && xRangeText.is() && !xRangeText->getString().isEmpty())
    {
        uno::Reference<container::XEnumeration> xParaEnum = xParas->createEnumeration();
        while (xParaEnum->hasMoreElements())
        {
            uno::Reference<container::XEnumerationAccess> xPortions(xParaEnum->nextElement(), uno::UNO_QUERY);
            if (!xPortions.is())
                continue;
            uno::Reference<container::XEnumeration> xEnum = xPortions->createEnumeration();
            while (xEnum->hasMoreElements())
            {
                uno::Reference<beans::XPropertySet> xPortion(xEnum->nextElement(), uno::UNO_QUERY);
                uno::Reference<text::XTextRange> xText(xPortion, uno::UNO_QUERY);
                if (!xPortion.is() || !xText.is() || xText->getString().isEmpty())
                    continue;
                lcl_StepHeights(xPortion, bGrow);
                bDone = true;
            }
        }
    }
    if (!bDone)
        lcl_StepHeights(xRange, bGrow);
}

bool lcl_IsToggle(const uno::Any& aValue)
{
    sal_Int32 nValue = 0;
    return aValue.getValueTypeClass() != uno::TypeClass_BOOLEAN && (aValue >>= nValue) && nValue == word::WdConstants::wdToggle;
}
}

void SAL_CALL SwVbaFont::Grow()
{
    lcl_StepFont(mxFont, true);
}

void SAL_CALL SwVbaFont::Shrink()
{
    lcl_StepFont(mxFont, false);
}

// Word records Font.Superscript/Subscript = wdToggle for the ribbon buttons
void SAL_CALL SwVbaFont::setSuperscript( const uno::Any& aValue )
{
    if (lcl_IsToggle(aValue))
    {
        bool bOn = false;
        SwVbaFont_BASE::getSuperscript() >>= bOn;
        SwVbaFont_BASE::setSuperscript(uno::Any(!bOn));
        return;
    }
    SwVbaFont_BASE::setSuperscript(aValue);
}

void SAL_CALL SwVbaFont::setSubscript( const uno::Any& aValue )
{
    if (lcl_IsToggle(aValue))
    {
        bool bOn = false;
        SwVbaFont_BASE::getSubscript() >>= bOn;
        SwVbaFont_BASE::setSubscript(uno::Any(!bOn));
        return;
    }
    SwVbaFont_BASE::setSubscript(aValue);
}

uno::Any SAL_CALL
SwVbaFont::getSubscript()
{
    bool bRes = false;
    SwVbaFont_BASE::getSubscript() >>= bRes;
    if ( bRes )
        return aLongAnyTrue;
    return aLongAnyFalse;
}

uno::Any SAL_CALL
SwVbaFont::getSuperscript()
{
    bool bRes = false;
    SwVbaFont_BASE::getSuperscript() >>= bRes;
    if ( bRes )
        return aLongAnyTrue;
    return aLongAnyFalse;
}

uno::Any SAL_CALL
SwVbaFont::getBold()
{
    bool bRes = false;
    SwVbaFont_BASE::getBold() >>= bRes;
    if ( bRes )
        return aLongAnyTrue;
    return aLongAnyFalse;
}

uno::Any SAL_CALL
SwVbaFont::getItalic()
{
    bool bRes = false;
    SwVbaFont_BASE::getItalic() >>= bRes;
    if ( bRes )
        return aLongAnyTrue;
    return aLongAnyFalse;
}

uno::Any SAL_CALL
SwVbaFont::getStrikethrough()
{
    bool bRes = false;
    SwVbaFont_BASE::getStrikethrough() >>= bRes;
    if ( bRes )
        return aLongAnyTrue;
    return aLongAnyFalse;
}

uno::Any SAL_CALL
SwVbaFont::getShadow()
{
    bool bRes = false;
    SwVbaFont_BASE::getShadow() >>= bRes;
    if ( bRes )
        return aLongAnyTrue;
    return aLongAnyFalse;
}

uno::Sequence< OUString >
SwVbaFont::getServiceNames()
{
        static uno::Sequence< OUString > const aServiceNames
        {
            u"ooo.vba.word.Font"_ustr
        };
        return aServiceNames;
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
