/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#pragma once

#include "TreemapChart.hxx"

namespace chart
{
/** Sunburst (chartex): the category hierarchy of TreemapChart drawn as
    rings, the outermost category level innermost. As in Excel, the
    segments run clockwise from 12 o'clock, largest first, the hole is one
    ring wide and labels follow the radius.
*/
class SunburstChart : public TreemapChart
{
public:
    SunburstChart() = delete;
    explicit SunburstChart(const rtl::Reference<ChartType>& xChartTypeModel);
    virtual ~SunburstChart() override;

    virtual void createShapes() override;
};

} // namespace chart

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
