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
/** Box and whisker chart (Excel chartex boxWhisker).

    Every series is a list of values. Points with the same category text
    form one box per series; the category axis shows each text once.
*/
class BoxWhiskerChart : public VSeriesPlotter
{
public:
    BoxWhiskerChart() = delete;
    BoxWhiskerChart(const rtl::Reference<::chart::ChartType>& xChartTypeModel,
                    sal_Int32 nDimensionCount);
    virtual ~BoxWhiskerChart() override;

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

    struct Stats
    {
        double fQ1 = 0, fMedian = 0, fQ3 = 0, fMean = 0;
        double fLowWhisker = 0, fHighWhisker = 0; // last values inside 1.5 IQR
        std::vector<double> aOutliers;
        std::vector<double> aInner; // values that are not outliers
    };
    /// rValues must be sorted; bExclusive selects QUARTILE.EXC over QUARTILE.INC.
    static Stats computeStats(const std::vector<double>& rValues, bool bExclusive);

private:
    /// Box index of every data point; rnGroupCount gets the number of boxes per series.
    std::vector<sal_Int32> pointGroups(sal_Int32 nPointCount, sal_Int32& rnGroupCount) const;

    BarPositionHelper m_aMainPosHelper;
};
} //namespace chart

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
