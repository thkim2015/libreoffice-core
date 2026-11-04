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

#include <algorithm>
#include <cmath>
#include <o3tl/any.hxx>
#include <o3tl/string_view.hxx>
#include <oox/token/tokens.hxx>
#include "epptooxml.hxx"
#include <sax/fshelper.hxx>
#include <sal/log.hxx>
#include <rtl/math.hxx>
#include <comphelper/sequenceashashmap.hxx>
#include <unordered_set>

#include <com/sun/star/animations/AnimationAdditiveMode.hpp>
#include <com/sun/star/animations/AnimationCalcMode.hpp>
#include <com/sun/star/animations/AnimationFill.hpp>
#include <com/sun/star/animations/AnimationNodeType.hpp>
#include <com/sun/star/animations/AnimationRestart.hpp>
#include <com/sun/star/animations/AnimationTransformType.hpp>
#include <com/sun/star/animations/AnimationValueType.hpp>
#include <com/sun/star/animations/AnimationColorSpace.hpp>
#include <com/sun/star/animations/Timing.hpp>
#include <com/sun/star/animations/ValuePair.hpp>
#include <com/sun/star/animations/XAnimate.hpp>
#include <com/sun/star/animations/XAnimateMotion.hpp>
#include <com/sun/star/animations/XAnimateTransform.hpp>
#include <com/sun/star/animations/XAnimationNode.hpp>
#include <com/sun/star/animations/XAnimationNodeSupplier.hpp>
#include <com/sun/star/animations/XAnimateColor.hpp>
#include <com/sun/star/animations/XCommand.hpp>
#include <com/sun/star/animations/XAudio.hpp>
#include <com/sun/star/animations/XTransitionFilter.hpp>
#include <com/sun/star/animations/XIterateContainer.hpp>
#include <com/sun/star/animations/Event.hpp>
#include <com/sun/star/animations/EventTrigger.hpp>
#include <com/sun/star/container/XEnumerationAccess.hpp>
#include <com/sun/star/presentation/EffectCommands.hpp>
#include <xmloff/SoundReference.hxx>
#include <com/sun/star/presentation/EffectNodeType.hpp>
#include <com/sun/star/presentation/EffectPresetClass.hpp>
#include <com/sun/star/presentation/ParagraphTarget.hpp>
#include <com/sun/star/presentation/TextAnimationType.hpp>
#include <com/sun/star/text/XSimpleText.hpp>
#include <com/sun/star/drawing/XShape.hpp>
#include <com/sun/star/drawing/XShapes.hpp>
#include <com/sun/star/beans/XPropertySet.hpp>
#include <com/sun/star/lang/XServiceInfo.hpp>
#include <map>
#include <optional>
#include <oox/export/utils.hxx>
#include <oox/ppt/pptfilterhelpers.hxx>
#include <basegfx/polygon/b2dpolypolygontools.hxx>

#include "pptexanimations.hxx"
#include "pptx-animations.hxx"
#include "pptx-animations-cond.hxx"
#include "pptx-animations-nodectx.hxx"

using namespace ::com::sun::star;
using namespace ::com::sun::star::animations;
using namespace ::com::sun::star::container;
using namespace ::com::sun::star::presentation;
using namespace ::com::sun::star::uno;
using namespace ::ppt;
using namespace oox::drawingml;
using namespace oox::core;
using namespace oox;

using ::com::sun::star::beans::NamedValue;
using ::com::sun::star::drawing::XDrawPage;
using ::com::sun::star::drawing::XShape;
using ::com::sun::star::drawing::XShapes;
using ::com::sun::star::text::XSimpleText;
using ::sax_fastparser::FSHelperPtr;

