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

#include <editeng/StripPortionsHelper.hxx>
#include <optional>
#include <functional>
#include <drawinglayer/primitive2d/PolygonStrokePrimitive2D.hxx>
#include <basegfx/polygon/b2dlinegeometry.hxx>
#include <drawinglayer/primitive2d/textdecoratedprimitive2d.hxx>
#include <drawinglayer/attribute/fontattribute.hxx>
#include <drawinglayer/primitive2d/textlayoutdevice.hxx>
#include <editeng/escapementitem.hxx>
#include <drawinglayer/primitive2d/texthierarchyprimitive2d.hxx>
#include <editeng/smallcaps.hxx>
#include <editeng/outliner.hxx>
#include <drawinglayer/primitive2d/drawinglayer_primitivetypes2d.hxx>
#include <drawinglayer/primitive2d/textdecoratedprimitive2d.hxx>
#include <drawinglayer/primitive2d/textbreakuphelper.hxx>
#include <drawinglayer/primitive2d/unifiedtransparenceprimitive2d.hxx>
#include <drawinglayer/primitive2d/wrongspellprimitive2d.hxx>
#include <drawinglayer/primitive2d/graphicprimitive2d.hxx>
#include <vcl/fntstyle.hxx>
#include <drawinglayer/attribute/fillgradientattribute.hxx>
#include <drawinglayer/geometry/viewinformation2d.hxx>
#include <drawinglayer/primitive2d/fillgradientprimitive2d.hxx>
#include <drawinglayer/primitive2d/modifiedcolorprimitive2d.hxx>
#include <drawinglayer/primitive2d/PolyPolygonColorPrimitive2D.hxx>
#include <drawinglayer/primitive2d/transparenceprimitive2d.hxx>
#include <drawinglayer/primitive2d/transformprimitive2d.hxx>
#include <drawinglayer/primitive2d/PolyPolygonStrokePrimitive2D.hxx>
#include <drawinglayer/primitive2d/groupprimitive2d.hxx>
#include <drawinglayer/primitive2d/shadowprimitive2d.hxx>
#include <drawinglayer/primitive2d/maskprimitive2d.hxx>
#include <basegfx/matrix/b2dhommatrixtools.hxx>
#include <drawinglayer/primitive2d/textprimitive2d.hxx>
#include <drawinglayer/attribute/lineattribute.hxx>
#include <o3tl/string_view.hxx>
#include <basegfx/color/bcolormodifier.hxx>
#include <basegfx/numeric/ftools.hxx>
#include <basegfx/polygon/b2dpolygontools.hxx>
#include <basegfx/polygon/b2dpolypolygon.hxx>
#include <basegfx/utils/bgradient.hxx>
#include <o3tl/string_view.hxx>
#include <algorithm>
#include <cmath>

