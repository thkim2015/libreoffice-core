/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#pragma once

#include <ChartTypeTemplate.hxx>

namespace chart
{
/** Templates com.sun.star.chart2.template.Surface / SurfaceWireframe (3D)
    and Contour / ContourWireframe (the same 3D grid seen from above). */
class SurfaceChartTypeTemplate final : public ChartTypeTemplate
{
public:
    SurfaceChartTypeTemplate(css::uno::Reference<css::uno::XComponentContext> const& xContext,
                             const OUString& rServiceName, bool bContour, bool bWireframe);
    virtual ~SurfaceChartTypeTemplate() override;

protected:
    // ____ ChartTypeTemplate ____
    virtual bool matchesTemplate2(const rtl::Reference<::chart::Diagram>& xDiagram,
                                  bool bAdaptProperties) override;
    virtual rtl::Reference<::chart::ChartType> getChartTypeForNewSeries2(
        const std::vector<rtl::Reference<::chart::ChartType>>& aFormerlyUsedChartTypes) override;
    virtual void applyStyle2(const rtl::Reference<::chart::DataSeries>& xSeries,
                             ::sal_Int32 nChartTypeGroupIndex, ::sal_Int32 nSeriesIndex,
                             ::sal_Int32 nSeriesCount) override;
    virtual sal_Int32 getDimension() const override;
    virtual StackMode getStackMode(sal_Int32 nChartTypeIndex) const override;
    virtual rtl::Reference<::chart::ChartType>
    getChartTypeForIndex(sal_Int32 nChartTypeIndex) override;

private:
    bool m_bContour;
    bool m_bWireframe;
};

} //  namespace chart

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
