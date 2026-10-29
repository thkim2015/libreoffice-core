/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#pragma once

#include "BarPositionHelper.hxx"
#include <VSeriesPlotter.hxx>

#include <vector>

namespace chart
{
/** Pareto chart (Excel chartex paretoLine) and its clustered column bars.

    Equal category texts share one bar with the sum of their values; the bars
    are sorted largest first and a line shows the cumulative percentage.
    OOXML import gives a ClusteredColumn chart type with the values and an
    empty ParetoLine series on the secondary axis; an inserted chart has only
    a ParetoLine chart type holding the values. Either way the plotter that
    owns the values draws both the bars and the line.
*/
class ParetoChart : public VSeriesPlotter
{
public:
    ParetoChart() = delete;
    ParetoChart(const rtl::Reference<::chart::ChartType>& xChartTypeModel,
                sal_Int32 nDimensionCount, bool bParetoLineType);
    virtual ~ParetoChart() override;

    virtual void createShapes() override;
    virtual void addSeries(std::unique_ptr<VDataSeries> pSeries, sal_Int32 zSlot, sal_Int32 xSlot,
                           sal_Int32 ySlot) override;

    virtual css::drawing::Direction3D getPreferredDiagramAspectRatio() const override;

    // MinimumAndMaximumSupplier
    virtual double getMinimumX() override;
    virtual double getMaximumX() override;
    virtual std::pair<double, double> getMinimumAndMaximumYInRange(double fMinimumX,
                                                                   double fMaximumX,
                                                                   sal_Int32 nAxisIndex) override;
    virtual bool isExpandWideValuesToZero(sal_Int32 nDimensionIndex) override;
    virtual bool isSeparateStackingForDifferentSigns(sal_Int32 nDimensionIndex) override;

    virtual LegendSymbolStyle getLegendSymbolStyle() override;

private:
    /// the series holding the values, or nullptr (the empty imported ParetoLine)
    VDataSeries* valueSeries() const;
    /// whether to merge, sort and draw the cumulative line
    bool isPareto() const;
    /// bar index of every point and the bar values in display order
    std::vector<sal_Int32> bars(const VDataSeries& rSeries, std::vector<double>& rSums) const;

    BarPositionHelper m_aMainPosHelper;
    bool m_bParetoLineType;
};
} //namespace chart

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
