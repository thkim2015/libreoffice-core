/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "ParetoChart.hxx"

#include <ChartType.hxx>
#include <CommonConverters.hxx>
#include <ExplicitCategoriesProvider.hxx>
#include <ObjectIdentifier.hxx>
#include <PropertyMapper.hxx>
#include <ShapeFactory.hxx>
#include <VDataSeries.hxx>

#include <com/sun/star/drawing/FillStyle.hpp>
#include <com/sun/star/drawing/LineStyle.hpp>
#include <svx/unoshape.hxx>

#include <algorithm>
#include <cmath>
#include <limits>

namespace chart
{
using namespace ::com::sun::star;

namespace
{
// Excel separates the bars with a thin white line
constexpr sal_Int32 nBarBorderWidth = 26; // 0.75 pt
constexpr sal_Int32 nBarBorderColor = 0xFFFFFF;
// the cumulative line: Office theme accent 2, 2.25 pt like an Excel line chart
constexpr sal_Int32 nLineColor = 0xED7D31;
constexpr sal_Int32 nLineWidth = 79;
}

ParetoChart::ParetoChart(const rtl::Reference<ChartType>& xChartTypeModel,
                         sal_Int32 nDimensionCount, bool bParetoLineType)
    : VSeriesPlotter(xChartTypeModel, nDimensionCount)
    , m_bParetoLineType(bParetoLineType)
{
    PlotterBase::m_pPosHelper = &m_aMainPosHelper;
    VSeriesPlotter::m_pMainPosHelper = &m_aMainPosHelper;
    // Excel draws Pareto bars without gaps
    m_aMainPosHelper.setOuterDistance(0.0);
    m_aMainPosHelper.setInnerDistance(0.0);
}

ParetoChart::~ParetoChart() {}

drawing::Direction3D ParetoChart::getPreferredDiagramAspectRatio() const
{
    return drawing::Direction3D(-1, -1, -1);
}

LegendSymbolStyle ParetoChart::getLegendSymbolStyle() { return LegendSymbolStyle::Box; }

bool ParetoChart::isSeparateStackingForDifferentSigns(sal_Int32) { return false; }

bool ParetoChart::isExpandWideValuesToZero(sal_Int32 nDimensionIndex)
{
    return nDimensionIndex == 1;
}

void ParetoChart::addSeries(std::unique_ptr<VDataSeries> pSeries, sal_Int32 /*zSlot*/,
                            sal_Int32 /*xSlot*/, sal_Int32 /*ySlot*/)
{
    VSeriesPlotter::addSeries(std::move(pSeries), 0, 0, 0);
}

VDataSeries* ParetoChart::valueSeries() const
{
    for (auto const& rZSlot : m_aZSlots)
        for (auto const& rXSlot : rZSlot)
            for (std::unique_ptr<VDataSeries> const& pSeries : rXSlot.m_aSeriesVector)
                if (pSeries && pSeries->getTotalPointCount() > 0)
                    return pSeries.get();
    return nullptr;
}

bool ParetoChart::isPareto() const
{
    return m_bParetoLineType
           || (m_pExplicitCategoriesProvider && m_pExplicitCategoriesProvider->isParetoOrder());
}

std::vector<sal_Int32> ParetoChart::bars(const VDataSeries& rSeries,
                                         std::vector<double>& rSums) const
{
    const sal_Int32 nPoints = rSeries.getTotalPointCount();
    std::vector<double> aValues(nPoints);
    for (sal_Int32 i = 0; i < nPoints; ++i)
        aValues[i] = rSeries.getYValue(i);
    if (!isPareto())
    {
        // a plain clustered column chart: one bar per point, in order
        std::vector<sal_Int32> aSlots(nPoints);
        rSums.assign(nPoints, 0.0);
        for (sal_Int32 i = 0; i < nPoints; ++i)
        {
            aSlots[i] = i;
            rSums[i] = std::isfinite(aValues[i]) ? aValues[i] : 0.0;
        }
        return aSlots;
    }
    uno::Sequence<OUString> aTexts;
    if (m_pExplicitCategoriesProvider)
        aTexts = m_pExplicitCategoriesProvider->getOriginalCategoryTexts();
    return ExplicitCategoriesProvider::paretoSlots(aTexts, aValues, rSums);
}

double ParetoChart::getMinimumX()
{
    if (!valueSeries())
        return VSeriesPlotter::getMinimumX();
    return 1.0;
}

double ParetoChart::getMaximumX()
{
    VDataSeries* pSeries = valueSeries();
    if (!pSeries)
        return VSeriesPlotter::getMaximumX();
    std::vector<double> aSums;
    bars(*pSeries, aSums);
    return std::max<double>(aSums.size(), 1.0);
}

std::pair<double, double> ParetoChart::getMinimumAndMaximumYInRange(double, double,
                                                                     sal_Int32 nAxisIndex)
{
    constexpr double fNaN = std::numeric_limits<double>::quiet_NaN();
    VDataSeries* pSeries = valueSeries();
    if (!pSeries || pSeries->getAttachedAxisIndex() != nAxisIndex)
        return { fNaN, fNaN };
    std::vector<double> aSums;
    bars(*pSeries, aSums);
    if (aSums.empty())
        return { fNaN, fNaN };
    const auto [itMin, itMax] = std::minmax_element(aSums.begin(), aSums.end());
    return { *itMin, *itMax };
}

void ParetoChart::createShapes()
{
    if (m_aZSlots.empty() || m_nDimension != 2)
        return;
    if (!(m_xLogicTarget.is() && m_xFinalTarget.is()))
        return;
    VDataSeries* pSeries = valueSeries();
    if (!pSeries)
        return; // the empty ParetoLine half of an imported chart

    rtl::Reference<SvxShapeGroupAnyD> xSeriesTarget = createGroupShape(m_xLogicTarget);

    BarPositionHelper* pPosHelper = &m_aMainPosHelper;
    PlotterBase::m_pPosHelper = pPosHelper;
    pPosHelper->updateSeriesCount(1);
    const double fHalfWidth = pPosHelper->getScaledSlotWidth() / 2.0;

    // logic (x, y) to page coordinates; false for values outside the axis range
    auto aToPage = [pPosHelper](double fX, double fY, drawing::Position3D& rOut) {
        double fScaledY = fY;
        pPosHelper->clipLogicValues(nullptr, &fScaledY, nullptr);
        pPosHelper->doLogicScaling(nullptr, &fScaledY, nullptr);
        rOut = pPosHelper->transformScaledLogicToScene(fX, fScaledY, 0, true);
        return isValidPosition(rOut);
    };

    sal_Int32 nColor = 0x4472C4;
    try
    {
        const uno::Reference<beans::XPropertySet>& xSeriesProps = pSeries->getPropertiesOfSeries();
        if (xSeriesProps.is())
            xSeriesProps->getPropertyValue(u"Color"_ustr) >>= nColor;
    }
    catch (const uno::Exception&)
    {
    }

    std::vector<double> aSums;
    const std::vector<sal_Int32> aSlots = bars(*pSeries, aSums);
    const sal_Int32 nBars = aSums.size();
    std::vector<sal_Int32> aFirstPoint(nBars, -1);
    for (size_t i = 0; i < aSlots.size(); ++i)
        if (aSlots[i] >= 0 && aSlots[i] < nBars && aFirstPoint[aSlots[i]] < 0)
            aFirstPoint[aSlots[i]] = i;

    rtl::Reference<SvxShapeGroupAnyD> xSeriesGroup = getSeriesGroupShape(pSeries, xSeriesTarget);
    const double fBase = std::max(0.0, pPosHelper->getLogicMinY());
    for (sal_Int32 b = 0; b < nBars; ++b)
    {
        const double fX = pPosHelper->getScaledSlotPos(b + 1.0, 0.0);
        drawing::Position3D aTopLeft, aBottomRight;
        if (!aToPage(fX - fHalfWidth, aSums[b], aTopLeft)
            || !aToPage(fX + fHalfWidth, fBase, aBottomRight))
            continue;
        rtl::Reference<SvxShapeGroupAnyD> xTarget = xSeriesGroup;
        if (aFirstPoint[b] >= 0)
            xTarget = createGroupShape(
                xSeriesGroup, ObjectIdentifier::createPointCID(pSeries->getPointCID_Stub(), aFirstPoint[b]));
        const sal_Int32 nLeft = static_cast<sal_Int32>(std::min(aTopLeft.PositionX, aBottomRight.PositionX));
        const sal_Int32 nTop = static_cast<sal_Int32>(std::min(aTopLeft.PositionY, aBottomRight.PositionY));
        awt::Size aSize(static_cast<sal_Int32>(std::fabs(aBottomRight.PositionX - aTopLeft.PositionX)),
                        std::max<sal_Int32>(1, static_cast<sal_Int32>(std::fabs(aBottomRight.PositionY - aTopLeft.PositionY))));
        rtl::Reference<SvxShapeRect> xBar = ShapeFactory::createRectangle(
            xTarget, aSize, awt::Point(nLeft, nTop), tNameSequence(), tAnySequence());
        if (!xBar.is())
            continue;
        if (aFirstPoint[b] >= 0 && pSeries->isAttributedDataPoint(aFirstPoint[b]))
            PropertyMapper::setMappedProperties(*xBar, pSeries->getPropertiesOfPoint(aFirstPoint[b]),
                                                PropertyMapper::getPropertyNameMapForFilledSeriesProperties());
        else
        {
            xBar->SvxShape::setPropertyValue(u"FillStyle"_ustr, uno::Any(drawing::FillStyle_SOLID));
            xBar->SvxShape::setPropertyValue(u"FillColor"_ustr, uno::Any(nColor));
            xBar->SvxShape::setPropertyValue(u"LineStyle"_ustr, uno::Any(drawing::LineStyle_SOLID));
            xBar->SvxShape::setPropertyValue(u"LineColor"_ustr, uno::Any(nBarBorderColor));
            xBar->SvxShape::setPropertyValue(u"LineWidth"_ustr, uno::Any(nBarBorderWidth));
        }
    }

    if (!isPareto())
        return;

    // cumulative percentage at the bar centres; 100 % meets the top of the value axis,
    // where the secondary percentage axis (if shown) has its maximum
    double fTotal = 0;
    for (double f : aSums)
        fTotal += f;
    if (!(fTotal > 0))
        return;
    const double fMinY = pPosHelper->getLogicMinY();
    const double fMaxY = pPosHelper->getLogicMaxY();
    std::vector<drawing::Position3D> aPoints;
    double fRunning = 0;
    for (sal_Int32 b = 0; b < nBars; ++b)
    {
        fRunning += aSums[b];
        drawing::Position3D aPos;
        if (aToPage(pPosHelper->getScaledSlotPos(b + 1.0, 0.0),
                    fMinY + (fRunning / fTotal) * (fMaxY - fMinY), aPos))
            aPoints.push_back(aPos);
    }
    if (aPoints.size() < 2)
        return;
    std::vector<std::vector<drawing::Position3D>> aPoly{ std::move(aPoints) };
    rtl::Reference<SvxShapePolyPolygon> xLine = ShapeFactory::createLine2D(xSeriesGroup, aPoly);
    if (xLine.is())
    {
        xLine->SvxShape::setPropertyValue(u"LineStyle"_ustr, uno::Any(drawing::LineStyle_SOLID));
        // the line series of an imported chart carries its color; an inserted
        // chart has no line series and gets Excel's default orange
        sal_Int32 nColor = m_pExplicitCategoriesProvider
                               ? m_pExplicitCategoriesProvider->getParetoLineColor()
                               : -1;
        if (nColor < 0)
            nColor = nLineColor;
        xLine->SvxShape::setPropertyValue(u"LineColor"_ustr, uno::Any(nColor));
        xLine->SvxShape::setPropertyValue(u"LineWidth"_ustr, uno::Any(nLineWidth));
    }
}

} //namespace chart

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