// anonymous Outline/EditEngine decompose helpers
namespace
{
rtl::Reference<drawinglayer::primitive2d::BasePrimitive2D>
CheckFieldPrimitive(drawinglayer::primitive2d::BasePrimitive2D* pPrimitive,
                    const DrawPortionInfo& rInfo)
{
    rtl::Reference<drawinglayer::primitive2d::BasePrimitive2D> xRet = pPrimitive;
    if (rInfo.mpFieldData)
    {
        // Support for FIELD_SEQ_BEGIN, FIELD_SEQ_END. If used, create a TextHierarchyFieldPrimitive2D
        // which holds the field type and, if applicable, the URL
        const SvxURLField* pURLField = dynamic_cast<const SvxURLField*>(rInfo.mpFieldData);
        const SvxPageField* pPageField = dynamic_cast<const SvxPageField*>(rInfo.mpFieldData);

        // embed current primitive to a sequence
        drawinglayer::primitive2d::Primitive2DContainer aSequence;

        if (pPrimitive)
        {
            aSequence.resize(1);
            aSequence[0] = drawinglayer::primitive2d::Primitive2DReference(pPrimitive);
        }

        if (pURLField)
        {
            // extended this to hold more of the contents of the original
            // SvxURLField since that stuff is still used in HitTest and e.g. Calc
            std::vector<std::pair<OUString, OUString>> meValues;
            meValues.emplace_back("URL", pURLField->GetURL());
            meValues.emplace_back("Representation", pURLField->GetRepresentation());
            meValues.emplace_back("TargetFrame", pURLField->GetTargetFrame());
            meValues.emplace_back("AltText", pURLField->GetName());
            meValues.emplace_back(
                "SvxURLFormat", OUString::number(static_cast<sal_uInt16>(pURLField->GetFormat())));
            xRet = new drawinglayer::primitive2d::TextHierarchyFieldPrimitive2D(
                std::move(aSequence), drawinglayer::primitive2d::FIELD_TYPE_URL, &meValues);
        }
        else if (pPageField)
        {
            xRet = new drawinglayer::primitive2d::TextHierarchyFieldPrimitive2D(
                std::move(aSequence), drawinglayer::primitive2d::FIELD_TYPE_PAGE);
        }
        else
        {
            xRet = new drawinglayer::primitive2d::TextHierarchyFieldPrimitive2D(
                std::move(aSequence), drawinglayer::primitive2d::FIELD_TYPE_COMMON);
        }
    }

    return xRet;
}

// weboffice: contoured (white) text with a text outline 'ZO1 <char color> <w> <line> <trans> <fill>'
// whose char color is still the imported one: the fill color to draw it with instead of LO's contour
// effect (glyph moved 8 ways in the outline color - it covers the fill and thickens the outline)
std::optional<Color> zetaOutlineFill(const DrawPortionInfo& rInfo)
{
    if (rInfo.maZetaTextOutline.isEmpty() || !rInfo.maZetaTextGradient.isEmpty()
        || !rInfo.mrFont.IsOutline())
        return std::nullopt;
    sal_Int32 nIdx = 0;
    if (o3tl::getToken(rInfo.maZetaTextOutline, 0, ' ', nIdx) != u"ZO1" || nIdx < 0)
        return std::nullopt;
    const sal_uInt32 nAt = o3tl::toUInt32(o3tl::getToken(rInfo.maZetaTextOutline, 0, ' ', nIdx), 16);
    for (int i = 0; i < 3 && nIdx >= 0; ++i)
        o3tl::getToken(rInfo.maZetaTextOutline, 0, ' ', nIdx);
    if (nIdx < 0)
        return std::nullopt;
    const std::u16string_view aFill = o3tl::getToken(rInfo.maZetaTextOutline, 0, ' ', nIdx);
    if (aFill.empty() || aFill == u"-"
        || (sal_uInt32(rInfo.mrFont.GetColor().GetRGBColor()) & 0xFFFFFF) != (nAt & 0xFFFFFF))
        return std::nullopt;
    return Color(ColorTransparency, o3tl::toUInt32(aFill, 16) & 0xFFFFFF);
}

rtl::Reference<drawinglayer::primitive2d::BasePrimitive2D>
buildTextPortionPrimitive(const DrawPortionInfo& rInfo, const OUString& rText,
                          const drawinglayer::attribute::FontAttribute& rZetaFontAttribute,
                          const std::vector<double>& rDXArray,
                          const basegfx::B2DHomMatrix& rNewTransform)
{
    // weboffice: outlined contoured text is drawn filled (underline/strikeout stay; auto colors = fill)
    const std::optional<Color> oZetaFill(zetaOutlineFill(rInfo));
    const drawinglayer::attribute::FontAttribute rFontAttribute
        = oZetaFill ? drawinglayer::attribute::FontAttribute(
              rZetaFontAttribute.getFamilyName(), rZetaFontAttribute.getStyleName(),
              rZetaFontAttribute.getWeight(), rZetaFontAttribute.getSymbol(),
              rZetaFontAttribute.getVertical(), rZetaFontAttribute.getItalic(),
              rZetaFontAttribute.getMonospaced(), false, rZetaFontAttribute.getRTL(),
              rZetaFontAttribute.getBiDiStrong())
                    : rZetaFontAttribute;
    ::std::vector<sal_Bool> aKashidaArray;

    if (!rInfo.mpKashidaArray.empty() && rInfo.mnTextLen)
    {
        aKashidaArray.reserve(rInfo.mnTextLen);

        for (sal_Int32 a = 0; a < rInfo.mnTextLen; a++)
        {
            aKashidaArray.push_back(rInfo.mpKashidaArray[a]);
        }
    }

    // create complex text primitive and append
    const Color aFontColor(oZetaFill ? *oZetaFill : rInfo.mrFont.GetColor());
    const basegfx::BColor aBFontColor(aFontColor.getBColor());

    const Color aTextFillColor(rInfo.mrFont.GetFillColor());

    // prepare wordLineMode (for underline and strikeout)
    // NOT for bullet texts. It is set (this may be an error by itself), but needs to be suppressed to hinder e.g. '1)'
    // to be split which would not look like the original
    const bool bWordLineMode(rInfo.mrFont.IsWordLineMode() && !rInfo.mbEndOfBullet);

    // prepare new primitive
    rtl::Reference<drawinglayer::primitive2d::BasePrimitive2D> pNewPrimitive;
    const bool bDecoratedIsNeeded(
        LINESTYLE_NONE != rInfo.mrFont.GetOverline()
        || LINESTYLE_NONE != rInfo.mrFont.GetUnderline()
        || STRIKEOUT_NONE != rInfo.mrFont.GetStrikeout()
        || FontEmphasisMark::NONE != (rInfo.mrFont.GetEmphasisMark() & FontEmphasisMark::Style)
        || FontRelief::NONE != rInfo.mrFont.GetRelief() || rInfo.mrFont.IsShadow()
        || bWordLineMode);

    if (bDecoratedIsNeeded)
    {
        // TextDecoratedPortionPrimitive2D needed, prepare some more data
        // get overline and underline color. If it's on automatic (0xffffffff) use FontColor instead
        const Color aUnderlineColor(rInfo.maTextLineColor);
        const basegfx::BColor aBUnderlineColor(
            (aUnderlineColor == COL_AUTO) ? aBFontColor : aUnderlineColor.getBColor());
        const Color aOverlineColor(rInfo.maOverlineColor);
        const basegfx::BColor aBOverlineColor(
            (aOverlineColor == COL_AUTO) ? aBFontColor : aOverlineColor.getBColor());

        // prepare overline and underline data
        const drawinglayer::primitive2d::TextLine eFontOverline(
            drawinglayer::primitive2d::mapFontLineStyleToTextLine(rInfo.mrFont.GetOverline()));
        const drawinglayer::primitive2d::TextLine eFontLineStyle(
            drawinglayer::primitive2d::mapFontLineStyleToTextLine(rInfo.mrFont.GetUnderline()));

        // check UnderlineAbove
        const bool bUnderlineAbove(drawinglayer::primitive2d::TEXT_LINE_NONE != eFontLineStyle
                                   && rInfo.mrFont.IsUnderlineAbove());

        // prepare strikeout data
        const drawinglayer::primitive2d::TextStrikeout eTextStrikeout(
            drawinglayer::primitive2d::mapFontStrikeoutToTextStrikeout(
                rInfo.mrFont.GetStrikeout()));

        // prepare emphasis mark data
        drawinglayer::primitive2d::TextEmphasisMark eTextEmphasisMark(
            drawinglayer::primitive2d::TEXT_FONT_EMPHASIS_MARK_NONE);

        switch (rInfo.mrFont.GetEmphasisMark() & FontEmphasisMark::Style)
        {
            case FontEmphasisMark::Dot:
                eTextEmphasisMark = drawinglayer::primitive2d::TEXT_FONT_EMPHASIS_MARK_DOT;
                break;
            case FontEmphasisMark::Circle:
                eTextEmphasisMark = drawinglayer::primitive2d::TEXT_FONT_EMPHASIS_MARK_CIRCLE;
                break;
            case FontEmphasisMark::Disc:
                eTextEmphasisMark = drawinglayer::primitive2d::TEXT_FONT_EMPHASIS_MARK_DISC;
                break;
            case FontEmphasisMark::Accent:
                eTextEmphasisMark = drawinglayer::primitive2d::TEXT_FONT_EMPHASIS_MARK_ACCENT;
                break;
            default:
                break;
        }

        const bool bEmphasisMarkAbove(rInfo.mrFont.GetEmphasisMark() & FontEmphasisMark::PosAbove);
        const bool bEmphasisMarkBelow(rInfo.mrFont.GetEmphasisMark() & FontEmphasisMark::PosBelow);

        // prepare font relief data
        drawinglayer::primitive2d::TextRelief eTextRelief(
            drawinglayer::primitive2d::TEXT_RELIEF_NONE);

        switch (rInfo.mrFont.GetRelief())
        {
            case FontRelief::Embossed:
                eTextRelief = drawinglayer::primitive2d::TEXT_RELIEF_EMBOSSED;
                break;
            case FontRelief::Engraved:
                eTextRelief = drawinglayer::primitive2d::TEXT_RELIEF_ENGRAVED;
                break;
            default:
                break; // RELIEF_NONE, FontRelief_FORCE_EQUAL_SIZE
        }

        // prepare shadow/outline data
        // weboffice: a soft shadow spec replaces the hard font shadow (drawn in processDrawPortionInfo)
        const bool bShadow(rInfo.mrFont.IsShadow() && rInfo.maZetaTextShadow.isEmpty());

        // TextDecoratedPortionPrimitive2D is needed, create one
        pNewPrimitive = new drawinglayer::primitive2d::TextDecoratedPortionPrimitive2D(

            // attributes for TextSimplePortionPrimitive2D
            rNewTransform, rText, rInfo.mnTextStart, rInfo.mnTextLen, std::vector(rDXArray),
            std::move(aKashidaArray), rFontAttribute,
            rInfo.mpLocale ? *rInfo.mpLocale : css::lang::Locale(), aBFontColor, aTextFillColor,
            rInfo.mrFont.GetFixKerning(), rInfo.mrFont.GetOpticalSizing(),
            rInfo.mrFont.GetVariations(), rInfo.mrFont.GetPropr(), rInfo.mrFont.GetEscapement(),

            // attributes for TextDecoratedPortionPrimitive2D
            aBOverlineColor, aBUnderlineColor, eFontOverline, eFontLineStyle, bUnderlineAbove,
            eTextStrikeout, bWordLineMode, eTextEmphasisMark, bEmphasisMarkAbove,
            bEmphasisMarkBelow, eTextRelief, bShadow);
    }
    else
    {
        // TextSimplePortionPrimitive2D is enough
        pNewPrimitive = new drawinglayer::primitive2d::TextSimplePortionPrimitive2D(
            rNewTransform, rText, rInfo.mnTextStart, rInfo.mnTextLen, std::vector(rDXArray),
            std::move(aKashidaArray), rFontAttribute,
            rInfo.mpLocale ? *rInfo.mpLocale : css::lang::Locale(), aBFontColor, aTextFillColor,
            rInfo.mrFont.GetFixKerning(), rInfo.mrFont.GetOpticalSizing(),
            rInfo.mrFont.GetVariations(), rInfo.mrFont.GetPropr(), rInfo.mrFont.GetEscapement());
    }

    return pNewPrimitive;
}

class DoCapitalsDrawPortionInfo : public SvxDoCapitals
{
private:
    drawinglayer::primitive2d::Primitive2DContainer& mrTarget;
    const basegfx::B2DHomMatrix& mrNewTransformA;
    const basegfx::B2DHomMatrix& mrNewTransformB;
    const DrawPortionInfo& m_rInfo;
    SvxFont m_aFont;

public:
    DoCapitalsDrawPortionInfo(drawinglayer::primitive2d::Primitive2DContainer& rTarget,
                              const basegfx::B2DHomMatrix& rNewTransformA,
                              const basegfx::B2DHomMatrix& rNewTransformB,
                              const DrawPortionInfo& rInfo)
        : SvxDoCapitals(rInfo.maText, rInfo.mnTextStart, rInfo.mnTextLen)
        , mrTarget(rTarget)
        , mrNewTransformA(rNewTransformA)
        , mrNewTransformB(rNewTransformB)
        , m_rInfo(rInfo)
        , m_aFont(rInfo.mrFont)
    {
        assert(!m_rInfo.mpDXArray.empty());

        /* turn all these off as they are handled outside subportions for the whole portion */
        m_aFont.SetTransparent(false);
        m_aFont.SetUnderline(LINESTYLE_NONE);
        m_aFont.SetOverline(LINESTYLE_NONE);
        m_aFont.SetStrikeout(STRIKEOUT_NONE);

        m_aFont.SetCaseMap(SvxCaseMap::NotMapped); /* otherwise this would call itself */
    }
    virtual void Do(const OUString& rSpanTxt, const sal_Int32 nSpanIdx, const sal_Int32 nSpanLen,
                    const bool bUpper) override;
};

void CreateTextPortionPrimitivesFromDrawPortionInfo(
    drawinglayer::primitive2d::Primitive2DContainer& rTarget,
    const basegfx::B2DHomMatrix& rNewTransformA, const basegfx::B2DHomMatrix& rNewTransformB,
    const DrawPortionInfo& rInfo)
{
    if (rInfo.maText.isEmpty() || !rInfo.mnTextLen)
        return;

    basegfx::B2DVector aFontScaling;
    drawinglayer::attribute::FontAttribute aFontAttribute(
        drawinglayer::primitive2d::getFontAttributeFromVclFont(aFontScaling, rInfo.mrFont,
                                                               rInfo.IsRTL(), false));
    basegfx::B2DHomMatrix aNewTransform;

    // add font scale to new transform
    aNewTransform.scale(aFontScaling.getX(), aFontScaling.getY());

    // look for proportional font scaling, if necessary, scale accordingly
    sal_Int8 nPropr(rInfo.mrFont.GetPropr());
    const double fPropFontFactor(nPropr / 100.0);
    if (100 != nPropr)
        aNewTransform.scale(fPropFontFactor, fPropFontFactor);

    // apply font rotate
    if (rInfo.mrFont.GetOrientation())
    {
        aNewTransform.rotate(-toRadians(rInfo.mrFont.GetOrientation()));
    }

    // look for escapement, if necessary, translate accordingly
    if (rInfo.mrFont.GetEscapement())
    {
        sal_Int16 nEsc(rInfo.mrFont.GetEscapement());

        if (DFLT_ESC_AUTO_SUPER == nEsc)
        {
            nEsc = .8 * (100 - nPropr);
            assert(nEsc == DFLT_ESC_SUPER
                   && "I'm sure this formula needs to be changed, but how to confirm that???");
            nEsc = DFLT_ESC_SUPER;
        }
        else if (DFLT_ESC_AUTO_SUB == nEsc)
        {
            nEsc = .2 * -(100 - nPropr);
            assert(nEsc == -20
                   && "I'm sure this formula needs to be changed, but how to confirm that???");
            nEsc = -20;
        }

        if (nEsc > MAX_ESC_POS)
        {
            nEsc = MAX_ESC_POS;
        }
        else if (nEsc < -MAX_ESC_POS)
        {
            nEsc = -MAX_ESC_POS;
        }

        const double fEscapement(nEsc / -100.0);
        aNewTransform.translate(0.0, fEscapement * aFontScaling.getY());
    }

    // apply transformA
    aNewTransform *= rNewTransformA;

    // apply local offset
    aNewTransform.translate(rInfo.mrStartPos.X(), rInfo.mrStartPos.Y());

    // also apply embedding object's transform
    aNewTransform *= rNewTransformB;

    // prepare DXArray content. To make it independent from font size (and such from
    // the text transformation), scale it to unit coordinates
    ::std::vector<double> aDXArray;

    if (!rInfo.mpDXArray.empty())
    {
        aDXArray.reserve(rInfo.mnTextLen);
        for (sal_Int32 a = 0; a < rInfo.mnTextLen; a++)
        {
            aDXArray.push_back(rInfo.mpDXArray[a]);
        }
    }

    OUString caseMappedText = rInfo.mrFont.CalcCaseMap(rInfo.maText);
    rtl::Reference<drawinglayer::primitive2d::BasePrimitive2D> pNewPrimitive(
        buildTextPortionPrimitive(rInfo, caseMappedText, aFontAttribute, aDXArray, aNewTransform));

    bool bSmallCaps = rInfo.mrFont.IsCapital();
    if (bSmallCaps && rInfo.mpDXArray.empty())
    {
        SAL_WARN("svx", "SmallCaps requested with DXArray, abandoning");
        bSmallCaps = false;
    }
    if (bSmallCaps)
    {
        // rerun with each sub-portion
        DoCapitalsDrawPortionInfo aDoDrawPortionInfo(rTarget, rNewTransformA, rNewTransformB,
                                                     rInfo);
        rInfo.mrFont.DoOnCapitals(aDoDrawPortionInfo);

        // transfer collected primitives from rTarget to a new container
        drawinglayer::primitive2d::Primitive2DContainer aContainer = std::move(rTarget);

        // Take any decoration for the whole formatted portion and keep it to get continuous over/under/strike-through
        if (pNewPrimitive->getPrimitive2DID() == PRIMITIVE2D_ID_TEXTDECORATEDPORTIONPRIMITIVE2D)
        {
            const drawinglayer::primitive2d::TextDecoratedPortionPrimitive2D* pTCPP
                = static_cast<const drawinglayer::primitive2d::TextDecoratedPortionPrimitive2D*>(
                    pNewPrimitive.get());

            if (pTCPP->getWordLineMode()) // single word mode: 'Individual words' in UI
            {
                // Split to single word primitives using TextBreakupHelper
                drawinglayer::primitive2d::TextBreakupHelper aTextBreakupHelper(*pTCPP);
                drawinglayer::primitive2d::Primitive2DContainer aBroken(
                    aTextBreakupHelper.extractResult(drawinglayer::primitive2d::BreakupUnit::Word));
                for (auto& rPortion : aBroken)
                {
                    assert(rPortion->getPrimitive2DID()
                               == PRIMITIVE2D_ID_TEXTDECORATEDPORTIONPRIMITIVE2D
                           && "TextBreakupHelper generates same output primitive type as input");

                    const drawinglayer::primitive2d::TextDecoratedPortionPrimitive2D* pPortion
                        = static_cast<
                            const drawinglayer::primitive2d::TextDecoratedPortionPrimitive2D*>(
                            rPortion.get());

                    // create and add decoration
                    const drawinglayer::primitive2d::Primitive2DContainer&
                        rDecorationGeometryContent(pPortion->getOrCreateDecorationGeometryContent(
                            pPortion->getTextTransform(), caseMappedText,
                            pPortion->getTextPosition(), pPortion->getTextLength(),
                            pPortion->getDXArray()));

                    aContainer.insert(aContainer.end(), rDecorationGeometryContent.begin(),
                                      rDecorationGeometryContent.end());
                }
            }
            else
            {
                // create and add decoration
                const drawinglayer::primitive2d::Primitive2DContainer& rDecorationGeometryContent(
                    pTCPP->getOrCreateDecorationGeometryContent(pTCPP->getTextTransform(),
                                                                caseMappedText, rInfo.mnTextStart,
                                                                rInfo.mnTextLen, aDXArray));

                aContainer.insert(aContainer.end(), rDecorationGeometryContent.begin(),
                                  rDecorationGeometryContent.end());
            }
        }

        pNewPrimitive = new drawinglayer::primitive2d::GroupPrimitive2D(std::move(aContainer));
    }

    const Color aFontColor(rInfo.mrFont.GetColor());
    if (aFontColor.IsTransparent())
    {
        // Handle semi-transparent text for both the decorated and simple case here.
        pNewPrimitive = new drawinglayer::primitive2d::UnifiedTransparencePrimitive2D(
            drawinglayer::primitive2d::Primitive2DContainer{ pNewPrimitive },
            (255 - aFontColor.GetAlpha()) / 255.0);
    }

    if (rInfo.mbEndOfBullet)
    {
        // embed in TextHierarchyBulletPrimitive2D
        drawinglayer::primitive2d::Primitive2DContainer aNewSequence{ pNewPrimitive };
        pNewPrimitive = new drawinglayer::primitive2d::TextHierarchyBulletPrimitive2D(
            std::move(aNewSequence));
    }

    if (rInfo.mpFieldData)
    {
        pNewPrimitive = CheckFieldPrimitive(pNewPrimitive.get(), rInfo);
    }

    rTarget.push_back(pNewPrimitive);

    // support for WrongSpellVector. Create WrongSpellPrimitives as needed
    if (!rInfo.mpWrongSpellVector || aDXArray.empty())
        return;

    const sal_Int32 nSize(rInfo.mpWrongSpellVector->size());
    const sal_Int32 nDXCount(aDXArray.size());
    const basegfx::BColor aSpellColor(1.0, 0.0, 0.0); // red, hard coded

    for (sal_Int32 a(0); a < nSize; a++)
    {
        const EEngineData::WrongSpellClass& rCandidate = (*rInfo.mpWrongSpellVector)[a];

        if (rCandidate.nStart >= rInfo.mnTextStart && rCandidate.nEnd >= rInfo.mnTextStart
            && rCandidate.nEnd > rCandidate.nStart)
        {
            const sal_Int32 nStart(rCandidate.nStart - rInfo.mnTextStart);
            const sal_Int32 nEnd(rCandidate.nEnd - rInfo.mnTextStart);
            double fStart(0.0);
            double fEnd(0.0);

            if (nStart > 0 && nStart - 1 < nDXCount)
            {
                fStart = aDXArray[nStart - 1];
            }

            if (nEnd > 0 && nEnd - 1 < nDXCount)
            {
                fEnd = aDXArray[nEnd - 1];
            }

            if (!basegfx::fTools::equal(fStart, fEnd))
            {
                if (rInfo.IsRTL())
                {
                    // #i98523#
                    // When the portion is RTL, mirror the redlining using the
                    // full portion width
                    const double fTextWidth(aDXArray[aDXArray.size() - 1]);

                    fStart = fTextWidth - fStart;
                    fEnd = fTextWidth - fEnd;
                }

                // need to take FontScaling out of values; it's already part of
                // aNewTransform and would be double applied
                const double fFontScaleX(aFontScaling.getX() * fPropFontFactor);

                if (!basegfx::fTools::equal(fFontScaleX, 1.0)
                    && !basegfx::fTools::equalZero(fFontScaleX))
                {
                    fStart /= fFontScaleX;
                    fEnd /= fFontScaleX;
                }

                rTarget.push_back(new drawinglayer::primitive2d::WrongSpellPrimitive2D(
                    aNewTransform, fStart, fEnd, aSpellColor));
            }
        }
    }
}

void DoCapitalsDrawPortionInfo::Do(const OUString& rSpanTxt, const sal_Int32 nSpanIdx,
                                   const sal_Int32 nSpanLen, const bool bUpper)
{
    sal_uInt8 nProp(0);
    if (!bUpper)
    {
        nProp = m_aFont.GetPropr();
        m_aFont.SetProprRel(SMALL_CAPS_PERCENTAGE);
    }

    sal_Int32 nStartOffset = nSpanIdx - nIdx;
    double nStartX = nStartOffset ? m_rInfo.mpDXArray[nStartOffset - 1] : 0;

    Point aStartPos(m_rInfo.mrStartPos.X() + nStartX, m_rInfo.mrStartPos.Y());

    KernArray aDXArray;
    aDXArray.resize(nSpanLen);
    for (sal_Int32 i = 0; i < nSpanLen; ++i)
        aDXArray[i] = m_rInfo.mpDXArray[nStartOffset + i] - nStartX;

    auto aKashidaArray
        = !m_rInfo.mpKashidaArray.empty()
              ? std::span<const sal_Bool>(m_rInfo.mpKashidaArray.data() + nStartOffset, nSpanLen)
              : std::span<const sal_Bool>();

    DrawPortionInfo aInfo(
        aStartPos, rSpanTxt, nSpanIdx, nSpanLen, aDXArray, aKashidaArray, m_aFont, m_rInfo.mnPara,
        m_rInfo.mnBiDiLevel, nullptr, /* no spelling in subportion, handled outside */
        nullptr, /* no field in subportion, handled outside */
        false, false, false, m_rInfo.mpLocale, m_rInfo.maOverlineColor, m_rInfo.maTextLineColor);

    CreateTextPortionPrimitivesFromDrawPortionInfo(mrTarget, mrNewTransformA, mrNewTransformB,
                                                   aInfo);

    if (!bUpper)
        m_aFont.SetPropr(nProp);
}

void CreateDrawBulletPrimitivesFromDrawBulletInfo(
    drawinglayer::primitive2d::Primitive2DContainer& rTarget,
    const basegfx::B2DHomMatrix& rNewTransformA, const basegfx::B2DHomMatrix& rNewTransformB,
    const DrawBulletInfo& rInfo)
{
    basegfx::B2DHomMatrix aNewTransform;

    // add size to new transform
    aNewTransform.scale(rInfo.maBulletSize.getWidth(), rInfo.maBulletSize.getHeight());

    // apply transformA
    aNewTransform *= rNewTransformA;

    // apply local offset
    aNewTransform.translate(rInfo.maBulletPosition.X(), rInfo.maBulletPosition.Y());

    // also apply embedding object's transform
    aNewTransform *= rNewTransformB;

    // prepare empty GraphicAttr
    const GraphicAttr aGraphicAttr;

    // create GraphicPrimitive2D
    const drawinglayer::primitive2d::Primitive2DReference aNewReference(
        new drawinglayer::primitive2d::GraphicPrimitive2D(
            aNewTransform, rInfo.maBulletGraphicObject, aGraphicAttr));

    // embed in TextHierarchyBulletPrimitive2D
    drawinglayer::primitive2d::Primitive2DContainer aNewSequence{ aNewReference };
    rtl::Reference<drawinglayer::primitive2d::BasePrimitive2D> pNewPrimitive
        = new drawinglayer::primitive2d::TextHierarchyBulletPrimitive2D(std::move(aNewSequence));

    // add to output
    rTarget.push_back(pNewPrimitive);
}
} // end of anonymous namespace