namespace
{
void WriteAnimationProperty(const FSHelperPtr& pFS, const Any& rAny, sal_Int32 nToken = 0)
{
    if (!rAny.hasValue())
        return;

    ValuePair aPair;

    if (rAny >>= aPair)
    {
        double x, y;
        if ((aPair.First >>= x) && (aPair.Second >>= y))
        {
            if (nToken == XML_by)
            {
                // MS needs ending values but we have offset values.
                x += 1.0;
                y += 1.0;
            }
            pFS->singleElementNS(XML_p, nToken, XML_x, OString::number(x * 100000), XML_y,
                                 OString::number(y * 100000));
        }
        return;
    }
    // If rAny is not a valid ValuePair and token is XML_by
    if (nToken == XML_by)
        return;

    sal_Int32 nRgb = {}; // spurious -Werror=maybe-uninitialized
    double fDouble = {}; // spurious -Werror=maybe-uninitialized

    TypeClass aClass = rAny.getValueTypeClass();
    bool bWriteToken
        = nToken
          && (aClass == TypeClass_LONG || aClass == TypeClass_DOUBLE || aClass == TypeClass_STRING);

    if (bWriteToken)
        pFS->startElementNS(XML_p, nToken);

    switch (rAny.getValueTypeClass())
    {
        case TypeClass_LONG:
            if (!(rAny >>= nRgb))
            {
                assert(false);
            }
            pFS->singleElementNS(XML_a, XML_srgbClr, XML_val, I32SHEX(nRgb));
            break;
        case TypeClass_DOUBLE:
            if (!(rAny >>= fDouble))
            {
                assert(false);
            }
            pFS->singleElementNS(XML_p, XML_fltVal, XML_val, OString::number(fDouble));
            break;
        case TypeClass_STRING:
            pFS->singleElementNS(XML_p, XML_strVal, XML_val, *o3tl::doAccess<OUString>(rAny));
            break;
        default:
            break;
    }

    if (bWriteToken)
        pFS->endElementNS(XML_p, nToken);
}

void WriteAnimateColorColor(const FSHelperPtr& pFS, const Any& rAny, sal_Int32 nToken)
{
    if (!rAny.hasValue())
        return;

    sal_Int32 nColor = 0;
    if (rAny >>= nColor)
    {
        pFS->startElementNS(XML_p, nToken);

        if (nToken == XML_by)
        {
            // CT_TLByRgbColorTransform
            SAL_WARN("sd.eppt", "Export p:rgb in p:by of animClr isn't implemented yet.");
        }
        else
        {
            // CT_Color
            pFS->singleElementNS(XML_a, XML_srgbClr, XML_val, I32SHEX(nColor));
        }

        pFS->endElementNS(XML_p, nToken);
    }

    Sequence<double> aHSL(3);
    if (!(rAny >>= aHSL))
        return;

    pFS->startElementNS(XML_p, nToken);

    if (nToken == XML_by)
    {
        // CT_TLByHslColorTransform
        pFS->singleElementNS(
            XML_p, XML_hsl, XML_h, OString::number(std::round(aHSL[0] * 60000)), // ST_Angel
            XML_s, OString::number(aHSL[1] * 100000), XML_l, OString::number(aHSL[2] * 100000));
    }
    else
    {
        // CT_Color
        SAL_WARN("sd.eppt", "Export p:hsl in p:from or p:to of animClr isn't implemented yet.");
    }

    pFS->endElementNS(XML_p, nToken);
}

void WriteAnimateTo(const FSHelperPtr& pFS, const Any& rValue, const OUString& rAttributeName)
{
    if (!rValue.hasValue())
        return;

    SAL_INFO("sd.eppt", "to attribute name: " << rAttributeName.toUtf8());

    WriteAnimationProperty(pFS, AnimationExporter::convertAnimateValue(rValue, rAttributeName),
                           XML_to);
}

void WriteAnimateValues(const FSHelperPtr& pFS, const Reference<XAnimate>& rXAnimate)
{
    const Sequence<double> aKeyTimes = rXAnimate->getKeyTimes();
    if (!aKeyTimes.hasElements())
        return;
    const Sequence<Any> aValues = rXAnimate->getValues();
    const OUString sFormula = rXAnimate->getFormula();
    const OUString sAttributeName = rXAnimate->getAttributeName();

    SAL_INFO("sd.eppt", "animate values, formula: " << sFormula.toUtf8());

    assert(aValues.getLength() == aKeyTimes.getLength());

    pFS->startElementNS(XML_p, XML_tavLst);

    for (int i = 0; i < aKeyTimes.getLength(); i++)
    {
        SAL_INFO("sd.eppt", "animate value " << i << ": " << aKeyTimes[i]);
        if (aValues[i].hasValue())
        {
            // ST_TLTimeAnimateValueTime can't be negative ([ISO/IEC 29500-1] 19.7.39)
            sal_uInt32 nTime = static_cast<sal_uInt32>(std::max(0.0, aKeyTimes[i] * 100000.0));

            pFS->startElementNS(XML_p, XML_tav, XML_fmla,
                                sax_fastparser::UseIf(sFormula, !sFormula.isEmpty()), XML_tm,
                                sax_fastparser::UseIf(OString::number(nTime), nTime != 0));
            pFS->startElementNS(XML_p, XML_val);
            ValuePair aPair;
            if (aValues[i] >>= aPair)
            {
                WriteAnimationProperty(
                    pFS, AnimationExporter::convertAnimateValue(aPair.First, sAttributeName));
                WriteAnimationProperty(
                    pFS, AnimationExporter::convertAnimateValue(aPair.Second, sAttributeName));
            }
            else
                WriteAnimationProperty(
                    pFS, AnimationExporter::convertAnimateValue(aValues[i], sAttributeName));

            pFS->endElementNS(XML_p, XML_val);
            pFS->endElementNS(XML_p, XML_tav);
        }
    }

    pFS->endElementNS(XML_p, XML_tavLst);
}

// Write condition list ( either prevCondlst or nextCondlst ) of Seq.
void WriteAnimationCondListForSeq(const FSHelperPtr& pFS, sal_Int32 nToken)
{
    const char* pEvent = (nToken == XML_prevCondLst) ? "onPrev" : "onNext";

    pFS->startElementNS(XML_p, nToken);
    pFS->startElementNS(XML_p, XML_cond, XML_evt, pEvent);
    pFS->startElementNS(XML_p, XML_tgtEl);
    pFS->singleElementNS(XML_p, XML_sldTgt);
    pFS->endElementNS(XML_p, XML_tgtEl);
    pFS->endElementNS(XML_p, XML_cond);
    pFS->endElementNS(XML_p, nToken);
}

void WriteAnimationAttributeName(const FSHelperPtr& pFS, const OUString& rAttributeName)
{
    if (rAttributeName.isEmpty())
        return;

    pFS->startElementNS(XML_p, XML_attrNameLst);

    SAL_INFO("sd.eppt", "write attribute name: " << rAttributeName.toUtf8());

    if (rAttributeName == "X;Y")
    {
        pFS->startElementNS(XML_p, XML_attrName);
        pFS->writeEscaped("ppt_x");
        pFS->endElementNS(XML_p, XML_attrName);

        pFS->startElementNS(XML_p, XML_attrName);
        pFS->writeEscaped("ppt_y");
        pFS->endElementNS(XML_p, XML_attrName);
    }
    else
    {
        const oox::ppt::ImplAttributeNameConversion* attrConv
            = oox::ppt::getAttributeConversionList();
        const char* pAttribute = nullptr;

        while (attrConv->mpAPIName != nullptr)
        {
            // API names are case insensitive for the slide show ("visibility" == "Visibility")
            if (rAttributeName.equalsIgnoreAsciiCaseAscii(attrConv->mpAPIName))
            {
                pAttribute = attrConv->mpMSName;
                break;
            }
            attrConv++;
        }

        pFS->startElementNS(XML_p, XML_attrName);
        if (pAttribute)
            pFS->writeEscaped(pAttribute);
        else
            SAL_WARN("sd.eppt", "unhandled animation attribute name: " << rAttributeName);
        pFS->endElementNS(XML_p, XML_attrName);
    }

    pFS->endElementNS(XML_p, XML_attrNameLst);
}

/// extract ooxml node type from a XAnimationNode.
sal_Int32 extractNodeType(const Reference<XAnimationNode>& rXNode)
{
    sal_Int16 nType = rXNode->getType();
    sal_Int32 xmlNodeType = -1;
    switch (nType)
    {
        case AnimationNodeType::ITERATE:
        case AnimationNodeType::PAR:
            xmlNodeType = XML_par;
            break;
        case AnimationNodeType::SEQ:
            xmlNodeType = XML_seq;
            break;
        case AnimationNodeType::ANIMATE:
            xmlNodeType = XML_anim;
            break;
        case AnimationNodeType::ANIMATEMOTION:
            xmlNodeType = XML_animMotion;
            break;
        case AnimationNodeType::ANIMATETRANSFORM:
        {
            Reference<XAnimateTransform> xTransform(rXNode, UNO_QUERY);
            if (xTransform.is())
            {
                if (xTransform->getTransformType() == AnimationTransformType::SCALE)
                    xmlNodeType = XML_animScale;
                else if (xTransform->getTransformType() == AnimationTransformType::ROTATE)
                    xmlNodeType = XML_animRot;
            }
            break;
        }
        case AnimationNodeType::ANIMATECOLOR:
            xmlNodeType = XML_animClr;
            break;
        case AnimationNodeType::SET:
            xmlNodeType = XML_set;
            break;
        case AnimationNodeType::TRANSITIONFILTER:
            xmlNodeType = XML_animEffect;
            break;
        case AnimationNodeType::COMMAND:
            xmlNodeType = XML_cmd;
            break;
        case AnimationNodeType::AUDIO:
            xmlNodeType = XML_audio;
            break;
        default:
            SAL_WARN("sd.eppt", "unhandled animation node: " << nType);
            break;
    }
    return xmlNodeType;
}

/// Convert AnimationRestart to ST_TLTimeNodeRestartType value.
const char* convertAnimationRestart(sal_Int16 nRestart)
{
    const char* pRestart = nullptr;
    switch (nRestart)
    {
        case AnimationRestart::ALWAYS:
            pRestart = "always";
            break;
        case AnimationRestart::WHEN_NOT_ACTIVE:
            pRestart = "whenNotActive";
            break;
        case AnimationRestart::NEVER:
            pRestart = "never";
            break;
    }
    return pRestart;
}

/// Convert EffectNodeType to ST_TLTimeNodeType
const char* convertEffectNodeType(sal_Int16 nType)
{
    const char* pNodeType = nullptr;
    switch (nType)
    {
        case EffectNodeType::TIMING_ROOT:
            pNodeType = "tmRoot";
            break;
        case EffectNodeType::MAIN_SEQUENCE:
            pNodeType = "mainSeq";
            break;
        case EffectNodeType::ON_CLICK:
            pNodeType = "clickEffect";
            break;
        case EffectNodeType::AFTER_PREVIOUS:
            pNodeType = "afterEffect";
            break;
        case EffectNodeType::WITH_PREVIOUS:
            pNodeType = "withEffect";
            break;
        case EffectNodeType::INTERACTIVE_SEQUENCE:
            pNodeType = "interactiveSeq";
            break;
    }
    return pNodeType;
}

/// Convert EffectPresetClass to ST_TLTimeNodePresetClassType
const char* convertEffectPresetClass(sal_Int16 nPresetClass)
{
    const char* pPresetClass = nullptr;
    switch (nPresetClass)
    {
        case EffectPresetClass::ENTRANCE:
            pPresetClass = "entr";
            break;
        case EffectPresetClass::EXIT:
            pPresetClass = "exit";
            break;
        case EffectPresetClass::EMPHASIS:
            pPresetClass = "emph";
            break;
        case EffectPresetClass::MOTIONPATH:
            pPresetClass = "path";
            break;
        case EffectPresetClass::OLEACTION:
            pPresetClass = "verb"; // ?
            break;
        case EffectPresetClass::MEDIACALL:
            pPresetClass = "mediacall";
            break;
    }
    return pPresetClass;
}

/// convert AnimationFill to ST_TLTimeNodeFillType.
const char* convertAnimationFill(sal_Int16 nFill)
{
    const char* pFill = nullptr;
    switch (nFill)
    {
        case AnimationFill::FREEZE:
            pFill = "hold";
            break;
        case AnimationFill::HOLD:
            pFill = "hold";
            break;
        case AnimationFill::REMOVE:
            pFill = "remove";
            break;
        case AnimationFill::TRANSITION:
            pFill = "transition";
            break;
    }
    return pFill;
}

/// Convert TextAnimationType to ST_IterateType.
const char* convertTextAnimationType(sal_Int16 nType)
{
    const char* sType = nullptr;
    switch (nType)
    {
        case TextAnimationType::BY_PARAGRAPH:
            sType = "el";
            break;
        case TextAnimationType::BY_LETTER:
            sType = "lt";
            break;
        case TextAnimationType::BY_WORD:
        default:
            sType = "wd";
            break;
    }
    return sType;
}

class PPTXAnimationExport
{
    void WriteAnimationNode(const NodeContextPtr& pContext) { WriteAnimationNode(pContext.get()); }
    void WriteAnimationNode(const NodeContext* pContext);
    /// Collects after effect nodes ("master-element") by their master effect node.
    void CollectAfterEffects(const NodeContext* pContext);

