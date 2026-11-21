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

#include <svx/sdr/contact/viewcontactofsdrobj.hxx>
#include <svx/sdr/contact/viewobjectcontactofsdrobj.hxx>
#include <svx/sdr/contact/viewobjectcontact.hxx>
#include <svx/svdobj.hxx>
#include <tools/debug.hxx>
#include <svx/svdpage.hxx>
#include <svx/sdr/contact/objectcontact.hxx>
#include <drawinglayer/primitive2d/markerarrayprimitive2d.hxx>
#include <drawinglayer/primitive2d/objectinfoprimitive2d.hxx>
#include <svx/svdhdl.hxx>
#include <svx/svdmodel.hxx>
#include <drawinglayer/primitive2d/transformprimitive2d.hxx>
#include <drawinglayer/primitive2d/transparenceprimitive2d.hxx>
#include <drawinglayer/primitive2d/PolyPolygonColorPrimitive2D.hxx>
#include <drawinglayer/geometry/viewinformation2d.hxx>
#include <basegfx/matrix/b2dhommatrixtools.hxx>
#include <basegfx/polygon/b2dpolygontools.hxx>
#include <com/sun/star/beans/PropertyValue.hpp>
#include <basegfx/numeric/ftools.hxx>
#include <drawinglayer/primitive2d/bitmapprimitive2d.hxx>
#include <drawinglayer/converters.hxx>
#include <vcl/bitmap.hxx>
#include <vcl/alpha.hxx>
#include <vcl/bitmap/BitmapFilterStackBlur.hxx>
#include <algorithm>
#include <cmath>