namespace
{
// zeta: 'ZG1 <열 때 글자색> <L|R|Q> <각도 1/10도> <X 중심%> <Y 중심%> <위치>:<색> ...'을 그라데이션 속성으로
bool zetaParseTextGradient(std::u16string_view aSpec,
                           drawinglayer::attribute::FillGradientAttribute& rAttr)
{
    std::vector<OUString> aTok;
    sal_Int32 nIdx = 0;
    do
    {
        OUString aOne(o3tl::getToken(aSpec, 0, ' ', nIdx));
        if (!aOne.isEmpty())
            aTok.push_back(aOne);
    } while (nIdx >= 0);
    if (aTok.size() < 8 || aTok[0] != "ZG1")
        return false;
    css::awt::GradientStyle eStyle = css::awt::GradientStyle_LINEAR;
    if (aTok[2] == "R")
        eStyle = css::awt::GradientStyle_RADIAL;
    else if (aTok[2] == "Q")
        eStyle = css::awt::GradientStyle_RECT;
    const double fAngle(basegfx::deg2rad(aTok[3].toInt32() / 10.0));
    const double fOfsX(aTok[4].toInt32() / 100.0);
    const double fOfsY(aTok[5].toInt32() / 100.0);
    basegfx::BColorStops aStops;
    for (size_t i = 6; i < aTok.size(); ++i)
    {
        const sal_Int32 nColon(aTok[i].indexOf(':'));
        if (nColon <= 0)
            return false;
        const double fPos(aTok[i].copy(0, nColon).toDouble());
        const ::Color aColor(ColorTransparency, aTok[i].copy(nColon + 1).toUInt32(16));
        aStops.addStop(std::clamp(fPos, 0.0, 1.0), aColor.getBColor());
    }
    if (aStops.size() < 2)
        return false;
    // PowerPoint는 멈춤점 사이를 선형 빛(감마를 푼 RGB)에서 섞는다(실측: 빨강→파랑 가운데 184,0,188 — sRGB 직선 섞기는 128,0,128).
    // LibreOffice 그라데이션은 멈춤점 사이를 sRGB 직선으로 섞으므로, 선형 빛으로 섞은 멈춤점을 촘촘히 넣는다
    {
        const auto toLinear = [](double c)
        { return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4); };
        const auto toSrgb = [](double c)
        {
            c = std::clamp(c, 0.0, 1.0);
            return c <= 0.0031308 ? c * 12.92 : 1.055 * std::pow(c, 1.0 / 2.4) - 0.055;
        };
        basegfx::BColorStops aDense;
        constexpr int nSub = 16;
        for (size_t i = 0; i < aStops.size(); ++i)
        {
            const basegfx::BColorStop& rA = aStops.getStop(i);
            aDense.addStop(rA.getStopOffset(), rA.getStopColor());
            if (i + 1 == aStops.size())
                break;
            const basegfx::BColorStop& rB = aStops.getStop(i + 1);
            if (rB.getStopOffset() - rA.getStopOffset() <= 0.0 || rA.getStopColor() == rB.getStopColor())
                continue;
            const basegfx::BColor& cA = rA.getStopColor();
            const basegfx::BColor& cB = rB.getStopColor();
            for (int k = 1; k < nSub; ++k)
            {
                const double t = static_cast<double>(k) / nSub;
                const basegfx::BColor aMix(
                    toSrgb(toLinear(cA.getRed()) * (1.0 - t) + toLinear(cB.getRed()) * t),
                    toSrgb(toLinear(cA.getGreen()) * (1.0 - t) + toLinear(cB.getGreen()) * t),
                    toSrgb(toLinear(cA.getBlue()) * (1.0 - t) + toLinear(cB.getBlue()) * t));
                aDense.addStop(rA.getStopOffset() + (rB.getStopOffset() - rA.getStopOffset()) * t,
                               aMix);
            }
        }
        aStops = aDense;
    }
    rAttr = drawinglayer::attribute::FillGradientAttribute(eStyle, 0.0, fOfsX, fOfsY, fAngle,
                                                           aStops);
    return true;
}