    /// One effect node of the build list (p:bldLst).
    struct BuildEffect
    {
        Reference<XAnimationNode> xNode;
        Reference<XAnimationNode> xSequence;
        Reference<XShape> xShape;
        sal_Int32 nShapeID = -1;
        sal_Int32 nParagraph = -1;
        sal_Int16 nNodeType = -1;
        sal_Int16 nPresetClass = 0;
        OUString aPresetId;
    };
    /// Effects of one shape that PowerPoint treats as one build (p:bldP, p:cTn grpId).
    struct BuildGroup
    {
        sal_Int32 nShapeID = -1;
        sal_Int32 nGroupId = 0;
        Reference<XShape> xShape;
        std::vector<size_t> aItems;
        bool bParagraphs = false;
    };
    void CollectBuildEffects(const NodeContext* pContext,
                             const Reference<XAnimationNode>& xSequence,
                             std::vector<BuildEffect>& rEffects);
    std::vector<BuildGroup> CreateBuildGroups(const std::vector<BuildEffect>& rEffects);
    void WriteBuildList(const std::vector<BuildEffect>& rEffects,
                        const std::vector<BuildGroup>& rGroups);
    /// grpId of the effect nodes (p:cTn with presetClass)
    std::map<Reference<XAnimationNode>, sal_Int32> maGroupIds;
    void WriteAnimationNodeAnimate(sal_Int32 nXmlNodeType);
    void WriteAnimationNodeAnimateInside(bool bSimple, bool bWriteTo = true);
    void WriteAnimationNodeSeq();
    void WriteAnimationNodeEffect();
    void WriteAnimationNodeCommand();
    /// Handles XAudio nodes, used for both video and audio.
    void WriteAnimationNodeMedia(const sal_Int16 nParentNodeType);
    void WriteAnimationNodeCommonPropsStart();
    void WriteAnimationTarget(const Any& rTarget);
    void WriteAnimationCondList(const std::vector<Cond>& rList, sal_Int32 nToken);
    void WriteAnimationCond(const Cond& rCond);
    const Reference<XAnimationNode>& getCurrentNode() const;

    PowerPointExport& mrPowerPointExport;
    const FSHelperPtr& mpFS;
    const NodeContext* mpContext;

    std::map<Reference<XAnimationNode>, sal_Int32> maAnimationNodeIdMap;
    /// PowerPoint 'After animation' (dim / hide): written into the master's p:subTnLst
    std::map<Reference<XAnimationNode>, std::vector<const NodeContext*>> maAfterEffects;
    sal_Int32 GetNextAnimationNodeId(const Reference<XAnimationNode>& rNode);
    sal_Int32 GetAnimationNodeId(const Reference<XAnimationNode>& rNode);

public:
    PPTXAnimationExport(PowerPointExport& rExport, const FSHelperPtr& pFS);
    void WriteAnimations(const Reference<XDrawPage>& rXDrawPage);
};

/// Returns if rURL has an extension which is an audio format.
bool IsAudioURL(std::u16string_view rURL)
{
    return o3tl::endsWithIgnoreAsciiCase(rURL, ".wav")
           || o3tl::endsWithIgnoreAsciiCase(rURL, ".m4a");
}

/// Returns if rURL has an extension which is a video format.
bool IsVideoURL(std::u16string_view rURL) { return o3tl::endsWithIgnoreAsciiCase(rURL, ".mp4"); }

/// First target (shape or paragraph) in the subtree of an effect node.
Any lcl_findEffectTarget(const Reference<XAnimationNode>& xNode)
{
    Reference<XAnimate> xAnimate(xNode, UNO_QUERY);
    if (xAnimate.is() && xAnimate->getTarget().hasValue())
        return xAnimate->getTarget();
    Reference<XIterateContainer> xIterate(xNode, UNO_QUERY);
    if (xIterate.is() && xIterate->getTarget().hasValue())
        return xIterate->getTarget();
    Reference<XCommand> xCommand(xNode, UNO_QUERY);
    if (xCommand.is() && xCommand->getTarget().hasValue())
        return xCommand->getTarget();
    Reference<XEnumerationAccess> xAccess(xNode, UNO_QUERY);
    if (!xAccess.is())
        return Any();
    Reference<XEnumeration> xEnum(xAccess->createEnumeration());
    while (xEnum.is() && xEnum->hasMoreElements())
    {
        Reference<XAnimationNode> xChild(xEnum->nextElement(), UNO_QUERY);
        Any aTarget = lcl_findEffectTarget(xChild);
        if (aTarget.hasValue())
            return aTarget;
    }
    return Any();
}

/// Outline depth (NumberingLevel) of every paragraph of the shape text.
std::vector<sal_Int16> lcl_paragraphDepths(const Reference<XShape>& xShape)
{
    std::vector<sal_Int16> aDepths;
    Reference<XEnumerationAccess> xAccess(xShape, UNO_QUERY);
    if (!xAccess.is())
        return aDepths;
    try
    {
        Reference<XEnumeration> xEnum(xAccess->createEnumeration());
        while (xEnum.is() && xEnum->hasMoreElements())
        {
            Reference<XPropertySet> xPara(xEnum->nextElement(), UNO_QUERY);
            sal_Int16 nDepth = 0;
            if (xPara.is())
                xPara->getPropertyValue(u"NumberingLevel"_ustr) >>= nDepth;
            aDepths.push_back(std::max<sal_Int16>(0, nDepth));
        }
    }
    catch (const Exception&)
    {
    }
    return aDepths;
}
}

namespace oox::core
{
void WriteAnimations(const FSHelperPtr& pFS, const Reference<XDrawPage>& rXDrawPage,
                     PowerPointExport& rExport)
{
    PPTXAnimationExport aAnimationExport(rExport, pFS);
    aAnimationExport.WriteAnimations(rXDrawPage);
}
}

PPTXAnimationExport::PPTXAnimationExport(PowerPointExport& rExport, const FSHelperPtr& pFS)
    : mrPowerPointExport(rExport)
    , mpFS(pFS)
    , mpContext(nullptr)
{
}

const Reference<XAnimationNode>& PPTXAnimationExport::getCurrentNode() const
{
    assert(mpContext);
    return mpContext->getNode();
}

