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

#include <drawingml/textcharacterproperties.hxx>
#include <com/sun/star/lang/Locale.hpp>
#include <com/sun/star/awt/FontSlant.hpp>
#include <com/sun/star/awt/FontWeight.hpp>
#include <com/sun/star/i18n/ScriptType.hpp>
#include <comphelper/sequence.hxx>
#include <comphelper/propertyvalue.hxx>
#include <com/sun/star/geometry/IntegerRectangle2D.hpp>
#include <rtl/ustrbuf.hxx>
#include <algorithm>
#include <vector>
#include <i18nlangtag/languagetag.hxx>
#include <i18nlangtag/mslangid.hxx>
#include <editeng/escapementitem.hxx>
#include <docmodel/uno/UnoComplexColor.hxx>
#include <oox/helper/helper.hxx>
#include <oox/helper/propertyset.hxx>
#include <oox/helper/graphichelper.hxx>
#include <oox/core/xmlfilterbase.hxx>
#include <oox/drawingml/drawingmltypes.hxx>
#include <oox/token/properties.hxx>
#include <oox/token/tokens.hxx>

using ::oox::core::XmlFilterBase;
using namespace ::com::sun::star;
using namespace ::com::sun::star::uno;
using namespace ::com::sun::star::beans;

namespace oox::drawingml {

void TextCharacterProperties::assignUsed( const TextCharacterProperties& rSourceProps )
{
    // overwrite all properties existing in rSourceProps
    maHyperlinkPropertyMap.assignUsed( rSourceProps.maHyperlinkPropertyMap );
    maLatinFont.assignIfUsed( rSourceProps.maLatinFont );
    maLatinThemeFont.assignIfUsed( rSourceProps.maLatinThemeFont );
    maAsianFont.assignIfUsed( rSourceProps.maAsianFont );
    maAsianThemeFont.assignIfUsed( rSourceProps.maAsianThemeFont );
    maComplexFont.assignIfUsed( rSourceProps.maComplexFont );
    maComplexThemeFont.assignIfUsed( rSourceProps.maComplexThemeFont );
    maSymbolFont.assignIfUsed( rSourceProps.maSymbolFont );
    maHighlightColor.assignIfUsed( rSourceProps.maHighlightColor );
    maHighlightOOXColor.assignIfUsed( rSourceProps.maHighlightOOXColor );
    maUnderlineColor.assignIfUsed( rSourceProps.maUnderlineColor );
    assignIfUsed( moLang, rSourceProps.moLang );
    assignIfUsed( moNoProof, rSourceProps.moNoProof );
    assignIfUsed( moHeight, rSourceProps.moHeight );
    assignIfUsed( moFontScale, rSourceProps.moFontScale);
    assignIfUsed( moSpacing, rSourceProps.moSpacing );
    assignIfUsed( moUnderline, rSourceProps.moUnderline );
    assignIfUsed( moBaseline, rSourceProps.moBaseline );
    assignIfUsed( moStrikeout, rSourceProps.moStrikeout );
    assignIfUsed( moCaseMap, rSourceProps.moCaseMap );
    assignIfUsed( moBold, rSourceProps.moBold );
    assignIfUsed( moItalic, rSourceProps.moItalic );
    assignIfUsed( moUnderlineLineFollowText, rSourceProps.moUnderlineLineFollowText );
    assignIfUsed( moUnderlineFillFollowText, rSourceProps.moUnderlineFillFollowText );
    assignIfUsed( moTextOutlineProperties, rSourceProps.moTextOutlineProperties);

    maTextEffectsProperties = rSourceProps.maTextEffectsProperties;
    mpEffectPropertiesPtr->assignUsed(*rSourceProps.mpEffectPropertiesPtr);
    maFillProperties.assignUsed( rSourceProps.maFillProperties );
}

void TextCharacterProperties::pushToPropMap( PropertyMap& rPropMap, const XmlFilterBase& rFilter ) const
{
    OUString aFontName;
    sal_Int16 nFontPitch = 0;
    sal_Int16 nFontFamily = 0;

    bool bRet = maLatinFont.getFontData( aFontName, nFontPitch, nFontFamily, nullptr, rFilter );
    if (!bRet)
        // In case there is no direct font, try to look it up as a theme reference.
        bRet = maLatinThemeFont.getFontData( aFontName, nFontPitch, nFontFamily, nullptr, rFilter );
    if (bRet)
    {
        rPropMap.setProperty( PROP_CharFontName, aFontName);
        rPropMap.setProperty( PROP_CharFontPitch, nFontPitch);
        rPropMap.setProperty( PROP_CharFontFamily, nFontFamily);
    }

    bRet = maAsianFont.getFontData( aFontName, nFontPitch, nFontFamily, nullptr, rFilter );
    if (!bRet)
        bRet = maAsianThemeFont.getFontData( aFontName, nFontPitch, nFontFamily, nullptr, rFilter );
    if (bRet)
    {
        rPropMap.setProperty( PROP_CharFontNameAsian, aFontName);
        rPropMap.setProperty( PROP_CharFontPitchAsian, nFontFamily);
        rPropMap.setProperty( PROP_CharFontFamilyAsian, nFontPitch);
    }

    bRet = maComplexFont.getFontData( aFontName, nFontPitch, nFontFamily, nullptr, rFilter );
    if (!bRet)
        bRet = maComplexThemeFont.getFontData( aFontName, nFontPitch, nFontFamily, nullptr, rFilter );
    if (bRet)
    {
        rPropMap.setProperty( PROP_CharFontNameComplex, aFontName);
        rPropMap.setProperty( PROP_CharFontPitchComplex, nFontPitch);
        rPropMap.setProperty( PROP_CharFontFamilyComplex, nFontFamily);
    }

    if ( maFillProperties.moFillType.has_value() )
    {
        Color aOOXColor = maFillProperties.getBestSolidColor();
        bool bContoured = false;

        // noFill doesn't exist for characters. Map noFill to 99% transparency
        if (maFillProperties.moFillType.value() == XML_noFill)
            aOOXColor.addTransformation(XML_alpha, 1000);

        // tdf#137438 Emulate text outline color/transparency.
        // If the outline color dominates, then use it as the text color.
        if (moTextOutlineProperties.has_value()
            && moTextOutlineProperties.value().maLineFill.moFillType.has_value()
            && moTextOutlineProperties.value().maLineFill.moFillType.value() != XML_noFill)
        {
            Color aLineColor = moTextOutlineProperties.value().maLineFill.getBestSolidColor();
            sal_Int16 nLineTransparency = aLineColor.getTransparency();

            // tdf#127696 If the text color is white (and the outline color doesn't dominate),
            //            then this is contoured text in LO.
            if (nLineTransparency < aOOXColor.getTransparency()
                || (bContoured = aOOXColor.getColor(rFilter.getGraphicHelper()) == COL_WHITE))
                aOOXColor = std::move(aLineColor);
        }
        rPropMap.setProperty(PROP_CharColor, aOOXColor.getColor(rFilter.getGraphicHelper()));

        // set theme color
        model::ComplexColor aComplexColor = aOOXColor.getComplexColor();
        sal_Int32 nToken = Color::getColorMapToken(aOOXColor.getSchemeColorName());
        if (nToken != -1)
        {
            rFilter.getGraphicHelper().getSchemeColorToken(nToken);
            model::ThemeColorType eThemeColorType = schemeTokenToThemeColorType(nToken);
            aComplexColor.setThemeColor(eThemeColorType);
        }
        rPropMap.setProperty(PROP_CharComplexColor, model::color::createXComplexColor(aComplexColor));
        rPropMap.setProperty(PROP_CharContoured, bContoured);

        if (aOOXColor.hasTransparency())
        {
            const auto nTransparency = aOOXColor.getTransparency();
            rPropMap.setProperty(PROP_CharTransparence, nTransparency);

            // WORKAROUND: Fully transparent white has the same value as COL_AUTO, avoid collision
            if (nTransparency == 100
                && aOOXColor.getColor(rFilter.getGraphicHelper()).GetRGBColor() == COL_AUTO.GetRGBColor())
                rPropMap.setProperty(PROP_CharColor, ::Color(ColorTransparency, 0xFFFFFFFE));
        }
    }

    if( moLang.has_value() && !moLang.value().isEmpty() )
    {
        LanguageTag aTag(moLang.value());
        lang::Locale aLocale(aTag.getLocale());
        switch(MsLangId::getScriptType(aTag.getLanguageType()))
        {
            case css::i18n::ScriptType::LATIN:
                rPropMap.setProperty( PROP_CharLocale, aLocale);break;
            case css::i18n::ScriptType::ASIAN:
                rPropMap.setProperty( PROP_CharLocaleAsian, aLocale);
                if (mbLangWestern)
                    rPropMap.setProperty( PROP_CharLocale, aLocale);
                break;
            case css::i18n::ScriptType::COMPLEX:
                rPropMap.setProperty( PROP_CharLocaleComplex, aLocale);break;
        }
    }

    // PowerPoint "Do not check spelling" (noProof="1"): LibreOffice skips spelling for the
    // "[None]" language; the original lang is kept in the grab bag for the export
    if (moNoProof.value_or(false))
    {
        const lang::Locale aNone(LanguageTag::convertToLocale(LANGUAGE_NONE));
        rPropMap.setProperty(PROP_CharLocale, aNone);
        rPropMap.setProperty(PROP_CharLocaleAsian, aNone);
        rPropMap.setProperty(PROP_CharLocaleComplex, aNone);
    }

    if( moHeight.has_value() )
    {
        float fHeight = GetFontHeight( moHeight.value() );
        if (moFontScale.has_value())
            fHeight *= (moFontScale.value() / 100000);
        rPropMap.setProperty( PROP_CharHeight, fHeight);
        rPropMap.setProperty( PROP_CharHeightAsian, fHeight);
        rPropMap.setProperty( PROP_CharHeightComplex, fHeight);
    }

    rPropMap.setProperty( PROP_CharKerning, static_cast<sal_Int16>(GetTextSpacingPoint( moSpacing.value_or( 0 ) )));

    rPropMap.setProperty( PROP_CharUnderline, GetFontUnderline( moUnderline.value_or( XML_none ) ));
    if( moUnderline.has_value() )
        rPropMap.setProperty( PROP_CharWordMode, *moUnderline == XML_words );
    rPropMap.setProperty( PROP_CharStrikeout, GetFontStrikeout( moStrikeout.value_or( XML_noStrike ) ));
    rPropMap.setProperty( PROP_CharCaseMap, GetCaseMap( moCaseMap.value_or( XML_none ) ));

    if( moBaseline.has_value() ) {
        rPropMap.setProperty( PROP_CharEscapement, sal_Int16(moBaseline.value_or( 0 ) / 1000));
        rPropMap.setProperty( PROP_CharEscapementHeight, sal_Int8(DFLT_ESC_PROP));
    } else {
        rPropMap.setProperty( PROP_CharEscapement, sal_Int16(0));
        rPropMap.setProperty( PROP_CharEscapementHeight, sal_Int8(100)); // 100%
    }

    float fWeight = moBold.value_or( false ) ? awt::FontWeight::BOLD : awt::FontWeight::NORMAL;
    rPropMap.setProperty( PROP_CharWeight, fWeight);
    rPropMap.setProperty( PROP_CharWeightAsian, fWeight);
    rPropMap.setProperty( PROP_CharWeightComplex, fWeight);

    awt::FontSlant eSlant = moItalic.value_or( false ) ? awt::FontSlant_ITALIC : awt::FontSlant_NONE;
    rPropMap.setProperty( PROP_CharPosture, eSlant);
    rPropMap.setProperty( PROP_CharPostureAsian, eSlant);
    rPropMap.setProperty( PROP_CharPostureComplex, eSlant);

    bool bUnderlineFillFollowText = moUnderlineFillFollowText.value_or( false );
    if( moUnderline.has_value() && maUnderlineColor.isUsed() && !bUnderlineFillFollowText )
    {
        rPropMap.setProperty( PROP_CharUnderlineHasColor, true);
        rPropMap.setProperty( PROP_CharUnderlineColor, maUnderlineColor.getRGBColor());
        rPropMap.setProperty( PROP_CharUnderlineComplexColor, model::color::createXComplexColor(maUnderlineColor));
    }
    else
    {
        rPropMap.setProperty( PROP_CharUnderlineHasColor, false);
        rPropMap.setProperty( PROP_CharUnderlineColor, sal_Int32(-1));
    }

    if (maHighlightColor.isUsed() && !maHighlightColor.isTransparent())
    {
        rPropMap.setProperty(PROP_CharBackColor, maHighlightOOXColor.getColor( rFilter.getGraphicHelper() ));
        rPropMap.setProperty(PROP_CharBackgroundComplexColor, model::color::createXComplexColor(maHighlightColor));
    }
    else
        rPropMap.setProperty( PROP_CharBackColor, sal_Int32(-1));
}

static void pushToGrabBag( PropertySet& rPropSet, const std::vector<PropertyValue>& aVectorOfPropertyValues )
{
    if (!rPropSet.hasProperty(PROP_CharInteropGrabBag) || aVectorOfPropertyValues.empty())
        return;
    Sequence<PropertyValue> aGrabBag;
    Any aAnyGrabBag = rPropSet.getAnyProperty(PROP_CharInteropGrabBag);
    aAnyGrabBag >>= aGrabBag;

    rPropSet.setAnyProperty(PROP_CharInteropGrabBag, Any(comphelper::concatSequences(aGrabBag, aVectorOfPropertyValues)));
}

// zeta: 글자 그라데이션(a:rPr/a:gradFill)을 화면에 그리도록 editeng에 넘길 글 —
// 'ZG1 <글자색(첫 색)> <L|R|Q> <각도 1/10도> <X 중심%> <Y 중심%> <위치>:<색> ...'(색은 16진 RGB).
// 글자색은 LibreOffice가 넣는 단색(getBestSolidColor)과 같다 — 글자색을 바꾸면 editeng이 단색으로 그린다.
static OUString lclZetaTextGradientSpec(const FillProperties& rFill, const GraphicHelper& rGraphicHelper)
{
    const GradientFillProperties& rGrad = rFill.maGradientProps;
    if (rGrad.maGradientStops.size() < 2)
        return OUString();
    OUStringBuffer aBuf("ZG1 "
                        + OUString::number(sal_uInt32(rFill.getBestSolidColor().getColor(rGraphicHelper).GetRGBColor()), 16));
    bool bReverse = false;
    if (rGrad.moGradientPath.has_value())
    {
        // fillproperties.cxx 도형 그라데이션과 같은 중심·차례(경로 그라데이션은 멈춤점을 뒤집는다)
        const css::geometry::IntegerRectangle2D aFillToRect = rGrad.moFillToRect.value_or(
            css::geometry::IntegerRectangle2D(0, 0, MAX_PERCENT, MAX_PERCENT));
        const sal_Int32 nCenterX = (MAX_PERCENT + aFillToRect.X1 - aFillToRect.X2) / 2 / PER_PERCENT;
        const sal_Int32 nCenterY = (MAX_PERCENT + aFillToRect.Y1 - aFillToRect.Y2) / 2 / PER_PERCENT;
        aBuf.append(rGrad.moGradientPath.value() == XML_circle ? u" R 0 " : u" Q 0 ");
        aBuf.append(OUString::number(std::clamp<sal_Int32>(nCenterX, 0, 100)) + " "
                    + OUString::number(std::clamp<sal_Int32>(nCenterY, 0, 100)));
        bReverse = true;
    }
    else
    {
        const sal_Int32 nDmlAngle = rGrad.moShadeAngle.value_or(0);
        aBuf.append(" L " + OUString::number((8100 - (nDmlAngle / (PER_DEGREE / 10))) % 3600) + " 0 0");
    }
    for (const auto& rStop : rGrad.maGradientStops)
    {
        const double fPos = bReverse ? 1.0 - rStop.first : rStop.first;
        aBuf.append(" " + OUString::number(fPos) + ":"
                    + OUString::number(sal_uInt32(rStop.second.getColor(rGraphicHelper).GetRGBColor()), 16));
    }
    return aBuf.makeStringAndClear();
}

void TextCharacterProperties::pushToPropSet( PropertySet& rPropSet, const XmlFilterBase& rFilter ) const
{
    PropertyMap aPropMap;
    pushToPropMap( aPropMap, rFilter );
    rPropSet.setProperties( aPropMap );
    pushToGrabBag(rPropSet, maTextEffectsProperties);

    // zeta: 글자 그라데이션 — 앞 조각에서 이어 받은 것은 지우고, 이 조각이 그라데이션이면 넣는다
    if (!rPropSet.hasProperty(PROP_CharInteropGrabBag))
        return;
    OUString aSpec;
    if (maFillProperties.moFillType.has_value() && maFillProperties.moFillType.value() == XML_gradFill)
        aSpec = lclZetaTextGradientSpec(maFillProperties, rFilter.getGraphicHelper());
    Sequence<PropertyValue> aGrabBag;
    rPropSet.getAnyProperty(PROP_CharInteropGrabBag) >>= aGrabBag;
    std::vector<PropertyValue> aNew;
    bool bChanged = false;
    for (const PropertyValue& rProp : aGrabBag)
    {
        if (rProp.Name == "ZetaTextGradient" || rProp.Name == "ZetaNoProofLang"
            || rProp.Name == "ZetaTextOutline")
            bChanged = true;
        else
            aNew.push_back(rProp);
    }
    if (moNoProof.value_or(false) && moLang.has_value() && !moLang.value().isEmpty())
    {
        aNew.push_back(comphelper::makePropertyValue(u"ZetaNoProofLang"_ustr, moLang.value()));
        bChanged = true;
    }
    if (!aSpec.isEmpty())
    {
        aNew.push_back(comphelper::makePropertyValue(u"ZetaTextGradient"_ustr, aSpec));
        bChanged = true;
    }
    // weboffice: text outline (a:ln) for drawing only - 'ZO1 <char color now> <width EMU> <line RGB>
    // <line transparency %> <fill RGB or ->'. editeng strokes the glyphs with it; the fill is drawn
    // under contoured (white) text only while the char color is still the one set here.
    if (moTextOutlineProperties.has_value()
        && moTextOutlineProperties.value().maLineFill.moFillType.has_value()
        && moTextOutlineProperties.value().maLineFill.moFillType.value() == XML_solidFill)
    {
        const LineProperties& rLine = moTextOutlineProperties.value();
        Color aLineColor = rLine.maLineFill.getBestSolidColor();
        sal_Int32 nCharColor = -1;
        rPropSet.getAnyProperty(PROP_CharColor) >>= nCharColor;
        OUString aFill(u"-"_ustr);
        if (maFillProperties.moFillType.has_value() && maFillProperties.moFillType.value() == XML_solidFill)
            aFill = OUString::number(
                sal_uInt32(maFillProperties.getBestSolidColor().getColor(rFilter.getGraphicHelper()).GetRGBColor()), 16);
        aNew.push_back(comphelper::makePropertyValue(u"ZetaTextOutline"_ustr,
            "ZO1 " + OUString::number(sal_uInt32(nCharColor) & 0xFFFFFF, 16)
            + " " + OUString::number(rLine.moLineWidth.value_or(0))
            + " " + OUString::number(sal_uInt32(aLineColor.getColor(rFilter.getGraphicHelper()).GetRGBColor()), 16)
            + " " + OUString::number(aLineColor.getTransparency())
            + " " + aFill));
        bChanged = true;
    }
    if (bChanged)
        rPropSet.setAnyProperty(PROP_CharInteropGrabBag, Any(comphelper::containerToSequence(aNew)));
}

float TextCharacterProperties::getCharHeightPoints( float fDefault ) const
{
    return moHeight.has_value() ? GetFontHeight( moHeight.value() ) : fDefault;
}

} // namespace oox::drawingml

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