// zeta: 글 조각의 글꼴 위(ascent)·아래(descent) 끝을 최종 좌표로 — PowerPoint는 그라데이션 높이를 줄 높이로 잡는다
basegfx::B2DRange zetaPortionVerticalRange(const DrawPortionInfo& rInfo,
                                           const basegfx::B2DHomMatrix& rTransformA,
                                           const basegfx::B2DHomMatrix& rTransformB)
{
    drawinglayer::primitive2d::TextLayouterDevice aLayouter;
    aLayouter.setFont(rInfo.mrFont);
    basegfx::B2DHomMatrix aTransform(rTransformA);
    aTransform.translate(rInfo.mrStartPos.X(), rInfo.mrStartPos.Y());
    aTransform *= rTransformB;
    basegfx::B2DRange aRange(aTransform * basegfx::B2DPoint(0.0, -aLayouter.getFontAscent()));
    aRange.expand(aTransform * basegfx::B2DPoint(0.0, aLayouter.getFontDescent()));
    return aRange;
}

// weboffice: the unrotated ascent part of the portion's line (top to baseline) - Excel 2021
// mirrors a text reflection around the baseline
basegfx::B2DRange zetaPortionAscentRange(const DrawPortionInfo& rInfo,
                                         const basegfx::B2DHomMatrix& rTransformA)
{
    drawinglayer::primitive2d::TextLayouterDevice aLayouter;
    aLayouter.setFont(rInfo.mrFont);
    basegfx::B2DHomMatrix aTransform(rTransformA);
    aTransform.translate(rInfo.mrStartPos.X(), rInfo.mrStartPos.Y());
    basegfx::B2DRange aRange(aTransform * basegfx::B2DPoint(0.0, -aLayouter.getFontAscent()));
    aRange.expand(aTransform * basegfx::B2DPoint(0.0, 0.0));
    return aRange;
}
}

