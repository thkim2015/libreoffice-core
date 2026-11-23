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

#include <FillProperties.hxx>
#include <com/sun/star/beans/PropertyAttribute.hpp>
#include <com/sun/star/drawing/BitmapMode.hpp>
#include <com/sun/star/drawing/FillStyle.hpp>
#include <com/sun/star/drawing/RectanglePoint.hpp>
#include <com/sun/star/util/XComplexColor.hpp>

#include <tools/color.hxx>

using namespace css;
namespace chart
{

namespace
{

void lcl_AddPropertiesToVector_without_BitmapProperties( std::vector<beans::Property> & rOutProperties )
{
    rOutProperties.emplace_back( "FillStyle",
                  FillProperties::PROP_FILL_STYLE,
                  cppu::UnoType<drawing::FillStyle>::get(),
                  beans::PropertyAttribute::BOUND
                  | beans::PropertyAttribute::MAYBEDEFAULT );

    rOutProperties.emplace_back( "FillColor",
                  FillProperties::PROP_FILL_COLOR,
                  cppu::UnoType<sal_Int32>::get(),
                  beans::PropertyAttribute::BOUND
                  | beans::PropertyAttribute::MAYBEVOID         // "maybe auto"
                  | beans::PropertyAttribute::MAYBEDEFAULT );

    rOutProperties.emplace_back("FillComplexColor",
                  FillProperties::PROP_FILL_COMPLEX_COLOR,
                  cppu::UnoType<util::XComplexColor>::get(),
                  beans::PropertyAttribute::BOUND | beans::PropertyAttribute::MAYBEVOID | beans::PropertyAttribute::MAYBEDEFAULT );

    rOutProperties.emplace_back( "FillTransparence",
                  FillProperties::PROP_FILL_TRANSPARENCE,
                  cppu::UnoType<sal_Int16>::get(),
                  beans::PropertyAttribute::BOUND
                  | beans::PropertyAttribute::MAYBEDEFAULT );

    rOutProperties.emplace_back( "FillTransparenceGradientName",
                  FillProperties::PROP_FILL_TRANSPARENCE_GRADIENT_NAME,
                  cppu::UnoType<OUString>::get(),
                  beans::PropertyAttribute::BOUND
                  | beans::PropertyAttribute::MAYBEVOID
                  | beans::PropertyAttribute::MAYBEDEFAULT );

    rOutProperties.emplace_back( "FillGradientName",
                  FillProperties::PROP_FILL_GRADIENT_NAME,
                  cppu::UnoType<OUString>::get(),
                  beans::PropertyAttribute::BOUND
                  | beans::PropertyAttribute::MAYBEVOID
                  | beans::PropertyAttribute::MAYBEDEFAULT );

    rOutProperties.emplace_back( "FillGradientStepCount",
                  FillProperties::PROP_FILL_GRADIENT_STEPCOUNT,
                  cppu::UnoType<sal_Int16>::get(),
                  beans::PropertyAttribute::BOUND
                  | beans::PropertyAttribute::MAYBEVOID );

    rOutProperties.emplace_back( "FillHatchName",
                  FillProperties::PROP_FILL_HATCH_NAME,
                  cppu::UnoType<OUString>::get(),
                  beans::PropertyAttribute::BOUND
                  | beans::PropertyAttribute::MAYBEVOID
                  | beans::PropertyAttribute::MAYBEDEFAULT );

    //bitmap properties see lcl_AddPropertiesToVector_only_BitmapProperties()

    rOutProperties.emplace_back( "FillBackground",
                  FillProperties::PROP_FILL_BACKGROUND,
                  cppu::UnoType<sal_Bool>::get(),
                  beans::PropertyAttribute::BOUND
                  | beans::PropertyAttribute::MAYBEDEFAULT );
}

void lcl_AddPropertiesToVector_only_BitmapProperties(std::vector<beans::Property> & rOutProperties)
{
    rOutProperties.emplace_back( "FillBitmapName",
                  FillProperties::PROP_FILL_BITMAP_NAME,
                  cppu::UnoType<OUString>::get(),
                  beans::PropertyAttribute::BOUND
                  | beans::PropertyAttribute::MAYBEVOID
                  | beans::PropertyAttribute::MAYBEDEFAULT );

    rOutProperties.emplace_back( "FillBitmapOffsetX",
                  FillProperties::PROP_FILL_BITMAP_OFFSETX,
                  cppu::UnoType<sal_Int16>::get(),
                  beans::PropertyAttribute::BOUND
                  | beans::PropertyAttribute::MAYBEDEFAULT );

    rOutProperties.emplace_back( "FillBitmapOffsetY",
                  FillProperties::PROP_FILL_BITMAP_OFFSETY,
                  cppu::UnoType<sal_Int16>::get(),
                  beans::PropertyAttribute::BOUND
                  | beans::PropertyAttribute::MAYBEDEFAULT );

    rOutProperties.emplace_back( "FillBitmapPositionOffsetX",
                  FillProperties::PROP_FILL_BITMAP_POSITION_OFFSETX,
                  cppu::UnoType<sal_Int16>::get(),
                  beans::PropertyAttribute::BOUND
                  | beans::PropertyAttribute::MAYBEDEFAULT );

    rOutProperties.emplace_back( "FillBitmapPositionOffsetY",
                  FillProperties::PROP_FILL_BITMAP_POSITION_OFFSETY,
                  cppu::UnoType<sal_Int16>::get(),
                  beans::PropertyAttribute::BOUND
                  | beans::PropertyAttribute::MAYBEDEFAULT );

    rOutProperties.emplace_back( "FillBitmapRectanglePoint",
                  FillProperties::PROP_FILL_BITMAP_RECTANGLEPOINT,
                  cppu::UnoType<drawing::RectanglePoint>::get(),
                  beans::PropertyAttribute::BOUND
                  | beans::PropertyAttribute::MAYBEDEFAULT );

    rOutProperties.emplace_back( "FillBitmapLogicalSize",
                  FillProperties::PROP_FILL_BITMAP_LOGICALSIZE,
                  cppu::UnoType<sal_Bool>::get(),
                  beans::PropertyAttribute::BOUND
                  | beans::PropertyAttribute::MAYBEDEFAULT );

    rOutProperties.emplace_back( "FillBitmapSizeX",
                  FillProperties::PROP_FILL_BITMAP_SIZEX,
                  cppu::UnoType<sal_Int32>::get(),
                  beans::PropertyAttribute::BOUND
                  | beans::PropertyAttribute::MAYBEDEFAULT );

    rOutProperties.emplace_back( "FillBitmapSizeY",
                  FillProperties::PROP_FILL_BITMAP_SIZEY,
                  cppu::UnoType<sal_Int32>::get(),
                  beans::PropertyAttribute::BOUND
                  | beans::PropertyAttribute::MAYBEDEFAULT );

    rOutProperties.emplace_back( "FillBitmapMode",
                  FillProperties::PROP_FILL_BITMAP_MODE,
                  cppu::UnoType<drawing::BitmapMode>::get(),
                  beans::PropertyAttribute::BOUND
                  | beans::PropertyAttribute::MAYBEDEFAULT );
}

void lcl_AddDefaultsToMap_without_BitmapProperties(
    ::chart::tPropertyValueMap & rOutMap )
{
    ::chart::PropertyHelper::setPropertyValueDefault( rOutMap, FillProperties::PROP_FILL_STYLE, drawing::FillStyle_SOLID );
    ::chart::PropertyHelper::setPropertyValueDefault( rOutMap, FillProperties::PROP_FILL_COLOR, Color(0xd9, 0xd9, 0xd9) ); // gray85
    ::chart::PropertyHelper::setPropertyValueDefault( rOutMap, FillProperties::PROP_FILL_TRANSPARENCE, sal_Int16(0) );
    ::chart::PropertyHelper::setPropertyValueDefault( rOutMap, FillProperties::PROP_FILL_BACKGROUND, false );
}

void lcl_AddDefaultsToMap_only_BitmapProperties(
    ::chart::tPropertyValueMap & rOutMap )
{
    ::chart::PropertyHelper::setPropertyValueDefault( rOutMap, FillProperties::PROP_FILL_BITMAP_OFFSETX, sal_Int16(0) );
    ::chart::PropertyHelper::setPropertyValueDefault( rOutMap, FillProperties::PROP_FILL_BITMAP_OFFSETY, sal_Int16(0) );
    ::chart::PropertyHelper::setPropertyValueDefault( rOutMap, FillProperties::PROP_FILL_BITMAP_POSITION_OFFSETX, sal_Int16(0) );
    ::chart::PropertyHelper::setPropertyValueDefault( rOutMap, FillProperties::PROP_FILL_BITMAP_POSITION_OFFSETY, sal_Int16(0) );

    ::chart::PropertyHelper::setPropertyValueDefault( rOutMap, FillProperties::PROP_FILL_BITMAP_RECTANGLEPOINT, drawing::RectanglePoint_MIDDLE_MIDDLE );
    ::chart::PropertyHelper::setPropertyValueDefault( rOutMap, FillProperties::PROP_FILL_BITMAP_LOGICALSIZE, true );
    ::chart::PropertyHelper::setPropertyValueDefault( rOutMap, FillProperties::PROP_FILL_BITMAP_SIZEX, sal_Int32(0) );
    ::chart::PropertyHelper::setPropertyValueDefault( rOutMap, FillProperties::PROP_FILL_BITMAP_SIZEY, sal_Int32(0) );
    ::chart::PropertyHelper::setPropertyValueDefault( rOutMap, FillProperties::PROP_FILL_BITMAP_MODE, drawing::BitmapMode_REPEAT );
}

}//end anonymous namespace

void FillProperties::AddZetaEffectPropertiesToVector(std::vector<beans::Property>& rOutProperties)
{
    const auto add = [&](const char* pName, sal_Int32 nId, const uno::Type& rType) {
        rOutProperties.emplace_back(OUString::createFromAscii(pName), nId, rType,
                                    beans::PropertyAttribute::BOUND
                                        | beans::PropertyAttribute::MAYBEDEFAULT);
    };
    add("Shadow", PROP_FILL_ZETA_SHADOW, cppu::UnoType<bool>::get());
    add("ShadowColor", PROP_FILL_ZETA_SHADOW_COLOR, cppu::UnoType<sal_Int32>::get());
    add("ShadowTransparence", PROP_FILL_ZETA_SHADOW_TRANSPARENCE, cppu::UnoType<sal_Int16>::get());
    add("ShadowXDistance", PROP_FILL_ZETA_SHADOW_XDISTANCE, cppu::UnoType<sal_Int32>::get());
    add("ShadowYDistance", PROP_FILL_ZETA_SHADOW_YDISTANCE, cppu::UnoType<sal_Int32>::get());
    add("ShadowBlur", PROP_FILL_ZETA_SHADOW_BLUR, cppu::UnoType<sal_Int32>::get());
    add("GlowEffectRadius", PROP_FILL_ZETA_GLOW_RADIUS, cppu::UnoType<sal_Int32>::get());
    add("GlowEffectColor", PROP_FILL_ZETA_GLOW_COLOR, cppu::UnoType<sal_Int32>::get());
    add("GlowEffectTransparency", PROP_FILL_ZETA_GLOW_TRANSPARENCY, cppu::UnoType<sal_Int16>::get());
    add("SoftEdgeRadius", PROP_FILL_ZETA_SOFTEDGE_RADIUS, cppu::UnoType<sal_Int32>::get());
    add("ShadowSizeX", PROP_FILL_ZETA_SHADOW_SIZEX, cppu::UnoType<sal_Int32>::get());
    add("ShadowSizeY", PROP_FILL_ZETA_SHADOW_SIZEY, cppu::UnoType<sal_Int32>::get());
    add("ShadowAlignment", PROP_FILL_ZETA_SHADOW_ALIGNMENT, cppu::UnoType<sal_Int32>::get());
    add("ZetaShadowKind", PROP_FILL_ZETA_SHADOW_KIND, cppu::UnoType<sal_Int32>::get());
    add("ZetaShadowSkewX", PROP_FILL_ZETA_SHADOW_SKEWX, cppu::UnoType<sal_Int32>::get());
}

void FillProperties::AddZetaEffectDefaultsToMap(::chart::tPropertyValueMap& rOutMap)
{
    ::chart::PropertyHelper::setPropertyValueDefault(rOutMap, PROP_FILL_ZETA_SHADOW, false);
    ::chart::PropertyHelper::setPropertyValueDefault(rOutMap, PROP_FILL_ZETA_SHADOW_COLOR, sal_Int32(0));
    ::chart::PropertyHelper::setPropertyValueDefault(rOutMap, PROP_FILL_ZETA_SHADOW_TRANSPARENCE, sal_Int16(0));
    ::chart::PropertyHelper::setPropertyValueDefault(rOutMap, PROP_FILL_ZETA_SHADOW_XDISTANCE, sal_Int32(0));
    ::chart::PropertyHelper::setPropertyValueDefault(rOutMap, PROP_FILL_ZETA_SHADOW_YDISTANCE, sal_Int32(0));
    ::chart::PropertyHelper::setPropertyValueDefault(rOutMap, PROP_FILL_ZETA_SHADOW_BLUR, sal_Int32(0));
    ::chart::PropertyHelper::setPropertyValueDefault(rOutMap, PROP_FILL_ZETA_GLOW_RADIUS, sal_Int32(0));
    ::chart::PropertyHelper::setPropertyValueDefault(rOutMap, PROP_FILL_ZETA_GLOW_COLOR, sal_Int32(0));
    ::chart::PropertyHelper::setPropertyValueDefault(rOutMap, PROP_FILL_ZETA_GLOW_TRANSPARENCY, sal_Int16(0));
    ::chart::PropertyHelper::setPropertyValueDefault(rOutMap, PROP_FILL_ZETA_SOFTEDGE_RADIUS, sal_Int32(0));
    ::chart::PropertyHelper::setPropertyValueDefault(rOutMap, PROP_FILL_ZETA_SHADOW_SIZEX, sal_Int32(100000));
    ::chart::PropertyHelper::setPropertyValueDefault(rOutMap, PROP_FILL_ZETA_SHADOW_SIZEY, sal_Int32(100000));
    ::chart::PropertyHelper::setPropertyValueDefault(rOutMap, PROP_FILL_ZETA_SHADOW_ALIGNMENT, sal_Int32(0));
    ::chart::PropertyHelper::setPropertyValueDefault(rOutMap, PROP_FILL_ZETA_SHADOW_KIND, sal_Int32(0));
    ::chart::PropertyHelper::setPropertyValueDefault(rOutMap, PROP_FILL_ZETA_SHADOW_SKEWX, sal_Int32(0));
}

void FillProperties::AddPropertiesToVector(
    std::vector<beans::Property> & rOutProperties )
{
    // Fill Properties see service drawing::FillProperties
    lcl_AddPropertiesToVector_without_BitmapProperties( rOutProperties );
    lcl_AddPropertiesToVector_only_BitmapProperties( rOutProperties );
    AddZetaEffectPropertiesToVector( rOutProperties );
}

void FillProperties::AddDefaultsToMap(
    ::chart::tPropertyValueMap & rOutMap )
{
    lcl_AddDefaultsToMap_without_BitmapProperties( rOutMap );
    lcl_AddDefaultsToMap_only_BitmapProperties( rOutMap );
    AddZetaEffectDefaultsToMap( rOutMap );
}

} //  namespace chart

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