void PPTXAnimationExport::WriteAnimationTarget(const Any& rTarget)
{
    sal_Int32 nParagraph = -1;
    bool bParagraphTarget = false;

    Reference<XShape> rXShape;
    rTarget >>= rXShape;

    if (!rXShape.is())
    {
        ParagraphTarget aParagraphTarget;
        if (rTarget >>= aParagraphTarget)
            rXShape = aParagraphTarget.Shape;
        if (rXShape.is())
        {
            nParagraph = static_cast<sal_Int32>(aParagraphTarget.Paragraph);
            Reference<XSimpleText> xText(rXShape, UNO_QUERY);
            if (xText.is())
            {
                bParagraphTarget = true;
            }
        }
    }

    if (!rXShape.is())
        return;

    sal_Int32 nShapeID = mrPowerPointExport.GetShapeID(rXShape);

    mpFS->startElementNS(XML_p, XML_tgtEl);
    mpFS->startElementNS(XML_p, XML_spTgt, XML_spid, OString::number(nShapeID));
    if (bParagraphTarget)
    {
        mpFS->startElementNS(XML_p, XML_txEl);
        mpFS->singleElementNS(XML_p, XML_pRg, XML_st, OString::number(nParagraph), XML_end,
                              OString::number(nParagraph));
        mpFS->endElementNS(XML_p, XML_txEl);
    }
    mpFS->endElementNS(XML_p, XML_spTgt);
    mpFS->endElementNS(XML_p, XML_tgtEl);
}

void PPTXAnimationExport::WriteAnimationCondList(const std::vector<Cond>& rList, sal_Int32 nToken)
{
    if (rList.size() > 0)
    {
        mpFS->startElementNS(XML_p, nToken);

        for (const Cond& rCond : rList)
            WriteAnimationCond(rCond);

        mpFS->endElementNS(XML_p, nToken);
    }
}

void PPTXAnimationExport::WriteAnimationCond(const Cond& rCond)
{
    if (rCond.mpEvent)
    {
        sal_Int32 nId = -1;
        if (rCond.mxShape.is())
        {
            mpFS->startElementNS(XML_p, XML_cond, XML_delay, rCond.getDelay(), XML_evt,
                                 rCond.mpEvent);
            WriteAnimationTarget(Any(rCond.mxShape));
            mpFS->endElementNS(XML_p, XML_cond);
        }
        else if (rCond.mxNode.is() && (nId = GetAnimationNodeId(rCond.mxNode)) != -1)
        {
            mpFS->startElementNS(XML_p, XML_cond, XML_delay, rCond.getDelay(), XML_evt,
                                 rCond.mpEvent);
            mpFS->singleElementNS(XML_p, XML_tn, XML_val, OString::number(nId));
            mpFS->endElementNS(XML_p, XML_cond);
        }
        else
        {
            mpFS->singleElementNS(XML_p, XML_cond, XML_delay, rCond.getDelay(), XML_evt,
                                  rCond.mpEvent);
        }
    }
    else
        mpFS->singleElementNS(XML_p, XML_cond, XML_delay, rCond.getDelay());
}

void PPTXAnimationExport::WriteAnimationNodeAnimate(sal_Int32 nXmlNodeType)
{
    const Reference<XAnimationNode>& rXNode = getCurrentNode();
    Reference<XAnimate> rXAnimate(rXNode, UNO_QUERY);
    if (!rXAnimate.is())
        return;

    const char* pCalcMode = nullptr;
    const char* pValueType = nullptr;
    bool bSimple = (nXmlNodeType != XML_anim);
    bool bTo = true;

    if (!bSimple)
    {
        switch (rXAnimate->getCalcMode())
        {
            case AnimationCalcMode::DISCRETE:
                pCalcMode = "discrete";
                break;
            case AnimationCalcMode::LINEAR:
                pCalcMode = "lin";
                break;
        }

        switch (AnimationExporter::GetValueTypeForAttributeName(rXAnimate->getAttributeName()))
        {
            case AnimationValueType::STRING:
                pValueType = "str";
                break;
            case AnimationValueType::NUMBER:
                pValueType = "num";
                break;
            case AnimationValueType::COLOR:
                pValueType = "clr";
                break;
        }
    }

    if (nXmlNodeType == XML_animMotion)
    {
        OUString aPath;
        Reference<XAnimateMotion> xMotion(rXNode, UNO_QUERY);
        if (xMotion.is())
        {
            xMotion->getPath() >>= aPath;
            ::basegfx::B2DPolyPolygon aPolyPoly;
            if (::basegfx::utils::importFromSvgD(aPolyPoly, aPath, true, nullptr))
                aPath = ::basegfx::utils::exportToSvgD(aPolyPoly, false, false, true, true);
        }

        mpFS->startElementNS(XML_p, nXmlNodeType, XML_origin, "layout", XML_path, aPath);
    }
    else if (nXmlNodeType == XML_animRot)
    {
        // when const char* is nullptr, the attribute is completely omitted in the output
        const char* pBy = nullptr;
        const char* pFrom = nullptr;
        const char* pTo = nullptr;
        OString aBy, aFrom, aTo;

        Reference<XAnimateTransform> xTransform(rXNode, UNO_QUERY);
        if (xTransform.is())
        {
            double value;
            if (xTransform->getBy() >>= value)
            {
                aBy = OString::number(static_cast<int>(value * PER_DEGREE));
                pBy = aBy.getStr();
            }

            if (xTransform->getFrom() >>= value)
            {
                aFrom = OString::number(static_cast<int>(value * PER_DEGREE));
                pFrom = aFrom.getStr();
            }

            if (xTransform->getTo() >>= value)
            {
                aTo = OString::number(static_cast<int>(value * PER_DEGREE));
                pTo = aTo.getStr();
            }
        }

        mpFS->startElementNS(XML_p, nXmlNodeType, XML_by, pBy, XML_from, pFrom, XML_to, pTo);
        // p:to is unexpected child element for p:animRot
        bTo = false;
    }
    else if (nXmlNodeType == XML_animClr)
    {
        Reference<XAnimateColor> xColor(rXNode, UNO_QUERY);
        const char* pColorSpace = "rgb";
        const char* pDirection = nullptr;
        if (xColor.is() && xColor->getColorInterpolation() == AnimationColorSpace::HSL)
        {
            // Note: from, to, by can still be specified in any supported format.
            pColorSpace = "hsl";
            pDirection = xColor->getDirection() ? "cw" : "ccw";
        }
        mpFS->startElementNS(XML_p, nXmlNodeType, XML_clrSpc, pColorSpace, XML_dir, pDirection,
                             XML_calcmode, pCalcMode, XML_valueType, pValueType);
    }
    else
    {
        OUString sFrom, sTo, sBy;
        if (rXAnimate.is() && nXmlNodeType == XML_anim)
        {
            OUString sAttributeName = rXAnimate->getAttributeName();
            Any aFrom
                = AnimationExporter::convertAnimateValue(rXAnimate->getFrom(), sAttributeName);
            aFrom >>= sFrom;
            Any aTo = AnimationExporter::convertAnimateValue(rXAnimate->getTo(), sAttributeName);
            aTo >>= sTo;
            Any aBy = AnimationExporter::convertAnimateValue(rXAnimate->getBy(), sAttributeName);
            aBy >>= sBy;
        }

        mpFS->startElementNS(XML_p, nXmlNodeType, XML_calcmode, pCalcMode, XML_valueType,
                             pValueType, XML_from, sax_fastparser::UseIf(sFrom, !sFrom.isEmpty()),
                             XML_to, sax_fastparser::UseIf(sTo, !sTo.isEmpty()), XML_by,
                             sax_fastparser::UseIf(sBy, !sBy.isEmpty()));
        bTo = sTo.isEmpty() && sFrom.isEmpty() && sBy.isEmpty();
    }

    WriteAnimationNodeAnimateInside(bSimple, bTo);
    mpFS->endElementNS(XML_p, nXmlNodeType);
}