namespace
{
// weboffice: collect the text portion primitives (also inside groups such as fields)
// weboffice: the glyphs (and decoration strokes) of text primitives as filled outlines with rMat
// baked into the coordinates. A perspective text shadow under a TransformPrimitive2D with an
// absolute matrix drew in the right place on screen but not in the shape picture export
// (GraphicExportFilter: the content moved to the picture origin while the matrix kept page
// coordinates, so a sy 30% shadow landed 0.7 x the shape position lower); baked geometry moves
// with the content.
void zetaBakeTextGeometry(const drawinglayer::primitive2d::Primitive2DReference& xPrim,
                          const basegfx::B2DHomMatrix& rMat,
                          drawinglayer::primitive2d::Primitive2DContainer& rOut, int nDepth)
{
    const drawinglayer::primitive2d::BasePrimitive2D* pPrim = xPrim.get();
    if (!pPrim)
        return;
    const auto push = [&](basegfx::B2DPolyPolygon aPoly, const basegfx::BColor& rColor) {
        if (!aPoly.count())
            return;
        aPoly.transform(rMat);
        rOut.push_back(new drawinglayer::primitive2d::PolyPolygonColorPrimitive2D(std::move(aPoly), rColor));
    };
    if (nDepth < 6)
    {
        if (auto pDeco = dynamic_cast<const drawinglayer::primitive2d::TextDecoratedPortionPrimitive2D*>(pPrim))
        {
            drawinglayer::primitive2d::Primitive2DContainer aSub;
            pDeco->get2DDecomposition(aSub, drawinglayer::geometry::ViewInformation2D());
            for (const auto& rChild : aSub)
                zetaBakeTextGeometry(rChild, rMat, rOut, nDepth + 1);
            return;
        }
        if (auto pText = dynamic_cast<const drawinglayer::primitive2d::TextSimplePortionPrimitive2D*>(pPrim))
        {
            basegfx::B2DPolyPolygonVector aOutlines;
            basegfx::B2DHomMatrix aTransform;
            pText->getTextOutlinesAndTransformation(aOutlines, aTransform);
            for (auto& rPoly : aOutlines)
            {
                rPoly.transform(aTransform);
                push(std::move(rPoly), pText->getFontColor());
            }
            return;
        }
        if (auto pStroke = dynamic_cast<const drawinglayer::primitive2d::PolygonStrokePrimitive2D*>(pPrim))
        {
            const double fHalf = pStroke->getLineAttribute().getWidth() / 2.0;
            if (fHalf > 0.0 && pStroke->getB2DPolygon().count() > 1)
                push(basegfx::utils::createAreaGeometry(pStroke->getB2DPolygon(), fHalf,
                                                        basegfx::B2DLineJoin::Miter,
                                                        css::drawing::LineCap_BUTT),
                     pStroke->getLineAttribute().getColor());
            return;
        }
        if (auto pFill = dynamic_cast<const drawinglayer::primitive2d::PolyPolygonColorPrimitive2D*>(pPrim))
        {
            push(pFill->getB2DPolyPolygon(), pFill->getBColor());
            return;
        }
        if (auto pGroup = dynamic_cast<const drawinglayer::primitive2d::GroupPrimitive2D*>(pPrim);
            pGroup && pPrim->getPrimitive2DID() == PRIMITIVE2D_ID_GROUPPRIMITIVE2D)
        {
            for (const auto& rChild : pGroup->getChildren())
                zetaBakeTextGeometry(rChild, rMat, rOut, nDepth + 1);
            return;
        }
    }
    // anything else (outline stroke with transparency and so on): under the matrix as it is
    rOut.push_back(new drawinglayer::primitive2d::TransformPrimitive2D(
        rMat, drawinglayer::primitive2d::Primitive2DContainer{ xPrim }));
}

void zetaCollectText(const drawinglayer::primitive2d::BasePrimitive2D* pPrim,
                     std::vector<const drawinglayer::primitive2d::TextSimplePortionPrimitive2D*>& rOut,
                     int nDepth)
{
    if (!pPrim || nDepth > 4)
        return;
    if (auto pText = dynamic_cast<const drawinglayer::primitive2d::TextSimplePortionPrimitive2D*>(pPrim))
    {
        rOut.push_back(pText);
        return;
    }
    if (auto pGroup = dynamic_cast<const drawinglayer::primitive2d::GroupPrimitive2D*>(pPrim))
        for (const auto& rChild : pGroup->getChildren())
            zetaCollectText(rChild.get(), rOut, nDepth + 1);
}

// weboffice: Office text outline (a:ln in a:rPr): stroke the glyph outlines with the line width
// and color, and under contoured text (LibreOffice reads white text with an outline as contour)
// fill the glyphs with the original fill while the char color is unchanged
void zetaAddTextOutline(drawinglayer::primitive2d::Primitive2DContainer& rPortions, size_t nBefore,
                        const OUString& rSpec, const Color& rFontColor, bool bNoUnderlay)
{
    sal_Int32 nIdx = 0;
    if (o3tl::getToken(rSpec, 0, ' ', nIdx) != u"ZO1" || nIdx < 0)
        return;
    const sal_uInt32 nAt = o3tl::toUInt32(o3tl::getToken(rSpec, 0, ' ', nIdx), 16);
    const double fWidth = o3tl::toInt32(o3tl::getToken(rSpec, 0, ' ', nIdx)) / 360.0;
    const Color aLine(ColorTransparency, o3tl::toUInt32(o3tl::getToken(rSpec, 0, ' ', nIdx), 16));
    const sal_Int32 nTrans = o3tl::toInt32(o3tl::getToken(rSpec, 0, ' ', nIdx));
    const std::u16string_view aFill = o3tl::getToken(rSpec, 0, ' ', nIdx);
    if (nTrans >= 100)
        return;
    std::vector<const drawinglayer::primitive2d::TextSimplePortionPrimitive2D*> aTexts;
    for (size_t i = nBefore; i < rPortions.size(); ++i)
        zetaCollectText(rPortions[i].get(), aTexts, 0);
    basegfx::B2DPolyPolygon aAll, aUnder;
    for (const auto* pText : aTexts)
    {
        basegfx::B2DPolyPolygonVector aOutlines;
        basegfx::B2DHomMatrix aTransform;
        pText->getTextOutlinesAndTransformation(aOutlines, aTransform);
        const bool bUnder = !bNoUnderlay && pText->getFontAttribute().getOutline();
        for (auto& rPoly : aOutlines)
        {
            rPoly.transform(aTransform);
            aAll.append(rPoly);
            if (bUnder)
                aUnder.append(rPoly);
        }
    }
    if (!aAll.count())
        return;
    // underline/strikeout of the portion: Excel outlines them too and draws them over the glyphs
    // (filled bar with the outline around it)
    std::vector<std::pair<basegfx::B2DPolyPolygon, basegfx::BColor>> aDecos;
    {
        const drawinglayer::geometry::ViewInformation2D aViewInfo;
        std::function<void(const drawinglayer::primitive2d::BasePrimitive2D*, int)> lcl_Deco
            = [&](const drawinglayer::primitive2d::BasePrimitive2D* pPrim, int nDepth) {
                  if (!pPrim || nDepth > 6)
                      return;
                  if (auto pStroke = dynamic_cast<const drawinglayer::primitive2d::PolygonStrokePrimitive2D*>(pPrim))
                  {
                      const double fHalf = pStroke->getLineAttribute().getWidth() / 2.0;
                      if (fHalf > 0.0 && pStroke->getB2DPolygon().count() > 1)
                          aDecos.emplace_back(basegfx::utils::createAreaGeometry(
                                                  pStroke->getB2DPolygon(), fHalf,
                                                  basegfx::B2DLineJoin::Miter,
                                                  css::drawing::LineCap_BUTT),
                                              pStroke->getLineAttribute().getColor());
                      return;
                  }
                  if (dynamic_cast<const drawinglayer::primitive2d::TextSimplePortionPrimitive2D*>(pPrim)
                      && !dynamic_cast<const drawinglayer::primitive2d::TextDecoratedPortionPrimitive2D*>(pPrim))
                      return;
                  drawinglayer::primitive2d::Primitive2DContainer aSub;
                  pPrim->get2DDecomposition(aSub, aViewInfo);
                  for (const auto& rSub : aSub)
                      lcl_Deco(rSub.get(), nDepth + 1);
              };
        for (const auto* pText : aTexts)
            if (dynamic_cast<const drawinglayer::primitive2d::TextDecoratedPortionPrimitive2D*>(pText))
                lcl_Deco(pText, 0);
    }
    if (aUnder.count() && aFill != u"-" && !aFill.empty()
        && (sal_uInt32(rFontColor.GetRGBColor()) & 0xFFFFFF) == (nAt & 0xFFFFFF))
    {
        const Color aFillColor(ColorTransparency, o3tl::toUInt32(aFill, 16));
        drawinglayer::primitive2d::Primitive2DContainer aNew;
        for (size_t i = 0; i < nBefore; ++i)
            aNew.push_back(rPortions[i]);
        aNew.push_back(new drawinglayer::primitive2d::PolyPolygonColorPrimitive2D(
            aUnder, aFillColor.getBColor()));
        for (size_t i = nBefore; i < rPortions.size(); ++i)
            aNew.push_back(rPortions[i]);
        rPortions = std::move(aNew);
    }
    drawinglayer::primitive2d::Primitive2DReference xStroke(
        new drawinglayer::primitive2d::PolyPolygonStrokePrimitive2D(
            aAll, drawinglayer::attribute::LineAttribute(aLine.getBColor(), fWidth,
                                                         basegfx::B2DLineJoin::Round)));
    if (nTrans > 0)
        xStroke = new drawinglayer::primitive2d::UnifiedTransparencePrimitive2D(
            drawinglayer::primitive2d::Primitive2DContainer{ xStroke }, nTrans / 100.0);
    rPortions.push_back(xStroke);
    for (const auto& [rArea, rColor] : aDecos)
    {
        rPortions.push_back(new drawinglayer::primitive2d::PolyPolygonColorPrimitive2D(rArea, rColor));
        drawinglayer::primitive2d::Primitive2DReference xDeco(
            new drawinglayer::primitive2d::PolyPolygonStrokePrimitive2D(
                rArea, drawinglayer::attribute::LineAttribute(aLine.getBColor(), fWidth,
                                                              basegfx::B2DLineJoin::Round)));
        if (nTrans > 0)
            xDeco = new drawinglayer::primitive2d::UnifiedTransparencePrimitive2D(
                drawinglayer::primitive2d::Primitive2DContainer{ xDeco }, nTrans / 100.0);
        rPortions.push_back(xDeco);
    }
}

// weboffice: the Office effect transform of a text line (sx/sy/kx/ky around the algn point of the
// portion's line box: glyph ink across, ascent to descent down), in final coordinates.
// rB is the shape transform (rotation) applied after the local text layout; fDx/fDy move in the
// local text direction when bLocalMove, else on the page
basegfx::B2DHomMatrix zetaLineEffectTransform(const basegfx::B2DRange& rLocalBox,
                                              const basegfx::B2DHomMatrix& rB, double fSx,
                                              double fSy, sal_Int32 nKx, sal_Int32 nKy,
                                              std::u16string_view aAlgn, double fDx, double fDy,
                                              bool bLocalMove)
{
    double fAx = rLocalBox.getCenterX(), fAy = rLocalBox.getCenterY();
    if (o3tl::starts_with(aAlgn, u"t"))
        fAy = rLocalBox.getMinY();
    else if (o3tl::starts_with(aAlgn, u"b"))
        fAy = rLocalBox.getMaxY();
    if (aAlgn == u"l" || aAlgn == u"tl" || aAlgn == u"bl")
        fAx = rLocalBox.getMinX();
    else if (aAlgn == u"r" || aAlgn == u"tr" || aAlgn == u"br")
        fAx = rLocalBox.getMaxX();
    basegfx::B2DHomMatrix aInvB(rB);
    aInvB.invert();
    basegfx::B2DHomMatrix aMat(aInvB);
    aMat *= basegfx::utils::createTranslateB2DHomMatrix(-fAx, -fAy);
    // Excel 2021 (CopyPicture, perspective preset 'upper right' sy 30% kx -30deg): the skew is
    // taken on the unscaled glyphs, then the scale (the top of a glyph moves tan(kx) x its full
    // height above the anchor, not x the squashed height)
    if (nKx != 0)
        aMat.shearX(std::tan(basegfx::deg2rad(nKx / 60000.0)));
    if (nKy != 0)
        aMat.shearY(std::tan(basegfx::deg2rad(nKy / 60000.0)));
    aMat.scale(fSx, fSy);
    aMat.translate(fAx + (bLocalMove ? fDx : 0.0), fAy + (bLocalMove ? fDy : 0.0));
    aMat *= rB;
    if (!bLocalMove)
        aMat.translate(fDx, fDy);
    return aMat;
}

// weboffice: the local (unrotated) line box of the portion primitives from nBefore
basegfx::B2DRange zetaPortionLocalBox(const drawinglayer::primitive2d::Primitive2DContainer& rPortions,
                                      size_t nBefore, bool bAscentOnly,
                                      const basegfx::B2DHomMatrix& rB)
{
    // glyph ink across; the font's ascent and descent (Excel 2021: the Windows ascent/descent line
    // box) around the baseline of the portion's text primitives, all unrotated (before rB)
    basegfx::B2DHomMatrix aInvB(rB);
    aInvB.invert();
    std::vector<const drawinglayer::primitive2d::TextSimplePortionPrimitive2D*> aTexts;
    for (size_t i = nBefore; i < rPortions.size(); ++i)
        zetaCollectText(rPortions[i].get(), aTexts, 0);
    basegfx::B2DRange aInk, aVert;
    for (const auto* pText : aTexts)
    {
        basegfx::B2DPolyPolygonVector aOutlines;
        basegfx::B2DHomMatrix aTransform;
        pText->getTextOutlinesAndTransformation(aOutlines, aTransform);
        aTransform *= aInvB;
        for (auto& rPoly : aOutlines)
        {
            rPoly.transform(aTransform);
            aInk.expand(rPoly.getB2DRange());
        }
        basegfx::B2DTuple aScale, aTranslate;
        double fRotate(0.0), fShearX(0.0);
        pText->getTextTransform().decompose(aScale, aTranslate, fRotate, fShearX);
        drawinglayer::primitive2d::TextLayouterDevice aLayouter;
        aLayouter.setFontAttribute(pText->getFontAttribute(), std::abs(aScale.getX()),
                                   std::abs(aScale.getY()), pText->getLocale());
        const basegfx::B2DPoint aBase(aInvB * basegfx::B2DPoint(aTranslate));
        aVert.expand(basegfx::B2DPoint(aBase.getX(), aBase.getY() - aLayouter.getFontAscent()));
        aVert.expand(basegfx::B2DPoint(aBase.getX(), aBase.getY()
                                       + (bAscentOnly ? 0.0 : aLayouter.getFontDescent())));
    }
    if (aInk.isEmpty() || aVert.isEmpty())
        return basegfx::B2DRange();
    return basegfx::B2DRange(aInk.getMinX(), aVert.getMinY(), aInk.getMaxX(), aVert.getMaxY());
}

// weboffice: Office text reflection (a:reflection in a:rPr), per line like Excel 2021 -
// 'ZR1 <stA> <stPos> <endA> <endPos> <dist> <dir> <fadeDir> <sx> <sy> <blur> <kx> <ky> <algn>'
// (DrawingML units): the portion mirrored around its line box (algn), faded from stA to endA
// over stPos..endPos of the reflection height. Blur is not drawn (Office presets use 0.5pt)
void zetaAddTextReflection(drawinglayer::primitive2d::Primitive2DContainer& rPortions,
                           size_t nBefore, const OUString& rSpec,
                           const basegfx::B2DRange& /*rLocalVertical*/, const basegfx::B2DHomMatrix& rB,
                           double fDistScale)
{
    sal_Int32 nIdx = 0;
    if (o3tl::getToken(rSpec, 0, ' ', nIdx) != u"ZR1" || nIdx < 0 || rPortions.size() <= nBefore)
        return;
    sal_Int32 aN[12] = {};
    for (sal_Int32& n : aN)
    {
        if (nIdx < 0)
            return;
        n = o3tl::toInt32(o3tl::getToken(rSpec, 0, ' ', nIdx));
    }
    const std::u16string_view aAlgn = nIdx >= 0 ? o3tl::getToken(rSpec, 0, ' ', nIdx) : std::u16string_view(u"b");
    const double fStA = aN[0] / 100000.0, fStPos = std::clamp(aN[1] / 100000.0, 0.0, 1.0);
    const double fEndA = aN[2] / 100000.0;
    const double fEndPos = std::clamp(aN[3] / 100000.0, fStPos, 1.0);
    const double fSx = aN[7] / 100000.0, fSy = aN[8] / 100000.0;
    if ((fStA <= 0.0 && fEndA <= 0.0) || fSx == 0.0 || fSy == 0.0)
        return;
    const basegfx::B2DRange aBox(zetaPortionLocalBox(rPortions, nBefore, true, rB));
    if (aBox.isEmpty() || aBox.getHeight() <= 0.0)
        return;
    const double fDirRad = basegfx::deg2rad(aN[5] / 60000.0);
    const double fDist = aN[4] / 360.0 * fDistScale;
    const basegfx::B2DHomMatrix aMat(zetaLineEffectTransform(
        aBox, rB, fSx, fSy, aN[10], aN[11], aAlgn, fDist * std::cos(fDirRad),
        fDist * std::sin(fDirRad), true));
    drawinglayer::primitive2d::Primitive2DContainer aCopy;
    for (size_t i = nBefore; i < rPortions.size(); ++i)
        aCopy.push_back(rPortions[i]);

    // the reflected line box in local coordinates (aMat without the shape transform)
    basegfx::B2DHomMatrix aInvB(rB);
    aInvB.invert();
    basegfx::B2DHomMatrix aLocalMat(rB);
    aLocalMat *= aMat;
    aLocalMat *= aInvB;
    basegfx::B2DRange aRefl(aBox);
    aRefl.transform(aLocalMat);
    // fade direction, nearest of right/down/left/up, from the reflection's start edge
    const sal_Int32 nQuarter = ((aN[6] + 2700000) / 5400000 % 4 + 4) % 4;
    const bool bVert = nQuarter == 1 || nQuarter == 3;
    const double fStart = nQuarter == 0 ? aRefl.getMinX() : nQuarter == 1 ? aRefl.getMinY()
                        : nQuarter == 2 ? aRefl.getMaxX() : aRefl.getMaxY();
    const double fSign = (nQuarter == 0 || nQuarter == 1) ? 1.0 : -1.0;
    const double fSize = bVert ? aRefl.getHeight() : aRefl.getWidth();
    if (fSize <= 0.0)
        return;
    basegfx::B2DRange aAll(aRefl);
    aAll.grow(fSize * 2.0 + aBox.getWidth() + aBox.getHeight());
    auto band = [&](double fFrom, double fTo, double fAlpha) {
        const double f1 = fStart + fSign * fFrom * fSize, f2 = fStart + fSign * fTo * fSize;
        const basegfx::B2DRange aBand = bVert
            ? basegfx::B2DRange(aAll.getMinX(), std::min(f1, f2), aAll.getMaxX(), std::max(f1, f2))
            : basegfx::B2DRange(std::min(f1, f2), aAll.getMinY(), std::max(f1, f2), aAll.getMaxY());
        const double fGray = 1.0 - std::clamp(fAlpha, 0.0, 1.0);
        return drawinglayer::primitive2d::Primitive2DReference(
            new drawinglayer::primitive2d::PolyPolygonColorPrimitive2D(
                basegfx::B2DPolyPolygon(basegfx::utils::createPolygonFromRect(aBand)),
                basegfx::BColor(fGray, fGray, fGray)));
    };
    const double fFarPos = 4.0;
    drawinglayer::primitive2d::Primitive2DContainer aMask;
    aMask.push_back(band(-fFarPos, fStPos, fStA));
    const int nSteps = 32;
    for (int i = 0; i < nSteps && fEndPos > fStPos; ++i)
    {
        const double fA = fStPos + (fEndPos - fStPos) * i / nSteps;
        const double fZ = fStPos + (fEndPos - fStPos) * (i + 1) / nSteps;
        aMask.push_back(band(fA, fZ, fStA + (fEndA - fStA) * (i + 0.5) / nSteps));
    }
    aMask.push_back(band(fEndPos, fFarPos, fEndA > 0.0 ? fEndA : 0.0));
    drawinglayer::primitive2d::Primitive2DContainer aMaskFinal{
        new drawinglayer::primitive2d::TransformPrimitive2D(rB, std::move(aMask)) };
    drawinglayer::primitive2d::Primitive2DContainer aMirrored{
        new drawinglayer::primitive2d::TransformPrimitive2D(aMat, std::move(aCopy)) };
    rPortions.insert(rPortions.begin() + nBefore,
                     new drawinglayer::primitive2d::TransparencePrimitive2D(
                         std::move(aMirrored), std::move(aMaskFinal)));
}
}

