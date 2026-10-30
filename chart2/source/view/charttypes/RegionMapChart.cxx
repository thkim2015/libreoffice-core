/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "RegionMapChart.hxx"

#include <ChartType.hxx>
#include <ExplicitCategoriesProvider.hxx>
#include <ObjectIdentifier.hxx>
#include <PropertyMapper.hxx>
#include <ShapeFactory.hxx>
#include <VDataSeries.hxx>

#include <com/sun/star/drawing/FillStyle.hpp>
#include <com/sun/star/drawing/LineStyle.hpp>
#include <com/sun/star/drawing/TextHorizontalAdjust.hpp>
#include <com/sun/star/drawing/TextVerticalAdjust.hpp>
#include <com/sun/star/style/ParagraphAdjust.hpp>
#include <comphelper/base64.hxx>
#include <comphelper/diagnose_ex.hxx>
#include <rtl/character.hxx>
#include <svx/unoshape.hxx>

#include <libxml/xmlreader.h>
#include <zlib.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <mutex>

namespace chart
{
using namespace ::com::sun::star;

namespace
{
// Excel filled map defaults (2-colour sequential scale of the Office theme)
constexpr sal_Int32 nLowColor = 0xDAE3F3; // accent 1, lighter 80 %
constexpr sal_Int32 nHighColor = 0x2F5597; // accent 1, darker 25 %
constexpr sal_Int32 nNoDataColor = 0xE0E0E0;
constexpr sal_Int32 nAttributionColor = 0x595959;
// drawing tolerances in 1/100 mm: closer points are merged, smaller rings dropped
constexpr double fPointTolerance = 8.0;
constexpr double fRingMinSize = 6.0;
// southern edge of the map (degrees), measured on Excel's world map
constexpr double fSouthLimit = -75.0;

// ST_GeoProjectionType and ST_GeoMappingLevel as oox stores them
enum Projection
{
    PROJECTION_MERCATOR = 0,
    PROJECTION_MILLER = 1,
    PROJECTION_ROBINSON = 2,
    PROJECTION_ALBERS = 3
};
constexpr sal_Int32 nMappingDataOnly = 0;

// Usual spellings of names that Bing writes differently; each pair is
// (alias, name in the cache)
constexpr std::pair<std::u16string_view, std::u16string_view> aAliases[] = {
    { u"Korea, Republic of", u"South Korea" },
    { u"Republic of Korea", u"South Korea" },
    { u"Korea", u"South Korea" },
    { u"Korea, Democratic People's Republic of", u"North Korea" },
    { u"Democratic People's Republic of Korea", u"North Korea" },
    { u"United States of America", u"United States" },
    { u"USA", u"United States" },
    { u"US", u"United States" },
    { u"U.S.A.", u"United States" },
    { u"U.S.", u"United States" },
    { u"America", u"United States" },
    { u"UK", u"United Kingdom" },
    { u"U.K.", u"United Kingdom" },
    { u"Great Britain", u"United Kingdom" },
    { u"Britain", u"United Kingdom" },
    { u"England", u"United Kingdom" },
    { u"Russian Federation", u"Russia" },
    { u"Viet Nam", u"Vietnam" },
    { u"Iran, Islamic Republic of", u"Iran" },
    { u"Syrian Arab Republic", u"Syria" },
    { u"Lao People's Democratic Republic", u"Laos" },
    { u"Lao PDR", u"Laos" },
    { u"Czechia", u"Czech Republic" },
    { u"Czech Republic", u"Czechia" },
    { u"Turkiye", u"Turkey" },
    { u"Türkiye", u"Turkey" },
    { u"Turkey", u"Türkiye" },
    { u"Holland", u"Netherlands" },
    { u"The Netherlands", u"Netherlands" },
    { u"Taiwan, Province of China", u"Taiwan" },
    { u"Bolivia, Plurinational State of", u"Bolivia" },
    { u"Venezuela, Bolivarian Republic of", u"Venezuela" },
    { u"Tanzania, United Republic of", u"Tanzania" },
    { u"Moldova, Republic of", u"Moldova" },
    { u"Congo", u"Republic of the Congo" },
    { u"Congo, The Democratic Republic of the", u"Democratic Republic of the Congo" },
    { u"DR Congo", u"Democratic Republic of the Congo" },
    { u"DRC", u"Democratic Republic of the Congo" },
    { u"Ivory Coast", u"Côte d'Ivoire" },
    { u"Cote d'Ivoire", u"Côte d'Ivoire" },
    { u"Burma", u"Myanmar" },
    { u"Swaziland", u"Eswatini" },
    { u"Macedonia", u"North Macedonia" },
    { u"Cape Verde", u"Cabo Verde" },
    { u"Cabo Verde", u"Cape Verde" },
    { u"UAE", u"United Arab Emirates" },
    { u"PRC", u"China" },
    { u"People's Republic of China", u"China" },
    { u"Hong Kong", u"Hong Kong SAR" },
    { u"Macao", u"Macao SAR" },
    { u"Macau", u"Macao SAR" },
    { u"Brunei Darussalam", u"Brunei" },
    { u"Palestine", u"West Bank" },
    { u"한국", u"대한민국" }, // 한국 -> 대한민국
    { u"남한", u"대한민국" }, // 남한 -> 대한민국
    { u"조선", u"북한" }, // 조선 -> 북한
    { u"미합중국", u"미국" }, // 미합중국 -> 미국
};

// Robinson projection table, every 5 degrees of latitude
constexpr double aRobinsonLength[] = { 1.0000, 0.9986, 0.9954, 0.9900, 0.9822, 0.9730, 0.9600,
                                       0.9427, 0.9216, 0.8962, 0.8679, 0.8350, 0.7986, 0.7597,
                                       0.7186, 0.6732, 0.6213, 0.5722, 0.5322 };
constexpr double aRobinsonHeight[] = { 0.0000, 0.0620, 0.1240, 0.1860, 0.2480, 0.3100, 0.3720,
                                       0.4340, 0.4958, 0.5571, 0.6176, 0.6769, 0.7346, 0.7903,
                                       0.8435, 0.8936, 0.9394, 0.9761, 1.0000 };

double toRad(double fDegrees) { return fDegrees * M_PI / 180.0; }

double srgbToLinear(double c)
{
    return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
}

double linearToSrgb(double c)
{
    c = std::clamp(c, 0.0, 1.0);
    return c <= 0.0031308 ? c * 12.92 : 1.055 * std::pow(c, 1.0 / 2.4) - 0.055;
}

/// Projects (longitude, latitude) in degrees; y grows to the north
class Projector
{
public:
    Projector(sal_Int32 nType, double fCenterLon)
        : m_nType(nType)
        , m_fLon0(fCenterLon)
    {
        if (m_nType == PROJECTION_ALBERS)
        {
            // the usual parallels of a USA map
            const double f1 = toRad(29.5), f2 = toRad(45.5);
            m_fN = (std::sin(f1) + std::sin(f2)) / 2.0;
            m_fC = std::cos(f1) * std::cos(f1) + 2.0 * m_fN * std::sin(f1);
            m_fRho0 = std::sqrt(m_fC - 2.0 * m_fN * std::sin(toRad(23.0))) / m_fN;
        }
    }