void PPTXAnimationExport::WriteAnimationNodeAnimateInside(bool bSimple, bool bWriteTo)
{
    const Reference<XAnimationNode>& rXNode = getCurrentNode();
    Reference<XAnimate> rXAnimate(rXNode, UNO_QUERY);
    if (!rXAnimate.is())
        return;

    const char* pAdditive = nullptr;

    if (!bSimple)
    {
        switch (rXAnimate->getAdditive())
        {
            case AnimationAdditiveMode::BASE:
                pAdditive = "base";
                break;
            case AnimationAdditiveMode::SUM:
                pAdditive = "sum";
                break;
            case AnimationAdditiveMode::REPLACE:
                pAdditive = "repl";
                break;
            case AnimationAdditiveMode::MULTIPLY:
                pAdditive = "mult";
                break;
            case AnimationAdditiveMode::NONE:
                pAdditive = "none";
                break;
        }
    }

    mpFS->startElementNS(XML_p, XML_cBhvr, XML_additive, pAdditive);
    WriteAnimationNodeCommonPropsStart();

    Reference<XIterateContainer> xIterate(rXNode->getParent(), UNO_QUERY);
    WriteAnimationTarget(xIterate.is() ? xIterate->getTarget() : rXAnimate->getTarget());

    Reference<XAnimateTransform> xTransform(rXNode, UNO_QUERY);

    // The attribute name of AnimateTransform is "Transform", we have to fix it.
    OUString sNewAttr;
    if (xTransform.is() && xTransform->getTransformType() == AnimationTransformType::ROTATE)
        sNewAttr = "Rotate";

    WriteAnimationAttributeName(mpFS, xTransform.is() ? sNewAttr : rXAnimate->getAttributeName());

    mpFS->endElementNS(XML_p, XML_cBhvr);
    WriteAnimateValues(mpFS, rXAnimate);

    Reference<XAnimateColor> xColor(rXNode, UNO_QUERY);

    if (xColor.is())
    {
        WriteAnimateColorColor(mpFS, xColor->getBy(), XML_by);
        WriteAnimateColorColor(mpFS, xColor->getFrom(), XML_from);
        WriteAnimateColorColor(mpFS, xColor->getTo(), XML_to);
    }
    else if (xTransform.is() && xTransform->getTransformType() == AnimationTransformType::SCALE)
    {
        WriteAnimationProperty(mpFS, rXAnimate->getBy(), XML_by);
        WriteAnimationProperty(mpFS, rXAnimate->getFrom(), XML_from);
        WriteAnimationProperty(mpFS, rXAnimate->getTo(), XML_to);
    }
    else if (bWriteTo)
        WriteAnimateTo(mpFS, rXAnimate->getTo(), rXAnimate->getAttributeName());
}

void PPTXAnimationExport::WriteAnimationNodeCommonPropsStart()
{
    const Reference<XAnimationNode>& rXNode = getCurrentNode();
    std::optional<OString> sDuration;
    std::optional<OString> sRepeatCount;
    const char* pRestart = nullptr;
    const char* pNodeType = nullptr;
    const char* pPresetClass = nullptr;
    const char* pFill = nullptr;
    double fDuration = 0;
    double fRepeatCount = 0;
    Any aAny;
    assert(mpContext);

    aAny = rXNode->getDuration();
    if (aAny.hasValue())
    {
        Timing eTiming;

        if (aAny >>= eTiming)
        {
            if (eTiming == Timing_INDEFINITE)
                sDuration = "indefinite";
        }
        else
            aAny >>= fDuration;
    }

    pRestart = convertAnimationRestart(rXNode->getRestart());

    sal_Int16 nType = mpContext->getEffectNodeType();
    if (nType != -1)
    {
        pNodeType = convertEffectNodeType(nType);
        if (nType == EffectNodeType::TIMING_ROOT)
        {
            if (!sDuration)
                sDuration = "indefinite";
            if (!pRestart)
                pRestart = "never";
        }
        else if (nType == EffectNodeType::MAIN_SEQUENCE)
        {
            sDuration = "indefinite";
        }
    }

    if (fDuration != 0)
        sDuration = OString::number(static_cast<sal_Int32>(fDuration * 1000.0));

    sal_uInt32 nPresetClass = mpContext->getEffectPresetClass();
    if (nPresetClass != EffectPresetClass::CUSTOM)
        pPresetClass = convertEffectPresetClass(nPresetClass);

    sal_uInt32 nPresetId = 0;
    bool bPresetId = false;
    const OUString& rPresetId = mpContext->getEffectPresetId();
    if (rPresetId.getLength() > 0)
    {
        nPresetId = AnimationExporter::GetPresetID(rPresetId, nPresetClass, bPresetId);
        bPresetId = true;
    }

    sal_uInt32 nPresetSubType = 0;
    bool bPresetSubType = false;
    const OUString& sPresetSubType = mpContext->getEffectPresetSubType();
    if (sPresetSubType.getLength() > 0)
    {
        nPresetSubType
            = AnimationExporter::TranslatePresetSubType(nPresetClass, nPresetId, sPresetSubType);
        bPresetSubType = true;
    }

    if (nType != EffectNodeType::TIMING_ROOT && nType != EffectNodeType::MAIN_SEQUENCE)
    {
        // it doesn't seem to work right on root and mainseq nodes
        sal_Int16 nFill = AnimationExporter::GetFillMode(rXNode, AnimationFill::AUTO);
        pFill = convertAnimationFill(nFill);
    }

    bool bAutoReverse = rXNode->getAutoReverse();

    aAny = rXNode->getRepeatCount();
    if (aAny.hasValue())
    {
        Timing eTiming;

        if (aAny >>= eTiming)
        {
            if (eTiming == Timing_INDEFINITE)
                sRepeatCount = "indefinite";
        }
        else
            aAny >>= fRepeatCount;
    }

    if (fRepeatCount != 0)
        sRepeatCount = OString::number(static_cast<sal_Int32>(fRepeatCount * 1000.0));

    // zeta: 부드럽게 시작/종료(PowerPoint 효과 옵션) — ST_PositiveFixedPercentage(100000 = 100%)
    std::optional<OString> sAccel;
    std::optional<OString> sDecel;
    if (nType != EffectNodeType::TIMING_ROOT && nType != EffectNodeType::MAIN_SEQUENCE)
    {
        const double fAccel = rXNode->getAcceleration();
        const double fDecel = rXNode->getDecelerate();
        if (fAccel > 0.0)
            sAccel = OString::number(static_cast<sal_Int32>((fAccel > 1.0 ? 1.0 : fAccel) * 100000.0 + 0.5));
        if (fDecel > 0.0)
            sDecel = OString::number(static_cast<sal_Int32>((fDecel > 1.0 ? 1.0 : fDecel) * 100000.0 + 0.5));
    }

    // After effect: hidden display, master relation (an END_EVENT of the master = same click,
    // otherwise the next click) — the way PowerPoint writes 'After animation'.
    const bool bAfterEffect = mpContext->isAfterEffect();
    bool bSameClick = false;
    if (bAfterEffect)
    {
        Event aEvent;
        bSameClick = (rXNode->getBegin() >>= aEvent) && aEvent.Trigger == EventTrigger::END_EVENT;
    }
    const char* pMasterRel = bAfterEffect ? (bSameClick ? "sameClick" : "nextClick") : nullptr;

    std::optional<OString> sGroupId;
    if (auto it = maGroupIds.find(rXNode); it != maGroupIds.end())
        sGroupId = OString::number(it->second);

    mpFS->startElementNS(XML_p, XML_cTn, XML_id, OString::number(GetNextAnimationNodeId(rXNode)),
                         XML_dur, sDuration, XML_restart, pRestart, XML_presetID,
                         sax_fastparser::UseIf(OString::number(nPresetId), bPresetId),
                         XML_presetClass, pPresetClass, XML_grpId, sGroupId, XML_fill, pFill,
                         XML_nodeType, pNodeType,
                         XML_autoRev, sax_fastparser::UseIf("1", bAutoReverse), XML_presetSubtype,
                         sax_fastparser::UseIf(OString::number(nPresetSubType), bPresetSubType),
                         XML_repeatCount, sRepeatCount, XML_accel, sAccel, XML_decel, sDecel,
                         XML_display, sax_fastparser::UseIf("0", bAfterEffect), XML_masterRel,
                         pMasterRel, XML_afterEffect, sax_fastparser::UseIf("1", bAfterEffect));

    if (!bAfterEffect || bSameClick)
        WriteAnimationCondList(mpContext->getBeginCondList(), XML_stCondLst);
    WriteAnimationCondList(mpContext->getEndCondList(), XML_endCondLst);

    if (rXNode->getType() == AnimationNodeType::ITERATE)
    {
        Reference<XIterateContainer> xIterate(rXNode, UNO_QUERY);
        if (xIterate.is())
        {
            const char* sType = convertTextAnimationType(xIterate->getIterateType());

            const double fInterval = xIterate->getIterateInterval();
            // PowerPoint stores the delay as a percentage of the duration of one
            // iteration (p:tmPct, 10% by default); fall back to p:tmAbs without a duration.
            double fDuration = 0.0;
            Reference<XEnumerationAccess> xAccess(xIterate, UNO_QUERY);
            if (xAccess.is())
            {
                Reference<XEnumeration> xEnum(xAccess->createEnumeration());
                while (xEnum.is() && xEnum->hasMoreElements())
                {
                    Reference<XAnimationNode> xChild(xEnum->nextElement(), UNO_QUERY);
                    double fChild = 0.0;
                    if (xChild.is() && (xChild->getDuration() >>= fChild) && fChild > fDuration)
                        fDuration = fChild;
                }
            }

            mpFS->startElementNS(XML_p, XML_iterate, XML_type, sType);
            if (fDuration > 0.0)
            {
                // ST_PositivePercentage ([ISO/IEC 29500-1] 20.1.10.46)
                const sal_Int32 nPercent
                    = static_cast<sal_Int32>(std::round(fInterval / fDuration * 100000.0));
                mpFS->singleElementNS(XML_p, XML_tmPct, XML_val,
                                      OString::number(std::max<sal_Int32>(0, nPercent)));
            }
            else
            {
                // ST_TLTime should be unsigned int ([ISO/IEC 29500-1] 19.7.38)
                sal_uInt32 nTime = static_cast<sal_uInt32>(fInterval * 1000);
                mpFS->singleElementNS(XML_p, XML_tmAbs, XML_val, OString::number(nTime));
            }
            mpFS->endElementNS(XML_p, XML_iterate);
        }
    }

    const std::vector<NodeContextPtr>& aChildNodes = mpContext->getChildNodes();
    const auto aAfterIt = maAfterEffects.find(rXNode);
    const bool bHasAfterEffects = aAfterIt != maAfterEffects.end() && !aAfterIt->second.empty();
    if (!aChildNodes.empty())
    {
        bool bSubTnLst = bHasAfterEffects;
        mpFS->startElementNS(XML_p, XML_childTnLst);
        for (const NodeContextPtr& pChildContext : aChildNodes)
        {
            if (pChildContext->isValid() && !pChildContext->isAfterEffect())
            {
                if (pChildContext->isOnSubTnLst())
                    bSubTnLst = true;
                else
                    WriteAnimationNode(pChildContext);
            }
        }
        mpFS->endElementNS(XML_p, XML_childTnLst);

        if (bSubTnLst)
        {
            mpFS->startElementNS(XML_p, XML_subTnLst);
            for (const NodeContextPtr& pChildContext : aChildNodes)
            {
                if (pChildContext->isValid() && pChildContext->isOnSubTnLst()
                    && !pChildContext->isAfterEffect())
                    WriteAnimationNode(pChildContext);
            }
            if (bHasAfterEffects)
            {
                // copy: writing may not touch the map, but keep the iteration safe
                const std::vector<const NodeContext*> aAfter(aAfterIt->second);
                for (const NodeContext* pAfter : aAfter)
                    WriteAnimationNode(pAfter);
            }
            mpFS->endElementNS(XML_p, XML_subTnLst);
        }
    }
    mpFS->endElementNS(XML_p, XML_cTn);
}