namespace
{
// weboffice: Office outer text shadow - 'ZS1 <dx EMU> <dy EMU> <blur EMU> <RGB hex> <opacity %>':
// a blurred, colored copy of the portion's glyphs under them. Optional tail ' i' = inner shadow,
// ' t <sx> <sy> <kx> <ky> <algn>' = scale/skew (perspective) around the line box
void zetaAddTextShadow(drawinglayer::primitive2d::Primitive2DContainer& rPortions, size_t nBefore,
                       const OUString& rSpec, const basegfx::B2DRange& /*rLocalVertical*/,
                       const basegfx::B2DHomMatrix& rB, double fScale)
{
    sal_Int32 nIdx = 0;
    if (o3tl::getToken(rSpec, 0, ' ', nIdx) != u"ZS1" || nIdx < 0)
        return;
    const double fDx = o3tl::toInt32(o3tl::getToken(rSpec, 0, ' ', nIdx)) / 360.0;
    const double fDy = o3tl::toInt32(o3tl::getToken(rSpec, 0, ' ', nIdx)) / 360.0;
    const double fBlur = o3tl::toInt32(o3tl::getToken(rSpec, 0, ' ', nIdx)) / 360.0;
    const Color aColor(ColorTransparency, o3tl::toUInt32(o3tl::getToken(rSpec, 0, ' ', nIdx), 16));
    const sal_Int32 nOpacity = o3tl::toInt32(o3tl::getToken(rSpec, 0, ' ', nIdx));
    bool bInner = false, bTransform = false;
    double fSx = 1.0, fSy = 1.0;
    sal_Int32 nKx = 0, nKy = 0;
    std::u16string_view aAlgn = u"b";
    while (nIdx >= 0)
    {
        const std::u16string_view aTok = o3tl::getToken(rSpec, 0, ' ', nIdx);
        if (aTok == u"i")
            bInner = true;
        else if (aTok == u"t" && nIdx >= 0)
        {
            fSx = o3tl::toInt32(o3tl::getToken(rSpec, 0, ' ', nIdx)) / 100000.0;
            fSy = nIdx >= 0 ? o3tl::toInt32(o3tl::getToken(rSpec, 0, ' ', nIdx)) / 100000.0 : 1.0;
            nKx = nIdx >= 0 ? o3tl::toInt32(o3tl::getToken(rSpec, 0, ' ', nIdx)) : 0;
            nKy = nIdx >= 0 ? o3tl::toInt32(o3tl::getToken(rSpec, 0, ' ', nIdx)) : 0;
            if (nIdx >= 0)
                aAlgn = o3tl::getToken(rSpec, 0, ' ', nIdx);
            bTransform = fSx != 0.0 && fSy != 0.0;
        }
    }
    if (nOpacity <= 0 || rPortions.size() <= nBefore)
        return;
    if (bInner)
    {
        // inner shadow (a:innerShdw): the blurred, shifted outside of the glyphs (a frame around
        // them minus the glyphs) masked by the glyphs, drawn over the portion
        std::vector<const drawinglayer::primitive2d::TextSimplePortionPrimitive2D*> aTexts;
        for (size_t i = nBefore; i < rPortions.size(); ++i)
            zetaCollectText(rPortions[i].get(), aTexts, 0);
        basegfx::B2DPolyPolygon aGlyphs;
        for (const auto* pText : aTexts)
        {
            basegfx::B2DPolyPolygonVector aOutlines;
            basegfx::B2DHomMatrix aTransform;
            pText->getTextOutlinesAndTransformation(aOutlines, aTransform);
            for (auto& rPoly : aOutlines)
            {
                rPoly.transform(aTransform);
                aGlyphs.append(rPoly);
            }
        }
        if (!aGlyphs.count())
            return;
        basegfx::B2DRange aFrame(aGlyphs.getB2DRange());
        aFrame.grow(2.0 * fBlur + std::abs(fDx) + std::abs(fDy) + 100.0);
        basegfx::B2DPolyPolygon aOutside(basegfx::utils::createPolygonFromRect(aFrame));
        aOutside.append(aGlyphs);
        drawinglayer::primitive2d::Primitive2DReference xInner(
            new drawinglayer::primitive2d::ShadowPrimitive2D(
                basegfx::utils::createTranslateB2DHomMatrix(fDx, fDy), aColor.getBColor(), fBlur,
                drawinglayer::primitive2d::Primitive2DContainer{
                    new drawinglayer::primitive2d::PolyPolygonColorPrimitive2D(
                        aOutside, aColor.getBColor()) }));
        if (nOpacity < 100)
            xInner = new drawinglayer::primitive2d::UnifiedTransparencePrimitive2D(
                drawinglayer::primitive2d::Primitive2DContainer{ xInner }, 1.0 - nOpacity / 100.0);
        rPortions.push_back(new drawinglayer::primitive2d::MaskPrimitive2D(
            aGlyphs, drawinglayer::primitive2d::Primitive2DContainer{ xInner }));
        return;
    }
    // perspective/scaled outer shadow (Excel 2021: per line around the algn point of the line box)
    // Excel 2021 draws the outer text shadow offset and blur smaller for smaller text (fScale)
    const double fOuterDx = fDx * fScale, fOuterDy = fDy * fScale, fOuterBlur = fBlur * fScale;
    basegfx::B2DHomMatrix aShadowMat(basegfx::utils::createTranslateB2DHomMatrix(fOuterDx, fOuterDy));
    if (bTransform && !(fSx == 1.0 && fSy == 1.0 && nKx == 0 && nKy == 0))
    {
        const basegfx::B2DRange aBox(zetaPortionLocalBox(rPortions, nBefore, false, rB));
        if (!aBox.isEmpty())
            aShadowMat = zetaLineEffectTransform(aBox, rB, fSx, fSy, nKx, nKy, aAlgn, fOuterDx,
                                                 fOuterDy, false);
    }
    // a pure offset stays the shadow transform; a perspective matrix is baked into the geometry
    const bool bPerspective = !(aShadowMat.get(0, 0) == 1.0 && aShadowMat.get(1, 1) == 1.0
                                && aShadowMat.get(0, 1) == 0.0 && aShadowMat.get(1, 0) == 0.0);
    drawinglayer::primitive2d::Primitive2DContainer aCopy;
    for (size_t i = nBefore; i < rPortions.size(); ++i)
    {
        if (bPerspective)
            zetaBakeTextGeometry(rPortions[i], aShadowMat, aCopy, 0);
        else
            aCopy.push_back(rPortions[i]);
    }
    if (bPerspective)
        aShadowMat.identity();
    drawinglayer::primitive2d::Primitive2DReference xShadow(
        new drawinglayer::primitive2d::ShadowPrimitive2D(
            aShadowMat, aColor.getBColor(), fOuterBlur, std::move(aCopy)));
    if (nOpacity < 100)
        xShadow = new drawinglayer::primitive2d::UnifiedTransparencePrimitive2D(
            drawinglayer::primitive2d::Primitive2DContainer{ xShadow }, 1.0 - nOpacity / 100.0);
    rPortions.insert(rPortions.begin() + nBefore, xShadow);
}
}

