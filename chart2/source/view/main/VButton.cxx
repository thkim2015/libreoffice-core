/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "VButton.hxx"

#include <ShapeFactory.hxx>
#include <com/sun/star/drawing/FillStyle.hpp>
#include <com/sun/star/drawing/LineStyle.hpp>
#include <com/sun/star/style/ParagraphAdjust.hpp>
#include <com/sun/star/drawing/TextVerticalAdjust.hpp>
#include <com/sun/star/drawing/TextHorizontalAdjust.hpp>

#include <CommonConverters.hxx>
#include <editeng/unoprnms.hxx>
#include <svx/svdotext.hxx>
#include <svx/unoshape.hxx>
#include <vcl/font.hxx>
#include <vcl/virdev.hxx>

namespace chart
{
using namespace css;

VButton::VButton()
    : m_bShowArrow(true)
    , m_nArrowColor(0x00000000)
    , m_nBGColor(0x00E6E6E6)
    , m_nLeftMargin(100)
    , m_nRightMargin(-1)
{
}

void VButton::init(const rtl::Reference<SvxShapeGroupAnyD>& xTargetPage)
{
    m_xTarget = xTargetPage;
}

rtl::Reference<SvxShapePolyPolygon> VButton::createTriangle(awt::Size aSize)
{
    rtl::Reference<SvxShapePolyPolygon> xShape = new SvxShapePolyPolygon(nullptr);
    xShape->setShapeKind(SdrObjKind::Polygon);

    drawing::PolyPolygonShape3D aPolyPolygon;
    aPolyPolygon.SequenceX.realloc(1);
    aPolyPolygon.SequenceY.realloc(1);
    aPolyPolygon.SequenceZ.realloc(1);

    drawing::DoubleSequence* pOuterSequenceX = aPolyPolygon.SequenceX.getArray();
    drawing::DoubleSequence* pOuterSequenceY = aPolyPolygon.SequenceY.getArray();
    drawing::DoubleSequence* pOuterSequenceZ = aPolyPolygon.SequenceZ.getArray();

    pOuterSequenceX->realloc(3);
    pOuterSequenceY->realloc(3);
    pOuterSequenceZ->realloc(3);

    double* pInnerSequenceX = pOuterSequenceX->getArray();
    double* pInnerSequenceY = pOuterSequenceY->getArray();
    double* pInnerSequenceZ = pOuterSequenceZ->getArray();

    pInnerSequenceX[0] = 0.0;
    pInnerSequenceY[0] = 0.0;
    pInnerSequenceZ[0] = 0.0;

    pInnerSequenceX[1] = aSize.Width / 2.0;
    pInnerSequenceY[1] = aSize.Height;
    pInnerSequenceZ[1] = 0.0;

    pInnerSequenceX[2] = aSize.Width;
    pInnerSequenceY[2] = 0.0;
    pInnerSequenceZ[2] = 0.0;

    xShape->SvxShape::setPropertyValue(u"Name"_ustr, uno::Any(m_sCID));
    xShape->SvxShape::setPropertyValue(UNO_NAME_POLYPOLYGON,
                                       uno::Any(PolyToPointSequence(aPolyPolygon)));
    xShape->SvxShape::setPropertyValue(u"LineStyle"_ustr, uno::Any(drawing::LineStyle_NONE));
    xShape->SvxShape::setPropertyValue(u"FillColor"_ustr, uno::Any(m_nArrowColor));

    return xShape;
}

void VButton::createShapes(const uno::Reference<beans::XPropertySet>& xTextProp)
{
    tNameSequence aPropNames;
    tAnySequence aPropValues;

    PropertyMapper::getTextLabelMultiPropertyLists(xTextProp, aPropNames, aPropValues);

    m_xShape = ShapeFactory::createGroup2D(m_xTarget, m_sCID);
    m_xShape->setPosition(m_aPosition);
    m_xShape->setSize(m_aSize);

    rtl::Reference<SvxShapeGroupAnyD> xContainer = m_xShape;

    tPropertyNameValueMap aTextValueMap;
    aTextValueMap[u"CharHeight"_ustr] <<= 10.0f;
    aTextValueMap[u"CharHeightAsian"_ustr] <<= 10.0f;
    aTextValueMap[u"CharHeightComplex"_ustr] <<= 10.0f;
    aTextValueMap[u"FillColor"_ustr] <<= m_nBGColor;
    aTextValueMap[u"FillStyle"_ustr] <<= drawing::FillStyle_SOLID;
    aTextValueMap[u"LineColor"_ustr] <<= sal_Int32(0xcccccc);
    aTextValueMap[u"LineStyle"_ustr] <<= drawing::LineStyle_SOLID;
    aTextValueMap[u"ParaAdjust"_ustr] <<= style::ParagraphAdjust_CENTER;
    aTextValueMap[u"TextHorizontalAdjust"_ustr] <<= drawing::TextHorizontalAdjust_LEFT;
    aTextValueMap[u"TextVerticalAdjust"_ustr] <<= drawing::TextVerticalAdjust_CENTER;
    // a field button label stays on one line (Excel)
    aTextValueMap[u"TextWordWrap"_ustr] <<= false;
    // the paragraph margins are the only inner space (no text frame distances)
    aTextValueMap[u"TextLeftDistance"_ustr] <<= sal_Int32(0);
    aTextValueMap[u"TextRightDistance"_ustr] <<= sal_Int32(0);
    aTextValueMap[u"ParaLeftMargin"_ustr] <<= m_nLeftMargin;
    // room for the drop-down arrow only when there is one
    aTextValueMap[u"ParaRightMargin"_ustr]
        <<= m_nRightMargin >= 0 ? m_nRightMargin : sal_Int32(m_bShowArrow ? 600 : 100);

    aTextValueMap[u"Name"_ustr] <<= m_sCID; //CID OUString

    PropertyMapper::getMultiPropertyListsFromValueMap(aPropNames, aPropValues, aTextValueMap);

    rtl::Reference<SvxShapeText> xEntry
        = ShapeFactory::createText(xContainer, m_sLabel, aPropNames, aPropValues, uno::Any());

    if (xEntry.is())
    {
        xEntry->setPosition(m_aPosition);
        xEntry->setSize(m_aSize);
    }

    if (!m_bShowArrow)
        return;

    awt::Size aPolySize{ 280, 180 };

    rtl::Reference<SvxShapePolyPolygon> xPoly = createTriangle(aPolySize);
    xPoly->setSize(aPolySize);
    xPoly->setPosition(
        { sal_Int32(m_aPosition.X + m_aSize.Width - aPolySize.Width - 100),
          sal_Int32(m_aPosition.Y + (m_aSize.Height / 2.0) - (aPolySize.Height / 2.0)) });
    xContainer->add(xPoly);
}

sal_Int32 VButton::measureLabel(const uno::Reference<beans::XPropertySet>& xTextProp) const
{
    if (m_sLabel.isEmpty())
        return 0;
    // Measure with the draw outliner of a text shape carrying the same text
    // properties as the drawn label, so font fallback, the script of weak
    // letters (spaces, colon) and the reference device are the drawn ones.
    // The shape of a fresh page group does not grow by itself here, so ask
    // the text object for its auto-grown width explicitly.
    if (m_xTarget.is())
    {
        tNameSequence aPropNames;
        tAnySequence aPropValues;
        PropertyMapper::getTextLabelMultiPropertyLists(xTextProp, aPropNames, aPropValues);
        tPropertyNameValueMap aTextValueMap;
        aTextValueMap[u"CharHeight"_ustr] <<= 10.0f;
        aTextValueMap[u"CharHeightAsian"_ustr] <<= 10.0f;
        aTextValueMap[u"CharHeightComplex"_ustr] <<= 10.0f;
        aTextValueMap[u"TextAutoGrowWidth"_ustr] <<= true;
        aTextValueMap[u"TextAutoGrowHeight"_ustr] <<= true;
        aTextValueMap[u"TextMaximumFrameWidth"_ustr] <<= sal_Int32(100000);
        aTextValueMap[u"TextLeftDistance"_ustr] <<= sal_Int32(0);
        aTextValueMap[u"TextRightDistance"_ustr] <<= sal_Int32(0);
        PropertyMapper::getMultiPropertyListsFromValueMap(aPropNames, aPropValues, aTextValueMap);
        rtl::Reference<SvxShapeText> xText
            = ShapeFactory::createText(m_xTarget, m_sLabel, aPropNames, aPropValues, uno::Any());
        if (xText.is())
        {
            sal_Int32 nWidth = 0;
            if (const SdrTextObj* pTextObj = DynCastSdrTextObj(xText->GetSdrObject()))
            {
                tools::Rectangle aRect(Point(0, 0), Size(2, 2));
                if (pTextObj->AdjustTextFrameWidthAndHeight(aRect, false, true))
                    nWidth = aRect.GetWidth();
            }
            m_xTarget->remove(xText);
            if (nWidth > 0)
                return nWidth;
        }
    }
    // fallback: the chart text fonts on a virtual device
    // Asian letters use the Asian font, the rest (spaces, colon, Latin) the
    // Western one, like the drawn label - measure each run with its own font
    OUString aWestern, aAsian;
    try
    {
        if (xTextProp.is())
        {
            xTextProp->getPropertyValue(u"CharFontName"_ustr) >>= aWestern;
            xTextProp->getPropertyValue(u"CharFontNameAsian"_ustr) >>= aAsian;
        }
    }
    catch (const uno::Exception&)
    {
    }
    ScopedVclPtrInstance<VirtualDevice> pDev;
    pDev->SetMapMode(MapMode(MapUnit::Map100thMM));
    const vcl::Font aWesternFont(aWestern, Size(0, 353)); // 10pt
    const vcl::Font aAsianFont(aAsian, Size(0, 353));
    sal_Int32 nWidth = 0;
    sal_Int32 nStart = 0;
    const sal_Int32 nLen = m_sLabel.getLength();
    while (nStart < nLen)
    {
        const bool bAsian = m_sLabel[nStart] >= 0x1100;
        sal_Int32 nEnd = nStart + 1;
        while (nEnd < nLen && (m_sLabel[nEnd] >= 0x1100) == bAsian)
            ++nEnd;
        pDev->SetFont(bAsian ? aAsianFont : aWesternFont);
        nWidth += pDev->GetTextWidth(m_sLabel.copy(nStart, nEnd - nStart));
        nStart = nEnd;
    }
    return nWidth;
}

} //namespace chart

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