    std::pair<double, double> operator()(double fLon, double fLat) const
    {
        // Excel cuts the world map at about 75 degrees south: Antarctica ends in a
        // straight edge instead of stretching to the pole
        fLat = std::clamp(fLat, fSouthLimit, 89.5);
        switch (m_nType)
        {
            case PROJECTION_MERCATOR:
            {
                const double f = toRad(std::clamp(fLat, -85.0, 85.0));
                return { toRad(fLon), std::log(std::tan(M_PI / 4.0 + f / 2.0)) };
            }
            case PROJECTION_ROBINSON:
            {
                const double fAbs = std::fabs(fLat) / 5.0;
                const size_t i = std::min<size_t>(static_cast<size_t>(fAbs), 17);
                const double t = fAbs - i;
                const double fLen = aRobinsonLength[i] + (aRobinsonLength[i + 1] - aRobinsonLength[i]) * t;
                const double fHeight = aRobinsonHeight[i] + (aRobinsonHeight[i + 1] - aRobinsonHeight[i]) * t;
                return { 0.8487 * fLen * toRad(fLon), std::copysign(1.3523 * fHeight, fLat) };
            }
            case PROJECTION_ALBERS:
            {
                const double fRho = std::sqrt(std::max(0.0, m_fC - 2.0 * m_fN * std::sin(toRad(fLat)))) / m_fN;
                const double fTheta = m_fN * toRad(fLon - m_fLon0);
                return { fRho * std::sin(fTheta), m_fRho0 - fRho * std::cos(fTheta) };
            }
            default: // Miller
                return { toRad(fLon), 1.25 * std::log(std::tan(M_PI / 4.0 + 0.4 * toRad(fLat))) };
        }
    }

private:
    sal_Int32 m_nType;
    double m_fLon0;
    double m_fN = 0.0;
    double m_fC = 0.0;
    double m_fRho0 = 0.0;
};

OUString attribute(xmlTextReaderPtr pReader, const char* pName)
{
    xmlChar* pValue = xmlTextReaderGetAttribute(pReader, reinterpret_cast<const xmlChar*>(pName));
    if (!pValue)
        return OUString();
    OUString aValue(reinterpret_cast<const char*>(pValue), strlen(reinterpret_cast<const char*>(pValue)), RTL_TEXTENCODING_UTF8);
    xmlFree(pValue);
    return aValue;
}

template <typename T> T seriesProperty(const VDataSeries& rSeries, const OUString& rName, T aDefault)
{
    try
    {
        const uno::Reference<beans::XPropertySet>& xProps = rSeries.getPropertiesOfSeries();
        if (xProps.is())
            xProps->getPropertyValue(rName) >>= aDefault;
    }
    catch (const uno::Exception&)
    {
    }
    return aDefault;
}
}

RegionMapChart::RegionMapChart(const rtl::Reference<ChartType>& xChartTypeModel)
    : VSeriesPlotter(xChartTypeModel, 2)
{
    PlotterBase::m_pPosHelper = &m_aMainPosHelper;
    VSeriesPlotter::m_pMainPosHelper = &m_aMainPosHelper;
}

RegionMapChart::~RegionMapChart() {}

std::vector<std::pair<double, double>> RegionMapChart::decodeRing(std::u16string_view aText)
{
    // Bing Maps point compression: zig-zag deltas of 1e-5 degrees, paired with
    // the Cantor pairing function, written as 5-bit groups in a URL safe alphabet
    std::vector<std::pair<double, double>> aPoints;
    sal_Int64 nLat = 0, nLon = 0;
    size_t i = 0;
    while (i < aText.size())
    {
        sal_uInt64 nIndex = 0;
        int nShift = 0;
        bool bEnd = false;
        while (i < aText.size())
        {
            const sal_Unicode c = aText[i++];
            int nDigit;
            if (c >= 'A' && c <= 'Z')
                nDigit = c - 'A';
            else if (c >= 'a' && c <= 'z')
                nDigit = c - 'a' + 26;
            else if (c >= '0' && c <= '9')
                nDigit = c - '0' + 52;
            else if (c == '_')
                nDigit = 62;
            else if (c == '-')
                nDigit = 63;
            else
                return aPoints; // not point compression
            if (nShift < 64)
                nIndex |= static_cast<sal_uInt64>(nDigit & 31) << nShift;
            nShift += 5;
            if (nDigit < 32)
            {
                bEnd = true;
                break;
            }
        }
        if (!bEnd)
            break;
        sal_uInt64 nDiag = static_cast<sal_uInt64>((std::sqrt(8.0 * static_cast<double>(nIndex) + 5.0) - 1.0) / 2.0);
        // guard the floating point square root
        while (nDiag > 0 && nDiag * (nDiag + 1) / 2 > nIndex)
            --nDiag;
        while ((nDiag + 1) * (nDiag + 2) / 2 <= nIndex)
            ++nDiag;
        const sal_uInt64 nY = nIndex - nDiag * (nDiag + 1) / 2;
        const sal_uInt64 nX = nDiag - nY;
        nLat += static_cast<sal_Int64>(nY >> 1) ^ -static_cast<sal_Int64>(nY & 1);
        nLon += static_cast<sal_Int64>(nX >> 1) ^ -static_cast<sal_Int64>(nX & 1);
        aPoints.emplace_back(nLon / 1e5, nLat / 1e5);
    }
    return aPoints;
}

std::vector<std::vector<std::pair<double, double>>> RegionMapChart::decodeRings(std::u16string_view aText)
{
    std::vector<std::vector<std::pair<double, double>>> aRings;
    size_t nStart = aText.find(',');
    while (nStart != std::u16string_view::npos)
    {
        const size_t nEnd = aText.find(',', nStart + 1);
        std::vector<std::pair<double, double>> aRing = decodeRing(
            aText.substr(nStart + 1, nEnd == std::u16string_view::npos ? std::u16string_view::npos : nEnd - nStart - 1));
        if (aRing.size() >= 3)
            aRings.push_back(std::move(aRing));
        nStart = nEnd;
    }
    return aRings;
}

RegionMapChart::Regions RegionMapChart::parseClearXml(const char* pXml, size_t nLength)
{
    Regions aRegions;
    std::vector<std::pair<OUString, OUString>> aHierarchyNames; // (id, English name)
    xmlTextReaderPtr pReader = xmlReaderForMemory(pXml, static_cast<int>(nLength), nullptr, nullptr,
                                                  XML_PARSE_NONET | XML_PARSE_HUGE);
    if (!pReader)
        return aRegions;
    while (xmlTextReaderRead(pReader) == 1)
    {
        if (xmlTextReaderNodeType(pReader) != XML_READER_TYPE_ELEMENT)
            continue;
        const char* pName = reinterpret_cast<const char*>(xmlTextReaderConstLocalName(pReader));
        if (!pName)
            continue;
        if (strcmp(pName, "geoData") == 0)
        {
            Region aRegion;
            aRegion.aId = attribute(pReader, "entityId");
            const OUString aName = attribute(pReader, "entityName");
            if (!aName.isEmpty())
                aRegion.aNames.push_back(aName);
            aRegions.push_back(std::move(aRegion));
        }
        else if (strcmp(pName, "geoPolygon") == 0 && !aRegions.empty())
        {
            std::vector<std::vector<std::pair<double, double>>> aRings = decodeRings(attribute(pReader, "pcaRings"));
            for (auto& rRing : aRings)
                aRegions.back().aRings.push_back(std::move(rRing));
        }
        else if (strcmp(pName, "geoHierarchyEntity") == 0)
            aHierarchyNames.emplace_back(attribute(pReader, "entityId"), attribute(pReader, "entityName"));
    }
    xmlFreeTextReader(pReader);

    // the same entity may come in more than one query result
    Regions aMerged;
    for (Region& rRegion : aRegions)
    {
        auto it = std::find_if(aMerged.begin(), aMerged.end(), [&](const Region& r) { return !r.aId.isEmpty() && r.aId == rRegion.aId; });
        if (it == aMerged.end())
            aMerged.push_back(std::move(rRegion));
        else if (it->aRings.empty())
            it->aRings = std::move(rRegion.aRings);
    }
    std::erase_if(aMerged, [](const Region& r) { return r.aRings.empty(); });
    // the hierarchy lists the regions with their English names
    for (Region& rRegion : aMerged)
        for (const auto& [rId, rName] : aHierarchyNames)
            if (rId == rRegion.aId && !rName.isEmpty()
                && std::find(rRegion.aNames.begin(), rRegion.aNames.end(), rName) == rRegion.aNames.end())
                rRegion.aNames.push_back(rName);
    return aMerged;
}

RegionMapChart::Regions RegionMapChart::parseBinary(std::u16string_view aBase64)
{
    uno::Sequence<sal_Int8> aDeflated;
    try
    {
        comphelper::Base64::decode(aDeflated, aBase64);
    }
    catch (const uno::Exception&)
    {
        return Regions();
    }
    if (!aDeflated.hasElements())
        return Regions();

    // raw deflate (no zlib header); Excel may leave the stream without its end
    z_stream aStream{};
    if (inflateInit2(&aStream, -MAX_WBITS) != Z_OK)
        return Regions();
    std::vector<char> aXml;
    std::vector<unsigned char> aBuffer(1 << 16);
    aStream.next_in = reinterpret_cast<Bytef*>(const_cast<sal_Int8*>(aDeflated.getConstArray()));
    aStream.avail_in = static_cast<uInt>(aDeflated.getLength());
    int nResult = Z_OK;
    while (nResult == Z_OK)
    {
        aStream.next_out = aBuffer.data();
        aStream.avail_out = static_cast<uInt>(aBuffer.size());
        nResult = inflate(&aStream, Z_NO_FLUSH);
        const size_t nGot = aBuffer.size() - aStream.avail_out;
        aXml.insert(aXml.end(), aBuffer.begin(), aBuffer.begin() + nGot);
        if (nResult == Z_BUF_ERROR || (nResult == Z_OK && nGot == 0 && aStream.avail_in == 0))
            break;
    }
    inflateEnd(&aStream);
    if (aXml.empty())
        return Regions();
    return parseClearXml(aXml.data(), aXml.size());
}

OUString RegionMapChart::normalizeName(std::u16string_view aName)
{
    OUStringBuffer aBuf(static_cast<sal_Int32>(aName.size()));
    for (sal_Unicode c : aName)
    {
        if (rtl::isAsciiAlphanumeric(c))
            aBuf.append(static_cast<sal_Unicode>(rtl::toAsciiLowerCase(c)));
        else if (c >= 0x80 && c != 0x00A0 && c != 0x2019 && c != 0x2018 && c != 0x2013 && c != 0x2014
                 && c != 0x3000)
            aBuf.append(c);
    }
    return aBuf.makeStringAndClear();
}

sal_Int32 RegionMapChart::findRegion(const Regions& rRegions, const OUString& rName)
{
    auto aFind = [&](std::u16string_view aName) -> sal_Int32 {
        const OUString aKey = normalizeName(aName);
        if (aKey.isEmpty())
            return -1;
        for (size_t i = 0; i < rRegions.size(); ++i)
            for (const OUString& rRegionName : rRegions[i].aNames)
                if (normalizeName(rRegionName) == aKey)
                    return static_cast<sal_Int32>(i);
        return -1;
    };
    sal_Int32 nFound = aFind(rName);
    if (nFound >= 0)
        return nFound;
    const OUString aKey = normalizeName(rName);
    for (const auto& [rAlias, rTarget] : aAliases)
        if (normalizeName(rAlias) == aKey && (nFound = aFind(rTarget)) >= 0)
            return nFound;
    return -1;
}

sal_Int32 RegionMapChart::scaleColor(double fT)
{
    fT = std::clamp(std::isnan(fT) ? 1.0 : fT, 0.0, 1.0);
    sal_Int32 nColor = 0;
    for (int nShift : { 16, 8, 0 })
    {
        const double fLow = srgbToLinear(((nLowColor >> nShift) & 0xff) / 255.0);
        const double fHigh = srgbToLinear(((nHighColor >> nShift) & 0xff) / 255.0);
        const double f = linearToSrgb(fLow + (fHigh - fLow) * fT);
        nColor |= static_cast<sal_Int32>(std::lround(f * 255.0)) << nShift;
    }
    return nColor;
}

VDataSeries* RegionMapChart::firstSeries() const
{
    if (m_aZSlots.empty())
        return nullptr;
    for (auto const& rXSlot : m_aZSlots.front())
        for (auto const& p : rXSlot.m_aSeriesVector)
            if (p)
                return p.get();
    return nullptr;
}

std::shared_ptr<const RegionMapChart::Regions> RegionMapChart::regionsOf(const VDataSeries& rSeries)
{
    const OUString aBinary = seriesProperty<OUString>(rSeries, u"GeoCacheBinary"_ustr, OUString());
    if (aBinary.isEmpty())
        return std::make_shared<const Regions>();
    // the chart is drawn again on every change: parse a cache once
    static std::mutex aMutex;
    static OUString aLastKey;
    static std::shared_ptr<const Regions> pLast;
    const sal_Int32 nLen = aBinary.getLength();
    const OUString aKey = OUString::number(nLen) + aBinary.subView(0, std::min<sal_Int32>(nLen, 256))
                          + aBinary.subView(std::max<sal_Int32>(0, nLen - 256));
    std::scoped_lock aGuard(aMutex);
    if (pLast && aKey == aLastKey)
        return pLast;
    pLast = std::make_shared<const Regions>(parseBinary(aBinary));
    aLastKey = aKey;
    return pLast;
}

std::vector<sal_Int32> RegionMapChart::matchPoints(const VDataSeries& rSeries, const Regions& rRegions)
{
    const sal_Int32 nPoints = rSeries.getTotalPointCount();
    std::vector<sal_Int32> aMatch(nPoints, -1);
    if (!m_pExplicitCategoriesProvider)
        return aMatch;
    const uno::Sequence<OUString>& rSimple = m_pExplicitCategoriesProvider->getSimpleCategories();
    // with several category columns (country, state...), try the innermost first
    std::vector<std::vector<OUString>> aNames(nPoints);
    const sal_Int32 nLevels = m_pExplicitCategoriesProvider->getCategoryLevelCount();
    for (sal_Int32 nLevel = 0; nLevel < nLevels; ++nLevel)
    {
        const std::vector<ComplexCategory>* pCategories = m_pExplicitCategoriesProvider->getCategoriesByLevel(nLevel);
        if (!pCategories)
            continue;
        sal_Int32 nPos = 0;
        for (const ComplexCategory& rCategory : *pCategories)
            for (sal_Int32 k = 0; k < rCategory.Count; ++k, ++nPos)
                if (nPos < nPoints && !rCategory.Text.isEmpty())
                    aNames[nPos].push_back(rCategory.Text);
    }
    for (sal_Int32 i = 0; i < nPoints; ++i)
    {
        if (i < rSimple.getLength())
            aNames[i].insert(aNames[i].begin(), rSimple[i]);
        for (const OUString& rName : aNames[i])
            if ((aMatch[i] = findRegion(rRegions, rName)) >= 0)
                break;
    }
    return aMatch;
}

void RegionMapChart::addSeries(std::unique_ptr<VDataSeries> pSeries, sal_Int32 /*zSlot*/,
                               sal_Int32 /*xSlot*/, sal_Int32 ySlot)
{
    if (!pSeries)
        return;
    pSeries->setCategoryXAxis();
    VSeriesPlotter::addSeries(std::move(pSeries), 0, 0, ySlot);
}

drawing::Direction3D RegionMapChart::getPreferredDiagramAspectRatio() const
{
    return drawing::Direction3D(-1, -1, -1);
}

std::vector<ViewLegendEntry> RegionMapChart::createLegendEntriesForSeries(
    const awt::Size& rEntryKeyAspectRatio, const VDataSeries& rSeries,
    const uno::Reference<beans::XPropertySet>& xTextProperties,
    const rtl::Reference<SvxShapeGroupAnyD>& xTarget,
    const uno::Reference<uno::XComponentContext>& /*xContext*/)
{
    std::vector<ViewLegendEntry> aResult;
    if (!xTarget.is())
        return aResult;
    sal_Int32 nMin = -1, nMax = -1;
    for (sal_Int32 i = 0; i < rSeries.getTotalPointCount(); ++i)
    {
        const double fValue = rSeries.getYValue(i);
        if (std::isnan(fValue))
            continue;
        if (nMin < 0 || fValue < rSeries.getYValue(nMin))
            nMin = i;
        if (nMax < 0 || fValue > rSeries.getYValue(nMax))
            nMax = i;
    }
    if (nMax < 0)
        return aResult;
    aResult.push_back(createBoxLegendEntry(rEntryKeyAspectRatio, getLabelTextForValue(rSeries, nMax, rSeries.getYValue(nMax), false),
                                           scaleColor(1.0), xTextProperties, xTarget));
    if (nMin != nMax && rSeries.getYValue(nMin) < rSeries.getYValue(nMax))
        aResult.push_back(createBoxLegendEntry(rEntryKeyAspectRatio, getLabelTextForValue(rSeries, nMin, rSeries.getYValue(nMin), false),
                                               scaleColor(0.0), xTextProperties, xTarget));
    return aResult;
}

void RegionMapChart::createShapes()
{
    if (!(m_xLogicTarget.is() && m_xFinalTarget.is()))
        return;
    VDataSeries* pSeries = firstSeries();
    if (!pSeries)
        return;

    const basegfx::B2DRectangle aClip = m_aMainPosHelper.getScaledLogicClipDoubleRect();
    const drawing::Position3D aP1 = m_aMainPosHelper.transformScaledLogicToScene(aClip.getMinX(), aClip.getMinY(), 0.0, false);
    const drawing::Position3D aP2 = m_aMainPosHelper.transformScaledLogicToScene(aClip.getMaxX(), aClip.getMaxY(), 0.0, false);
    const double fPlotX = std::min(aP1.PositionX, aP2.PositionX);
    const double fPlotY = std::min(aP1.PositionY, aP2.PositionY);
    const double fPlotW = std::fabs(aP2.PositionX - aP1.PositionX);
    const double fPlotH = std::fabs(aP2.PositionY - aP1.PositionY);
    if (fPlotW <= 0.0 || fPlotH <= 0.0)
        return;

    const std::shared_ptr<const Regions> pRegions = regionsOf(*pSeries);
    const Regions& rRegions = *pRegions;
    if (rRegions.empty())
        return;
    const std::vector<sal_Int32> aMatch = matchPoints(*pSeries, rRegions);

    // value range of the named regions
    std::vector<sal_Int32> aPointOfRegion(rRegions.size(), -1);
    double fMin = std::numeric_limits<double>::infinity();
    double fMax = -std::numeric_limits<double>::infinity();
    for (size_t i = 0; i < aMatch.size(); ++i)
    {
        const double fValue = pSeries->getYValue(static_cast<sal_Int32>(i));
        if (aMatch[i] < 0 || std::isnan(fValue))
            continue;
        aPointOfRegion[aMatch[i]] = static_cast<sal_Int32>(i);
        fMin = std::min(fMin, fValue);
        fMax = std::max(fMax, fValue);
    }

    // which regions are drawn: all of the cache, or only those with data
    const sal_Int32 nViewed = seriesProperty<sal_Int32>(*pSeries, u"GeographyViewedRegionType"_ustr, -1);
    std::vector<bool> aShown(rRegions.size(), true);
    if (nViewed == nMappingDataOnly)
        for (size_t r = 0; r < rRegions.size(); ++r)
            aShown[r] = aPointOfRegion[r] >= 0;

    // projection: Excel picks Miller for the world and Mercator for smaller areas
    double fWest = 180.0, fEast = -180.0, fSouth = 90.0, fNorth = -90.0;
    for (size_t r = 0; r < rRegions.size(); ++r)
        if (aShown[r])
            for (const auto& rRing : rRegions[r].aRings)
                for (const auto& [fLon, fLat] : rRing)
                {
                    fWest = std::min(fWest, fLon);
                    fEast = std::max(fEast, fLon);
                    fSouth = std::min(fSouth, fLat);
                    fNorth = std::max(fNorth, fLat);
                }
    if (fWest > fEast)
        return;
    sal_Int32 nProjection = seriesProperty<sal_Int32>(*pSeries, u"GeographyProjectionType"_ustr, -1);
    if (nProjection < PROJECTION_MERCATOR || nProjection > PROJECTION_ALBERS)
        nProjection = fEast - fWest > 90.0 ? PROJECTION_MILLER : PROJECTION_MERCATOR;
    const Projector aProject(nProjection, (fWest + fEast) / 2.0);

    // projected rings and their bounds
    std::vector<std::vector<std::vector<std::pair<double, double>>>> aProjected(rRegions.size());
    double fMinX = std::numeric_limits<double>::infinity(), fMaxX = -fMinX, fMinY = fMinX, fMaxY = -fMinX;
    for (size_t r = 0; r < rRegions.size(); ++r)
    {
        if (!aShown[r])
            continue;
        for (const auto& rRing : rRegions[r].aRings)
        {
            std::vector<std::pair<double, double>>& rOut = aProjected[r].emplace_back();
            rOut.reserve(rRing.size());
            for (const auto& [fLon, fLat] : rRing)
            {
                const auto [x, y] = aProject(fLon, fLat);
                rOut.emplace_back(x, y);
                fMinX = std::min(fMinX, x);
                fMaxX = std::max(fMaxX, x);
                fMinY = std::min(fMinY, y);
                fMaxY = std::max(fMaxY, y);
            }
        }
    }
    if (!(fMaxX > fMinX && fMaxY > fMinY))
        return;
    // fit into the plot area keeping the aspect ratio, centred
    const double fScale = std::min(fPlotW / (fMaxX - fMinX), fPlotH / (fMaxY - fMinY));
    const double fOffX = fPlotX + (fPlotW - (fMaxX - fMinX) * fScale) / 2.0;
    const double fOffY = fPlotY + (fPlotH - (fMaxY - fMinY) * fScale) / 2.0;

    rtl::Reference<SvxShapeGroupAnyD> xSeriesTarget = createGroupShape(m_xLogicTarget);
    rtl::Reference<SvxShapeGroupAnyD> xSeriesGroupShape = getSeriesGroupShape(pSeries, xSeriesTarget);
    for (size_t r = 0; r < rRegions.size(); ++r)
    {
        if (!aShown[r])
            continue;
        std::vector<std::vector<drawing::Position3D>> aPoly;
        size_t nLargest = 0;
        double fLargest = -1.0;
        std::vector<std::vector<drawing::Position3D>> aAll; // every ring, to keep tiny regions
        for (const auto& rRing : aProjected[r])
        {
            std::vector<drawing::Position3D> aOut;
            double fX0 = std::numeric_limits<double>::infinity(), fX1 = -fX0, fY0 = fX0, fY1 = -fX0;
            for (const auto& [x, y] : rRing)
            {
                const double fX = fOffX + (x - fMinX) * fScale;
                const double fY = fOffY + (fMaxY - y) * fScale;
                fX0 = std::min(fX0, fX);
                fX1 = std::max(fX1, fX);
                fY0 = std::min(fY0, fY);
                fY1 = std::max(fY1, fY);
                if (!aOut.empty() && std::fabs(aOut.back().PositionX - fX) < fPointTolerance
                    && std::fabs(aOut.back().PositionY - fY) < fPointTolerance)
                    continue;
                aOut.emplace_back(fX, fY, 0.0);
            }
            const double fSize = std::max(fX1 - fX0, fY1 - fY0);
            if (fSize > fLargest)
            {
                fLargest = fSize;
                nLargest = aAll.size();
            }
            aAll.push_back(aOut);
            if (fSize >= fRingMinSize && aOut.size() >= 3)
                aPoly.push_back(std::move(aOut));
        }
        if (aPoly.empty() && !aAll.empty() && aAll[nLargest].size() >= 3)
            aPoly.push_back(aAll[nLargest]);
        if (aPoly.empty())
            continue;

        rtl::Reference<SvxShapePolyPolygon> xShape = ShapeFactory::createArea2D(xSeriesGroupShape, aPoly);
        if (!xShape.is())
            continue;
        const sal_Int32 nPoint = aPointOfRegion[r];
        if (nPoint >= 0)
        {
            uno::Reference<beans::XPropertySet> xPointProperties(pSeries->getPropertiesOfPoint(nPoint));
            PropertyMapper::setMappedProperties(*xShape, xPointProperties, PropertyMapper::getPropertyNameMapForFilledSeriesProperties());
            ShapeFactory::setShapeName(xShape, ObjectIdentifier::createPointCID(pSeries->getPointCID_Stub(), nPoint));
        }
        if (nPoint < 0 || !pSeries->isAttributedDataPoint(nPoint))
        {
            const double fValue = nPoint >= 0 ? pSeries->getYValue(nPoint) : 0.0;
            const sal_Int32 nColor = nPoint < 0 ? nNoDataColor : scaleColor(fMax > fMin ? (fValue - fMin) / (fMax - fMin) : 1.0);
            xShape->SvxShape::setPropertyValue(u"FillStyle"_ustr, uno::Any(drawing::FillStyle_SOLID));
            xShape->SvxShape::setPropertyValue(u"FillColor"_ustr, uno::Any(nColor));
            xShape->SvxShape::setPropertyValue(u"FillTransparence"_ustr, uno::Any(sal_Int16(0)));
        }
        xShape->SvxShape::setPropertyValue(u"LineStyle"_ustr, uno::Any(drawing::LineStyle_SOLID));
        xShape->SvxShape::setPropertyValue(u"LineColor"_ustr, uno::Any(sal_Int32(0xFFFFFF)));
        xShape->SvxShape::setPropertyValue(u"LineWidth"_ustr, uno::Any(sal_Int32(0)));
    }

    // map data attribution ("Powered by Bing") at the bottom right, 7pt grey
    const OUString aAttribution = seriesProperty<OUString>(*pSeries, u"GeographyAttribution"_ustr, OUString());
    if (!aAttribution.isEmpty())
    {
        rtl::Reference<SvxShapeGroupAnyD> xTextTarget = ShapeFactory::createGroup2D(m_xFinalTarget);
        tPropertyNameValueMap aValues;
        const float fHeight = 7.0f;
        aValues[u"CharHeight"_ustr] <<= fHeight;
        aValues[u"CharHeightAsian"_ustr] <<= fHeight;
        aValues[u"CharHeightComplex"_ustr] <<= fHeight;
        aValues[u"CharColor"_ustr] <<= nAttributionColor;
        aValues[u"FillStyle"_ustr] <<= drawing::FillStyle_NONE;
        aValues[u"LineStyle"_ustr] <<= drawing::LineStyle_NONE;
        aValues[u"ParaAdjust"_ustr] <<= style::ParagraphAdjust_RIGHT;
        aValues[u"TextHorizontalAdjust"_ustr] <<= drawing::TextHorizontalAdjust_RIGHT;
        aValues[u"TextVerticalAdjust"_ustr] <<= drawing::TextVerticalAdjust_BOTTOM;
        aValues[u"TextAutoGrowHeight"_ustr] <<= false;
        aValues[u"TextAutoGrowWidth"_ustr] <<= false;
        aValues[u"TextWordWrap"_ustr] <<= false;
        aValues[u"TextLeftDistance"_ustr] <<= sal_Int32(0);
        aValues[u"TextRightDistance"_ustr] <<= sal_Int32(0);
        aValues[u"TextUpperDistance"_ustr] <<= sal_Int32(0);
        aValues[u"TextLowerDistance"_ustr] <<= sal_Int32(0);
        tNameSequence aNames;
        tAnySequence aAnys;
        PropertyMapper::getMultiPropertyListsFromValueMap(aNames, aAnys, aValues);
        rtl::Reference<SvxShapeText> xText = ShapeFactory::createText(xTextTarget, aAttribution, aNames, aAnys, uno::Any());
        if (xText.is())
        {
            const sal_Int32 nTextH = 400;
            xText->setPosition(awt::Point(static_cast<sal_Int32>(fPlotX), static_cast<sal_Int32>(fPlotY + fPlotH - nTextH)));
            xText->setSize(awt::Size(static_cast<sal_Int32>(fPlotW), nTextH));
        }
    }
}

} // namespace chart

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
