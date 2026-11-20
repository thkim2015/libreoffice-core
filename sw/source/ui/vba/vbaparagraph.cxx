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
#include "vbaparagraph.hxx"
#include "vbarange.hxx"
#include <com/sun/star/lang/XServiceInfo.hpp>
#include <com/sun/star/beans/Pair.hpp>
#include <com/sun/star/beans/XPropertySet.hpp>
#include <com/sun/star/container/XNameAccess.hpp>
#include <com/sun/star/style/XStyleFamiliesSupplier.hpp>
#include <algorithm>
#include <cmath>
#include <vector>
#include <cppuhelper/implbase.hxx>
#include <utility>
#include <unotxdoc.hxx>

using namespace ::ooo::vba;
using namespace ::com::sun::star;

SwVbaParagraph::SwVbaParagraph( const uno::Reference< ooo::vba::XHelperInterface >& rParent,
                                const uno::Reference< uno::XComponentContext >& rContext,
                                rtl::Reference< SwXTextDocument >  xDocument,
                                uno::Reference< text::XTextRange >  xTextRange ) :
    SwVbaParagraph_BASE( rParent, rContext ), mxTextDocument(std::move( xDocument )), mxTextRange(std::move( xTextRange ))
{
}

SwVbaParagraph::~SwVbaParagraph()
{
}

uno::Reference< word::XRange > SAL_CALL
SwVbaParagraph::getRange( )
{
    return uno::Reference< word::XRange >( new SwVbaRange( this, mxContext, mxTextDocument, mxTextRange->getStart(), mxTextRange->getEnd(), mxTextRange->getText() ) );
}

uno::Any SAL_CALL
SwVbaParagraph::getStyle( )
{
    uno::Reference< word::XRange > xRange = getRange();
    return xRange->getStyle();
}

void SAL_CALL
SwVbaParagraph::setStyle( const uno::Any& style )
{
    uno::Reference< word::XRange > xRange = getRange();
    xRange->setStyle( style );
}

OUString
SwVbaParagraph::getServiceImplName()
{
    return u"SwVbaParagraph"_ustr;
}

uno::Sequence< OUString >
SwVbaParagraph::getServiceNames()
{
    static uno::Sequence< OUString > const aServiceNames
    {
        u"ooo.vba.word.Paragraph"_ustr
    };
    return aServiceNames;
}

namespace {

class ParagraphCollectionHelper : public ::cppu::WeakImplHelper< container::XIndexAccess,
                                                                 container::XEnumerationAccess >
{
private:
    // the document text, or a cursor spanning the paragraphs of the selection (Selection.Paragraphs)
    uno::Reference< container::XEnumerationAccess > mxParEnumAccess;

    /// @throws uno::RuntimeException
    uno::Reference< container::XEnumeration > getEnumeration()
    {
        return mxParEnumAccess->createEnumeration();
    }

public:
    /// @throws uno::RuntimeException
    explicit ParagraphCollectionHelper( uno::Reference< container::XEnumerationAccess > xParEnumAccess ): mxParEnumAccess(std::move( xParEnumAccess ))
    {
    }
    // XElementAccess
    virtual uno::Type SAL_CALL getElementType(  ) override { return  cppu::UnoType<text::XTextRange>::get(); }
    virtual sal_Bool SAL_CALL hasElements(  ) override { return true; }
    // XIndexAccess
    virtual ::sal_Int32 SAL_CALL getCount(  ) override
    {
        sal_Int32 nCount = 0;
        uno::Reference< container::XEnumeration > xParEnum = getEnumeration();
        while( xParEnum->hasMoreElements() )
        {
            uno::Reference< lang::XServiceInfo > xServiceInfo( xParEnum->nextElement(), uno::UNO_QUERY_THROW );
            if( xServiceInfo->supportsService(u"com.sun.star.text.Paragraph"_ustr) )
            {
                nCount++;
            }
        }
        return nCount;
    }
    virtual uno::Any SAL_CALL getByIndex( ::sal_Int32 Index ) override
    {
        if( Index < getCount() )
        {
            sal_Int32 nCount = 0;
            uno::Reference< container::XEnumeration > xParEnum = getEnumeration();
            while( xParEnum->hasMoreElements() )
            {
                uno::Reference< lang::XServiceInfo > xServiceInfo( xParEnum->nextElement(), uno::UNO_QUERY_THROW );
                if( xServiceInfo->supportsService(u"com.sun.star.text.Paragraph"_ustr) )
                {
                    if( Index == nCount )
                        return uno::Any( xServiceInfo );
                    nCount++;
                }
            }
        }
        throw lang::IndexOutOfBoundsException();
    }
    // XEnumerationAccess
    virtual uno::Reference< container::XEnumeration > SAL_CALL createEnumeration(  ) override
    {
        return getEnumeration();
    }
};

}

