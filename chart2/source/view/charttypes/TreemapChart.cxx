/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "TreemapChart.hxx"

#include <ChartType.hxx>
#include <ExplicitCategoriesProvider.hxx>
#include <ObjectIdentifier.hxx>
#include <PropertyMapper.hxx>
#include <ShapeFactory.hxx>
#include <VDataSeries.hxx>

#include <com/sun/star/chart2/DataPointLabel.hpp>
#include <com/sun/star/drawing/FillStyle.hpp>
#include <com/sun/star/drawing/LineStyle.hpp>
#include <com/sun/star/drawing/TextHorizontalAdjust.hpp>
#include <com/sun/star/drawing/TextVerticalAdjust.hpp>
#include <com/sun/star/style/ParagraphAdjust.hpp>
#include <com/sun/star/awt/FontWeight.hpp>
#include <comphelper/diagnose_ex.hxx>
#include <svx/unoshape.hxx>

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <map>
#include <numeric>

namespace chart
{
using namespace ::com::sun::star;

namespace
{
// Office 2013-2022 theme accents 1-6: Excel colours treemap tiles by the
// outermost category
constexpr sal_Int32 aAccents[] = { 0x4472C4, 0xED7D31, 0xA5A5A5, 0xFFC000, 0x5B9BD5, 0x70AD47 };

// Gap between tiles (1/100 mm), drawn as a white border
constexpr sal_Int32 nTileBorder = 35;
// Height of a parent banner (parentLabelLayout="banner")
constexpr double fBannerHeight = 450.0;
// Inner padding of a tile label
constexpr sal_Int32 nLabelPadding = 80;
// one line of the 9pt overlapping parent label (1/100 mm)
constexpr double fParentLabelHeight = 9.0 * 2540.0 / 72.0 * 1.25;

// the worst aspect ratio of a row of tiles laid along a side of length fSide
double worstRatio(double fSum, double fMin, double fMax, double fSide)
{
    if (fSum <= 0.0 || fMin <= 0.0)
        return std::numeric_limits<double>::infinity();
    const double s2 = fSum * fSum;
    const double w2 = fSide * fSide;
    return std::max(w2 * fMax / s2, s2 / (w2 * fMin));
}
}

TreemapChart::TreemapChart(const rtl::Reference<ChartType>& xChartTypeModel)
    : VSeriesPlotter(xChartTypeModel, 2)
{
    PlotterBase::m_pPosHelper = &m_aMainPosHelper;
    VSeriesPlotter::m_pMainPosHelper = &m_aMainPosHelper;
}

TreemapChart::~TreemapChart() {}

bool TreemapChart::isDarkColor(sal_Int32 nColor)
{
    const double r = ((nColor >> 16) & 0xff) / 255.0;
    const double g = ((nColor >> 8) & 0xff) / 255.0;
    const double b = (nColor & 0xff) / 255.0;
    // Excel writes white on the grey accent (A5A5A5)
    return 0.299 * r + 0.587 * g + 0.114 * b < 0.7;
}

sal_Int32 TreemapChart::nodeColor(const Node& rNode)
{
    return aAccents[std::max<sal_Int32>(0, rNode.nTop) % std::size(aAccents)];
}

VDataSeries* TreemapChart::firstSeries() const
{
    if (m_aZSlots.empty())
        return nullptr;
    for (auto const& rXSlot : m_aZSlots.front())
        for (auto const& p : rXSlot.m_aSeriesVector)
            if (p)
                return p.get();
    return nullptr;
}

std::vector<TreemapChart::Node> TreemapChart::buildTree(const VDataSeries& rSeries)
{
    const sal_Int32 nPoints = rSeries.getTotalPointCount();
    const std::vector<std::vector<std::pair<sal_Int32, OUString>>> aPaths = categoryPaths(nPoints);

    std::vector<Node> aNodes(1); // the root
    std::map<std::pair<size_t, sal_Int32>, size_t> aChildOf; // (parent node, group) -> node
    for (sal_Int32 i = 0; i < nPoints; ++i)
    {
        const double fValue = rSeries.getYValue(i);
        if (std::isnan(fValue) || fValue <= 0.0)
            continue;
        const std::vector<std::pair<sal_Int32, OUString>>& aPath = aPaths[i];
        size_t nNode = 0;
        for (size_t nDepth = 0; nDepth < aPath.size(); ++nDepth)
        {
            // the leaf level is per point; groups above are shared
            const sal_Int32 nGroup = nDepth == 0 ? topKey(aPath, i) : nDepth + 1 == aPath.size() ? -1 - i : aPath[nDepth].first;
            auto aFound = aChildOf.find({ nNode, nGroup });
            size_t nChild;
            if (aFound == aChildOf.end())
            {
                nChild = aNodes.size();
                aNodes.emplace_back();
                aNodes[nChild].aLabel = aPath[nDepth].second;
                aNodes[nChild].nTop = nNode == 0 ? static_cast<sal_Int32>(aNodes[0].aChildren.size()) : aNodes[nNode].nTop;
                aNodes[nNode].aChildren.push_back(nChild);
                aChildOf[{ nNode, nGroup }] = nChild;
            }
            else
                nChild = aFound->second;
            aNodes[nNode].fValue += fValue;
            nNode = nChild;
        }
        aNodes[nNode].fValue += fValue;
        aNodes[nNode].nPoint = i;
    }
    return aNodes;
}

std::vector<std::vector<std::pair<sal_Int32, OUString>>> TreemapChart::categoryPaths(sal_Int32 nPoints)
{
    std::vector<std::vector<std::pair<sal_Int32, OUString>>> aPaths(nPoints); // (group, text)
    if (m_pExplicitCategoriesProvider)
    {
        // getSimpleCategories() builds the complex categories that getCategoriesByLevel() returns
        const uno::Sequence<OUString>& rSimple = m_pExplicitCategoriesProvider->getSimpleCategories();
        const sal_Int32 nLevels = m_pExplicitCategoriesProvider->getCategoryLevelCount();
        for (sal_Int32 nLevel = nLevels - 1; nLevel >= 0; --nLevel)
        {
            const std::vector<ComplexCategory>* pCategories = m_pExplicitCategoriesProvider->getCategoriesByLevel(nLevel);
            if (!pCategories)
                continue;
            sal_Int32 nPos = 0;
            sal_Int32 nGroup = 0;
            for (const ComplexCategory& rCategory : *pCategories)
            {
                // the list starts with an empty entry of count 0; it covers no point
                for (sal_Int32 k = 0; k < rCategory.Count; ++k, ++nPos)
                    if (nPos < nPoints)
                        aPaths[nPos].emplace_back(nGroup, rCategory.Text);
                ++nGroup;
            }
        }
        if (std::all_of(aPaths.begin(), aPaths.end(), [](const auto& r) { return r.empty(); }))
        {
            for (sal_Int32 i = 0; i < nPoints && i < rSimple.getLength(); ++i)
                aPaths[i].emplace_back(i, rSimple[i]);
        }
    }
    for (sal_Int32 i = 0; i < nPoints; ++i)
    {
        std::vector<std::pair<sal_Int32, OUString>>& rPath = aPaths[i];
        if (rPath.empty())
            rPath.emplace_back(i, OUString());
        // a blank inner level (an item without sub items) ends the path early
        while (rPath.size() > 1 && rPath.back().second.isEmpty())
            rPath.pop_back();
    }
    // Excel also groups a repeated parent text (East, East, West...), not only
    // a text followed by blank cells: renumber the parent levels so that
    // neighbouring points with the same text under the same parent share a group
    sal_Int32 nNextGroup = 0;
    for (sal_Int32 i = 0; i < nPoints; ++i)
    {
        std::vector<std::pair<sal_Int32, OUString>>& rPath = aPaths[i];
        for (size_t d = 0; d + 1 < rPath.size(); ++d)
        {
            bool bSame = false;
            if (i > 0)
            {
                const std::vector<std::pair<sal_Int32, OUString>>& rPrev = aPaths[i - 1];
                bSame = d + 1 < rPrev.size() && rPrev[d].second == rPath[d].second
                        && (d == 0 || rPrev[d - 1].first == rPath[d - 1].first);
            }
            rPath[d].first = bSame ? aPaths[i - 1][d].first : nNextGroup++;
        }
    }
    return aPaths;
}

sal_Int32 TreemapChart::topKey(const std::vector<std::pair<sal_Int32, OUString>>& rPath, sal_Int32 nPoint)
{
    // a one level path is a leaf: every point is its own top level node
    return rPath.size() == 1 ? -1 - nPoint : rPath[0].first;
}

std::vector<ViewLegendEntry> TreemapChart::createLegendEntriesForSeries(
    const awt::Size& rEntryKeyAspectRatio, const VDataSeries& rSeries,
    const uno::Reference<beans::XPropertySet>& xTextProperties,
    const rtl::Reference<SvxShapeGroupAnyD>& xTarget,
    const uno::Reference<uno::XComponentContext>& /*xContext*/)
{
    std::vector<ViewLegendEntry> aResult;
    if (!xTarget.is())
        return aResult;
    // the top level nodes in the order createShapes() makes them, which picks their colour
    const sal_Int32 nPoints = rSeries.getTotalPointCount();
    const std::vector<std::vector<std::pair<sal_Int32, OUString>>> aPaths = categoryPaths(nPoints);
    std::vector<sal_Int32> aSeen;
    for (sal_Int32 i = 0; i < nPoints; ++i)
    {
        const double fValue = rSeries.getYValue(i);
        if (std::isnan(fValue) || fValue <= 0.0)
            continue;
        const sal_Int32 nKey = topKey(aPaths[i], i);
        if (std::find(aSeen.begin(), aSeen.end(), nKey) != aSeen.end())
            continue;
        const sal_Int32 nColor = aAccents[aSeen.size() % std::size(aAccents)];
        aSeen.push_back(nKey);
        aResult.push_back(createBoxLegendEntry(rEntryKeyAspectRatio, aPaths[i][0].second, nColor, xTextProperties, xTarget));
    }
    return aResult;
}

void TreemapChart::addSeries(std::unique_ptr<VDataSeries> pSeries, sal_Int32 /*zSlot*/,
                             sal_Int32 /*xSlot*/, sal_Int32 ySlot)
{
    if (!pSeries)
        return;
    pSeries->setCategoryXAxis();
    VSeriesPlotter::addSeries(std::move(pSeries), 0, 0, ySlot);
}

drawing::Direction3D TreemapChart::getPreferredDiagramAspectRatio() const
{
    return drawing::Direction3D(-1, -1, -1);
}

std::vector<TreemapChart::Rect> TreemapChart::squarify(const std::vector<Item>& rItems,
                                                       const Rect& rArea)
{
    std::vector<Rect> aRects(rItems.size(), Rect{ rArea.fX, rArea.fY, 0.0, 0.0 });
    double fTotal = 0.0;
    for (const Item& rItem : rItems)
        fTotal += std::max(0.0, rItem.fValue);
    if (fTotal <= 0.0 || rArea.fW <= 0.0 || rArea.fH <= 0.0)
        return aRects;

    const double fScale = rArea.fW * rArea.fH / fTotal; // area per value unit
    Rect aFree = rArea;
    size_t i = 0;
    while (i < rItems.size())
    {
        const bool bVertical = aFree.fW >= aFree.fH; // lay the row along the shorter side
        const double fSide = bVertical ? aFree.fH : aFree.fW;
        size_t j = i;
        double fSum = 0.0, fMin = 0.0, fMax = 0.0;
        double fWorst = std::numeric_limits<double>::infinity();
        while (j < rItems.size())
        {
            const double fArea = std::max(0.0, rItems[j].fValue) * fScale;
            const double fNewSum = fSum + fArea;
            const double fNewMin = j == i ? fArea : std::min(fMin, fArea);
            const double fNewMax = j == i ? fArea : std::max(fMax, fArea);
            const double fNewWorst = worstRatio(fNewSum, fNewMin, fNewMax, fSide);
            if (j > i && fNewWorst > fWorst)
                break;
            fSum = fNewSum;
            fMin = fNewMin;
            fMax = fNewMax;
            fWorst = fNewWorst;
            ++j;
        }
        // the last row takes what is left, so rounding does not leave a gap
        const bool bLast = j >= rItems.size();
        const double fThick = bLast ? (bVertical ? aFree.fW : aFree.fH) : (fSide > 0.0 ? fSum / fSide : 0.0);
        double fPos = bVertical ? aFree.fY : aFree.fX;
        for (size_t k = i; k < j; ++k)
        {
            const double fArea = std::max(0.0, rItems[k].fValue) * fScale;
            double fLen = fThick > 0.0 ? fArea / fThick : 0.0;
            if (k + 1 == j) // the end of a row meets the end of the side
                fLen = (bVertical ? aFree.fY + aFree.fH : aFree.fX + aFree.fW) - fPos;
            aRects[k] = bVertical ? Rect{ aFree.fX, fPos, fThick, fLen } : Rect{ fPos, aFree.fY, fLen, fThick };
            fPos += fLen;
        }
        if (bVertical)
        {
            aFree.fX += fThick;
            aFree.fW -= fThick;
        }
        else
        {
            aFree.fY += fThick;
            aFree.fH -= fThick;
        }
        i = j;
    }
    return aRects;
}

void TreemapChart::createShapes()
{
    if (!(m_xLogicTarget.is() && m_xFinalTarget.is()))
        return;
    VDataSeries* pSeries = firstSeries();
    if (!pSeries)
        return;

    rtl::Reference<SvxShapeGroupAnyD> xSeriesTarget = createGroupShape(m_xLogicTarget);
    rtl::Reference<SvxShapeGroupAnyD> xTextTarget = ShapeFactory::createGroup2D(m_xFinalTarget);
    rtl::Reference<SvxShapeGroupAnyD> xSeriesGroupShape = getSeriesGroupShape(pSeries, xSeriesTarget);

    // plot area in page coordinates
    const basegfx::B2DRectangle aClip = m_aMainPosHelper.getScaledLogicClipDoubleRect();
    const drawing::Position3D aP1 = m_aMainPosHelper.transformScaledLogicToScene(aClip.getMinX(), aClip.getMinY(), 0.0, false);
    const drawing::Position3D aP2 = m_aMainPosHelper.transformScaledLogicToScene(aClip.getMaxX(), aClip.getMaxY(), 0.0, false);
    const Rect aPlot{ std::min(aP1.PositionX, aP2.PositionX), std::min(aP1.PositionY, aP2.PositionY),
                      std::fabs(aP2.PositionX - aP1.PositionX), std::fabs(aP2.PositionY - aP1.PositionY) };
    if (aPlot.fW <= 0.0 || aPlot.fH <= 0.0)
        return;

    // the hierarchy: one path of category texts per point, outermost level first
    const sal_Int32 nPoints = pSeries->getTotalPointCount();
    const std::vector<Node> aNodes = buildTree(*pSeries);
    if (aNodes[0].fValue <= 0.0)
        return;

    OUString aParentLayout(u"overlapping"_ustr);
    try
    {
        const uno::Reference<beans::XPropertySet>& xSeriesProperties = pSeries->getPropertiesOfSeries();
        if (xSeriesProperties.is())
            xSeriesProperties->getPropertyValue(u"ParentLabelLayout"_ustr) >>= aParentLayout;
    }
    catch (const uno::Exception&)
    {
    }
    bool bHasLabels = false;
    for (sal_Int32 i = 0; i < nPoints && !bHasLabels; ++i)
        bHasLabels = pSeries->getDataPointLabelIfLabel(i) != nullptr;
    const bool bBanner = bHasLabels && aParentLayout == "banner";
    const bool bOverlapping = bHasLabels && aParentLayout == "overlapping";

    auto aTileColor = [](const Node& rNode) { return nodeColor(rNode); };

    auto aCreateTile = [&](const Rect& r, sal_Int32 nColor, sal_Int32 nPoint) {
        std::vector<std::vector<drawing::Position3D>> aPoly{ {
            drawing::Position3D(r.fX, r.fY, 0.0), drawing::Position3D(r.fX + r.fW, r.fY, 0.0),
            drawing::Position3D(r.fX + r.fW, r.fY + r.fH, 0.0), drawing::Position3D(r.fX, r.fY + r.fH, 0.0) } };
        rtl::Reference<SvxShapePolyPolygon> xShape = ShapeFactory::createArea2D(xSeriesGroupShape, aPoly);
        if (!xShape.is())
            return;
        if (nPoint >= 0)
        {
            uno::Reference<beans::XPropertySet> xPointProperties(pSeries->getPropertiesOfPoint(nPoint));
            PropertyMapper::setMappedProperties(*xShape, xPointProperties, PropertyMapper::getPropertyNameMapForFilledSeriesProperties());
            ShapeFactory::setShapeName(xShape, ObjectIdentifier::createPointCID(pSeries->getPointCID_Stub(), nPoint));
        }
        if (nPoint < 0 || !pSeries->isAttributedDataPoint(nPoint))
        {
            xShape->SvxShape::setPropertyValue(u"FillStyle"_ustr, uno::Any(drawing::FillStyle_SOLID));
            xShape->SvxShape::setPropertyValue(u"FillColor"_ustr, uno::Any(nColor));
        }
        xShape->SvxShape::setPropertyValue(u"LineStyle"_ustr, uno::Any(drawing::LineStyle_SOLID));
        xShape->SvxShape::setPropertyValue(u"LineColor"_ustr, uno::Any(sal_Int32(0xFFFFFF)));
        xShape->SvxShape::setPropertyValue(u"LineWidth"_ustr, uno::Any(nTileBorder));
    };

    // Excel treemap style: every label is 9pt regular in lt1 (white), parents at the
    // top left, leaves at the bottom left
    auto aCreateLabel = [&](const Rect& r, const OUString& rText, float fHeight, bool bBold, bool bBottom) {
        if (rText.isEmpty() || r.fW < 2 * nLabelPadding + 200 || r.fH < 2 * nLabelPadding + 250)
            return;
        tPropertyNameValueMap aValues;
        aValues[u"CharHeight"_ustr] <<= fHeight;
        aValues[u"CharHeightAsian"_ustr] <<= fHeight;
        aValues[u"CharHeightComplex"_ustr] <<= fHeight;
        const float fWeight = bBold ? awt::FontWeight::BOLD : awt::FontWeight::NORMAL;
        aValues[u"CharWeight"_ustr] <<= fWeight;
        aValues[u"CharWeightAsian"_ustr] <<= fWeight;
        aValues[u"CharWeightComplex"_ustr] <<= fWeight;
        aValues[u"CharColor"_ustr] <<= sal_Int32(0xFFFFFF);
        aValues[u"FillStyle"_ustr] <<= drawing::FillStyle_NONE;
        aValues[u"LineStyle"_ustr] <<= drawing::LineStyle_NONE;
        aValues[u"ParaAdjust"_ustr] <<= style::ParagraphAdjust_LEFT;
        aValues[u"TextHorizontalAdjust"_ustr] <<= drawing::TextHorizontalAdjust_LEFT;
        aValues[u"TextVerticalAdjust"_ustr] <<= bBottom ? drawing::TextVerticalAdjust_BOTTOM : drawing::TextVerticalAdjust_TOP;
        aValues[u"TextAutoGrowHeight"_ustr] <<= false;
        aValues[u"TextAutoGrowWidth"_ustr] <<= false;
        aValues[u"TextWordWrap"_ustr] <<= true;
        aValues[u"TextLeftDistance"_ustr] <<= sal_Int32(0);
        aValues[u"TextRightDistance"_ustr] <<= sal_Int32(0);
        aValues[u"TextUpperDistance"_ustr] <<= sal_Int32(0);
        aValues[u"TextLowerDistance"_ustr] <<= sal_Int32(0);
        tNameSequence aNames;
        tAnySequence aAnys;
        PropertyMapper::getMultiPropertyListsFromValueMap(aNames, aAnys, aValues);
        rtl::Reference<SvxShapeText> xText = ShapeFactory::createText(xTextTarget, rText, aNames, aAnys, uno::Any());
        if (!xText.is())
            return;
        xText->setPosition(awt::Point(static_cast<sal_Int32>(r.fX + nLabelPadding), static_cast<sal_Int32>(r.fY + nLabelPadding)));
        xText->setSize(awt::Size(static_cast<sal_Int32>(r.fW - 2 * nLabelPadding), static_cast<sal_Int32>(r.fH - 2 * nLabelPadding)));
    };

    auto aLeafText = [&](const Node& rNode) -> OUString {
        const chart2::DataPointLabel* pLabel = pSeries->getDataPointLabelIfLabel(rNode.nPoint);
        if (!pLabel)
            return OUString();
        OUString aText;
        if (pLabel->ShowCategoryName)
            aText = rNode.aLabel;
        if (pLabel->ShowNumber)
        {
            OUString aNumber = getLabelTextForValue(*pSeries, rNode.nPoint, pSeries->getYValue(rNode.nPoint), false);
            aText = aText.isEmpty() ? aNumber : aText + "\n" + aNumber;
        }
        return aText;
    };

    // parent labels are drawn after all tiles so that they stay on top
    std::vector<std::pair<Rect, size_t>> aParentLabels;
    // fCornerSkip: height of the parent label covering the top left corner of rArea
    std::function<void(size_t, const Rect&, double)> aLayout = [&](size_t nNode, const Rect& rArea, double fCornerSkip) {
        std::vector<size_t> aChildren = aNodes[nNode].aChildren;
        std::stable_sort(aChildren.begin(), aChildren.end(),
                         [&](size_t a, size_t b) { return aNodes[a].fValue > aNodes[b].fValue; });
        std::vector<Item> aItems;
        for (size_t nChild : aChildren)
            aItems.push_back({ aNodes[nChild].fValue, static_cast<sal_Int32>(nChild) });
        const std::vector<Rect> aRects = squarify(aItems, rArea);
        for (size_t k = 0; k < aChildren.size(); ++k)
        {
            const Node& rChild = aNodes[aChildren[k]];
            Rect r = aRects[k];
            const bool bAtCorner = std::fabs(r.fX - rArea.fX) < 1.0 && std::fabs(r.fY - rArea.fY) < 1.0;
            const double fSkip = bAtCorner ? fCornerSkip : 0.0;
            if (rChild.aChildren.empty())
            {
                aCreateTile(r, aTileColor(rChild), rChild.nPoint);
                aCreateLabel(Rect{ r.fX, r.fY + fSkip, r.fW, r.fH - fSkip }, aLeafText(rChild), 9.0f, false, true);
                continue;
            }
            if (bBanner && r.fH > 2 * fBannerHeight)
            {
                const Rect aBanner{ r.fX, r.fY, r.fW, fBannerHeight };
                aCreateTile(aBanner, aTileColor(rChild), -1);
                aCreateLabel(Rect{ aBanner.fX, aBanner.fY - nLabelPadding / 2.0, aBanner.fW, aBanner.fH + nLabelPadding }, rChild.aLabel, 9.0f, false, false);
                r.fY += fBannerHeight;
                r.fH -= fBannerHeight;
            }
            else if (bOverlapping)
            {
                aParentLabels.emplace_back(Rect{ r.fX, r.fY + fSkip, r.fW, r.fH - fSkip }, aChildren[k]);
                aLayout(aChildren[k], r, fSkip + fParentLabelHeight);
                continue;
            }
            aLayout(aChildren[k], r, 0.0);
        }
    };
    aLayout(0, aPlot, 0.0);
    for (const auto& [r, nNode] : aParentLabels)
        aCreateLabel(r, aNodes[nNode].aLabel, 9.0f, false, false);
}

} // namespace chart

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