void PPTXAnimationExport::WriteAnimationNodeSeq()
{
    SAL_INFO("sd.eppt", "write animation node SEQ");

    mpFS->startElementNS(XML_p, XML_seq);

    WriteAnimationNodeCommonPropsStart();

    WriteAnimationCondListForSeq(mpFS, XML_prevCondLst);
    WriteAnimationCondListForSeq(mpFS, XML_nextCondLst);

    mpFS->endElementNS(XML_p, XML_seq);
}

void PPTXAnimationExport::WriteAnimationNodeEffect()
{
    SAL_INFO("sd.eppt", "write animation node FILTER");
    Reference<XTransitionFilter> xFilter(getCurrentNode(), UNO_QUERY);
    if (xFilter.is())
    {
        const char* pFilter = ::ppt::AnimationExporter::FindTransitionName(
            xFilter->getTransition(), xFilter->getSubtype(), xFilter->getDirection());
        const char* pMode = xFilter->getMode() ? "in" : "out";
        mpFS->startElementNS(XML_p, XML_animEffect, XML_transition, pMode, XML_filter, pFilter);

        WriteAnimationNodeAnimateInside(false);

        mpFS->endElementNS(XML_p, XML_animEffect);
    }
}

void PPTXAnimationExport::WriteAnimationNodeCommand()
{
    SAL_INFO("sd.eppt", "write animation node COMMAND");
    Reference<XCommand> xCommand(getCurrentNode(), UNO_QUERY);
    if (!xCommand.is())
        return;

    const char* pType = "call";
    OString aCommand;
    switch (xCommand->getCommand())
    {
        case EffectCommands::VERB:
            pType = "verb";
            aCommand = "1"_ostr; /* FIXME hardcoded viewing */
            break;
        case EffectCommands::PLAY:
        {
            aCommand = "play"_ostr;
            uno::Sequence<beans::NamedValue> aParamSeq;
            xCommand->getParameter() >>= aParamSeq;
            comphelper::SequenceAsHashMap aMap(aParamSeq);
            auto it = aMap.find(u"MediaTime"_ustr);
            if (it != aMap.end())
            {
                double fMediaTime = 0;
                it->second >>= fMediaTime;
                // PowerPoint represents 0 as 0.0, so just use a single decimal.
                OString aMediaTime
                    = rtl::math::doubleToString(fMediaTime, rtl_math_StringFormat_F, 1, '.');
                aCommand += "From(" + aMediaTime + ")";
            }
            break;
        }
        case EffectCommands::TOGGLEPAUSE:
            aCommand = "togglePause"_ostr;
            break;
        case EffectCommands::STOP:
            aCommand = "stop"_ostr;
            break;
        default:
            SAL_WARN("sd.eppt", "unknown command: " << xCommand->getCommand());
            break;
    }

    mpFS->startElementNS(XML_p, XML_cmd, XML_type, pType, XML_cmd, aCommand.getStr());

    WriteAnimationNodeAnimateInside(false);
    mpFS->startElementNS(XML_p, XML_cBhvr);
    WriteAnimationNodeCommonPropsStart();
    WriteAnimationTarget(xCommand->getTarget());
    mpFS->endElementNS(XML_p, XML_cBhvr);

    mpFS->endElementNS(XML_p, XML_cmd);
}

