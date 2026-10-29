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

#include <ExplicitCategoriesProvider.hxx>
#include <DiagramHelper.hxx>
#include <ChartType.hxx>
#include <Axis.hxx>
#include <AxisHelper.hxx>
#include <DataSourceHelper.hxx>
#include <ChartModel.hxx>
#include <NumberFormatterWrapper.hxx>
#include <unonames.hxx>
#include <BaseCoordinateSystem.hxx>
#include <DataSeries.hxx>
#include <DataSeriesHelper.hxx>
#include <com/sun/star/chart2/data/XLabeledDataSequence.hpp>
#include <com/sun/star/chart2/data/XNumericalDataSequence.hpp>

#include <com/sun/star/chart2/AxisType.hpp>
#include <com/sun/star/chart/ChartDataRowSource.hpp>
#include <o3tl/compare.hxx>
#include <o3tl/safeint.hxx>
#include <rtl/ustrbuf.hxx>
#include <comphelper/diagnose_ex.hxx>

#include <algorithm>
#include <cmath>
#include <limits>

namespace chart
{

using namespace ::com::sun::star;
using namespace ::com::sun::star::chart2;
using ::com::sun::star::uno::Reference;
using ::com::sun::star::uno::Sequence;
using std::vector;

ExplicitCategoriesProvider::ExplicitCategoriesProvider( const rtl::Reference< BaseCoordinateSystem >& xCooSysModel
                                                       , ChartModel& rModel )
    : m_bDirty(true)
    , m_xCooSysModel( xCooSysModel.get() )
    , mrModel(rModel)
    , m_bIsExplicitCategoriesInited(false)
    , m_bIsDateAxis(false)
    , m_bIsAutoDate(false)
{
    try
    {
        if( xCooSysModel.is() )
        {
            // TODO: handle different category names on the primary and secondary category axis.
            rtl::Reference< Axis > xAxis = xCooSysModel->getAxisByDimension2(0,0);
            if( xAxis.is() )
            {
                ScaleData aScale( xAxis->getScaleData() );
                m_xOriginalCategories = aScale.Categories;
                m_bIsAutoDate = (aScale.AutoDateAxis && aScale.AxisType==chart2::AxisType::CATEGORY);
                m_bIsDateAxis = (aScale.AxisType == chart2::AxisType::DATE || m_bIsAutoDate);
            }
        }

        if( m_xOriginalCategories.is() )
        {
            implInitSplit();
            if( m_aSplitCategoriesList.empty() )
                m_aSplitCategoriesList = { m_xOriginalCategories };
        }
    }
    catch( const uno::Exception & )
    {
        DBG_UNHANDLED_EXCEPTION("chart2");
    }
}

void ExplicitCategoriesProvider::implInitSplit()
{
    uno::Reference< data::XDataProvider > xDataProvider( mrModel.getDataProvider() );
    if( !xDataProvider.is() )
        return;

    OUString aCategoriesRange( DataSourceHelper::getRangeFromValues( m_xOriginalCategories ) );
    if( aCategoriesRange.isEmpty() )
        return;

    const bool bFirstCellAsLabel = false;
    const bool bHasCategories = false;
    const uno::Sequence< sal_Int32 > aSequenceMapping;

    uno::Reference< data::XDataSource > xColumnCategoriesSource( xDataProvider->createDataSource(
                DataSourceHelper::createArguments( aCategoriesRange, aSequenceMapping, true /*bUseColumns*/
                    , bFirstCellAsLabel, bHasCategories ) ) );
    if( !xColumnCategoriesSource )
        return;

    Sequence< Reference< data::XLabeledDataSequence> > aColumns = xColumnCategoriesSource->getDataSequences();
    sal_Int32 nColumnCount = aColumns.getLength();
    if( nColumnCount<=1 )
        return;

    uno::Reference< data::XDataSource > xRowCategoriesSource( xDataProvider->createDataSource(
                DataSourceHelper::createArguments( aCategoriesRange, aSequenceMapping, false /*bUseColumns*/
                    , bFirstCellAsLabel, bHasCategories ) ) );
    if( !xRowCategoriesSource )
        return;

    Sequence< Reference< data::XLabeledDataSequence> > aRows = xRowCategoriesSource->getDataSequences();
    sal_Int32 nRowCount = aRows.getLength();
    if( nRowCount<=1 )
        return;

    //we have complex categories
    //->split them in the direction of the first series
    //detect whether the first series is a row or a column
    bool bSeriesUsesColumns = true;
    std::vector< rtl::Reference< DataSeries > > aSeries = mrModel.getDataSeries();
    if( !aSeries.empty() )
    {
        const rtl::Reference< DataSeries >& xSeriesSource = aSeries.front();
        for(const auto& rArgument : xDataProvider->detectArguments( xSeriesSource))
        {
            if ( rArgument.Name == "DataRowSource" )
            {
                css::chart::ChartDataRowSource eRowSource;
                if( rArgument.Value >>= eRowSource )
                {
                    bSeriesUsesColumns = (eRowSource == css::chart::ChartDataRowSource_COLUMNS);
                    break;
                }
            }
        }
    }
    if( bSeriesUsesColumns )
        m_aSplitCategoriesList = comphelper::sequenceToContainer<std::vector<Reference<data::XLabeledDataSequence>>>(aColumns);
    else
        m_aSplitCategoriesList = comphelper::sequenceToContainer<std::vector<Reference<data::XLabeledDataSequence>>>(aRows);
}

ExplicitCategoriesProvider::~ExplicitCategoriesProvider()
{
}

Reference< chart2::data::XDataSequence > ExplicitCategoriesProvider::getOriginalCategories()
{
    if( m_xOriginalCategories.is() )
        return m_xOriginalCategories->getValues();
    return nullptr;
}

Sequence< OUString > ExplicitCategoriesProvider::getOriginalCategoryTexts()
{
    Sequence< OUString > aTexts;
    if( m_xOriginalCategories.is() && !hasComplexCategories() )
    {
        uno::Reference< data::XDataSequence > xDataSequence( m_xOriginalCategories->getValues() );
        if( xDataSequence.is() )
            convertCategoryAnysToText( aTexts, xDataSequence->getData(), mrModel );
    }
    return aTexts;
}

std::vector< sal_Int32 > ExplicitCategoriesProvider::groupDuplicateCategories(
    const Sequence< OUString >& rTexts, sal_Int32& rnGroupCount )
{
    std::vector< sal_Int32 > aGroups;
    std::vector< OUString > aDistinct;
    aGroups.reserve( rTexts.getLength() );
    for( const OUString& rText : rTexts )
    {
        auto aFound = std::find( aDistinct.begin(), aDistinct.end(), rText );
        if( aFound == aDistinct.end() )
        {
            aGroups.push_back( static_cast< sal_Int32 >( aDistinct.size() ) );
            aDistinct.push_back( rText );
        }
        else
            aGroups.push_back( static_cast< sal_Int32 >( aFound - aDistinct.begin() ) );
    }
    rnGroupCount = static_cast< sal_Int32 >( aDistinct.size() );
    return aGroups;
}

std::vector< sal_Int32 > ExplicitCategoriesProvider::paretoSlots(
    const Sequence< OUString >& rTexts, const std::vector< double >& rValues,
    std::vector< double >& rSums )
{
    const sal_Int32 nPoints = std::max< sal_Int32 >( rTexts.getLength(), rValues.size() );
    sal_Int32 nGroups = 0;
    std::vector< sal_Int32 > aGroups( groupDuplicateCategories( rTexts, nGroups ) );
    // points without a category text get a bar of their own
    while( static_cast< sal_Int32 >( aGroups.size() ) < nPoints )
        aGroups.push_back( nGroups++ );

    std::vector< double > aSums( nGroups, 0.0 );
    for( sal_Int32 i = 0; i < nPoints; ++i )
    {
        const double fValue = o3tl::make_unsigned( i ) < rValues.size() ? rValues[i] : 0.0;
        if( std::isfinite( fValue ) )
            aSums[ aGroups[i] ] += fValue;
    }

    // largest first; equal sums keep the order of first appearance
    std::vector< sal_Int32 > aOrder( nGroups );
    for( sal_Int32 g = 0; g < nGroups; ++g )
        aOrder[g] = g;
    std::stable_sort( aOrder.begin(), aOrder.end(),
                      [&aSums]( sal_Int32 a, sal_Int32 b ) { return aSums[a] > aSums[b]; } );
    std::vector< sal_Int32 > aRank( nGroups );
    rSums.assign( nGroups, 0.0 );
    for( sal_Int32 k = 0; k < nGroups; ++k )
    {
        aRank[ aOrder[k] ] = k;
        rSums[k] = aSums[ aOrder[k] ];
    }
    for( sal_Int32& rGroup : aGroups )
        rGroup = aRank[ rGroup ];
    return aGroups;
}

bool ExplicitCategoriesProvider::isParetoOrder()
{
    getSimpleCategories();
    return m_bParetoOrder;
}

namespace
{
bool lcl_isBoxWhisker( const rtl::Reference< BaseCoordinateSystem >& xCooSys )
{
    if( !xCooSys.is() )
        return false;
    rtl::Reference< ChartType > xChartType( AxisHelper::getChartTypeByIndex( xCooSys, 0 ) );
    return xChartType.is()
           && xChartType->getChartType().equalsIgnoreAsciiCase( u"com.sun.star.chart2.BoxWhiskerChartType" );
}

// Values of a Pareto chart: the first series with numbers, whether it belongs to
// the ParetoLine chart type (inserted chart) or to its clustered column partner
// (OOXML chartex import). False when the coordinate system has no ParetoLine.
bool lcl_getParetoValues( const rtl::Reference< BaseCoordinateSystem >& xCooSys,
                          std::vector< double >& rValues )
{
    if( !xCooSys.is() )
        return false;
    bool bPareto = false;
    for( rtl::Reference< ChartType > const & xChartType : xCooSys->getChartTypes2() )
        if( xChartType.is()
            && xChartType->getChartType().equalsIgnoreAsciiCase( u"com.sun.star.chart2.ParetoLineChartType" ) )
            bPareto = true;
    if( !bPareto )
        return false;
    for( rtl::Reference< ChartType > const & xChartType : xCooSys->getChartTypes2() )
    {
        if( !xChartType.is() )
            continue;
        for( rtl::Reference< DataSeries > const & xSeries : xChartType->getDataSeries2() )
        {
            uno::Reference< data::XLabeledDataSequence > xLabeled(
                DataSeriesHelper::getDataSequenceByRole( xSeries, u"values-y"_ustr ) );
            if( !xLabeled.is() )
                continue;
            uno::Reference< data::XNumericalDataSequence > xNumbers( xLabeled->getValues(), uno::UNO_QUERY );
            if( !xNumbers.is() )
                continue;
            const Sequence< double > aData( xNumbers->getNumericalData() );
            if( !aData.hasElements() )
                continue;
            rValues.assign( aData.begin(), aData.end() );
            return true;
        }
    }
    return false;
}
}

bool ExplicitCategoriesProvider::hasComplexCategories() const
{
    return m_aSplitCategoriesList.size() > 1;
}

sal_Int32 ExplicitCategoriesProvider::getCategoryLevelCount() const
{
    sal_Int32 nCount = m_aSplitCategoriesList.size();
    if(!nCount)
        nCount = 1;
    return nCount;
}

static std::vector<sal_Int32> lcl_getLimitingBorders( const std::vector< ComplexCategory >& rComplexCategories )
{
    std::vector<sal_Int32> aLimitingBorders;
    sal_Int32 nBorderIndex = 0; /*border below the index*/
    for (auto const& complexCategory : rComplexCategories)
    {
        nBorderIndex += complexCategory.Count;
        aLimitingBorders.push_back(nBorderIndex);
    }
    return aLimitingBorders;
}

void ExplicitCategoriesProvider::convertCategoryAnysToText( uno::Sequence< OUString >& rOutTexts, const uno::Sequence< uno::Any >& rInAnys, ChartModel& rModel )
{
    sal_Int32 nCount = rInAnys.getLength();
    if(!nCount)
        return;
    rOutTexts.realloc(nCount);
    auto pOutTexts = rOutTexts.getArray();

    sal_Int32 nAxisNumberFormat = 0;
    rtl::Reference< BaseCoordinateSystem > xCooSysModel( rModel.getFirstCoordinateSystem() );
    if( xCooSysModel.is() )
    {
        rtl::Reference< Axis > xAxis = xCooSysModel->getAxisByDimension2(0,0);
        nAxisNumberFormat = AxisHelper::getExplicitNumberFormatKeyForAxis(
            xAxis, xCooSysModel, &rModel, false );
    }

    Color nLabelColor;
    bool bColorChanged = false;

    NumberFormatterWrapper aNumberFormatterWrapper( rModel.getNumberFormatsSupplier() );

    for(sal_Int32 nN=0;nN<nCount;nN++)
    {
        OUString aText;
        const uno::Any& aAny = rInAnys[nN];
        if( aAny.hasValue() )
        {
            double fDouble = 0;
            if( aAny>>=fDouble )
            {
                if( !std::isnan(fDouble) )
                    aText = aNumberFormatterWrapper.getFormattedString(
                        nAxisNumberFormat, fDouble, nLabelColor, bColorChanged );
            }
            else
            {
                aAny>>=aText;
            }
        }
        pOutTexts[nN] = aText;
    }
}

SplitCategoriesProvider::~SplitCategoriesProvider()
{
}

namespace {

class SplitCategoriesProvider_ForLabeledDataSequences : public SplitCategoriesProvider
{
public:

    explicit SplitCategoriesProvider_ForLabeledDataSequences(
        const std::vector< Reference< data::XLabeledDataSequence> >& rSplitCategoriesList
        , ChartModel& rModel )
        : m_rSplitCategoriesList( rSplitCategoriesList )
        , mrModel( rModel )
    {}

    virtual sal_Int32 getLevelCount() const override;
    virtual uno::Sequence< OUString > getStringsForLevel( sal_Int32 nIndex ) const override;

private:
    const std::vector< Reference< data::XLabeledDataSequence> >& m_rSplitCategoriesList;

    ChartModel& mrModel;
};

}

sal_Int32 SplitCategoriesProvider_ForLabeledDataSequences::getLevelCount() const
{
    return m_rSplitCategoriesList.size();
}
uno::Sequence< OUString > SplitCategoriesProvider_ForLabeledDataSequences::getStringsForLevel( sal_Int32 nLevel ) const
{
    uno::Sequence< OUString > aRet;
    Reference< data::XLabeledDataSequence > xLabeledDataSequence( m_rSplitCategoriesList[nLevel] );
    if( xLabeledDataSequence.is() )
    {
        uno::Reference< data::XDataSequence > xDataSequence( xLabeledDataSequence->getValues() );
        if( xDataSequence.is() )
            ExplicitCategoriesProvider::convertCategoryAnysToText( aRet, xDataSequence->getData(), mrModel );
    }
    return aRet;
}

static std::vector< ComplexCategory > lcl_DataSequenceToComplexCategoryVector(
    const uno::Sequence< OUString >& rStrings
    , const std::vector<sal_Int32>& rLimitingBorders, bool bCreateSingleCategories )
{
    std::vector< ComplexCategory > aResult;

    sal_Int32 nMaxCount = rStrings.getLength();
    OUString aPrevious;
    sal_Int32 nCurrentCount=0;
    for( sal_Int32 nN=0; nN<nMaxCount; nN++ )
    {
        const OUString& aCurrent = rStrings[nN];
        if( bCreateSingleCategories || std::find( rLimitingBorders.begin(), rLimitingBorders.end(), nN ) != rLimitingBorders.end() )
        {
            aResult.emplace_back(aPrevious,nCurrentCount );
            nCurrentCount=1;
            aPrevious = aCurrent;
        }
        else
        {
            // Empty value is interpreted as a continuation of the previous
            // category. Note that having the same value as the previous one
            // does not equate to a continuation of the category.

            if (aCurrent.isEmpty())
                ++nCurrentCount;
            else
            {
                aResult.emplace_back(aPrevious,nCurrentCount );
                nCurrentCount=1;
                aPrevious = aCurrent;
            }
        }
    }
    if( nCurrentCount )
        aResult.emplace_back(aPrevious,nCurrentCount );

    return aResult;
}

static sal_Int32 lcl_getCategoryCount( std::vector< ComplexCategory >& rComplexCategories )
{
    sal_Int32 nCount = 0;
    for (auto const& complexCategory : rComplexCategories)
        nCount+=complexCategory.Count;
    return nCount;
}

static Sequence< OUString > lcl_getExplicitSimpleCategories(
    const SplitCategoriesProvider& rSplitCategoriesProvider,
    std::vector< std::vector< ComplexCategory > >& rComplexCats )
{
    Sequence< OUString > aRet;

    rComplexCats.clear();
    sal_Int32 nLCount = rSplitCategoriesProvider.getLevelCount();
    for( sal_Int32 nL = 0; nL < nLCount; nL++ )
    {
        std::vector<sal_Int32> aLimitingBorders;
        if(nL>0)
            aLimitingBorders = lcl_getLimitingBorders( rComplexCats.back() );
        rComplexCats.push_back( lcl_DataSequenceToComplexCategoryVector(
            rSplitCategoriesProvider.getStringsForLevel(nL), aLimitingBorders, nL==(nLCount-1) ) );
    }

    //ensure that the category count is the same on each level
    sal_Int32 nMaxCategoryCount = 0;
    {
        for (auto & complexCat : rComplexCats)
        {
            sal_Int32 nCurrentCount = lcl_getCategoryCount(complexCat);
            nMaxCategoryCount = std::max( nCurrentCount, nMaxCategoryCount );
        }
        for (auto & complexCat : rComplexCats)
        {
            if ( !complexCat.empty() )
            {
                sal_Int32 nCurrentCount = lcl_getCategoryCount(complexCat);
                if( nCurrentCount< nMaxCategoryCount )
                {
                    ComplexCategory& rComplexCategory = complexCat.back();
                    rComplexCategory.Count += (nMaxCategoryCount-nCurrentCount);
                }
            }
        }
    }

    //create a list with an element for every index
    std::vector< std::vector< ComplexCategory > > aComplexCatsPerIndex;
    for (auto const& complexCat : rComplexCats)
    {
        std::vector< ComplexCategory > aSingleLevel;
        for (auto const& elem : complexCat)
        {
            sal_Int32 nCount = elem.Count;
            while( nCount-- )
                aSingleLevel.push_back(elem);
        }
        aComplexCatsPerIndex.push_back(std::move(aSingleLevel));
    }

    if(nMaxCategoryCount)
    {
        aRet.realloc(nMaxCategoryCount);
        auto pRet = aRet.getArray();
        for(sal_Int32 nN=0; nN<nMaxCategoryCount; nN++)
        {
            OUStringBuffer aText;
            for (auto const& complexCatPerIndex : aComplexCatsPerIndex)
            {
                if ( o3tl::make_unsigned(nN) < complexCatPerIndex.size() )
                {
                    OUString aAddText = complexCatPerIndex[nN].Text;
                    if( !aAddText.isEmpty() )
                    {
                        if(!aText.isEmpty())
                            aText.append(" ");
                        aText.append(aAddText);
                    }
                }
            }
            pRet[nN]=aText.makeStringAndClear();
        }
    }
    return aRet;
}

Sequence< OUString > ExplicitCategoriesProvider::getExplicitSimpleCategories(
    const SplitCategoriesProvider& rSplitCategoriesProvider )
{
    vector< vector< ComplexCategory > > aComplexCats;
    return lcl_getExplicitSimpleCategories( rSplitCategoriesProvider, aComplexCats );
}

static bool lcl_fillDateCategories( const uno::Reference< data::XDataSequence >& xDataSequence, std::vector< double >& rDateCategories, bool bIsAutoDate, ChartModel& rModel )
{
    bool bOnlyDatesFound = true;
    bool bAnyDataFound = false;

    if( xDataSequence.is() )
    {
        uno::Sequence< uno::Any > aValues = xDataSequence->getData();
        sal_Int32 nCount = aValues.getLength();
        rDateCategories.reserve(nCount);
        Reference< util::XNumberFormats > xNumberFormats( rModel.getNumberFormats() );

        bool bOwnData = false;
        bool bOwnDataAnddAxisHasAnyFormat = false;
        bool bOwnDataAnddAxisHasDateFormat = false;
        rtl::Reference< BaseCoordinateSystem > xCooSysModel( rModel.getFirstCoordinateSystem() );
        if( xCooSysModel.is() )
        {
            if( rModel.hasInternalDataProvider() )
            {
                bOwnData = true;
                rtl::Reference< Axis > xAxisProps = xCooSysModel->getAxisByDimension2(0,0);
                sal_Int32 nAxisNumberFormat = 0;
                if (xAxisProps.is() && (xAxisProps->getPropertyValue(CHART_UNONAME_NUMFMT) >>= nAxisNumberFormat))
                {
                    bOwnDataAnddAxisHasAnyFormat = true;
                    bOwnDataAnddAxisHasDateFormat = DiagramHelper::isDateNumberFormat( nAxisNumberFormat, xNumberFormats );
                }
            }
        }

        for(sal_Int32 nN=0;nN<nCount;nN++)
        {
            bool bIsDate = false;
            if( bIsAutoDate )
            {
                if( bOwnData )
                    bIsDate = !bOwnDataAnddAxisHasAnyFormat || bOwnDataAnddAxisHasDateFormat;
                else
                    bIsDate = DiagramHelper::isDateNumberFormat( xDataSequence->getNumberFormatKeyByIndex( nN ), xNumberFormats );
            }
            else
                bIsDate = true;

            bool bContainsEmptyString = false;
            const uno::Any& aAny = aValues[nN];
            if( aAny.hasValue() )
            {
                OUString aTest;
                double fTest = 0;
                bool bContainsNan = false;
                if( (aAny>>=aTest) && aTest.isEmpty() ) //empty String
                    bContainsEmptyString = true;
                else if( (aAny>>=fTest) &&  std::isnan(fTest) )
                    bContainsNan = true;

                if( !bContainsEmptyString && !bContainsNan )
                    bAnyDataFound = true;
            }
            double aDate( 1.0 );
            if( bIsDate && (aAny >>= aDate) )
                rDateCategories.push_back( aDate );
            else
            {
                if( aAny.hasValue() && !bContainsEmptyString )//empty string does not count as non date value!
                    bOnlyDatesFound=false;
                rDateCategories.push_back( std::numeric_limits<double>::quiet_NaN() );
            }
        }
        std::sort(
            rDateCategories.begin(), rDateCategories.end(),
            [](auto x, auto y) { return o3tl::strong_order(x, y) < 0; } );
    }

    return bAnyDataFound && bOnlyDatesFound;
}

void ExplicitCategoriesProvider::init()
{
    if( !m_bDirty )
        return;

    m_aComplexCats.clear();//not one per index
    m_aDateCategories.clear();

    if( m_xOriginalCategories.is() )
    {
        if( !hasComplexCategories() )
        {
            if(m_bIsDateAxis)
            {
                auto xChartType = AxisHelper::getChartTypeByIndex(m_xCooSysModel.get(), 0);
                if (xChartType.is() ? xChartType->isSupportingDateAxis(0) : true)
                    m_bIsDateAxis = lcl_fillDateCategories( m_xOriginalCategories->getValues(), m_aDateCategories, m_bIsAutoDate, mrModel );
                else
                    m_bIsDateAxis = false;
            }
        }
        else
        {
            m_bIsDateAxis = false;
        }
    }
    else
        m_bIsDateAxis=false;
    m_bDirty = false;
}

Sequence< OUString > const & ExplicitCategoriesProvider::getSimpleCategories()
{
    if( !m_bIsExplicitCategoriesInited )
    {
        init();
        m_aExplicitCategories.realloc(0);
        m_bParetoOrder = false;
        if( m_xOriginalCategories.is() )
        {
            if( !hasComplexCategories() )
            {
                uno::Reference< data::XDataSequence > xDataSequence( m_xOriginalCategories->getValues() );
                if( xDataSequence.is() )
                    ExplicitCategoriesProvider::convertCategoryAnysToText( m_aExplicitCategories, xDataSequence->getData(), mrModel );
                // box and whisker: one category per distinct text (like Excel)
                if( m_aExplicitCategories.hasElements() && lcl_isBoxWhisker( m_xCooSysModel.get() ) )
                {
                    sal_Int32 nGroups = 0;
                    std::vector< sal_Int32 > aGroups( groupDuplicateCategories( m_aExplicitCategories, nGroups ) );
                    Sequence< OUString > aDistinct( nGroups );
                    auto pDistinct = aDistinct.getArray();
                    for( sal_Int32 i = 0; i < m_aExplicitCategories.getLength(); ++i )
                        pDistinct[ aGroups[i] ] = m_aExplicitCategories[i];
                    m_aExplicitCategories = std::move( aDistinct );
                }
                // Pareto: one category per distinct text, sorted by the sum of its values
                std::vector< double > aParetoValues;
                if( m_aExplicitCategories.hasElements()
                    && lcl_getParetoValues( m_xCooSysModel.get(), aParetoValues ) )
                {
                    std::vector< double > aSums;
                    const std::vector< sal_Int32 > aSlots(
                        paretoSlots( m_aExplicitCategories, aParetoValues, aSums ) );
                    Sequence< OUString > aSorted( aSums.size() );
                    auto pSorted = aSorted.getArray();
                    for( sal_Int32 i = 0; i < m_aExplicitCategories.getLength(); ++i )
                        pSorted[ aSlots[i] ] = m_aExplicitCategories[i];
                    m_aExplicitCategories = std::move( aSorted );
                    m_bParetoOrder = true;
                }
            }
            else
            {
                m_aExplicitCategories = lcl_getExplicitSimpleCategories(
                    SplitCategoriesProvider_ForLabeledDataSequences( m_aSplitCategoriesList, mrModel ), m_aComplexCats );
            }
        }
        if(!m_aExplicitCategories.hasElements())
            m_aExplicitCategories = DiagramHelper::generateAutomaticCategoriesFromCooSys( m_xCooSysModel.get() );
        m_bIsExplicitCategoriesInited = true;
    }
    return m_aExplicitCategories;
}

const std::vector<ComplexCategory>* ExplicitCategoriesProvider::getCategoriesByLevel( sal_Int32 nLevel )
{
    init();
    sal_Int32 nMaxIndex = m_aComplexCats.size()-1;
    if (nLevel >= 0 && nLevel <= nMaxIndex)
        return &m_aComplexCats[nMaxIndex-nLevel];
    return nullptr;
}

OUString ExplicitCategoriesProvider::getCategoryByIndex(
          const rtl::Reference< BaseCoordinateSystem >& xCooSysModel
        , ChartModel& rModel
        , sal_Int32 nIndex )
{
    if( xCooSysModel.is())
    {
        ExplicitCategoriesProvider aExplicitCategoriesProvider( xCooSysModel, rModel );
        Sequence< OUString > aCategories( aExplicitCategoriesProvider.getSimpleCategories());
        if( nIndex < aCategories.getLength())
            return aCategories[ nIndex ];
    }
    return OUString();
}

bool ExplicitCategoriesProvider::isDateAxis()
{
    init();
    return m_bIsDateAxis;
}

const std::vector< double >&  ExplicitCategoriesProvider::getDateCategories()
{
    init();
    return m_aDateCategories;
}

} //namespace chart

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
