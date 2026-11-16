/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include <comphelper/lok.hxx>

#include <docmodel/theme/ColorSet.hxx>

#include <editeng/colritem.hxx>
#include <editeng/editeng.hxx>
#include <editeng/eeitem.hxx>
#include <editeng/section.hxx>

#include <LibreOfficeKit/LibreOfficeKitEnums.h>

#include <sal/config.h>

#include <sfx2/lokhelper.hxx>

#include <svx/PaletteManager.hxx>
#include <svx/svdmodel.hxx>
#include <svx/svdotext.hxx>
#include <svx/svdundo.hxx>
#include <svx/theme/ThemeColorChangerCommon.hxx>
#include <svx/theme/ThemeColorPaletteManager.hxx>
#include <svx/xdef.hxx>
#include <svx/xlnclit.hxx>
#include <svx/xflclit.hxx>
#include <tools/json_writer.hxx>

#include <algorithm>
#include <cmath>

using namespace css;

namespace svx::theme
{
namespace
{
// Shape fill/line theme colors carry DrawingML transformations (oox import, Office styles):
// a:tint/a:shade keep that share of the color in linear RGB (sRGB curve), unlike
// ComplexColor::applyTransformations, which scales HSL luminance (Word themeShade semantics).
// Excel 2021: accent FF0000 with shade 15% -> 6C0000, accent 4472C4 -> 172C51.
Color resolveDrawingMLColor(model::ColorSet const& rColorSet,
                            model::ComplexColor const& rComplexColor)
{
    Color aColor = rColorSet.getColor(rComplexColor.getThemeColorType());
    auto toLinear = [](double c) {
        return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
    };
    auto toSrgb = [](double c) {
        c = std::clamp(c, 0.0, 1.0);
        return c <= 0.0031308 ? c * 12.92 : 1.055 * std::pow(c, 1.0 / 2.4) - 0.055;
    };
    sal_Int16 nLumMod = 10'000;
    sal_Int16 nLumOff = 0;
    for (auto const& rTransform : rComplexColor.getTransformations())
    {
        switch (rTransform.meType)
        {
            case model::TransformationType::Tint:
            case model::TransformationType::Shade:
            {
                const bool bTint = rTransform.meType == model::TransformationType::Tint;
                const double f = std::clamp(rTransform.mnValue / 10000.0, 0.0, 1.0);
                auto apply = [&](sal_uInt8 n) {
                    double c = toLinear(n / 255.0);
                    c = bTint ? c * f + (1.0 - f) : c * f;
                    return sal_uInt8(std::round(toSrgb(c) * 255.0));
                };
                aColor = Color(apply(aColor.GetRed()), apply(aColor.GetGreen()),
                               apply(aColor.GetBlue()));
                break;
            }
            case model::TransformationType::LumMod:
                nLumMod = rTransform.mnValue;
                break;
            case model::TransformationType::LumOff:
                nLumOff = rTransform.mnValue;
                break;
            default:
                break;
        }
    }
    if (nLumMod != 10'000 || nLumOff != 0)
        aColor.ApplyLumModOff(nLumMod, nLumOff);
    return aColor;
}

const SvxColorItem* getColorItem(const editeng::Section& rSection)
{
    auto iterator = std::find_if(
        rSection.maAttributes.begin(), rSection.maAttributes.end(),
        [](const SfxPoolItem* pPoolItem) { return pPoolItem->Which() == EE_CHAR_COLOR; });

    if (iterator != rSection.maAttributes.end())
        return static_cast<const SvxColorItem*>(*iterator);
    return nullptr;
}

bool updateEditEngTextSections(model::ColorSet const& rColorSet, SdrObject* pObject, SdrView& rView)
{
    SdrTextObj* pTextObject = DynCastSdrTextObj(pObject);

    if (!pTextObject)
        return false;

    rView.SdrBeginTextEdit(pTextObject);

    auto* pOutlinerView = rView.GetTextEditOutlinerView();
    if (!pOutlinerView)
        return false;

    auto& rEditEngine = pOutlinerView->GetEditView().getEditEngine();

    OutlinerParaObject* pOutlinerParagraphObject = pTextObject->GetOutlinerParaObject();
    if (pOutlinerParagraphObject)
    {
        const EditTextObject& rEditText = pOutlinerParagraphObject->GetTextObject();
        std::vector<editeng::Section> aSections;
        rEditText.GetAllSections(aSections);

        for (editeng::Section const& rSection : aSections)
        {
            const SvxColorItem* pItem = getColorItem(rSection);
            if (!pItem)
                continue;

            model::ComplexColor const& rComplexColor = pItem->getComplexColor();
            if (rComplexColor.isValidThemeType())
            {
                SfxItemSet aSet(rEditEngine.GetAttribs(rSection.mnParagraph, rSection.mnStart,
                                                       rSection.mnEnd,
                                                       GetAttribsFlags::CHARATTRIBS));
                Color aNewColor = rColorSet.resolveColor(rComplexColor);
                std::unique_ptr<SvxColorItem> pNewItem(pItem->Clone());
                pNewItem->setColor(aNewColor);
                aSet.Put(*pNewItem);

                ESelection aSelection(rSection.mnParagraph, rSection.mnStart, rSection.mnParagraph,
                                      rSection.mnEnd);
                rEditEngine.QuickSetAttribs(aSet, aSelection);
            }
        }
    }

    rView.SdrEndTextEdit();

    return true;
}

bool updateObjectAttributes(model::ColorSet const& rColorSet, SdrObject& rObject,
                            SfxUndoManager* pUndoManager)
{
    bool bChanged = false;

    auto aItemSet = rObject.GetMergedItemSet();

    if (const XFillColorItem* pItem = aItemSet.GetItemIfSet(XATTR_FILLCOLOR, false))
    {
        model::ComplexColor const& rComplexColor = pItem->getComplexColor();
        if (rComplexColor.isValidThemeType())
        {
            Color aNewColor = resolveDrawingMLColor(rColorSet, rComplexColor);
            std::unique_ptr<XFillColorItem> pNewItem(pItem->Clone());
            pNewItem->SetColorValue(aNewColor);
            aItemSet.Put(*pNewItem);
            bChanged = true;
        }
    }
    if (const XLineColorItem* pItem = aItemSet.GetItemIfSet(XATTR_LINECOLOR, false))
    {
        model::ComplexColor const& rComplexColor = pItem->getComplexColor();
        if (rComplexColor.isValidThemeType())
        {
            Color aNewColor = resolveDrawingMLColor(rColorSet, rComplexColor);
            std::unique_ptr<XLineColorItem> pNewItem(pItem->Clone());
            pNewItem->SetColorValue(aNewColor);
            aItemSet.Put(*pNewItem);
            bChanged = true;
        }
    }
    if (const SvxColorItem* pItem = aItemSet.GetItemIfSet(EE_CHAR_COLOR, false))
    {
        model::ComplexColor const& rComplexColor = pItem->getComplexColor();
        if (rComplexColor.isValidThemeType())
        {
            Color aNewColor = rColorSet.resolveColor(rComplexColor);
            std::unique_ptr<SvxColorItem> pNewItem(pItem->Clone());
            pNewItem->setColor(aNewColor);
            aItemSet.Put(*pNewItem);
            bChanged = true;
        }
    }
    if (bChanged)
    {
        const bool bUndo = pUndoManager && pUndoManager->IsInListAction();
        if (bUndo)
        {
            pUndoManager->AddUndoAction(
                rObject.getSdrModelFromSdrObject().GetSdrUndoFactory().CreateUndoAttrObject(
                    rObject));
        }
        rObject.SetMergedItemSetAndBroadcast(aItemSet);
    }
    return bChanged;
}

} // end anonymous namespace

/// Updates properties of the SdrObject
void updateSdrObject(model::ColorSet const& rColorSet, SdrObject* pObject, SdrView* pView,
                     SfxUndoManager* pUndoManager)
{
    if (!pObject)
        return;
    updateObjectAttributes(rColorSet, *pObject, pUndoManager);
    if (pView)
        updateEditEngTextSections(rColorSet, pObject, *pView);
}

} // end svx::theme namespace

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
