/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "SurfaceChartTypeTemplate.hxx"
#include "SurfaceChartType.hxx"
#include <ChartType.hxx>
#include <DataSeries.hxx>
#include <Diagram.hxx>
#include <StackMode.hxx>

#include <com/sun/star/drawing/LineStyle.hpp>
#include <comphelper/diagnose_ex.hxx>

using namespace ::com::sun::star;

namespace chart
{
SurfaceChartTypeTemplate::SurfaceChartTypeTemplate(
    uno::Reference<uno::XComponentContext> const& xContext, const OUString& rServiceName,
    bool bContour, bool bWireframe)
    : ChartTypeTemplate(xContext, rServiceName)
    , m_bContour(bContour)
    , m_bWireframe(bWireframe)
{
}

SurfaceChartTypeTemplate::~SurfaceChartTypeTemplate() {}

sal_Int32 SurfaceChartTypeTemplate::getDimension() const { return 3; }

// every series is a row of the grid, one behind the other
StackMode SurfaceChartTypeTemplate::getStackMode(sal_Int32 /*nChartTypeIndex*/) const
{
    return StackMode::ZStacked;
}

bool SurfaceChartTypeTemplate::matchesTemplate2(const rtl::Reference<::chart::Diagram>& xDiagram,
                                                bool bAdaptProperties)
{
    if (!ChartTypeTemplate::matchesTemplate2(xDiagram, bAdaptProperties))
        return false;
    // the four variants share the chart type; tell them apart by its properties
    try
    {
        for (rtl::Reference<ChartType> const& xChartType : xDiagram->getChartTypes())
        {
            bool bWireframe = false, bContour = false;
            xChartType->getPropertyValue(u"Wireframe"_ustr) >>= bWireframe;
            xChartType->getPropertyValue(u"Contour"_ustr) >>= bContour;
            if (bWireframe != m_bWireframe || bContour != m_bContour)
                return false;
        }
    }
    catch (const uno::Exception&)
    {
        DBG_UNHANDLED_EXCEPTION("chart2");
    }
    return true;
}

rtl::Reference<ChartType>
SurfaceChartTypeTemplate::getChartTypeForIndex(sal_Int32 /*nChartTypeIndex*/)
{
    rtl::Reference<ChartType> xResult(new SurfaceChartType());
    xResult->setPropertyValue(u"Wireframe"_ustr, uno::Any(m_bWireframe));
    xResult->setPropertyValue(u"Contour"_ustr, uno::Any(m_bContour));
    return xResult;
}

rtl::Reference<ChartType> SurfaceChartTypeTemplate::getChartTypeForNewSeries2(
    const std::vector<rtl::Reference<ChartType>>& aFormerlyUsedChartTypes)
{
    rtl::Reference<ChartType> xResult(getChartTypeForIndex(0));
    ChartTypeTemplate::copyPropertiesFromOldToNewCoordinateSystem(aFormerlyUsedChartTypes, xResult);
    return xResult;
}

void SurfaceChartTypeTemplate::applyStyle2(const rtl::Reference<DataSeries>& xSeries,
                                           ::sal_Int32 nChartTypeIndex, ::sal_Int32 nSeriesIndex,
                                           ::sal_Int32 nSeriesCount)
{
    ChartTypeTemplate::applyStyle2(xSeries, nChartTypeIndex, nSeriesIndex, nSeriesCount);
    xSeries->setPropertyAlsoToAllAttributedDataPoints(u"BorderStyle"_ustr,
                                                      uno::Any(drawing::LineStyle_NONE));
}

} //  namespace chart

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