SwVbaParagraphs::SwVbaParagraphs( const uno::Reference< XHelperInterface >& xParent,
                                const uno::Reference< css::uno::XComponentContext > & xContext,
                                const rtl::Reference< SwXTextDocument >& xDocument )
: SwVbaParagraphs_BASE( xParent, xContext, new ParagraphCollectionHelper( uno::Reference< container::XEnumerationAccess >( xDocument->getText(), uno::UNO_QUERY_THROW ) ) ), mxTextDocument( xDocument )
{
}

SwVbaParagraphs::SwVbaParagraphs( const uno::Reference< XHelperInterface >& xParent,
                                const uno::Reference< css::uno::XComponentContext > & xContext,
                                const rtl::Reference< SwXTextDocument >& xDocument,
                                const uno::Reference< container::XEnumerationAccess >& xParEnumAccess )
: SwVbaParagraphs_BASE( xParent, xContext, new ParagraphCollectionHelper( xParEnumAccess ) ), mxTextDocument( xDocument )
{
}

namespace {

std::vector< uno::Reference< beans::XPropertySet > > lcl_paragraphProps( const uno::Reference< container::XIndexAccess >& xIndexAccess )
{
    std::vector< uno::Reference< beans::XPropertySet > > aParas;
    sal_Int32 nCount = xIndexAccess->getCount();
    for( sal_Int32 i = 0; i < nCount; ++i )
        aParas.emplace_back( xIndexAccess->getByIndex( i ), uno::UNO_QUERY_THROW );
    return aParas;
}

bool lcl_isListParagraph( const uno::Reference< beans::XPropertySet >& xProps )
{
    bool bIsNumber = false;
    OUString sStyle, sListId;
    try
    {
        xProps->getPropertyValue(u"NumberingIsNumber"_ustr) >>= bIsNumber;
        xProps->getPropertyValue(u"NumberingStyleName"_ustr) >>= sStyle;
        xProps->getPropertyValue(u"ListId"_ustr) >>= sListId;
    }
    catch( const uno::Exception& )
    {
        return false;
    }
    return bIsNumber && ( !sStyle.isEmpty() || !sListId.isEmpty() );
}

// Word: Increase/Decrease Indent moves a list paragraph one list level, any other paragraph to the
// next/previous default tab stop
void lcl_indent( const rtl::Reference< SwXTextDocument >& xDoc, const uno::Reference< container::XIndexAccess >& xIndexAccess, int nStep )
{
    sal_Int32 nTab = 1270;
    try
    {
        uno::Reference< beans::XPropertySet > xDefaults( xDoc->createInstance(u"com.sun.star.text.Defaults"_ustr), uno::UNO_QUERY_THROW );
        xDefaults->getPropertyValue(u"TabStopDistance"_ustr) >>= nTab;
    }
    catch( const uno::Exception& ) {}
    if( nTab <= 0 )
        nTab = 1270;
    for( const auto& xProps : lcl_paragraphProps( xIndexAccess ) )
    {
        if( lcl_isListParagraph( xProps ) )
        {
            sal_Int16 nLevel = 0;
            xProps->getPropertyValue(u"NumberingLevel"_ustr) >>= nLevel;
            nLevel = std::clamp< sal_Int16 >( nLevel + nStep, 0, 9 );
            xProps->setPropertyValue(u"NumberingLevel"_ustr, uno::Any( nLevel ) );
            continue;
        }
        sal_Int32 nLeft = 0;
        xProps->getPropertyValue(u"ParaLeftMargin"_ustr) >>= nLeft;
        sal_Int32 n = nStep > 0 ? ( nLeft < 0 ? 0 : nLeft / nTab ) + 1
                                : std::max< sal_Int32 >( 0, ( nLeft + nTab - 1 ) / nTab - 1 );
        xProps->setPropertyValue(u"ParaLeftMargin"_ustr, uno::Any( n * nTab ) );
    }
}

}

