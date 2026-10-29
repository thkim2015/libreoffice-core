/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "BoxWhiskerChart.hxx"

#include <ChartType.hxx>
#include <CommonConverters.hxx>
#include <ExplicitCategoriesProvider.hxx>
#include <ObjectIdentifier.hxx>
#include <PropertyMapper.hxx>
#include <ShapeFactory.hxx>
#include <VDataSeries.hxx>

#include <com/sun/star/drawing/FillStyle.hpp>
#include <com/sun/star/drawing/LineStyle.hpp>
#include <comphelper/diagnose_ex.hxx>
#include <svx/unoshape.hxx>

#include <algorithm>
#include <cmath>
#include <limits>

namespace chart
{
using namespace ::com::sun::star;

namespace
{
// Excel draws box borders, whiskers, the median and the mean marker in the
// series colour darkened to 80 %
sal_Int32 lcl_darker(sal_Int32 nColor)
{
    auto aPart = [nColor](int nShift) {
        return static_cast<sal_Int32>(std::lround(((nColor >> nShift) & 0xFF) * 0.8)) << nShift;
    };
    return aPart(16) | aPart(8) | aPart(0);
}

// 0.75 pt, the Excel default line
constexpr sal_Int32 nLineWidth = 26;

// value at fraction fP of the sorted values, QUARTILE.EXC or QUARTILE.INC style
double lcl_quantile(const std::vector<double>& rValues, double fP, bool bExclusive)
{
    const size_t n = rValues.size();
    if (n == 1)
        return rValues[0];
    double fPos = bExclusive ? fP * (n + 1) - 1.0 : fP * (n - 1);
    fPos = std::clamp(fPos, 0.0, static_cast<double>(n - 1));
    const size_t nLow = static_cast<size_t>(std::floor(fPos));
    const size_t nHigh = std::min(nLow + 1, n - 1);
    return rValues[nLow] + (fPos - nLow) * (rValues[nHigh] - rValues[nLow]);
}

bool lcl_getBool(const uno::Reference<beans::XPropertySet>& xProps, const OUString& rName,
                 bool bDefault)
{
    bool bValue = bDefault;
    try
    {
        if (xProps.is())
            xProps->getPropertyValue(rName) >>= bValue;
    }
    catch (const uno::Exception&)
    {
    }
    return bValue;
}

void lcl_setLine(SvxShape& rShape, sal_Int32 nColor)
{
    rShape.SvxShape::setPropertyValue(u"LineStyle"_ustr, uno::Any(drawing::LineStyle_SOLID));
    rShape.SvxShape::setPropertyValue(u"LineColor"_ustr, uno::Any(nColor));
    rShape.SvxShape::setPropertyValue(u"LineWidth"_ustr, uno::Any(nLineWidth));
}
}

BoxWhiskerChart::BoxWhiskerChart(const rtl::Reference<ChartType>& xChartTypeModel,
                                 sal_Int32 nDimensionCount)
    : VSeriesPlotter(xChartTypeModel, nDimensionCount)
{
    PlotterBase::m_pPosHelper = &m_aMainPosHelper;
    VSeriesPlotter::m_pMainPosHelper = &m_aMainPosHelper;
    try
    {
        if (m_xChartTypeModel.is())
        {
            uno::Sequence<sal_Int32> aGap, aOverlap;
            m_xChartTypeModel->getPropertyValue(u"GapwidthSequence"_ustr) >>= aGap;
            m_xChartTypeModel->getPropertyValue(u"OverlapSequence"_ustr) >>= aOverlap;
            if (aGap.hasElements())
                m_aMainPosHelper.setOuterDistance(aGap[0] / 100.0);
            if (aOverlap.hasElements())
                m_aMainPosHelper.setInnerDistance(-aOverlap[0] / 100.0);
        }
    }
    catch (const uno::Exception&)
    {
        TOOLS_WARN_EXCEPTION("chart2", "");
    }
}

BoxWhiskerChart::~BoxWhiskerChart() {}

drawing::Direction3D BoxWhiskerChart::getPreferredDiagramAspectRatio() const
{
    return drawing::Direction3D(-1, -1, -1);
}

LegendSymbolStyle BoxWhiskerChart::getLegendSymbolStyle() { return LegendSymbolStyle::Box; }

bool BoxWhiskerChart::isSeparateStackingForDifferentSigns(sal_Int32) { return false; }

bool BoxWhiskerChart::isExpandWideValuesToZero(sal_Int32 nDimensionIndex)
{
    return nDimensionIndex == 1;
}

void BoxWhiskerChart::addSeries(std::unique_ptr<VDataSeries> pSeries, sal_Int32 /*zSlot*/,
                                sal_Int32 /*xSlot*/, sal_Int32 /*ySlot*/)
{
    // one z slot; every series gets its own x slot so the boxes stand side by side
    VSeriesPlotter::addSeries(std::move(pSeries), 0, -1, 0);
}

std::vector<sal_Int32> BoxWhiskerChart::pointGroups(sal_Int32 nPointCount,
                                                    sal_Int32& rnGroupCount) const
{
    uno::Sequence<OUString> aTexts;
    if (m_pExplicitCategoriesProvider)
        aTexts = m_pExplicitCategoriesProvider->getOriginalCategoryTexts();
    std::vector<sal_Int32> aGroups;
    if (aTexts.hasElements())
        aGroups = ExplicitCategoriesProvider::groupDuplicateCategories(aTexts, rnGroupCount);
    else
        rnGroupCount = nPointCount > 0 ? 1 : 0; // no categories: one box per series
    // points past the category range go to the last box
    aGroups.resize(nPointCount, aGroups.empty() ? 0 : aGroups.back());
    return aGroups;
}

double BoxWhiskerChart::getMinimumX() { return 1.0; }

double BoxWhiskerChart::getMaximumX()
{
    sal_Int32 nPoints = 0;
    for (auto const& rZSlot : m_aZSlots)
        for (auto const& rXSlot : rZSlot)
            for (std::unique_ptr<VDataSeries> const& pSeries : rXSlot.m_aSeriesVector)
                nPoints = std::max(nPoints, pSeries->getTotalPointCount());
    sal_Int32 nGroups = 0;
    pointGroups(nPoints, nGroups);
    return std::max<sal_Int32>(nGroups, 1);
}

std::pair<double, double> BoxWhiskerChart::getMinimumAndMaximumYInRange(double, double,
                                                                         sal_Int32 nAxisIndex)
{
    // the boxes summarise all values, whatever x range is shown
    double fMin = std::numeric_limits<double>::infinity();
    double fMax = -std::numeric_limits<double>::infinity();
    for (auto const& rZSlot : m_aZSlots)
        for (auto const& rXSlot : rZSlot)
            for (std::unique_ptr<VDataSeries> const& pSeries : rXSlot.m_aSeriesVector)
            {
                if (pSeries->getAttachedAxisIndex() != nAxisIndex)
                    continue;
                for (sal_Int32 i = 0; i < pSeries->getTotalPointCount(); ++i)
                {
                    const double fY = pSeries->getYValue(i);
                    if (std::isnan(fY) || std::isinf(fY))
                        continue;
                    fMin = std::min(fMin, fY);
                    fMax = std::max(fMax, fY);
                }
            }
    if (std::isinf(fMin))
        return { std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::quiet_NaN() };
    return { fMin, fMax };
}

BoxWhiskerChart::Stats BoxWhiskerChart::computeStats(const std::vector<double>& rValues,
                                                     bool bExclusive)
{
    Stats aStats;
    if (rValues.empty())
        return aStats;
    aStats.fQ1 = lcl_quantile(rValues, 0.25, bExclusive);
    aStats.fMedian = lcl_quantile(rValues, 0.5, bExclusive);
    aStats.fQ3 = lcl_quantile(rValues, 0.75, bExclusive);
    double fSum = 0;
    for (double f : rValues)
        fSum += f;
    aStats.fMean = fSum / rValues.size();
    const double fRange = 1.5 * (aStats.fQ3 - aStats.fQ1);
    const double fLowFence = aStats.fQ1 - fRange;
    const double fHighFence = aStats.fQ3 + fRange;
    aStats.fLowWhisker = aStats.fQ1;
    aStats.fHighWhisker = aStats.fQ3;
    bool bFirstInner = true;
    for (double f : rValues)
    {
        if (f < fLowFence || f > fHighFence)
        {
            aStats.aOutliers.push_back(f);
            continue;
        }
        aStats.aInner.push_back(f);
        if (bFirstInner)
            aStats.fLowWhisker = f;
        bFirstInner = false;
        aStats.fHighWhisker = f;
    }
    return aStats;
}

void BoxWhiskerChart::createShapes()
{
    if (m_aZSlots.empty() || m_nDimension != 2)
        return;
    if (!(m_xLogicTarget.is() && m_xFinalTarget.is()))
        return;

    rtl::Reference<SvxShapeGroupAnyD> xSeriesTarget = createGroupShape(m_xLogicTarget);

    BarPositionHelper* pPosHelper = &m_aMainPosHelper;
    PlotterBase::m_pPosHelper = pPosHelper;
    const std::vector<VDataSeriesGroup>& rXSlots = m_aZSlots.front();
    pPosHelper->updateSeriesCount(rXSlots.size());
    // Excel leaves a small gap between neighbouring boxes: a box takes
    // about 93% of its slot (measured on an Excel 365 rendering).
    const double fHalfWidth = pPosHelper->getScaledSlotWidth() * 0.93 / 2.0;

    // logic (x, y) to page coordinates; false for values outside the axis range
    auto aToPage = [pPosHelper](double fX, double fY, drawing::Position3D& rOut) {
        double fScaledY = fY;
        pPosHelper->clipLogicValues(nullptr, &fScaledY, nullptr);
        pPosHelper->doLogicScaling(nullptr, &fScaledY, nullptr);
        rOut = pPosHelper->transformScaledLogicToScene(fX, fScaledY, 0, true);
        return isValidPosition(rOut);
    };
    auto aLine = [&](const rtl::Reference<SvxShapeGroupAnyD>& xTarget,
                     std::vector<drawing::Position3D> aPoints, sal_Int32 nColor) {
        std::vector<std::vector<drawing::Position3D>> aPoly{ std::move(aPoints) };
        rtl::Reference<SvxShapePolyPolygon> xShape = ShapeFactory::createLine2D(xTarget, aPoly);
        if (xShape.is())
            lcl_setLine(*xShape, nColor);
    };

    double fSlot = 0;
    for (auto const& rXSlot : rXSlots)
    {
        for (std::unique_ptr<VDataSeries> const& pSeries : rXSlot.m_aSeriesVector)
        {
            const uno::Reference<beans::XPropertySet>& xSeriesProps = pSeries->getPropertiesOfSeries();
            OUString aMethod;
            sal_Int32 nColor = 0x4472C4;
            try
            {
                if (xSeriesProps.is())
                {
                    xSeriesProps->getPropertyValue(u"QuartileMethod"_ustr) >>= aMethod;
                    xSeriesProps->getPropertyValue(u"Color"_ustr) >>= nColor;
                }
            }
            catch (const uno::Exception&)
            {
            }
            const bool bExclusive = !aMethod.equalsIgnoreAsciiCase(u"inclusive");
            const bool bMeanMarker = lcl_getBool(xSeriesProps, u"MeanMarker"_ustr, true);
            const bool bMeanLine = lcl_getBool(xSeriesProps, u"MeanLine"_ustr, false);
            const bool bOutliers = lcl_getBool(xSeriesProps, u"Outliers"_ustr, true);
            const bool bInner = lcl_getBool(xSeriesProps, u"Nonoutliers"_ustr, false);
            const sal_Int32 nStroke = lcl_darker(nColor);

            const sal_Int32 nPoints = pSeries->getTotalPointCount();
            sal_Int32 nGroups = 0;
            const std::vector<sal_Int32> aGroups = pointGroups(nPoints, nGroups);
            std::vector<std::vector<double>> aValues(nGroups);
            std::vector<sal_Int32> aFirstPoint(nGroups, -1);
            for (sal_Int32 i = 0; i < nPoints; ++i)
            {
                const double fY = pSeries->getYValue(i);
                if (std::isnan(fY) || std::isinf(fY) || aGroups[i] < 0 || aGroups[i] >= nGroups)
                    continue;
                aValues[aGroups[i]].push_back(fY);
                if (aFirstPoint[aGroups[i]] < 0)
                    aFirstPoint[aGroups[i]] = i;
            }

            rtl::Reference<SvxShapeGroupAnyD> xSeriesGroup = getSeriesGroupShape(pSeries.get(), xSeriesTarget);
            std::vector<drawing::Position3D> aMeans;
            for (sal_Int32 g = 0; g < nGroups; ++g)
            {
                std::vector<double>& rValues = aValues[g];
                if (rValues.empty())
                    continue;
                std::sort(rValues.begin(), rValues.end());
                const Stats aStats = computeStats(rValues, bExclusive);
                const double fX = pPosHelper->getScaledSlotPos(g + 1.0, fSlot);

                rtl::Reference<SvxShapeGroupAnyD> xTarget = xSeriesGroup;
                if (aFirstPoint[g] >= 0)
                    xTarget = createGroupShape(
                        xSeriesGroup, ObjectIdentifier::createPointCID(pSeries->getPointCID_Stub(), aFirstPoint[g]));

                // box from the first to the third quartile
                drawing::Position3D aTopLeft, aBottomRight;
                if (aToPage(fX - fHalfWidth, aStats.fQ3, aTopLeft)
                    && aToPage(fX + fHalfWidth, aStats.fQ1, aBottomRight))
                {
                    const sal_Int32 nLeft = static_cast<sal_Int32>(std::min(aTopLeft.PositionX, aBottomRight.PositionX));
                    const sal_Int32 nTop = static_cast<sal_Int32>(std::min(aTopLeft.PositionY, aBottomRight.PositionY));
                    awt::Size aSize(static_cast<sal_Int32>(std::fabs(aBottomRight.PositionX - aTopLeft.PositionX)),
                                    std::max<sal_Int32>(1, static_cast<sal_Int32>(std::fabs(aBottomRight.PositionY - aTopLeft.PositionY))));
                    rtl::Reference<SvxShapeRect> xBox = ShapeFactory::createRectangle(
                        xTarget, aSize, awt::Point(nLeft, nTop), tNameSequence(), tAnySequence());
                    if (xBox.is())
                    {
                        if (aFirstPoint[g] >= 0 && pSeries->isAttributedDataPoint(aFirstPoint[g]))
                            PropertyMapper::setMappedProperties(*xBox, pSeries->getPropertiesOfPoint(aFirstPoint[g]),
                                                                PropertyMapper::getPropertyNameMapForFilledSeriesProperties());
                        else
                        {
                            xBox->SvxShape::setPropertyValue(u"FillStyle"_ustr, uno::Any(drawing::FillStyle_SOLID));
                            xBox->SvxShape::setPropertyValue(u"FillColor"_ustr, uno::Any(nColor));
                            lcl_setLine(*xBox, nStroke);
                        }
                    }
                }

                // median across the box
                drawing::Position3D aA, aB;
                if (aToPage(fX - fHalfWidth, aStats.fMedian, aA) && aToPage(fX + fHalfWidth, aStats.fMedian, aB))
                    aLine(xTarget, { aA, aB }, nStroke);

                // whiskers with caps a third of the box wide
                const double fCap = fHalfWidth * 0.3;
                auto aWhisker = [&](double fFrom, double fTo) {
                    drawing::Position3D aStart, aEnd, aCapLeft, aCapRight;
                    if (aToPage(fX, fFrom, aStart) && aToPage(fX, fTo, aEnd))
                        aLine(xTarget, { aStart, aEnd }, nStroke);
                    if (aToPage(fX - fCap, fTo, aCapLeft) && aToPage(fX + fCap, fTo, aCapRight))
                        aLine(xTarget, { aCapLeft, aCapRight }, nStroke);
                };
                if (aStats.fHighWhisker > aStats.fQ3)
                    aWhisker(aStats.fQ3, aStats.fHighWhisker);
                if (aStats.fLowWhisker < aStats.fQ1)
                    aWhisker(aStats.fQ1, aStats.fLowWhisker);

                // mean marker: a thin x
                drawing::Position3D aMean;
                if (aToPage(fX, aStats.fMean, aMean))
                {
                    aMeans.push_back(aMean);
                    if (bMeanMarker)
                    {
                        drawing::Position3D aLeft;
                        aToPage(fX - fHalfWidth * 0.12, aStats.fMean, aLeft);
                        const double d = std::fabs(aMean.PositionX - aLeft.PositionX);
                        auto aAt = [&aMean](double fDX, double fDY) {
                            return drawing::Position3D(aMean.PositionX + fDX, aMean.PositionY + fDY, aMean.PositionZ);
                        };
                        aLine(xTarget, { aAt(-d, -d), aAt(d, d) }, nStroke);
                        aLine(xTarget, { aAt(-d, d), aAt(d, -d) }, nStroke);
                    }
                }

                // single points: outliers, and the inner points when asked for
                auto aDot = [&](double fY) {
                    drawing::Position3D aPos;
                    if (!aToPage(fX, fY, aPos))
                        return;
                    rtl::Reference<SvxShapeCircle> xDot
                        = ShapeFactory::createCircle2D(xTarget, aPos, drawing::Direction3D(150, 150, 0));
                    if (!xDot.is())
                        return;
                    xDot->SvxShape::setPropertyValue(u"FillStyle"_ustr, uno::Any(drawing::FillStyle_SOLID));
                    xDot->SvxShape::setPropertyValue(u"FillColor"_ustr, uno::Any(nColor));
                    lcl_setLine(*xDot, nStroke);
                };
                if (bOutliers)
                    for (double f : aStats.aOutliers)
                        aDot(f);
                if (bInner)
                    for (double f : aStats.aInner)
                        aDot(f);
            }
            if (bMeanLine && aMeans.size() > 1)
                aLine(xSeriesGroup, aMeans, nStroke);
        }
        fSlot += 1.0;
    }
}

} //namespace chart

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