void TextHierarchyBreakup::zetaApplyTextGradients()
{
    // PowerPoint 2021 실측(창 없이 COM Export): 글자 그라데이션은 줄마다 처음부터 다시 — 가로는 그 줄 글자 전체
    // (그라데이션이 낱말 하나뿐이어도 줄 전체 범위의 가운데 색), 세로는 줄 높이
    const drawinglayer::geometry::ViewInformation2D aViewInfo;
    basegfx::B2DRange aInk;
    for (const auto& rPortion : maTextPortionPrimitives)
        if (rPortion)
            aInk.expand(rPortion->getB2DRange(aViewInfo));
    basegfx::B2DRange aVertical;
    for (const auto& rGrad : maZetaGradientPortions)
        aVertical.expand(rGrad.maVertical);
    if (aInk.isEmpty())
    {
        maZetaGradientPortions.clear();
        return;
    }
    if (!maZetaLocalLineInk.isEmpty())
    {
        // 회전·기울인 글 상자: PowerPoint는 그라데이션을 회전 전 줄 상자에 잡고 글과 함께 돌린다 — 회전 전 좌표로
        // 범위를 재어 그라데이션을 그 좌표에 칠한 뒤 maNewTransformB로 돌린다(최종 좌표의 둘러싼 상자로 재면 범위가 커진다)
        basegfx::B2DRange aLocalVertical;
        for (const auto& rGrad : maZetaGradientPortions)
            aLocalVertical.expand(rGrad.maLocalVertical);
        const basegfx::B2DRange aLocalDefinition(
            maZetaLocalLineInk.getMinX(),
            aLocalVertical.isEmpty() ? maZetaLocalLineInk.getMinY() : aLocalVertical.getMinY(),
            maZetaLocalLineInk.getMaxX(),
            aLocalVertical.isEmpty() ? maZetaLocalLineInk.getMaxY() : aLocalVertical.getMaxY());
        for (const auto& rGrad : maZetaGradientPortions)
        {
            drawinglayer::attribute::FillGradientAttribute aAttr;
            if (!zetaParseTextGradient(rGrad.maSpec, aAttr))
                continue;
            basegfx::B2DRange aLocalOutput(aLocalDefinition);
            aLocalOutput.expand(rGrad.maLocalInk);
            // 기울임꼴 넘침 등을 덮도록 줄 높이만큼 넓힌다
            aLocalOutput.grow(std::max(aLocalOutput.getHeight(), 1.0));
            basegfx::B2DRange aOutput(aLocalOutput);
            aOutput.transform(maNewTransformB);
            for (size_t i = rGrad.mnStart; i < rGrad.mnEnd && i < maTextPortionPrimitives.size(); ++i)
            {
                const drawinglayer::primitive2d::Primitive2DReference xText(maTextPortionPrimitives[i]);
                if (!xText)
                    continue;
                basegfx::B2DRange aMaskRange(aOutput);
                aMaskRange.expand(xText->getB2DRange(aViewInfo));
                drawinglayer::primitive2d::Primitive2DContainer aMask{
                    new drawinglayer::primitive2d::PolyPolygonColorPrimitive2D(
                        basegfx::B2DPolyPolygon(basegfx::utils::createPolygonFromRect(aMaskRange)),
                        basegfx::BColor(1.0, 1.0, 1.0)),
                    new drawinglayer::primitive2d::ModifiedColorPrimitive2D(
                        drawinglayer::primitive2d::Primitive2DContainer{ xText },
                        std::make_shared<basegfx::BColorModifier_replace>(
                            basegfx::BColor(0.0, 0.0, 0.0)))
                };
                drawinglayer::primitive2d::Primitive2DContainer aContent{
                    new drawinglayer::primitive2d::TransformPrimitive2D(
                        maNewTransformB,
                        drawinglayer::primitive2d::Primitive2DContainer{
                            new drawinglayer::primitive2d::FillGradientPrimitive2D(
                                aLocalOutput, aLocalDefinition, aAttr) })
                };
                maTextPortionPrimitives[i] = new drawinglayer::primitive2d::TransparencePrimitive2D(
                    std::move(aContent), std::move(aMask));
            }
        }
        maZetaGradientPortions.clear();
        return;
    }
    const basegfx::B2DRange aDefinition(aInk.getMinX(),
                                        aVertical.isEmpty() ? aInk.getMinY() : aVertical.getMinY(),
                                        aInk.getMaxX(),
                                        aVertical.isEmpty() ? aInk.getMaxY() : aVertical.getMaxY());
    for (const auto& rGrad : maZetaGradientPortions)
    {
        drawinglayer::attribute::FillGradientAttribute aAttr;
        if (!zetaParseTextGradient(rGrad.maSpec, aAttr))
            continue;
        for (size_t i = rGrad.mnStart; i < rGrad.mnEnd && i < maTextPortionPrimitives.size(); ++i)
        {
            const drawinglayer::primitive2d::Primitive2DReference xText(maTextPortionPrimitives[i]);
            if (!xText)
                continue;
            basegfx::B2DRange aOutput(aDefinition);
            aOutput.expand(xText->getB2DRange(aViewInfo));
            // 글자를 검정으로 그린 것을 밝기 마스크로(흰 곳은 투명) 그라데이션을 글자 모양대로 남긴다
            drawinglayer::primitive2d::Primitive2DContainer aMask{
                new drawinglayer::primitive2d::PolyPolygonColorPrimitive2D(
                    basegfx::B2DPolyPolygon(basegfx::utils::createPolygonFromRect(aOutput)),
                    basegfx::BColor(1.0, 1.0, 1.0)),
                new drawinglayer::primitive2d::ModifiedColorPrimitive2D(
                    drawinglayer::primitive2d::Primitive2DContainer{ xText },
                    std::make_shared<basegfx::BColorModifier_replace>(
                        basegfx::BColor(0.0, 0.0, 0.0)))
            };
            drawinglayer::primitive2d::Primitive2DContainer aContent{
                new drawinglayer::primitive2d::FillGradientPrimitive2D(aOutput, aDefinition, aAttr)
            };
            maTextPortionPrimitives[i] = new drawinglayer::primitive2d::TransparencePrimitive2D(
                std::move(aContent), std::move(aMask));
        }
    }
    maZetaGradientPortions.clear();
}

void TextHierarchyBreakup::flushTextPortionPrimitivesToLinePrimitives()
{
    if (!maZetaGradientPortions.empty())
        zetaApplyTextGradients();
    maZetaLocalLineInk.reset();

    // only create a line primitive when we had content; there is no need for
    // empty line primitives (contrary to paragraphs, see below).
    if (!maTextPortionPrimitives.empty())
    {
        maLinePrimitives.push_back(new drawinglayer::primitive2d::TextHierarchyLinePrimitive2D(
            std::move(maTextPortionPrimitives)));
    }
}

sal_Int16 TextHierarchyBreakup::getOutlineLevelFromParagraph(sal_Int32 /*nPara*/) const
{
    return -1;
}

sal_Int32 TextHierarchyBreakup::getParagraphCount() const { return 0; }

