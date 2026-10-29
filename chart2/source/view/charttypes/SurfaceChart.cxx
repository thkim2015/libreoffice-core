/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "SurfaceChart.hxx"

#include <ChartType.hxx>
#include <PropertyMapper.hxx>
#include <ShapeFactory.hxx>
#include <Stripe.hxx>
#include <VDataSeries.hxx>
#include <VLineProperties.hxx>

#include <com/sun/star/drawing/FillStyle.hpp>
#include <com/sun/star/drawing/LineStyle.hpp>
#include <comphelper/diagnose_ex.hxx>
#include <rtl/math.hxx>
#include <svx/unoshape.hxx>

#include <algorithm>
#include <cmath>
#include <limits>

namespace chart
{
using namespace ::com::sun::star;

namespace
{
// Office theme accents 1-6, then the same darkened (lumMod 60 %) and
// lightened (lumMod 80 % lumOff 20 %), as Excel cycles them over the bands
constexpr sal_Int32 aBandColors[]
    = { 0x4472C4, 0xED7D31, 0xA5A5A5, 0xFFC000, 0x5B9BD5, 0x70AD47,
        0x264478, 0x9E480E, 0x636363, 0x997300, 0x255E91, 0x43682B,
        0x698ED0, 0xF1975A, 0xB7B7B7, 0xFFCD33, 0x7CAFDD, 0x8CC168 };

// 0.75 pt, the Excel default line
constexpr sal_Int32 nLineWidth = 26;

SurfaceChart::GridPoint lcl_mix(const SurfaceChart::GridPoint& rA,
                                const SurfaceChart::GridPoint& rB, double fValue)
{
    const double fT = (fValue - rA.fValue) / (rB.fValue - rA.fValue);
    return { rA.fX + fT * (rB.fX - rA.fX), rA.fZ + fT * (rB.fZ - rA.fZ), fValue };
}

// Sutherland-Hodgman against one value limit
std::vector<SurfaceChart::GridPoint> lcl_clip(const std::vector<SurfaceChart::GridPoint>& rPoly,
                                              double fLimit, bool bKeepAbove)
{
    std::vector<SurfaceChart::GridPoint> aOut;
    const size_t n = rPoly.size();
    for (size_t i = 0; i < n; ++i)
    {
        const SurfaceChart::GridPoint& rCur = rPoly[i];
        const SurfaceChart::GridPoint& rNext = rPoly[(i + 1) % n];
        const bool bCurIn = bKeepAbove ? rCur.fValue >= fLimit : rCur.fValue <= fLimit;
        const bool bNextIn = bKeepAbove ? rNext.fValue >= fLimit : rNext.fValue <= fLimit;
        if (bCurIn)
            aOut.push_back(rCur);
        if (bCurIn != bNextIn)
            aOut.push_back(lcl_mix(rCur, rNext, fLimit));
    }
    return aOut;
}

OUString lcl_number(double f)
{
    return ::rtl::math::doubleToUString(f, rtl_math_StringFormat_Automatic,
                                        rtl_math_DecimalPlaces_Max, '.', true);
}
} // namespace

SurfaceChart::SurfaceChart(const rtl::Reference<ChartType>& xChartTypeModel,
                           sal_Int32 nDimensionCount)
    : VSeriesPlotter(xChartTypeModel, nDimensionCount)
{
    PlotterBase::m_pPosHelper = &m_aMainPosHelper;
    VSeriesPlotter::m_pMainPosHelper = &m_aMainPosHelper;
}

SurfaceChart::~SurfaceChart() {}

drawing::Direction3D SurfaceChart::getPreferredDiagramAspectRatio() const
{
    return drawing::Direction3D(-1, -1, -1);
}

LegendSymbolStyle SurfaceChart::getLegendSymbolStyle() { return LegendSymbolStyle::Box; }

bool SurfaceChart::isSeparateStackingForDifferentSigns(sal_Int32) { return false; }

bool SurfaceChart::isExpandWideValuesToZero(sal_Int32 nDimensionIndex)
{
    return nDimensionIndex == 1;
}

bool SurfaceChart::boolProperty(const OUString& rName) const
{
    bool bValue = false;
    try
    {
        if (m_xChartTypeModel.is())
            m_xChartTypeModel->getPropertyValue(rName) >>= bValue;
    }
    catch (const uno::Exception&)
    {
    }
    return bValue;
}

bool SurfaceChart::isWireframe() const { return boolProperty(u"Wireframe"_ustr); }

bool SurfaceChart::isContour() const { return boolProperty(u"Contour"_ustr); }

void SurfaceChart::addSeries(std::unique_ptr<VDataSeries> pSeries, sal_Int32 /*zSlot*/,
                             sal_Int32 /*xSlot*/, sal_Int32 /*ySlot*/)
{
    if (!pSeries)
        return;
    pSeries->setCategoryXAxis();
    // every series is one row of the grid
    VSeriesPlotter::addSeries(std::move(pSeries), -1, 0, 0);
}

std::vector<VDataSeries*> SurfaceChart::rows() const
{
    std::vector<VDataSeries*> aRows;
    for (auto const& rZSlot : m_aZSlots)
        for (auto const& rXSlot : rZSlot)
            for (std::unique_ptr<VDataSeries> const& pSeries : rXSlot.m_aSeriesVector)
                if (pSeries)
                    aRows.push_back(pSeries.get());
    return aRows;
}

double SurfaceChart::getMinimumX() { return 1.0; }

double SurfaceChart::getMaximumX()
{
    sal_Int32 nPoints = 1;
    for (VDataSeries* pRow : rows())
        nPoints = std::max(nPoints, pRow->getTotalPointCount());
    return nPoints;
}

std::vector<double> SurfaceChart::bandEdges(double fMin, double fMax, sal_Int32 nTargetBands)
{
    std::vector<double> aEdges;
    if (!(fMin <= fMax) || std::isinf(fMin) || std::isinf(fMax) || nTargetBands < 1)
        return aEdges;
    double fBase = fMin >= 0.0 ? 0.0 : fMin;
    double fTop = fMax <= 0.0 ? 0.0 : fMax;
    if (fTop - fBase <= 0.0)
        fTop = fBase + 1.0;
    // the smallest 1, 2 or 5 times a power of ten that gives at most nTargetBands bands
    const double fRaw = (fTop - fBase) / nTargetBands;
    const double fPower = std::pow(10.0, std::floor(std::log10(fRaw)));
    double fStep = fPower * 10.0;
    for (double fNice : { 1.0, 2.0, 5.0, 10.0 })
        if (fNice * fPower >= fRaw * (1.0 - 1e-9))
        {
            fStep = fNice * fPower;
            break;
        }
    fBase = std::floor(fBase / fStep + 1e-9) * fStep;
    const sal_Int32 nBands
        = std::max<sal_Int32>(1, static_cast<sal_Int32>(std::ceil((fTop - fBase) / fStep - 1e-9)));
    for (sal_Int32 i = 0; i <= nBands; ++i)
        aEdges.push_back(fBase + i * fStep);
    return aEdges;
}

sal_Int32 SurfaceChart::bandColor(sal_Int32 nBand)
{
    return aBandColors[std::max<sal_Int32>(0, nBand) % std::size(aBandColors)];
}

std::vector<double> SurfaceChart::currentBandEdges() const
{
    double fMin = std::numeric_limits<double>::infinity();
    double fMax = -std::numeric_limits<double>::infinity();
    for (VDataSeries* pRow : rows())
        for (sal_Int32 i = 0; i < pRow->getTotalPointCount(); ++i)
        {
            const double f = pRow->getYValue(i);
            if (std::isnan(f) || std::isinf(f))
                continue;
            fMin = std::min(fMin, f);
            fMax = std::max(fMax, f);
        }
    // Excel's automatic axis is finer for the 3D view than for the contour view
    return bandEdges(fMin, fMax, isContour() ? 4 : 7);
}

std::vector<SurfaceChart::GridPoint>
SurfaceChart::clipToBand(const std::vector<GridPoint>& rTriangle, double fLow, double fHigh)
{
    std::vector<GridPoint> aPoly = lcl_clip(rTriangle, fLow, true);
    if (aPoly.size() < 3)
        return {};
    aPoly = lcl_clip(aPoly, fHigh, false);
    if (aPoly.size() < 3)
        return {};
    return aPoly;
}

std::vector<ViewLegendEntry> SurfaceChart::createLegendEntriesForSeries(
    const awt::Size& rEntryKeyAspectRatio, const VDataSeries& rSeries,
    const uno::Reference<beans::XPropertySet>& xTextProperties,
    const rtl::Reference<SvxShapeGroupAnyD>& xTarget,
    const uno::Reference<uno::XComponentContext>& /*xContext*/)
{
    std::vector<ViewLegendEntry> aResult;
    // the bands belong to the whole grid: list them once, with the first row
    const std::vector<VDataSeries*> aRows = rows();
    if (!xTarget.is() || aRows.empty() || aRows.front() != &rSeries)
        return aResult;
    const std::vector<double> aEdges = currentBandEdges();
    for (size_t i = 0; i + 1 < aEdges.size(); ++i)
        aResult.push_back(createBoxLegendEntry(
            rEntryKeyAspectRatio, lcl_number(aEdges[i]) + "-" + lcl_number(aEdges[i + 1]),
            bandColor(i), xTextProperties, xTarget));
    return aResult;
}

void SurfaceChart::createShapes()
{
    if (m_aZSlots.empty() || m_nDimension != 3)
        return;
    if (!(m_xLogicTarget.is() && m_xFinalTarget.is()))
        return;

    const std::vector<VDataSeries*> aRows = rows();
    const std::vector<double> aEdges = currentBandEdges();
    if (aRows.size() < 2 || aEdges.size() < 2)
        return;
    sal_Int32 nPoints = 0;
    for (VDataSeries* pRow : aRows)
        nPoints = std::max(nPoints, pRow->getTotalPointCount());
    if (nPoints < 2)
        return;

    rtl::Reference<SvxShapeGroupAnyD> xTarget = createGroupShape(m_xLogicTarget);
    PlottingPositionHelper* pPosHelper = &m_aMainPosHelper;
    PlotterBase::m_pPosHelper = pPosHelper;
    const bool bWireframe = isWireframe();

    auto aValue = [&](size_t nRow, sal_Int32 nPoint) {
        return nPoint < aRows[nRow]->getTotalPointCount()
                   ? aRows[nRow]->getYValue(nPoint)
                   : std::numeric_limits<double>::quiet_NaN();
    };
    auto aToScene = [pPosHelper](const GridPoint& rP) {
        return pPosHelper->transformLogicToScene(rP.fX, rP.fValue, rP.fZ, true);
    };
    auto aBandOf = [&aEdges](double f) {
        sal_Int32 nBand = 0;
        while (nBand + 2 < static_cast<sal_Int32>(aEdges.size()) && f >= aEdges[nBand + 1])
            ++nBand;
        return nBand;
    };
    auto aLine = [&](const std::vector<GridPoint>& rPoints, sal_Int32 nBand) {
        std::vector<std::vector<drawing::Position3D>> aPoly(1);
        for (const GridPoint& rP : rPoints)
            aPoly[0].push_back(aToScene(rP));
        VLineProperties aLineProps;
        aLineProps.Color <<= bandColor(nBand);
        aLineProps.Width <<= nLineWidth;
        ShapeFactory::createLine3D(xTarget, aPoly, aLineProps);
    };

    const uno::Reference<beans::XPropertySet> xFirstRowProps
        = aRows.front()->getPropertiesOfSeries();
    for (size_t nRow = 0; nRow + 1 < aRows.size(); ++nRow)
    {
        for (sal_Int32 nPoint = 0; nPoint + 1 < nPoints; ++nPoint)
        {
            // one grid cell as two triangles; x = category (1-based), z = the
            // middle of the depth slot of the row, as AreaChart places deep series
            const GridPoint a{ nPoint + 1.0, nRow + 1.5, aValue(nRow, nPoint) };
            const GridPoint b{ nPoint + 2.0, nRow + 1.5, aValue(nRow, nPoint + 1) };
            const GridPoint c{ nPoint + 2.0, nRow + 2.5, aValue(nRow + 1, nPoint + 1) };
            const GridPoint d{ nPoint + 1.0, nRow + 2.5, aValue(nRow + 1, nPoint) };
            for (const std::vector<GridPoint>& rTriangle :
                 { std::vector<GridPoint>{ a, b, c }, std::vector<GridPoint>{ a, c, d } })
            {
                if (std::any_of(rTriangle.begin(), rTriangle.end(),
                                [](const GridPoint& p) { return std::isnan(p.fValue); }))
                    continue;
                for (size_t nBand = 0; nBand + 1 < aEdges.size(); ++nBand)
                {
                    const double fLow = nBand == 0 ? -std::numeric_limits<double>::max()
                                                   : aEdges[nBand];
                    const double fHigh = nBand + 2 == aEdges.size()
                                             ? std::numeric_limits<double>::max()
                                             : aEdges[nBand + 1];
                    if (bWireframe)
                    {
                        // the contour line where this band starts
                        if (nBand == 0)
                            continue;
                        std::vector<GridPoint> aCut;
                        for (size_t i = 0; i < 3; ++i)
                        {
                            const GridPoint& p = rTriangle[i];
                            const GridPoint& q = rTriangle[(i + 1) % 3];
                            if ((p.fValue < fLow) != (q.fValue < fLow))
                                aCut.push_back(lcl_mix(p, q, fLow));
                        }
                        if (aCut.size() == 2)
                            aLine(aCut, nBand);
                        continue;
                    }
                    const std::vector<GridPoint> aPart = clipToBand(rTriangle, fLow, fHigh);
                    // fan into triangles; a stripe with a repeated corner is a triangle
                    for (size_t i = 1; i + 1 < aPart.size(); ++i)
                    {
                        Stripe aStripe(aToScene(aPart[0]), aToScene(aPart[i]),
                                       aToScene(aPart[i + 1]), aToScene(aPart[i + 1]));
                        rtl::Reference<Svx3DPolygonObject> xShape = ShapeFactory::createStripe(
                            xTarget, aStripe, xFirstRowProps,
                            PropertyMapper::getPropertyNameMapForFilledSeriesProperties(), true);
                        if (!xShape.is())
                            continue;
                        xShape->SvxShape::setPropertyValue(u"FillStyle"_ustr,
                                                           uno::Any(drawing::FillStyle_SOLID));
                        xShape->SvxShape::setPropertyValue(u"FillColor"_ustr,
                                                           uno::Any(bandColor(nBand)));
                        xShape->SvxShape::setPropertyValue(u"LineStyle"_ustr,
                                                           uno::Any(drawing::LineStyle_NONE));
                    }
                }
            }
        }
    }

    if (!bWireframe)
        return;
    // the grid lines, cut where they cross from one band into the next
    auto aGridLine = [&](GridPoint p, GridPoint q) {
        if (std::isnan(p.fValue) || std::isnan(q.fValue))
            return;
        if (p.fValue > q.fValue)
            std::swap(p, q);
        GridPoint aFrom = p;
        for (size_t nEdge = 1; nEdge + 1 < aEdges.size(); ++nEdge)
            if (aEdges[nEdge] > p.fValue && aEdges[nEdge] < q.fValue)
            {
                GridPoint aTo = lcl_mix(p, q, aEdges[nEdge]);
                aLine({ aFrom, aTo }, aBandOf((aFrom.fValue + aTo.fValue) / 2.0));
                aFrom = aTo;
            }
        aLine({ aFrom, q }, aBandOf((aFrom.fValue + q.fValue) / 2.0));
    };
    for (size_t nRow = 0; nRow < aRows.size(); ++nRow)
        for (sal_Int32 nPoint = 0; nPoint < nPoints; ++nPoint)
        {
            const GridPoint p{ nPoint + 1.0, nRow + 1.5, aValue(nRow, nPoint) };
            if (nPoint + 1 < nPoints)
                aGridLine(p, { nPoint + 2.0, nRow + 1.5, aValue(nRow, nPoint + 1) });
            if (nRow + 1 < aRows.size())
                aGridLine(p, { nPoint + 1.0, nRow + 2.5, aValue(nRow + 1, nPoint) });
        }
}

} //namespace chart

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
