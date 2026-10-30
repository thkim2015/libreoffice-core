/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#pragma once

#include <VSeriesPlotter.hxx>
#include "BarPositionHelper.hxx"

#include <memory>
#include <string_view>
#include <vector>

namespace chart
{
/** Region map (chartex filled map): the regions of the geography cache that
    Excel saves with the chart (Bing polygons) are projected into the plot
    area. Regions named by a category take a colour between the lowest and
    the highest value; the other regions are drawn grey.

    Excel looks the category names up online each time it opens the file; we
    match them against the English and the localised region names of the
    cache instead, so the map is drawn offline.
*/
class RegionMapChart : public VSeriesPlotter
{
public:
    RegionMapChart() = delete;
    explicit RegionMapChart(const rtl::Reference<ChartType>& xChartTypeModel);
    virtual ~RegionMapChart() override;

    virtual void createShapes() override;
    virtual void addSeries(std::unique_ptr<VDataSeries> pSeries, sal_Int32 zSlot, sal_Int32 xSlot,
                           sal_Int32 ySlot) override;

    virtual css::drawing::Direction3D getPreferredDiagramAspectRatio() const override;

    /// The highest and the lowest value with their colours, like Excel's colour scale
    virtual std::vector<ViewLegendEntry> createLegendEntriesForSeries(
        const css::awt::Size& rEntryKeyAspectRatio, const VDataSeries& rSeries,
        const css::uno::Reference<css::beans::XPropertySet>& xTextProperties,
        const rtl::Reference<SvxShapeGroupAnyD>& xTarget,
        const css::uno::Reference<css::uno::XComponentContext>& xContext) override;

    /// A region of the geography cache: names to match and rings in degrees
    struct Region
    {
        OUString aId;
        std::vector<OUString> aNames; ///< localised name, English name
        /// rings of (longitude, latitude); outer rings and holes alike
        std::vector<std::vector<std::pair<double, double>>> aRings;
    };
    using Regions = std::vector<Region>;

    /// Bing "point compression" text of one ring to (longitude, latitude) points
    static std::vector<std::pair<double, double>> decodeRing(std::u16string_view aText);
    /// pcaRings attribute: a count, then the encoded rings separated by commas
    static std::vector<std::vector<std::pair<double, double>>> decodeRings(std::u16string_view aText);
    /// The regions of a cx:clear document (the inflated cx:geoCache/cx:binary)
    static Regions parseClearXml(const char* pXml, size_t nLength);
    /// cx:binary: base64 of the raw deflated cx:clear document
    static Regions parseBinary(std::u16string_view aBase64);
    /// Lower case, letters and digits only, so "Korea, Rep." and "korea rep" compare equal
    static OUString normalizeName(std::u16string_view aName);
    /// Index of the region named rName in rRegions (or one of its usual aliases), or -1
    static sal_Int32 findRegion(const Regions& rRegions, const OUString& rName);
    /// Excel's colour for fT in [0, 1]: from #DAE3F3 (lowest) to #2F5597 (highest), blended
    /// in linear light
    static sal_Int32 scaleColor(double fT);

private:
    VDataSeries* firstSeries() const;
    /// The regions of the series geography cache (parsed once per cache text)
    std::shared_ptr<const Regions> regionsOf(const VDataSeries& rSeries);
    /// Per point, the region it names, or -1
    std::vector<sal_Int32> matchPoints(const VDataSeries& rSeries, const Regions& rRegions);

    BarPositionHelper m_aMainPosHelper;
};

} // namespace chart

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