void TextHierarchyBreakup::flushLinePrimitivesToParagraphPrimitives(sal_Int32 nPara)
{
    // ALWAYS create a paragraph primitive, even when no content was added. This is done to
    // have the correct paragraph count even with empty paragraphs. Those paragraphs will
    // have an empty sub-PrimitiveSequence.
    maParagraphPrimitives.push_back(
        new drawinglayer::primitive2d::TextHierarchyParagraphPrimitive2D(
            std::move(maLinePrimitives), getOutlineLevelFromParagraph(nPara)));
}

void TextHierarchyBreakup::processDrawPortionInfo(const DrawPortionInfo& rDrawPortionInfo)
{
    const size_t nBefore(maTextPortionPrimitives.size());
    CreateTextPortionPrimitivesFromDrawPortionInfo(maTextPortionPrimitives, maNewTransformA,
                                                   maNewTransformB, rDrawPortionInfo);
    // zeta: 회전·기울인 글 상자면 회전 전 좌표 범위도 잰다(그라데이션 범위를 PowerPoint처럼 회전 전 줄 상자로)
    basegfx::B2DRange aLocalInk;
    bool bZetaRotated(false);
    if (maTextPortionPrimitives.size() > nBefore)
    {
        basegfx::B2DTuple aScale, aTranslate;
        double fRotate(0.0), fShearX(0.0);
        maNewTransformB.decompose(aScale, aTranslate, fRotate, fShearX);
        bZetaRotated = !basegfx::fTools::equalZero(fRotate) || !basegfx::fTools::equalZero(fShearX);
        if (bZetaRotated)
        {
            drawinglayer::primitive2d::Primitive2DContainer aLocal;
            CreateTextPortionPrimitivesFromDrawPortionInfo(aLocal, maNewTransformA,
                                                           basegfx::B2DHomMatrix(), rDrawPortionInfo);
            const drawinglayer::geometry::ViewInformation2D aViewInfo;
            for (const auto& rLocal : aLocal)
                if (rLocal)
                    aLocalInk.expand(rLocal->getB2DRange(aViewInfo));
            maZetaLocalLineInk.expand(aLocalInk);
        }
    }
    if (!rDrawPortionInfo.maZetaTextGradient.isEmpty()
        && maTextPortionPrimitives.size() > nBefore)
    {
        maZetaGradientPortions.push_back(
            { nBefore, maTextPortionPrimitives.size(), rDrawPortionInfo.maZetaTextGradient,
              zetaPortionVerticalRange(rDrawPortionInfo, maNewTransformA, maNewTransformB),
              bZetaRotated ? zetaPortionVerticalRange(rDrawPortionInfo, maNewTransformA,
                                                      basegfx::B2DHomMatrix())
                           : basegfx::B2DRange(),
              aLocalInk });
    }
    // weboffice: font size in pt for the Excel text effect scale (Excel 2021 CopyPicture,
    // tests/tmp/xltextfx: a 30pt shadow distance is 11px at 11pt text, 20px at 28pt, 33px at 56pt;
    // a reflection distance is about size/15.2 times its nominal length)
    const double fZetaFontPt = rDrawPortionInfo.mrFont.GetFontHeight() * 72.0 / 2540.0;
    // weboffice: text outline stroke (after the gradient range so the gradient leaves it alone)
    if (!rDrawPortionInfo.maZetaTextOutline.isEmpty()
        && maTextPortionPrimitives.size() > nBefore)
    {
        zetaAddTextOutline(maTextPortionPrimitives, nBefore, rDrawPortionInfo.maZetaTextOutline,
                           rDrawPortionInfo.mrFont.GetColor(),
                           !rDrawPortionInfo.maZetaTextGradient.isEmpty());
    }
    // weboffice: soft text shadow under the portion (and its outline); shift the gradient range
    if (!rDrawPortionInfo.maZetaTextShadow.isEmpty() && rDrawPortionInfo.mrFont.IsShadow()
        && maTextPortionPrimitives.size() > nBefore)
    {
        const size_t nOld = maTextPortionPrimitives.size();
        zetaAddTextShadow(maTextPortionPrimitives, nBefore, rDrawPortionInfo.maZetaTextShadow,
                          zetaPortionVerticalRange(rDrawPortionInfo, maNewTransformA,
                                                   basegfx::B2DHomMatrix()),
                          maNewTransformB, std::clamp(0.13 + 0.0125 * fZetaFontPt, 0.1, 1.5));
        if (maTextPortionPrimitives.size() > nOld && !rDrawPortionInfo.maZetaTextGradient.isEmpty()
            && !maZetaGradientPortions.empty() && maZetaGradientPortions.back().mnStart == nBefore)
        {
            ++maZetaGradientPortions.back().mnStart;
            ++maZetaGradientPortions.back().mnEnd;
        }
    }
    // weboffice: text reflection under the portion (with its outline and shadow)
    if (!rDrawPortionInfo.maZetaTextReflection.isEmpty()
        && maTextPortionPrimitives.size() > nBefore)
    {
        const size_t nOld = maTextPortionPrimitives.size();
        zetaAddTextReflection(maTextPortionPrimitives, nBefore,
                              rDrawPortionInfo.maZetaTextReflection,
                              zetaPortionAscentRange(rDrawPortionInfo, maNewTransformA),
                              maNewTransformB, std::clamp(fZetaFontPt / 15.2, 0.1, 6.0));
        if (maTextPortionPrimitives.size() > nOld && !rDrawPortionInfo.maZetaTextGradient.isEmpty()
            && !maZetaGradientPortions.empty() && maZetaGradientPortions.back().mnStart == nBefore)
        {
            ++maZetaGradientPortions.back().mnStart;
            ++maZetaGradientPortions.back().mnEnd;
        }
    }

    if (rDrawPortionInfo.mbEndOfLine || rDrawPortionInfo.mbEndOfParagraph)
    {
        flushTextPortionPrimitivesToLinePrimitives();
    }

    if (rDrawPortionInfo.mbEndOfParagraph)
    {
        flushLinePrimitivesToParagraphPrimitives(rDrawPortionInfo.mnPara);
    }
}

void TextHierarchyBreakup::processDrawBulletInfo(const DrawBulletInfo& rDrawBulletInfo)
{
    CreateDrawBulletPrimitivesFromDrawBulletInfo(maTextPortionPrimitives, maNewTransformA,
                                                 maNewTransformB, rDrawBulletInfo);
    basegfx::B2DHomMatrix aNewTransform;

    // add size to new transform
    aNewTransform.scale(rDrawBulletInfo.maBulletSize.getWidth(),
                        rDrawBulletInfo.maBulletSize.getHeight());

    // apply transformA
    aNewTransform *= maNewTransformA;

    // apply local offset
    aNewTransform.translate(rDrawBulletInfo.maBulletPosition.X(),
                            rDrawBulletInfo.maBulletPosition.Y());

    // also apply embedding object's transform
    aNewTransform *= maNewTransformB;

    // prepare empty GraphicAttr
    const GraphicAttr aGraphicAttr;

    // create GraphicPrimitive2D
    const drawinglayer::primitive2d::Primitive2DReference aNewReference(
        new drawinglayer::primitive2d::GraphicPrimitive2D(
            aNewTransform, rDrawBulletInfo.maBulletGraphicObject, aGraphicAttr));

    // embed in TextHierarchyBulletPrimitive2D
    drawinglayer::primitive2d::Primitive2DContainer aNewSequence{ aNewReference };
    rtl::Reference<drawinglayer::primitive2d::BasePrimitive2D> pNewPrimitive
        = new drawinglayer::primitive2d::TextHierarchyBulletPrimitive2D(std::move(aNewSequence));

    // add to output
    maTextPortionPrimitives.push_back(pNewPrimitive);
}

void TextHierarchyBreakup::directlyAddB2DPrimitive(
    const drawinglayer::primitive2d::Primitive2DReference& rSource)
{
    maTextPortionPrimitives.push_back(rSource);
}

TextHierarchyBreakup::TextHierarchyBreakup()
    : maTextPortionPrimitives()
    , maLinePrimitives()
    , maParagraphPrimitives()
    , maNewTransformA()
    , maNewTransformB()
{
}

TextHierarchyBreakup::TextHierarchyBreakup(const basegfx::B2DHomMatrix& rNewTransformA,
                                           const basegfx::B2DHomMatrix& rNewTransformB)
    : maTextPortionPrimitives()
    , maLinePrimitives()
    , maParagraphPrimitives()
    , maNewTransformA(rNewTransformA)
    , maNewTransformB(rNewTransformB)
{
}

const drawinglayer::primitive2d::Primitive2DContainer&
TextHierarchyBreakup::getTextPortionPrimitives()
{
    if (!maTextPortionPrimitives.empty())
    {
        // collect non-closed lines
        flushTextPortionPrimitivesToLinePrimitives();
    }

    if (!maLinePrimitives.empty())
    {
        // collect non-closed paragraphs
        flushLinePrimitivesToParagraphPrimitives(getParagraphCount() - 1);
    }

    return maParagraphPrimitives;
}

sal_Int16 TextHierarchyBreakupOutliner::getOutlineLevelFromParagraph(sal_Int32 nPara) const
{
    sal_Int16 nDepth(mrOutliner.GetDepth(nPara));
    EBulletInfo eInfo(mrOutliner.GetBulletInfo(nPara));
    // Pass -1 to signal VclMetafileProcessor2D that there is no active
    // bullets/numbering in this paragraph (i.e. this is normal text)
    return eInfo.bVisible ? nDepth : -1;
}

sal_Int32 TextHierarchyBreakupOutliner::getParagraphCount() const
{
    return mrOutliner.GetParagraphCount();
}

TextHierarchyBreakupOutliner::TextHierarchyBreakupOutliner(Outliner& rOutliner)
    : TextHierarchyBreakup()
    , mrOutliner(rOutliner)
{
}

TextHierarchyBreakupOutliner::TextHierarchyBreakupOutliner(
    Outliner& rOutliner, const basegfx::B2DHomMatrix& rNewTransformA,
    const basegfx::B2DHomMatrix& rNewTransformB)
    : TextHierarchyBreakup(rNewTransformA, rNewTransformB)
    , mrOutliner(rOutliner)
{
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
