/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#pragma once

#include <VSeriesPlotter.hxx>
#include "BarPositionHelper.hxx"

namespace chart
{
/** Treemap (chartex): the plot area is split into tiles whose areas are
    proportional to the point values. Multi-level categories form the
    hierarchy; the outermost level picks the tile colour, as in Excel.
    Tiles are laid out with the squarified algorithm, largest first.
*/
class TreemapChart : public VSeriesPlotter
{
public:
    TreemapChart() = delete;
    explicit TreemapChart(const rtl::Reference<ChartType>& xChartTypeModel);
    virtual ~TreemapChart() override;

    virtual void createShapes() override;
    virtual void addSeries(std::unique_ptr<VDataSeries> pSeries, sal_Int32 zSlot, sal_Int32 xSlot,
                           sal_Int32 ySlot) override;

    virtual css::drawing::Direction3D getPreferredDiagramAspectRatio() const override;

    struct Rect
    {
        double fX;
        double fY;
        double fW;
        double fH;
    };
    struct Item
    {
        double fValue;
        sal_Int32 nIndex; // caller's index
    };
    /// Squarified layout of rItems (sorted by value, largest first) in rArea
    static std::vector<Rect> squarify(const std::vector<Item>& rItems, const Rect& rArea);

private:
    BarPositionHelper m_aMainPosHelper;
};

} // namespace chart

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