void SAL_CALL SwVbaParagraphs::Indent()
{
    lcl_indent( mxTextDocument, m_xIndexAccess, 1 );
}

void SAL_CALL SwVbaParagraphs::Outdent()
{
    lcl_indent( mxTextDocument, m_xIndexAccess, -1 );
}

// Word (East Asian): indents by whole characters of the Normal style; the indent keeps its character unit
// like the Korean Word Increase Indent button (FONT_CJK_ADVANCE = 20)
void SAL_CALL SwVbaParagraphs::IndentCharWidth( sal_Int16 Count )
{
    double fUnit = 11.0 * 2540.0 / 72.0;
    try
    {
        uno::Reference< container::XNameAccess > xFamily( mxTextDocument->getStyleFamilies()->getByName(u"ParagraphStyles"_ustr), uno::UNO_QUERY_THROW );
        uno::Reference< beans::XPropertySet > xStd( xFamily->getByName(u"Standard"_ustr), uno::UNO_QUERY_THROW );
        float fPt = 0;
        xStd->getPropertyValue(u"CharHeightAsian"_ustr) >>= fPt;
        if( fPt > 0 )
            fUnit = fPt * 2540.0 / 72.0;
    }
    catch( const uno::Exception& ) {}
    const double eps = 0.01;
    for( const auto& xProps : lcl_paragraphProps( m_xIndexAccess ) )
    {
        css::beans::Pair< double, sal_Int16 > aUnit( 0, 0 );
        bool bHasUnit = false;
        try { bHasUnit = ( xProps->getPropertyValue(u"ParaLeftMarginUnit"_ustr) >>= aUnit ) && aUnit.Second == 20 && aUnit.First != 0; }
        catch( const uno::Exception& ) {}
        double fChars;
        if( bHasUnit )
            fChars = aUnit.First;
        else
        {
            sal_Int32 nLeft = 0;
            xProps->getPropertyValue(u"ParaLeftMargin"_ustr) >>= nLeft;
            fChars = nLeft / fUnit;
        }
        double n = Count > 0 ? std::floor( fChars + eps ) + Count : std::max( 0.0, std::ceil( fChars - eps ) + Count );
        xProps->setPropertyValue(u"ParaLeftMargin"_ustr, uno::Any( static_cast< sal_Int32 >( std::lround( n * fUnit ) ) ) );
        if( n > 0 || bHasUnit )
        {
            try { xProps->setPropertyValue(u"ParaLeftMarginUnit"_ustr, uno::Any( css::beans::Pair< double, sal_Int16 >( n, 20 ) ) ); }
            catch( const uno::Exception& ) {}
        }
    }
}

// XEnumerationAccess
uno::Type
SwVbaParagraphs::getElementType()
{
    return cppu::UnoType<word::XParagraph>::get();
}
uno::Reference< container::XEnumeration >
SwVbaParagraphs::createEnumeration()
{
    uno::Reference< container::XEnumerationAccess > xEnumerationAccess( m_xIndexAccess, uno::UNO_QUERY_THROW );
    return xEnumerationAccess->createEnumeration();
}

uno::Any
SwVbaParagraphs::createCollectionObject( const css::uno::Any& aSource )
{
    uno::Reference< text::XTextRange > xTextRange( aSource, uno::UNO_QUERY_THROW );
    return uno::Any( uno::Reference< word::XParagraph >( new SwVbaParagraph( this, mxContext, mxTextDocument, xTextRange ) ) );
}

OUString
SwVbaParagraphs::getServiceImplName()
{
    return u"SwVbaParagraphs"_ustr;
}

css::uno::Sequence<OUString>
SwVbaParagraphs::getServiceNames()
{
    static uno::Sequence< OUString > const sNames
    {
        u"ooo.vba.word.Paragraphs"_ustr
    };
    return sNames;
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