void PPTXAnimationExport::WriteAnimationNodeMedia(const sal_Int16 nParentNodeType)
{
    SAL_INFO("sd.eppt", "write animation node media");
    Reference<XAudio> xAudio(getCurrentNode(), UNO_QUERY);

    OUString sUrl;
    uno::Reference<drawing::XShape> xShape;
    OUString sRelId;
    OUString sName;

    if (!xAudio.is())
    {
        return;
    }

    bool bValid = false;
    sUrl = xmloff::getSoundURL(xAudio->getSource());
    if (!sUrl.isEmpty() && IsAudioURL(sUrl))
    {
        bValid = true;
    }

    bool bVideo = false;
    if (!bValid)
    {
        if (xAudio->getSource() >>= xShape)
        {
            uno::Reference<beans::XPropertySet> xShapeProps(xShape, uno::UNO_QUERY);
            bool bHasMediaURL
                = xShapeProps->getPropertySetInfo()->hasPropertyByName(u"MediaURL"_ustr);
            if (bHasMediaURL && (xShapeProps->getPropertyValue(u"MediaURL"_ustr) >>= sUrl))
            {
                bVideo = IsVideoURL(sUrl);
                bValid = IsAudioURL(sUrl) || bVideo;
            }
        }
    }

    if (!bValid)
        return;

    if (!xShape.is() && xmloff::getSoundAllowed(xAudio->getSource()))
    {
        mrPowerPointExport.embedEffectAudio(mpFS, sUrl, sRelId, sName);
    }

    if (bVideo)
    {
        mpFS->startElementNS(XML_p, XML_video);
        mpFS->startElementNS(XML_p, XML_cMediaNode);
    }
    else
    {
        // Don't export audio node if the context doesn't have any triggers
        if (!convertEffectNodeType(nParentNodeType))
            return;

        // Don't export audio node if embedding audio was unsuccessful
        if (!xShape.is() && (sRelId.isEmpty() || sUrl.isEmpty()))
            return;

        bool bNarration = xAudio->getNarration();
        mpFS->startElementNS(XML_p, XML_audio, XML_isNarration, bNarration ? "1" : "0");
        bool bHideDuringShow = xAudio->getHideDuringShow();
        mpFS->startElementNS(XML_p, XML_cMediaNode, XML_showWhenStopped,
                             bHideDuringShow ? "0" : "1");
    }

    animations::Timing eTiming{};
    bool bLooping
        = (xAudio->getRepeatCount() >>= eTiming) && eTiming == animations::Timing_INDEFINITE;
    if (bVideo && bLooping)
    {
        mpFS->startElementNS(XML_p, XML_cTn, XML_repeatCount, "indefinite");
    }
    else
    {
        mpFS->startElementNS(XML_p, XML_cTn);
    }
    WriteAnimationCondList(mpContext->getBeginCondList(), XML_stCondLst);
    WriteAnimationCondList(mpContext->getEndCondList(), XML_endCondLst);
    mpFS->endElementNS(XML_p, XML_cTn);

    mpFS->startElementNS(XML_p, XML_tgtEl);
    if (xShape.is())
    {
        sal_Int32 nShapeID = mrPowerPointExport.GetShapeID(xShape);
        mpFS->singleElementNS(XML_p, XML_spTgt, XML_spid, OString::number(nShapeID));
    }
    else
    {
        mpFS->singleElementNS(XML_p, XML_sndTgt, FSNS(XML_r, XML_embed),
                              sax_fastparser::UseIf(sRelId, !sRelId.isEmpty()), XML_name,
                              sax_fastparser::UseIf(sName, !sUrl.isEmpty()));
    }
    mpFS->endElementNS(XML_p, XML_tgtEl);

    mpFS->endElementNS(XML_p, XML_cMediaNode);
    if (bVideo)
    {
        mpFS->endElementNS(XML_p, XML_video);
    }
    else
    {
        mpFS->endElementNS(XML_p, XML_audio);
    }
}

void PPTXAnimationExport::CollectAfterEffects(const NodeContext* pContext)
{
    for (const NodeContextPtr& pChild : pContext->getChildNodes())
    {
        if (pChild->isAfterEffect())
        {
            if (pChild->isValid())
                maAfterEffects[pChild->getAfterEffectMaster()].push_back(pChild.get());
        }
        else
            CollectAfterEffects(pChild.get());
    }
}

void PPTXAnimationExport::CollectBuildEffects(const NodeContext* pContext,
                                              const Reference<XAnimationNode>& xSequence,
                                              std::vector<BuildEffect>& rEffects)
{
    for (const NodeContextPtr& pChild : pContext->getChildNodes())
    {
        if (pChild->isAfterEffect() || !pChild->isValid())
            continue;
        const sal_Int16 nType = pChild->getEffectNodeType();
        if (nType == EffectNodeType::MAIN_SEQUENCE || nType == EffectNodeType::INTERACTIVE_SEQUENCE)
        {
            CollectBuildEffects(pChild.get(), pChild->getNode(), rEffects);
            continue;
        }
        if (nType != EffectNodeType::ON_CLICK && nType != EffectNodeType::WITH_PREVIOUS
            && nType != EffectNodeType::AFTER_PREVIOUS)
        {
            CollectBuildEffects(pChild.get(), xSequence, rEffects);
            continue;
        }
        // an effect node: its target decides the shape (and paragraph) of the build
        const Any aTarget = lcl_findEffectTarget(pChild->getNode());
        BuildEffect aEffect;
        aEffect.xNode = pChild->getNode();
        aEffect.xSequence = xSequence;
        aEffect.nNodeType = nType;
        aEffect.nPresetClass = pChild->getEffectPresetClass();
        aEffect.aPresetId = pChild->getEffectPresetId();
        ParagraphTarget aParagraph;
        if (aTarget >>= aParagraph)
        {
            aEffect.xShape = aParagraph.Shape;
            aEffect.nParagraph = aParagraph.Paragraph;
        }
        else
            aTarget >>= aEffect.xShape;
        if (!aEffect.xShape.is())
            continue;
        aEffect.nShapeID = mrPowerPointExport.GetShapeID(aEffect.xShape);
        if (aEffect.nShapeID == -1)
            continue;
        rEffects.push_back(aEffect);
    }
}

std::vector<PPTXAnimationExport::BuildGroup>
PPTXAnimationExport::CreateBuildGroups(const std::vector<BuildEffect>& rEffects)
{
    // Paragraph effects that follow each other in one sequence with the same shape and preset
    // are one text build (the way the effect options group text); every other effect is its own.
    std::vector<BuildGroup> aGroups;
    std::map<sal_Int32, sal_Int32> aNextGroupId;
    for (size_t i = 0; i < rEffects.size(); ++i)
    {
        const BuildEffect& rEffect = rEffects[i];
        bool bJoin = false;
        if (rEffect.nParagraph >= 0 && i > 0 && !aGroups.empty())
        {
            const BuildEffect& rPrev = rEffects[i - 1];
            bJoin = aGroups.back().bParagraphs && rPrev.xSequence == rEffect.xSequence
                    && rPrev.nShapeID == rEffect.nShapeID && rPrev.nParagraph >= 0
                    && rPrev.aPresetId == rEffect.aPresetId
                    && rPrev.nPresetClass == rEffect.nPresetClass;
        }
        if (!bJoin)
        {
            BuildGroup aGroup;
            aGroup.nShapeID = rEffect.nShapeID;
            aGroup.nGroupId = aNextGroupId[rEffect.nShapeID]++;
            aGroup.xShape = rEffect.xShape;
            aGroup.bParagraphs = rEffect.nParagraph >= 0;
            aGroups.push_back(aGroup);
        }
        aGroups.back().aItems.push_back(i);
        maGroupIds[rEffect.xNode] = aGroups.back().nGroupId;
    }
    return aGroups;
}

