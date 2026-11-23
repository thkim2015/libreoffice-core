/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * This file incorporates work covered by the following license notice:
 *
 *   Licensed to the Apache Software Foundation (ASF) under one or more
 *   contributor license agreements. See the NOTICE file distributed
 *   with this work for additional information regarding copyright
 *   ownership. The ASF licenses this file to you under the Apache
 *   License, Version 2.0 (the "License"); you may not use this file
 *   except in compliance with the License. You may obtain a copy of
 *   the License at http://www.apache.org/licenses/LICENSE-2.0 .
 */

#include <svx/charthelper.hxx>
#include <comphelper/diagnose_ex.hxx>
#include <com/sun/star/embed/XEmbeddedObject.hpp>
#include <com/sun/star/util/XUpdatable2.hpp>
#include <com/sun/star/drawing/XDrawPageSupplier.hpp>
#include <com/sun/star/beans/XPropertySet.hpp>
#include <com/sun/star/lang/XMultiServiceFactory.hpp>
#include <drawinglayer/geometry/viewinformation2d.hxx>
#include <com/sun/star/chart2/XChartDocument.hpp>
#include <com/sun/star/drawing/FillStyle.hpp>
#include <com/sun/star/drawing/LineStyle.hpp>
#include <sdr/primitive2d/primitivefactory2d.hxx>

using namespace ::com::sun::star;

void ChartHelper::updateChart( const uno::Reference< ::frame::XModel >& rXModel )
{
    if (!rXModel.is())
        return;

    try
    {
        const uno::Reference< lang::XMultiServiceFactory > xChartFact(rXModel, uno::UNO_QUERY_THROW);
        const uno::Reference< util::XUpdatable2 > xChartView(xChartFact->createInstance(u"com.sun.star.chart2.ChartView"_ustr), uno::UNO_QUERY_THROW);

        xChartView->updateHard();
    }
    catch(uno::Exception&)
    {
        TOOLS_WARN_EXCEPTION("svx", "");
    }
}

drawinglayer::primitive2d::Primitive2DContainer ChartHelper::tryToGetChartContentAsPrimitive2DSequence(
    const uno::Reference< ::frame::XModel >& rXModel,
    basegfx::B2DRange& rRange)
{
    drawinglayer::primitive2d::Primitive2DContainer aRetval;

    if (!rXModel.is())
        return aRetval;

    // don't broadcast until we're finished building, more efficient
    rXModel->lockControllers();
    updateChart(rXModel);
    rXModel->unlockControllers();

    try
    {
        const uno::Reference< drawing::XDrawPageSupplier > xDrawPageSupplier(rXModel, uno::UNO_QUERY_THROW);
        const uno::Reference< container::XIndexAccess > xShapeAccess(xDrawPageSupplier->getDrawPage(), uno::UNO_QUERY_THROW);

        if(xShapeAccess->getCount())
        {
            const sal_Int32 nShapeCount(xShapeAccess->getCount());
            const uno::Sequence< beans::PropertyValue > aParams;
            uno::Reference< drawing::XShape > xShape;

            for(sal_Int32 a(0); a < nShapeCount; a++)
            {
                xShapeAccess->getByIndex(a) >>= xShape;

                if(xShape.is())
                {
                    PrimitiveFactory2D::createPrimitivesFromXShape(
                        xShape,
                        aParams,
                        aRetval);
                }
            }
        }
    }
    catch(uno::Exception&)
    {
        TOOLS_WARN_EXCEPTION("svx", "");
    }

    if(!aRetval.empty())
    {
        const drawinglayer::geometry::ViewInformation2D aViewInformation2D;

        rRange = aRetval.getB2DRange(aViewInformation2D);

        // weboffice: map the chart page, not the content bounds, to the object - effects
        // drawn outside the chart area (Excel chart area shadow, perspective shadow, glow)
        // must not shrink and move the chart; they spill over the object like in Excel
        try
        {
            const uno::Reference< drawing::XDrawPageSupplier > xDrawPageSupplier(rXModel, uno::UNO_QUERY_THROW);
            const uno::Reference< beans::XPropertySet > xPageProps(xDrawPageSupplier->getDrawPage(), uno::UNO_QUERY_THROW);
            sal_Int32 nWidth(0);
            sal_Int32 nHeight(0);
            xPageProps->getPropertyValue(u"Width"_ustr) >>= nWidth;
            xPageProps->getPropertyValue(u"Height"_ustr) >>= nHeight;
            if (nWidth > 0 && nHeight > 0
                && rRange.getMinX() <= 1.0 && rRange.getMinY() <= 1.0
                && rRange.getMaxX() >= nWidth - 1.0 && rRange.getMaxY() >= nHeight - 1.0)
                rRange = basegfx::B2DRange(0.0, 0.0, nWidth, nHeight);
        }
        catch(uno::Exception&)
        {
        }
    }

    return aRetval;
}

void ChartHelper::AdaptDefaultsForChart(
    const uno::Reference < embed::XEmbeddedObject > & xEmbObj)
{
    if( !xEmbObj.is())
        return;

    uno::Reference< chart2::XChartDocument > xChartDoc( xEmbObj->getComponent(), uno::UNO_QUERY );
    OSL_ENSURE( xChartDoc.is(), "Trying to set chart property to non-chart OLE" );
    if( !xChartDoc.is())
        return;

    try
    {
        if (uno::Reference< beans::XPropertySet > xPageProp = xChartDoc->getPageBackground())
        {
            // set background to transparent (none)
            xPageProp->setPropertyValue(u"FillStyle"_ustr, uno::Any(drawing::FillStyle_NONE));
            // set no border
            xPageProp->setPropertyValue(u"LineStyle"_ustr, uno::Any(drawing::LineStyle_NONE));
        }
    }
    catch( const uno::Exception & )
    {
        TOOLS_WARN_EXCEPTION("svx.svdraw", "");
    }
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