namespace sdr::contact {

namespace {

// weboffice: Office shape and picture reflection (a:reflection). LibreOffice has no reflection
// attribute; oox keeps it in the object's InteropGrabBag "EffectProperties" and writes it back
// on export. Draw it from there only: a mirrored copy of the object, faded by a transparence
// mask. Blur (blurRad) from 1pt is drawn as a blurred bitmap; skew (kx/ky) is not drawn.
// The reflection sits in an ObjectInfoPrimitive2D named "ZetaReflection" that hit tests skip.
struct ZetaReflection
{
    double fStA = 1.0, fStPos = 0.0, fEndA = 0.0, fEndPos = 1.0;
    sal_Int32 nDist = 0, nDir = 0, nFadeDir = 5400000, nSx = 100000, nSy = 100000;
    sal_Int32 nBlur = 0;
    OUString aAlgn = u"b"_ustr;
};

const css::uno::Sequence<css::beans::PropertyValue>* lcl_ZetaFind(
    const css::uno::Sequence<css::beans::PropertyValue>& rSeq, std::u16string_view aName,
    css::uno::Sequence<css::beans::PropertyValue>& rOut)
{
    for (const auto& rProp : rSeq)
        if (rProp.Name == aName && (rProp.Value >>= rOut))
            return &rOut;
    return nullptr;
}

bool lcl_ZetaGetReflection(const SdrObject& rObj, ZetaReflection& rRefl)
{
    css::uno::Any aBag;
    rObj.GetGrabBagItem(aBag);
    css::uno::Sequence<css::beans::PropertyValue> aSeq, aEffects, aReflection, aAttribs;
    if (!(aBag >>= aSeq) || !aSeq.hasElements())
        return false;
    if (!lcl_ZetaFind(aSeq, u"EffectProperties", aEffects)
        || !lcl_ZetaFind(aEffects, u"reflection", aReflection)
        || !lcl_ZetaFind(aReflection, u"Attribs", aAttribs))
        return false;
    for (const auto& rAttr : aAttribs)
    {
        sal_Int32 n = 0;
        if (rAttr.Name == "algn")
        {
            rAttr.Value >>= rRefl.aAlgn;
            continue;
        }
        if (!(rAttr.Value >>= n))
        {
            OUString aText;
            if (!(rAttr.Value >>= aText))
                continue;
            n = aText.toInt32();
        }
        if (rAttr.Name == "stA") rRefl.fStA = n / 100000.0;
        else if (rAttr.Name == "stPos") rRefl.fStPos = n / 100000.0;
        else if (rAttr.Name == "endA") rRefl.fEndA = n / 100000.0;
        else if (rAttr.Name == "endPos") rRefl.fEndPos = n / 100000.0;
        else if (rAttr.Name == "dist") rRefl.nDist = n;
        else if (rAttr.Name == "blurRad") rRefl.nBlur = n;
        else if (rAttr.Name == "dir") rRefl.nDir = n;
        else if (rAttr.Name == "fadeDir") rRefl.nFadeDir = n;
        else if (rAttr.Name == "sx") rRefl.nSx = n;
        else if (rAttr.Name == "sy") rRefl.nSy = n;
    }
    return true;
}

drawinglayer::primitive2d::Primitive2DContainer lcl_ZetaAddReflection(
    const SdrObject& rObj, drawinglayer::primitive2d::Primitive2DContainer aSource)
{
    ZetaReflection aRefl;
    if (aSource.empty() || !lcl_ZetaGetReflection(rObj, aRefl))
        return aSource;
    const tools::Rectangle aSnap(rObj.GetSnapRect());
    if (aSnap.IsEmpty() || (aRefl.fStA <= 0.0 && aRefl.fEndA <= 0.0))
        return aSource;
    const double fL = aSnap.Left(), fT = aSnap.Top(), fR = aSnap.Right(), fB = aSnap.Bottom();
    double fAx = (fL + fR) / 2, fAy = (fT + fB) / 2;
    const OUString& rA = aRefl.aAlgn;
    if (rA.startsWith("t")) fAy = fT;
    else if (rA.startsWith("b")) fAy = fB;
    if (rA == "l" || rA == "tl" || rA == "bl") fAx = fL;
    else if (rA == "r" || rA == "tr" || rA == "br") fAx = fR;
    // EMU to the model unit (1/100 mm in Calc and Impress, twips in Writer)
    const double fEmu = rObj.getSdrModelFromSdrObject().GetScaleUnit() == MapUnit::MapTwip
        ? 1.0 / 635.0 : 1.0 / 360.0;
    const double fDirRad = basegfx::deg2rad(aRefl.nDir / 60000.0);
    const double fDx = aRefl.nDist * fEmu * std::cos(fDirRad);
    const double fDy = aRefl.nDist * fEmu * std::sin(fDirRad);
    const double fSx = aRefl.nSx / 100000.0, fSy = aRefl.nSy / 100000.0;
    if (fSx == 0.0 || fSy == 0.0)
        return aSource;
    basegfx::B2DHomMatrix aMat(basegfx::utils::createTranslateB2DHomMatrix(-fAx, -fAy));
    aMat.scale(fSx, fSy);
    aMat.translate(fAx + fDx, fAy + fDy);

    drawinglayer::primitive2d::Primitive2DContainer aMirrored{
        new drawinglayer::primitive2d::TransformPrimitive2D(aMat, drawinglayer::primitive2d::Primitive2DContainer(aSource)) };
    basegfx::B2DRange aShape(fL, fT, fR, fB);
    aShape.transform(aMat);
    basegfx::B2DRange aAll(aMirrored.getB2DRange(drawinglayer::geometry::ViewInformation2D()));
    aAll.expand(aShape);
    if (aAll.isEmpty() || aShape.getWidth() <= 0.0 || aShape.getHeight() <= 0.0)
        return aSource;

    // fade direction, nearest of right/down/left/up; positions run from the reflection's
    // start edge across its size along that direction
    const sal_Int32 nQuarter = ((aRefl.nFadeDir + 2700000) / 5400000 % 4 + 4) % 4;
    const bool bVert = nQuarter == 1 || nQuarter == 3;
    const double fStart = nQuarter == 0 ? aShape.getMinX() : nQuarter == 1 ? aShape.getMinY()
                        : nQuarter == 2 ? aShape.getMaxX() : aShape.getMaxY();
    const double fSign = (nQuarter == 0 || nQuarter == 1) ? 1.0 : -1.0;
    const double fSize = bVert ? aShape.getHeight() : aShape.getWidth();
    const double fFar = 4.0 * (aAll.getWidth() + aAll.getHeight() + fSize);
    auto band = [&](double fFrom, double fTo, double fAlpha) {
        // fFrom/fTo are positions (0..1 over the size, may be beyond); alpha 1 = opaque
        double f1 = fStart + fSign * fFrom * fSize, f2 = fStart + fSign * fTo * fSize;
        basegfx::B2DRange aBand = bVert
            ? basegfx::B2DRange(aAll.getMinX(), std::min(f1, f2), aAll.getMaxX(), std::max(f1, f2))
            : basegfx::B2DRange(std::min(f1, f2), aAll.getMinY(), std::max(f1, f2), aAll.getMaxY());
        const double fGray = 1.0 - std::clamp(fAlpha, 0.0, 1.0);
        return drawinglayer::primitive2d::Primitive2DReference(
            new drawinglayer::primitive2d::PolyPolygonColorPrimitive2D(
                basegfx::B2DPolyPolygon(basegfx::utils::createPolygonFromRect(aBand)),
                basegfx::BColor(fGray, fGray, fGray)));
    };
    drawinglayer::primitive2d::Primitive2DContainer aMask;
    const double fFarPos = fFar / fSize;
    const double fStPos = std::clamp(aRefl.fStPos, 0.0, 1.0);
    const double fEndPos = std::clamp(aRefl.fEndPos, fStPos, 1.0);
    aMask.push_back(band(-fFarPos, fStPos, aRefl.fStA));
    const int nSteps = 48;
    for (int i = 0; i < nSteps && fEndPos > fStPos; ++i)
    {
        const double fA = fStPos + (fEndPos - fStPos) * i / nSteps;
        const double fZ = fStPos + (fEndPos - fStPos) * (i + 1) / nSteps;
        const double fT2 = (i + 0.5) / nSteps;
        aMask.push_back(band(fA, fZ, aRefl.fStA + (aRefl.fEndA - aRefl.fStA) * fT2));
    }
    if (aRefl.fEndA > 0.0)
        aMask.push_back(band(fEndPos, fFarPos, aRefl.fEndA));
    else
        aMask.push_back(band(fEndPos, fFarPos, 0.0));

    drawinglayer::primitive2d::Primitive2DReference xRefl(
        new drawinglayer::primitive2d::TransparencePrimitive2D(std::move(aMirrored), std::move(aMask)));

    // blur: render the faded reflection at 2 px per pt and blur color and alpha (stack blur);
    // below 1pt (Office presets use 0.5pt) the difference is under a pixel, keep it vector
    const double fBlurPt = aRefl.nBlur / 12700.0;
    if (fBlurPt >= 1.0)
    {
        const double fUnitsPerPt = fEmu * 12700.0;
        basegfx::B2DRange aRange(aAll);
        aRange.grow(fBlurPt * fUnitsPerPt);
        const double fPxPerUnit = 2.0 / fUnitsPerPt;
        const sal_uInt32 nW = std::max<sal_uInt32>(1, std::ceil(aRange.getWidth() * fPxPerUnit));
        const sal_uInt32 nH = std::max<sal_uInt32>(1, std::ceil(aRange.getHeight() * fPxPerUnit));
        basegfx::B2DHomMatrix aEmbed(basegfx::utils::createTranslateB2DHomMatrix(-aRange.getMinX(), -aRange.getMinY()));
        aEmbed.scale(nW / aRange.getWidth(), nH / aRange.getHeight());
        drawinglayer::primitive2d::Primitive2DContainer aSeq{
            new drawinglayer::primitive2d::TransformPrimitive2D(aEmbed, drawinglayer::primitive2d::Primitive2DContainer{ xRefl }) };
        const Bitmap aBmp(drawinglayer::convertToBitmap(std::move(aSeq), drawinglayer::geometry::ViewInformation2D(),
                                                        nW, nH, 1000000, true));
        if (!aBmp.IsEmpty() && aBmp.GetSizePixel().Width() > 0)
        {
            const double fScale = double(aBmp.GetSizePixel().Width()) / nW;
            const sal_Int32 nRadius = std::max<sal_Int32>(2, std::lround(fBlurPt * 2.0 * fScale));
            Bitmap aColor(aBmp.CreateColorBitmap());
            Bitmap aAlpha(aBmp.CreateAlphaMask().GetBitmap());
            BitmapFilter::Filter(aColor, BitmapFilterStackBlur(nRadius));
            BitmapFilter::Filter(aAlpha, BitmapFilterStackBlur(nRadius));
            xRefl = new drawinglayer::primitive2d::BitmapPrimitive2D(
                Bitmap(aColor, AlphaMask(aAlpha)),
                basegfx::utils::createScaleTranslateB2DHomMatrix(aRange.getWidth(), aRange.getHeight(),
                                                                 aRange.getMinX(), aRange.getMinY()));
        }
    }

    drawinglayer::primitive2d::Primitive2DContainer aRet;
    aRet.push_back(new drawinglayer::primitive2d::ObjectInfoPrimitive2D(
        drawinglayer::primitive2d::Primitive2DContainer{ xRefl }, u"ZetaReflection"_ustr, OUString(), OUString()));
    aRet.append(std::move(aSource));
    return aRet;
}

}

// Create an Object-Specific ViewObjectContact, set ViewContact and
// ObjectContact. Always needs to return something.
ViewObjectContact& ViewContactOfSdrObj::CreateObjectSpecificViewObjectContact(ObjectContact& rObjectContact)
{
    ViewObjectContact* pRetval = new ViewObjectContactOfSdrObj(rObjectContact, *this);
    DBG_ASSERT(pRetval, "ViewContactOfSdrObj::CreateObjectSpecificViewObjectContact() failed (!)");

    return *pRetval;
}

ViewContactOfSdrObj::ViewContactOfSdrObj(SdrObject& rObj)
:   mrObject(rObj)
{
}

ViewContactOfSdrObj::~ViewContactOfSdrObj()
{
}

// Access to possible sub-hierarchy
sal_uInt32 ViewContactOfSdrObj::GetObjectCount() const
{
    if(GetSdrObject().GetSubList())
    {
        return GetSdrObject().GetSubList()->GetObjCount();
    }

    return 0;
}

ViewContact& ViewContactOfSdrObj::GetViewContact(sal_uInt32 nIndex) const
{
    assert(GetSdrObject().GetSubList() &&
        "ViewContactOfSdrObj::GetViewContact: Access to non-existent Sub-List (!)");
    SdrObject* pObj = GetSdrObject().GetSubList()->GetObj(nIndex);
    assert(pObj && "ViewContactOfSdrObj::GetViewContact: Corrupt SdrObjList (!)");
    return pObj->GetViewContact();
}

ViewContact* ViewContactOfSdrObj::GetParentContact() const
{
    ViewContact* pRetval = nullptr;
    SdrObjList* pObjList = GetSdrObject().getParentSdrObjListFromSdrObject();

    if(pObjList)
    {
        if(auto pPage = dynamic_cast<SdrPage*>( pObjList))
        {
            // Is a page
            pRetval = &(pPage->GetViewContact());
        }
        else
        {
            // Is a group?
            if(pObjList->getSdrObjectFromSdrObjList())
            {
                pRetval = &(pObjList->getSdrObjectFromSdrObjList()->GetViewContact());
            }
        }
    }

    return pRetval;
}

// React on changes of the object of this ViewContact
void ViewContactOfSdrObj::ActionChanged()
{
    // look for own changes
    if (SdrTextObj* pTextObj = DynCastSdrTextObj(&GetSdrObject()))
    {
        //  tdf#146860 no idea why, but calling this makes the text boxes render properly
        pTextObj->GetTextAniKind();
    }

    // call parent
    ViewContact::ActionChanged();
}

// override for accessing the SdrObject
SdrObject* ViewContactOfSdrObj::TryToGetSdrObject() const
{
    return &GetSdrObject();
}


// primitive stuff

// add Gluepoints (if available)
drawinglayer::primitive2d::Primitive2DContainer ViewContactOfSdrObj::createGluePointPrimitive2DSequence() const
{
    drawinglayer::primitive2d::Primitive2DContainer xRetval;
    const SdrGluePointList* pGluePointList = GetSdrObject().GetGluePointList();

    if(pGluePointList)
    {
        const sal_uInt32 nCount(pGluePointList->GetCount());

        if(nCount)
        {
            // prepare point vector
            std::vector< basegfx::B2DPoint > aGluepointVector;

            // create GluePoint primitives. ATM these are relative to the SnapRect
            for(sal_uInt32 a(0); a < nCount; a++)
            {
                const SdrGluePoint& rCandidate = (*pGluePointList)[static_cast<sal_uInt16>(a)];
                const Point aPosition(rCandidate.GetAbsolutePos(GetSdrObject()));

                aGluepointVector.emplace_back(aPosition.X(), aPosition.Y());
            }

            if(!aGluepointVector.empty())
            {
                const drawinglayer::primitive2d::Primitive2DReference xReference(
                        new drawinglayer::primitive2d::MarkerArrayPrimitive2D(
                                std::move(aGluepointVector), SdrHdl::createGluePointBitmap()));
                xRetval = drawinglayer::primitive2d::Primitive2DContainer{ xReference };
            }
        }
    }

    return xRetval;
}

drawinglayer::primitive2d::Primitive2DContainer ViewContactOfSdrObj::embedToObjectSpecificInformation(drawinglayer::primitive2d::Primitive2DContainer aSource) const
{
    // weboffice: the Office reflection kept in the grab bag (draw only)
    aSource = lcl_ZetaAddReflection(GetSdrObject(), std::move(aSource));

    if(!aSource.empty() &&
        (!GetSdrObject().GetName().isEmpty() ||
         !GetSdrObject().GetTitle().isEmpty() ||
         !GetSdrObject().GetDescription().isEmpty()))
    {
        const drawinglayer::primitive2d::Primitive2DReference xRef(
            new drawinglayer::primitive2d::ObjectInfoPrimitive2D(
                std::move(aSource),
                GetSdrObject().GetName(),
                GetSdrObject().GetTitle(),
                GetSdrObject().GetDescription()));

        return drawinglayer::primitive2d::Primitive2DContainer { xRef };
    }

    return aSource;
}

}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
