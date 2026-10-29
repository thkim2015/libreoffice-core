/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#pragma once

#include <PlottingPositionHelper.hxx>
#include <VSeriesPlotter.hxx>

#include <vector>

namespace chart
{
/** Excel style surface chart.

    The values form a grid: categories along x, series along z (depth).
    The grid is colored by value bands, like Excel: one legend entry per
    band instead of per series. "Wireframe" on the chart type draws only the
    grid and contour lines in the band colors.
*/
class SurfaceChart : public VSeriesPlotter
{
public:
    SurfaceChart() = delete;
    SurfaceChart(const rtl::Reference<::chart::ChartType>& xChartTypeModel,
                 sal_Int32 nDimensionCount);
    virtual ~SurfaceChart() override;

    virtual void createShapes() override;
    virtual void addSeries(std::unique_ptr<VDataSeries> pSeries, sal_Int32 zSlot, sal_Int32 xSlot,
                           sal_Int32 ySlot) override;

    virtual css::drawing::Direction3D getPreferredDiagramAspectRatio() const override;

    // MinimumAndMaximumSupplier
    virtual double getMinimumX() override;
    virtual double getMaximumX() override;
    virtual bool isExpandWideValuesToZero(sal_Int32 nDimensionIndex) override;
    virtual bool isSeparateStackingForDifferentSigns(sal_Int32 nDimensionIndex) override;

    virtual LegendSymbolStyle getLegendSymbolStyle() override;
    virtual std::vector<ViewLegendEntry> createLegendEntriesForSeries(
        const css::awt::Size& rEntryKeyAspectRatio, const VDataSeries& rSeries,
        const css::uno::Reference<css::beans::XPropertySet>& xTextProperties,
        const rtl::Reference<SvxShapeGroupAnyD>& xTarget,
        const css::uno::Reference<css::uno::XComponentContext>& xContext) override;

    /// Band edges from the smallest to the largest value: Excel's automatic
    /// value axis (starts at 0 for positive data) with about nTargetBands steps.
    static std::vector<double> bandEdges(double fMin, double fMax, sal_Int32 nTargetBands);
    /// Office theme color of band nBand (accents 1-6, then darker, then lighter)
    static sal_Int32 bandColor(sal_Int32 nBand);

    struct GridPoint
    {
        double fX, fZ, fValue;
    };
    /// The part of the triangle rTriangle with fLow <= value <= fHigh
    static std::vector<GridPoint> clipToBand(const std::vector<GridPoint>& rTriangle,
                                             double fLow, double fHigh);

private:
    std::vector<VDataSeries*> rows() const;
    std::vector<double> currentBandEdges() const;
    bool isWireframe() const;
    bool isContour() const;
    bool boolProperty(const OUString& rName) const;

    PlottingPositionHelper m_aMainPosHelper;
};
} //namespace chart

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
