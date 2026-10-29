/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "SunburstChart.hxx"

#include <CommonConverters.hxx>
#include <ObjectIdentifier.hxx>
#include <PropertyMapper.hxx>
#include <ShapeFactory.hxx>
#include <VDataSeries.hxx>

#include <basegfx/matrix/b2dhommatrix.hxx>
#include <basegfx/matrix/b3dhommatrix.hxx>
#include <basegfx/numeric/ftools.hxx>
#include <com/sun/star/chart2/DataPointLabel.hpp>
#include <com/sun/star/drawing/FillStyle.hpp>
#include <com/sun/star/drawing/LineStyle.hpp>
#include <com/sun/star/drawing/TextHorizontalAdjust.hpp>
#include <com/sun/star/drawing/TextVerticalAdjust.hpp>
#include <com/sun/star/style/ParagraphAdjust.hpp>
#include <svx/unoshape.hxx>

#include <algorithm>
#include <cmath>
#include <functional>

namespace chart
{
using namespace ::com::sun::star;

namespace
{
// Gap between segments (1/100 mm), drawn as a white border
constexpr sal_Int32 nSegmentBorder = 35;
// Room kept around a label inside its segment
constexpr double fLabelPadding = 60.0;
}

SunburstChart::SunburstChart(const rtl::Reference<ChartType>& xChartTypeModel)
    : TreemapChart(xChartTypeModel)
{
}

SunburstChart::~SunburstChart() {}

void SunburstChart::createShapes()
{
    if (!(m_xLogicTarget.is() && m_xFinalTarget.is()))
        return;
    VDataSeries* pSeries = firstSeries();
    if (!pSeries)
        return;

    rtl::Reference<SvxShapeGroupAnyD> xSeriesTarget = createGroupShape(m_xLogicTarget);
    rtl::Reference<SvxShapeGroupAnyD> xTextTarget = ShapeFactory::createGroup2D(m_xFinalTarget);
    rtl::Reference<SvxShapeGroupAnyD> xSeriesGroupShape = getSeriesGroupShape(pSeries, xSeriesTarget);

    // plot area in page coordinates
    const basegfx::B2DRectangle aClip = m_aMainPosHelper.getScaledLogicClipDoubleRect();
    const drawing::Position3D aP1 = m_aMainPosHelper.transformScaledLogicToScene(aClip.getMinX(), aClip.getMinY(), 0.0, false);
    const drawing::Position3D aP2 = m_aMainPosHelper.transformScaledLogicToScene(aClip.getMaxX(), aClip.getMaxY(), 0.0, false);
    const double fW = std::fabs(aP2.PositionX - aP1.PositionX);
    const double fH = std::fabs(aP2.PositionY - aP1.PositionY);
    if (fW <= 0.0 || fH <= 0.0)
        return;
    const double fCenterX = std::min(aP1.PositionX, aP2.PositionX) + fW / 2.0;
    const double fCenterY = std::min(aP1.PositionY, aP2.PositionY) + fH / 2.0;
    const double fRadius = std::min(fW, fH) / 2.0;

    const std::vector<Node> aNodes = buildTree(*pSeries);
    if (aNodes[0].fValue <= 0.0)
        return;

    // depth of the deepest leaf: the rings plus the hole share the radius
    std::function<sal_Int32(size_t)> aDepth = [&](size_t nNode) {
        sal_Int32 nMax = 0;
        for (size_t nChild : aNodes[nNode].aChildren)
            nMax = std::max(nMax, 1 + aDepth(nChild));
        return nMax;
    };
    const sal_Int32 nRings = aDepth(0);
    if (nRings <= 0)
        return;
    const double fRingWidth = fRadius / (nRings + 1);

    bool bHasLabels = false;
    const sal_Int32 nPoints = pSeries->getTotalPointCount();
    for (sal_Int32 i = 0; i < nPoints && !bHasLabels; ++i)
        bHasLabels = pSeries->getDataPointLabelIfLabel(i) != nullptr;

    // unit circle to page coordinates; page y runs down, so angles run
    // clockwise from 3 o'clock (a mirrored matrix breaks segments over 180)
    basegfx::B3DHomMatrix aUnitToPage;
    aUnitToPage.scale(fRadius, fRadius, 1.0);
    aUnitToPage.translate(fCenterX, fCenterY, 0.0);
    const drawing::HomogenMatrix aUnitCircleToScene = B3DHomMatrixToHomogenMatrix(aUnitToPage);

    // fStart and fWidth: degrees clockwise from 12 o'clock
    auto aCreateSegment = [&](const Node& rNode, sal_Int32 nRing, double fStart, double fWidth) {
        const double fInner = (nRing * fRingWidth) / fRadius;
        const double fOuter = ((nRing + 1) * fRingWidth) / fRadius;
        rtl::Reference<SvxShapePolyPolygon> xShape = ShapeFactory::createPieSegment2D(
            xSeriesGroupShape, fStart - 90.0, fWidth, fInner, fOuter,
            drawing::Direction3D(0.0, 0.0, 0.0), aUnitCircleToScene);
        if (!xShape.is())
            return;
        if (rNode.nPoint >= 0)
        {
            uno::Reference<beans::XPropertySet> xPointProperties(pSeries->getPropertiesOfPoint(rNode.nPoint));
            PropertyMapper::setMappedProperties(*xShape, xPointProperties, PropertyMapper::getPropertyNameMapForFilledSeriesProperties());
            ShapeFactory::setShapeName(xShape, ObjectIdentifier::createPointCID(pSeries->getPointCID_Stub(), rNode.nPoint));
        }
        if (rNode.nPoint < 0 || !pSeries->isAttributedDataPoint(rNode.nPoint))
        {
            xShape->SvxShape::setPropertyValue(u"FillStyle"_ustr, uno::Any(drawing::FillStyle_SOLID));
            xShape->SvxShape::setPropertyValue(u"FillColor"_ustr, uno::Any(nodeColor(rNode)));
        }
        xShape->SvxShape::setPropertyValue(u"LineStyle"_ustr, uno::Any(drawing::LineStyle_SOLID));
        xShape->SvxShape::setPropertyValue(u"LineColor"_ustr, uno::Any(sal_Int32(0xFFFFFF)));
        xShape->SvxShape::setPropertyValue(u"LineWidth"_ustr, uno::Any(nSegmentBorder));
    };

    auto aNodeText = [&](const Node& rNode) -> OUString {
        if (rNode.nPoint < 0)
            return rNode.aLabel; // a parent shows its category
        const chart2::DataPointLabel* pLabel = pSeries->getDataPointLabelIfLabel(rNode.nPoint);
        if (!pLabel)
            return OUString();
        OUString aText;
        if (pLabel->ShowCategoryName)
            aText = rNode.aLabel;
        if (pLabel->ShowNumber)
        {
            OUString aNumber = getLabelTextForValue(*pSeries, rNode.nPoint, pSeries->getYValue(rNode.nPoint), false);
            aText = aText.isEmpty() ? aNumber : aText + " " + aNumber;
        }
        return aText;
    };

    // a label along the radius through the middle of the segment, kept upright
    auto aCreateLabel = [&](const Node& rNode, sal_Int32 nRing, double fStart, double fWidth) {
        const OUString aText = aNodeText(rNode);
        if (aText.isEmpty())
            return;
        const double fMid = fStart + fWidth / 2.0;
        const double fMidRadius = (nRing + 0.5) * fRingWidth;
        const double fRad = basegfx::deg2rad(fMid);
        const double fX = fCenterX + fMidRadius * std::sin(fRad);
        const double fY = fCenterY - fMidRadius * std::cos(fRad);
        // clockwise text rotation; the left half reads towards the centre
        const double fTurn = fMid <= 180.0 ? fMid - 90.0 : fMid - 270.0;

        const sal_Int32 nColor = nodeColor(rNode);
        tPropertyNameValueMap aValues;
        const float fHeight = 9.0f;
        aValues[u"CharHeight"_ustr] <<= fHeight;
        aValues[u"CharHeightAsian"_ustr] <<= fHeight;
        aValues[u"CharHeightComplex"_ustr] <<= fHeight;
        aValues[u"CharColor"_ustr] <<= sal_Int32(isDarkColor(nColor) ? 0xFFFFFF : 0x000000);
        aValues[u"FillStyle"_ustr] <<= drawing::FillStyle_NONE;
        aValues[u"LineStyle"_ustr] <<= drawing::LineStyle_NONE;
        aValues[u"ParaAdjust"_ustr] <<= style::ParagraphAdjust_CENTER;
        aValues[u"TextHorizontalAdjust"_ustr] <<= drawing::TextHorizontalAdjust_CENTER;
        aValues[u"TextVerticalAdjust"_ustr] <<= drawing::TextVerticalAdjust_CENTER;
        aValues[u"TextAutoGrowHeight"_ustr] <<= true;
        aValues[u"TextAutoGrowWidth"_ustr] <<= true;
        aValues[u"TextWordWrap"_ustr] <<= false;
        aValues[u"TextLeftDistance"_ustr] <<= sal_Int32(0);
        aValues[u"TextRightDistance"_ustr] <<= sal_Int32(0);
        aValues[u"TextUpperDistance"_ustr] <<= sal_Int32(0);
        aValues[u"TextLowerDistance"_ustr] <<= sal_Int32(0);
        tNameSequence aNames;
        tAnySequence aAnys;
        PropertyMapper::getMultiPropertyListsFromValueMap(aNames, aAnys, aValues);
        rtl::Reference<SvxShapeText> xText = ShapeFactory::createText(
            xTextTarget, aText, aNames, aAnys,
            ShapeFactory::makeTransformation(awt::Point(static_cast<sal_Int32>(fX), static_cast<sal_Int32>(fY)),
                                             basegfx::deg2rad(fTurn)));
        if (!xText.is())
            return;
        // drop a label that does not fit: its length runs along the radius,
        // its height across the segment (the chord at the middle radius)
        const awt::Size aSize = xText->getSize();
        const double fChord = fWidth >= 180.0 ? 2.0 * fMidRadius
                                              : 2.0 * fMidRadius * std::sin(basegfx::deg2rad(fWidth / 2.0));
        if (aSize.Width > fRingWidth - fLabelPadding || aSize.Height > fChord - fLabelPadding)
            xTextTarget->remove(uno::Reference<drawing::XShape>(xText));
    };

    // parent labels are part of the hierarchy, so they show with the leaf labels
    std::function<void(size_t, sal_Int32, double, double)> aLayout
        = [&](size_t nNode, sal_Int32 nRing, double fStart, double fWidth) {
              std::vector<size_t> aChildren = aNodes[nNode].aChildren;
              // Excel: largest first; equal values keep the later data row first
              std::sort(aChildren.begin(), aChildren.end(), [&](size_t a, size_t b) {
                  if (aNodes[a].fValue != aNodes[b].fValue)
                      return aNodes[a].fValue > aNodes[b].fValue;
                  return a > b;
              });
              double fPos = fStart;
              for (size_t nChild : aChildren)
              {
                  const double fChildWidth = fWidth * aNodes[nChild].fValue / aNodes[nNode].fValue;
                  aCreateSegment(aNodes[nChild], nRing, fPos, fChildWidth);
                  if (bHasLabels)
                      aCreateLabel(aNodes[nChild], nRing, fPos, fChildWidth);
                  aLayout(nChild, nRing + 1, fPos, fChildWidth);
                  fPos += fChildWidth;
              }
          };
    aLayout(0, 1, 0.0, 360.0);
}

} // namespace chart

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
