/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include <drawingml/effectproperties.hxx>
#include <oox/drawingml/drawingmltypes.hxx>
#include <oox/helper/graphichelper.hxx>
#include <oox/token/properties.hxx>

#include <basegfx/numeric/ftools.hxx>
#include <comphelper/propertyvalue.hxx>
#include <docmodel/theme/FormatScheme.hxx>

#include <algorithm>
#include <cmath>

namespace oox::drawingml {

void EffectGlowProperties ::assignUsed(const EffectGlowProperties& rSourceProps)
{
    assignIfUsed( moGlowRad, rSourceProps.moGlowRad );
    moGlowColor.assignIfUsed( rSourceProps.moGlowColor );
}

void EffectSoftEdgeProperties::assignUsed(const EffectSoftEdgeProperties& rSourceProps)
{
    assignIfUsed(moRad, rSourceProps.moRad);
}

void EffectShadowProperties::assignUsed(const EffectShadowProperties& rSourceProps)
{
    assignIfUsed( moShadowDist, rSourceProps.moShadowDist );
    assignIfUsed( moShadowDir, rSourceProps.moShadowDir );
    assignIfUsed( moShadowSx, rSourceProps.moShadowSx );
    assignIfUsed( moShadowSy, rSourceProps.moShadowSy );
    moShadowColor.assignIfUsed( rSourceProps.moShadowColor );
    assignIfUsed( moShadowBlur, rSourceProps.moShadowBlur );
    assignIfUsed( moShadowAlignment, rSourceProps.moShadowAlignment );

}

void EffectProperties::assignUsed( const EffectProperties& rSourceProps )
{
    maShadow.assignUsed(rSourceProps.maShadow);
    maGlow.assignUsed(rSourceProps.maGlow);
    maSoftEdge.assignUsed(rSourceProps.maSoftEdge);
    if (!rSourceProps.m_Effects.empty())
    {
        m_Effects.clear();
        m_Effects.reserve(rSourceProps.m_Effects.size());
        for (auto const& it : rSourceProps.m_Effects)
        {
            m_Effects.push_back(std::make_unique<Effect>(*it));
        }
    }
}

PresetShadowApprox approximatePresetShadow(std::u16string_view rPrst, sal_Int64 nDistEmu,
                                           sal_Int64 nDir)
{
    // zeta: measured on PowerPoint 2021 (Shadow.Type 1-20 exported as pictures): the drop presets
    // are offset copies (shdw9 75 % from the top left, shdw10 125 % from the bottom right), the
    // back perspective ones lie behind the lower half, the front ones below the shape
    PresetShadowApprox aRet;
    const double fAngle = basegfx::deg2rad(static_cast<double>(nDir) / PER_DEGREE);
    const sal_Int32 nDist = convertEmuToHmm(nDistEmu);
    aRet.nXDist = std::cos(fAngle) * nDist;
    aRet.nYDist = std::sin(fAngle) * nDist;
    constexpr sal_Int32 nSlant = 1058; // 30 pt: half of the slant of the perspective presets
    const auto eTop = static_cast<sal_Int32>(model::RectangleAlignment::Top);
    const auto eBottom = static_cast<sal_Int32>(model::RectangleAlignment::Bottom);
    if (rPrst == u"shdw9")
    {
        aRet.nSizeX = aRet.nSizeY = 75000;
        aRet.nAlignment = static_cast<sal_Int32>(model::RectangleAlignment::TopLeft);
    }
    else if (rPrst == u"shdw10")
    {
        aRet.nSizeX = aRet.nSizeY = 125000;
        aRet.nAlignment = static_cast<sal_Int32>(model::RectangleAlignment::BottomRight);
    }
    else if (rPrst == u"shdw3" || rPrst == u"shdw11" || rPrst == u"shdw4" || rPrst == u"shdw12"
             || rPrst == u"shdw19")
    {
        aRet.nSizeY = 50000;
        aRet.nAlignment = eBottom;
        if (rPrst == u"shdw3" || rPrst == u"shdw11")
            aRet.nXDist -= nSlant;
        else if (rPrst != u"shdw19")
            aRet.nXDist += nSlant;
    }
    else if (rPrst == u"shdw7" || rPrst == u"shdw15" || rPrst == u"shdw8" || rPrst == u"shdw16")
    {
        aRet.nSizeY = 150000;
        aRet.nAlignment = eTop;
        aRet.nXDist += (rPrst == u"shdw7" || rPrst == u"shdw15") ? -nSlant : nSlant;
    }
    else if (rPrst == u"shdw20")
    {
        aRet.nSizeY = 200000;
        aRet.nAlignment = eTop;
    }
    return aRet;
}

void EffectProperties::pushToPropMap( PropertyMap& rPropMap,
        const GraphicHelper& rGraphicHelper ) const
{
    const bool bOuterShdw = std::any_of(m_Effects.begin(), m_Effects.end(),
                                        [](const auto& p) { return p->msName == "outerShdw"; });
    for (auto const& it : m_Effects)
    {
        if (it->msName == "prstShdw" && !bOuterShdw)
        {
            // zeta: draw the preset shadow approximately (the export writes the prstShdw again
            // while these properties are unchanged, see DrawingML::WriteShapeEffects)
            OUString sPrst;
            sal_Int32 nDist = 0, nDir = 0;
            auto attribIt = it->maAttribs.find(u"prst"_ustr);
            if (attribIt != it->maAttribs.end())
                attribIt->second >>= sPrst;
            attribIt = it->maAttribs.find(u"dist"_ustr);
            if (attribIt != it->maAttribs.end())
                attribIt->second >>= nDist;
            attribIt = it->maAttribs.find(u"dir"_ustr);
            if (attribIt != it->maAttribs.end())
                attribIt->second >>= nDir;
            const PresetShadowApprox aShadow = approximatePresetShadow(sPrst, nDist, nDir);
            rPropMap.setProperty(PROP_Shadow, true);
            rPropMap.setProperty(PROP_ShadowXDistance, aShadow.nXDist);
            rPropMap.setProperty(PROP_ShadowYDistance, aShadow.nYDist);
            rPropMap.setProperty(PROP_ShadowSizeX, aShadow.nSizeX);
            rPropMap.setProperty(PROP_ShadowSizeY, aShadow.nSizeY);
            rPropMap.setProperty(PROP_ShadowColor, it->moColor.getColor(rGraphicHelper));
            rPropMap.setProperty(PROP_ShadowTransparence, it->moColor.getTransparency());
            rPropMap.setProperty(PROP_ShadowBlur, sal_Int32(0));
            rPropMap.setProperty(PROP_ShadowAlignment, aShadow.nAlignment);
            continue;
        }
        if( it->msName == "outerShdw" )
        {
            sal_Int32 nAttrDir = 0, nAttrDist = 0;
            sal_Int32 nAttrSizeX = 100000, nAttrSizeY = 100000; // If shadow size is %100=100000 (means equal to object's size), sx sy is not exists,
                                                                // Default values of sx, sy should be 100000 in this case.
            sal_Int32 nAttrBlur = 0;

            std::map< OUString, css::uno::Any >::const_iterator attribIt = it->maAttribs.find( u"dir"_ustr );
            if( attribIt != it->maAttribs.end() )
                attribIt->second >>= nAttrDir;

            attribIt = it->maAttribs.find( u"dist"_ustr );
            if( attribIt != it->maAttribs.end() )
                attribIt->second >>= nAttrDist;

            attribIt = it->maAttribs.find( u"sx"_ustr );
            if( attribIt != it->maAttribs.end() )
                attribIt->second >>= nAttrSizeX;

            attribIt = it->maAttribs.find( u"sy"_ustr );
            if( attribIt != it->maAttribs.end() )
                attribIt->second >>= nAttrSizeY;

            attribIt = it->maAttribs.find( u"blurRad"_ustr );
            if( attribIt != it->maAttribs.end() )
                attribIt->second >>= nAttrBlur;

            // Negative X or Y dist indicates left or up, respectively
            // Negative X or Y dist indicates left or up, respectively
            double nAngle = basegfx::deg2rad(static_cast<double>(nAttrDir) / PER_DEGREE);
            sal_Int32 nDist = convertEmuToHmm( nAttrDist );
            sal_Int32 nXDist = cos(nAngle) * nDist;
            sal_Int32 nYDist = sin(nAngle) * nDist;


            rPropMap.setProperty( PROP_Shadow, true );
            rPropMap.setProperty( PROP_ShadowXDistance, nXDist);
            rPropMap.setProperty( PROP_ShadowYDistance, nYDist);
            rPropMap.setProperty( PROP_ShadowSizeX, nAttrSizeX);
            rPropMap.setProperty( PROP_ShadowSizeY, nAttrSizeY);
            rPropMap.setProperty( PROP_ShadowColor, it->moColor.getColor(rGraphicHelper ) );
            rPropMap.setProperty( PROP_ShadowTransparence, it->moColor.getTransparency());
            rPropMap.setProperty( PROP_ShadowBlur, convertEmuToHmm(nAttrBlur));
            rPropMap.setProperty(
                PROP_ShadowAlignment,
                static_cast<sal_Int32>(maShadow.moShadowAlignment.value_or(model::RectangleAlignment::Bottom)));

        }
    }
}

css::beans::PropertyValue Effect::getEffect()
{
    css::beans::PropertyValue aRet;
    if( msName.isEmpty() )
        return aRet;

    css::uno::Sequence< css::beans::PropertyValue > aSeq( maAttribs.size() );
    std::transform(maAttribs.begin(), maAttribs.end(), aSeq.getArray(),
                   [](const auto& attrib)
                   { return comphelper::makePropertyValue(attrib.first, attrib.second); });

    aRet.Name = msName;
    aRet.Value <<= aSeq;

    return aRet;
}

} // namespace oox::drawingml

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