void PPTXAnimationExport::WriteBuildList(const std::vector<BuildEffect>& rEffects,
                                         const std::vector<BuildGroup>& rGroups)
{
    bool bStarted = false;
    for (const BuildGroup& rGroup : rGroups)
    {
        Reference<XServiceInfo> xInfo(rGroup.xShape, UNO_QUERY);
        Reference<XSimpleText> xText(rGroup.xShape, UNO_QUERY);
        if (!xInfo.is() || !xText.is()
            || xInfo->supportsService(u"com.sun.star.drawing.GraphicObjectShape"_ustr)
            || xInfo->supportsService(u"com.sun.star.drawing.OLE2Shape"_ustr)
            || xInfo->supportsService(u"com.sun.star.drawing.TableShape"_ustr)
            || xInfo->supportsService(u"com.sun.star.drawing.GroupShape"_ustr)
            || xInfo->supportsService(u"com.sun.star.drawing.MediaShape"_ustr))
            continue;

        const char* pBuild = nullptr;
        std::optional<OString> sLevel;
        bool bReverse = false;
        bool bAnimBg = false;
        if (rGroup.bParagraphs)
        {
            // Same reading as the effect options (text animation tab): 'All paragraphs at once'
            // when every paragraph starts with the first one, otherwise 'By nth level paragraphs'
            // where n is the deepest paragraph that starts its own step.
            const std::vector<sal_Int16> aDepths = lcl_paragraphDepths(rGroup.xShape);
            auto depthOf = [&aDepths](sal_Int32 nPara) -> sal_Int16 {
                return (nPara >= 0 && static_cast<size_t>(nPara) < aDepths.size()) ? aDepths[nPara] : 0;
            };
            std::vector<sal_Int32> aOrder;
            std::vector<sal_Int32> aHeads;
            for (size_t k = 0; k < rGroup.aItems.size(); ++k)
            {
                const BuildEffect& rEffect = rEffects[rGroup.aItems[k]];
                aOrder.push_back(rEffect.nParagraph);
                if (k == 0 || rEffect.nNodeType != EffectNodeType::WITH_PREVIOUS)
                    aHeads.push_back(rEffect.nParagraph);
            }
            auto descending = [](const std::vector<sal_Int32>& rList) {
                for (size_t k = 1; k < rList.size(); ++k)
                    if (rList[k] >= rList[k - 1])
                        return false;
                return rList.size() > 1;
            };
            bReverse = aHeads.size() > 1 ? descending(aHeads) : descending(aOrder);
            bool bAllAtOnce = false;
            if (aOrder.size() > 1 && aHeads.size() == 1)
            {
                sal_Int16 nFirst = SAL_MAX_INT16;
                for (sal_Int32 nPara : aOrder)
                    nFirst = std::min(nFirst, depthOf(nPara));
                bAllAtOnce = std::count_if(aOrder.begin(), aOrder.end(),
                                           [&](sal_Int32 nPara) { return depthOf(nPara) == nFirst; })
                             > 1;
            }
            if (bAllAtOnce)
                pBuild = "allAtOnce";
            else
            {
                sal_Int32 nLevel = 1;
                for (sal_Int32 nPara : aHeads)
                    nLevel = std::max<sal_Int32>(nLevel, depthOf(nPara) + 1);
                nLevel = std::min<sal_Int32>(nLevel, 5);
                pBuild = "p";
                if (nLevel > 1)
                    sLevel = OString::number(nLevel);
            }
        }
        else if (xText->getString().isEmpty())
            bAnimBg = true;

        if (!bStarted)
        {
            mpFS->startElementNS(XML_p, XML_bldLst);
            bStarted = true;
        }
        mpFS->singleElementNS(XML_p, XML_bldP, XML_spid, OString::number(rGroup.nShapeID),
                              XML_grpId, OString::number(rGroup.nGroupId), XML_build, pBuild,
                              XML_bldLvl, sLevel, XML_animBg, sax_fastparser::UseIf("1", bAnimBg),
                              XML_rev, sax_fastparser::UseIf("1", bReverse));
    }
    if (bStarted)
        mpFS->endElementNS(XML_p, XML_bldLst);
}

void PPTXAnimationExport::WriteAnimationNode(const NodeContext* pContext)
{
    const NodeContext* pSavedContext = mpContext;
    mpContext = pContext;

    const Reference<XAnimationNode>& rXNode = getCurrentNode();

    SAL_INFO("sd.eppt", "export node type: " << rXNode->getType());
    sal_Int32 xmlNodeType = extractNodeType(rXNode);

    switch (xmlNodeType)
    {
        case XML_par:
            mpFS->startElementNS(XML_p, xmlNodeType);
            WriteAnimationNodeCommonPropsStart();
            mpFS->endElementNS(XML_p, xmlNodeType);
            break;
        case XML_seq:
            WriteAnimationNodeSeq();
            break;
        case XML_animScale:
        case XML_animRot:
        case XML_anim:
        case XML_animMotion:
        case XML_animClr:
        case XML_set:
            WriteAnimationNodeAnimate(xmlNodeType);
            break;
        case XML_animEffect:
            WriteAnimationNodeEffect();
            break;
        case XML_cmd:
            WriteAnimationNodeCommand();
            break;
        case XML_audio:
            WriteAnimationNodeMedia(pSavedContext->getEffectNodeType());
            break;
        default:
            SAL_WARN("sd.eppt", "export ooxml node type: " << xmlNodeType);
            break;
    }

    mpContext = pSavedContext;
}

void PPTXAnimationExport::WriteAnimations(const Reference<XDrawPage>& rXDrawPage)
{
    Reference<XAnimationNodeSupplier> xNodeSupplier(rXDrawPage, UNO_QUERY);
    if (!xNodeSupplier.is())
        return;

    const Reference<XAnimationNode> xNode(xNodeSupplier->getAnimationNode());
    if (!xNode.is())
        return;

    Reference<XEnumerationAccess> xEnumerationAccess(xNode, UNO_QUERY);
    if (!xEnumerationAccess.is())
        return;

    Reference<XEnumeration> xEnumeration = xEnumerationAccess->createEnumeration();
    if (!(xEnumeration.is() && xEnumeration->hasMoreElements()))
        return;

    Reference<XShapes> xShapes = rXDrawPage;
    sal_uInt32 nShapes = xShapes->getCount();
    std::unordered_set<sal_Int32> aSlideShapeIDs;
    if (xShapes.is())
    {
        for (sal_uInt32 i = 0; i < nShapes; i++)
        {
            Reference<XShape> xShape;
            xShapes->getByIndex(i) >>= xShape;
            sal_Int32 nId = mrPowerPointExport.GetShapeID(xShape);
            if (nId != -1)
                aSlideShapeIDs.insert(nId);
        }
    }

    auto pNodeContext
        = std::make_unique<NodeContext>(xNode, aSlideShapeIDs, mrPowerPointExport, false, false);
    maAfterEffects.clear();
    CollectAfterEffects(pNodeContext.get());
    if (pNodeContext->isValid())
    {
        // PowerPoint reads the text build ('By 1st level paragraphs' etc.) from p:bldLst
        std::vector<BuildEffect> aBuildEffects;
        maGroupIds.clear();
        CollectBuildEffects(pNodeContext.get(), Reference<XAnimationNode>(), aBuildEffects);
        const std::vector<BuildGroup> aBuildGroups = CreateBuildGroups(aBuildEffects);

        mpFS->startElementNS(XML_p, XML_timing);
        mpFS->startElementNS(XML_p, XML_tnLst);

        WriteAnimationNode(pNodeContext);

        mpFS->endElementNS(XML_p, XML_tnLst);
        WriteBuildList(aBuildEffects, aBuildGroups);
        mpFS->endElementNS(XML_p, XML_timing);
    }
}

sal_Int32 PPTXAnimationExport::GetNextAnimationNodeId(const Reference<XAnimationNode>& xNode)
{
    sal_Int32 nId = mrPowerPointExport.GetNextAnimationNodeID();
    maAnimationNodeIdMap[xNode] = nId;
    return nId;
}

sal_Int32 PPTXAnimationExport::GetAnimationNodeId(const Reference<XAnimationNode>& xNode)
{
    sal_Int32 nId = -1;
    const auto aIter = maAnimationNodeIdMap.find(xNode);
    if (aIter != maAnimationNodeIdMap.end())
    {
        nId = aIter->second;
    }
    return nId;
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
