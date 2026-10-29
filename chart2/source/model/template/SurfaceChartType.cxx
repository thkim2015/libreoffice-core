/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "SurfaceChartType.hxx"
#include <servicenames_charttypes.hxx>
#include <Axis.hxx>
#include <AxisHelper.hxx>
#include <AxisIndexDefines.hxx>
#include <CartesianCoordinateSystem.hxx>
#include <PropertyHelper.hxx>

#include <com/sun/star/beans/PropertyAttribute.hpp>
#include <com/sun/star/chart2/AxisType.hpp>

#include <cppuhelper/supportsservice.hxx>

using namespace ::com::sun::star;
using ::com::sun::star::uno::Sequence;
using ::com::sun::star::beans::Property;

namespace
{
enum
{
    PROP_SURFACECHARTTYPE_WIREFRAME,
    PROP_SURFACECHARTTYPE_CONTOUR
};

::cppu::OPropertyArrayHelper& StaticSurfaceChartTypeInfoHelper()
{
    static ::cppu::OPropertyArrayHelper aPropHelper = []() {
        std::vector<css::beans::Property> aProperties;
        aProperties.emplace_back("Wireframe", PROP_SURFACECHARTTYPE_WIREFRAME,
                                 cppu::UnoType<bool>::get(),
                                 beans::PropertyAttribute::BOUND
                                     | beans::PropertyAttribute::MAYBEDEFAULT);
        // Excel c:surfaceChart: the grid seen from above (contour), with coarser bands
        aProperties.emplace_back("Contour", PROP_SURFACECHARTTYPE_CONTOUR,
                                 cppu::UnoType<bool>::get(),
                                 beans::PropertyAttribute::BOUND
                                     | beans::PropertyAttribute::MAYBEDEFAULT);
        std::sort(aProperties.begin(), aProperties.end(), ::chart::PropertyNameLess());
        return comphelper::containerToSequence(aProperties);
    }();
    return aPropHelper;
};

} // anonymous namespace

namespace chart
{
SurfaceChartType::SurfaceChartType() {}

SurfaceChartType::SurfaceChartType(const SurfaceChartType& rOther)
    : ChartType(rOther)
{
}

SurfaceChartType::~SurfaceChartType() {}

// ____ XCloneable ____
uno::Reference<util::XCloneable> SAL_CALL SurfaceChartType::createClone()
{
    return uno::Reference<util::XCloneable>(new SurfaceChartType(*this));
}

rtl::Reference<ChartType> SurfaceChartType::cloneChartType() const
{
    return new SurfaceChartType(*this);
}

// ____ XChartType ____
OUString SAL_CALL SurfaceChartType::getChartType()
{
    return CHART2_SERVICE_NAME_CHARTTYPE_SURFACE;
}

uno::Sequence<OUString> SurfaceChartType::getSupportedPropertyRoles()
{
    return { "FillColor", "BorderColor" };
}

// ____ OPropertySet ____
void SurfaceChartType::GetDefaultValue(sal_Int32 nHandle, uno::Any& rAny) const
{
    static const ::chart::tPropertyValueMap aStaticDefaults = []() {
        ::chart::tPropertyValueMap aTmp;
        ::chart::PropertyHelper::setPropertyValueDefault(aTmp, PROP_SURFACECHARTTYPE_WIREFRAME,
                                                         false);
        ::chart::PropertyHelper::setPropertyValueDefault(aTmp, PROP_SURFACECHARTTYPE_CONTOUR,
                                                         false);
        return aTmp;
    }();
    tPropertyValueMap::const_iterator aFound(aStaticDefaults.find(nHandle));
    if (aFound == aStaticDefaults.end())
        rAny.clear();
    else
        rAny = (*aFound).second;
}

rtl::Reference<::chart::BaseCoordinateSystem>
SurfaceChartType::createCoordinateSystem2(sal_Int32 DimensionCount)
{
    rtl::Reference<CartesianCoordinateSystem> xResult
        = new CartesianCoordinateSystem(DimensionCount);

    for (sal_Int32 i = 0; i < DimensionCount; i++)
    {
        rtl::Reference<Axis> xAxis = xResult->getAxisByDimension2(i, MAIN_AXIS_INDEX);
        if (!xAxis.is())
            continue;

        chart2::ScaleData aScaleData = xAxis->getScaleData();
        aScaleData.Orientation = chart2::AxisOrientation_MATHEMATICAL;
        // the grid points sit on the ticks, like Excel's surface charts
        aScaleData.ShiftedCategoryPosition = false;
        if (i == 1)
        {
            aScaleData.AxisType = chart2::AxisType::REALNUMBER;
            aScaleData.Scaling = AxisHelper::createLinearScaling();
        }
        else
        {
            aScaleData.AxisType = i == 0 ? chart2::AxisType::CATEGORY
                                         : chart2::AxisType::SERIES;
        }
        xAxis->setScaleData(aScaleData);
    }

    return xResult;
}

::cppu::IPropertyArrayHelper& SAL_CALL SurfaceChartType::getInfoHelper()
{
    return StaticSurfaceChartTypeInfoHelper();
}

// ____ XPropertySet ____
uno::Reference<beans::XPropertySetInfo> SAL_CALL SurfaceChartType::getPropertySetInfo()
{
    static uno::Reference<beans::XPropertySetInfo> xPropertySetInfo(
        ::cppu::OPropertySetHelper::createPropertySetInfo(StaticSurfaceChartTypeInfoHelper()));
    return xPropertySetInfo;
}

OUString SAL_CALL SurfaceChartType::getImplementationName()
{
    return "com.sun.star.comp.chart.SurfaceChartType";
}

sal_Bool SAL_CALL SurfaceChartType::supportsService(const OUString& rServiceName)
{
    return cppu::supportsService(this, rServiceName);
}

css::uno::Sequence<OUString> SAL_CALL SurfaceChartType::getSupportedServiceNames()
{
    return { CHART2_SERVICE_NAME_CHARTTYPE_SURFACE, "com.sun.star.chart2.ChartType" };
}

} //  namespace chart

extern "C" SAL_DLLPUBLIC_EXPORT css::uno::XInterface*
com_sun_star_comp_chart_SurfaceChartType_get_implementation(
    css::uno::XComponentContext* /*context*/, css::uno::Sequence<css::uno::Any> const&)
{
    return cppu::acquire(new ::chart::SurfaceChartType);
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
