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

#include "bessel.hxx"
#include <cmath>
#include <numbers>
#include <rtl/math.hxx>

#include <com/sun/star/lang/IllegalArgumentException.hpp>
#include <com/sun/star/sheet/NoConvergenceException.hpp>

using ::com::sun::star::lang::IllegalArgumentException;
using ::com::sun::star::sheet::NoConvergenceException;

namespace sca::analysis {

// BESSEL J


/*  The BESSEL function, first kind, unmodified:
    The algorithm follows
    http://www.reference-global.com/isbn/978-3-11-020354-7
    Numerical Mathematics 1 / Numerische Mathematik 1,
    An algorithm-based introduction / Eine algorithmisch orientierte Einfuehrung
    Deuflhard, Peter; Hohmann, Andreas
    Berlin, New York (Walter de Gruyter) 2008
    4. ueberarb. u. erw. Aufl. 2008
    eBook ISBN: 978-3-11-020355-4
    Chapter 6.3.2 , algorithm 6.24
    The source is in German.
    The BesselJ-function is a special case of the adjoint summation with
    a_k = 2*(k-1)/x for k=1,...
    b_k = -1, for all k, directly substituted
    m_0=1, m_k=2 for k even, and m_k=0 for k odd, calculated on the fly
    alpha_k=1 for k=N and alpha_k=0 otherwise
*/

double BesselJ( double x, sal_Int32 N )

{
    if( N < 0 )
        throw IllegalArgumentException();
    if (x==0.0)
        return (N==0) ? 1.0 : 0.0;

    /*  The algorithm works only for x>0, therefore remember sign. BesselJ
        with integer order N is an even function for even N (means J(-x)=J(x))
        and an odd function for odd N (means J(-x)=-J(x)).*/
    double fSign = (N % 2 == 1 && x < 0) ? -1.0 : 1.0;
    double fX = fabs(x);

    const double fMaxIteration = 9000000.0; //experimental, for to return in < 3 seconds
    double fEstimateIteration = fX * 1.5 + N;
    bool bAsymptoticPossible = pow(fX,0.4) > N;
    if (fEstimateIteration > fMaxIteration)
    {
        if (!bAsymptoticPossible)
            throw NoConvergenceException();
        return fSign * sqrt(M_2_PI/fX)* cos(fX-N*M_PI_2-M_PI_4);
    }

    double const epsilon = 1.0e-15; // relative error
    bool bHasfound = false;
    double k= 0.0;
    // e_{-1} = 0; e_0 = alpha_0 / b_2
    double  u ; // u_0 = e_0/f_0 = alpha_0/m_0 = alpha_0

    // first used with k=1
    double m_bar;         // m_bar_k = m_k * f_bar_{k-1}
    double g_bar;         // g_bar_k = m_bar_k - a_{k+1} + g_{k-1}
    double g_bar_delta_u; // g_bar_delta_u_k = f_bar_{k-1} * alpha_k
                          // - g_{k-1} * delta_u_{k-1} - m_bar_k * u_{k-1}
    // f_{-1} = 0.0; f_0 = m_0 / b_2 = 1/(-1) = -1
    double g = 0.0;       // g_0= f_{-1} / f_0 = 0/(-1) = 0
    double delta_u = 0.0; // dummy initialize, first used with * 0
    double f_bar = -1.0;  // f_bar_k = 1/f_k, but only used for k=0

    if (N==0)
    {
        //k=0; alpha_0 = 1.0
        u = 1.0; // u_0 = alpha_0
        // k = 1.0; at least one step is necessary
        // m_bar_k = m_k * f_bar_{k-1} ==> m_bar_1 = 0.0
        g_bar_delta_u = 0.0;    // alpha_k = 0.0, m_bar = 0.0; g= 0.0
        g_bar = - 2.0/fX;       // k = 1.0, g = 0.0
        delta_u = g_bar_delta_u / g_bar;
        u = u + delta_u ;       // u_k = u_{k-1} + delta_u_k
        g = -1.0 / g_bar;       // g_k=b_{k+2}/g_bar_k
        f_bar = f_bar * g;      // f_bar_k = f_bar_{k-1}* g_k
        k = 2.0;
        // From now on all alpha_k = 0.0 and k > N+1
    }
    else
    {   // N >= 1 and alpha_k = 0.0 for k<N
        u=0.0; // u_0 = alpha_0
        for (k =1.0; k<= N-1; k = k + 1.0)
        {
            m_bar=2.0 * fmod(k-1.0, 2.0) * f_bar;
            g_bar_delta_u = - g * delta_u - m_bar * u; // alpha_k = 0.0
            g_bar = m_bar - 2.0*k/fX + g;
            delta_u = g_bar_delta_u / g_bar;
            u = u + delta_u;
            g = -1.0/g_bar;
            f_bar=f_bar * g;
        }
        // Step alpha_N = 1.0
        m_bar=2.0 * fmod(k-1.0, 2.0) * f_bar;
        g_bar_delta_u = f_bar - g * delta_u - m_bar * u; // alpha_k = 1.0
        g_bar = m_bar - 2.0*k/fX + g;
        delta_u = g_bar_delta_u / g_bar;
        u = u + delta_u;
        g = -1.0/g_bar;
        f_bar = f_bar * g;
        k = k + 1.0;
    }
    // Loop until desired accuracy, always alpha_k = 0.0
    do
    {
        m_bar = 2.0 * fmod(k-1.0, 2.0) * f_bar;
        g_bar_delta_u = - g * delta_u - m_bar * u;
        g_bar = m_bar - 2.0*k/fX + g;
        delta_u = g_bar_delta_u / g_bar;
        u = u + delta_u;
        g = -1.0/g_bar;
        f_bar = f_bar * g;
        bHasfound = (fabs(delta_u)<=fabs(u)*epsilon);
        k = k + 1.0;
    }
    while (!bHasfound && k <= fMaxIteration);
    if (!bHasfound)
        throw NoConvergenceException(); // unlikely to happen

    return u * fSign;
}


// BESSEL I


/*  The BESSEL function, first kind, modified:

                     inf                                  (x/2)^(n+2k)
        I_n(x)  =  SUM   TERM(n,k)   with   TERM(n,k) := --------------
                     k=0                                   k! (n+k)!

    No asymptotic approximation used, see issue 43040.
 */

double BesselI( double x, sal_Int32 n )
{
    const sal_Int32 nMaxIteration = 2000;
    const double fXHalf = x / 2.0;
    if( n < 0 )
        throw IllegalArgumentException();

    double fResult = 0.0;

    /*  Start the iteration without TERM(n,0), which is set here.

            TERM(n,0) = (x/2)^n / n!
     */
    sal_Int32 nK = 0;
    double fTerm = 1.0;
    // avoid overflow in Fak(n)
    for( nK = 1; nK <= n; ++nK )
    {
        fTerm = fTerm / static_cast< double >( nK ) * fXHalf;
    }
    fResult = fTerm;    // Start result with TERM(n,0).
    if( fTerm != 0.0 )
    {
        nK = 1;
        const double fEpsilon = 1.0E-15;
        do
        {
            /*  Calculation of TERM(n,k) from TERM(n,k-1):

                                   (x/2)^(n+2k)
                    TERM(n,k)  =  --------------
                                    k! (n+k)!

                                   (x/2)^2 (x/2)^(n+2(k-1))
                               =  --------------------------
                                   k (k-1)! (n+k) (n+k-1)!

                                   (x/2)^2     (x/2)^(n+2(k-1))
                               =  --------- * ------------------
                                   k(n+k)      (k-1)! (n+k-1)!

                                   x^2/4
                               =  -------- TERM(n,k-1)
                                   k(n+k)
            */
            fTerm = fTerm * fXHalf / static_cast<double>(nK) * fXHalf / static_cast<double>(nK+n);
            fResult += fTerm;
            nK++;
        }
        while( (fabs( fTerm ) > fabs(fResult) * fEpsilon) && (nK < nMaxIteration) );

    }
    return fResult;
}

/// @throws IllegalArgumentException
/// @throws NoConvergenceException
static double Besselk0( double fNum )
{
    double  fRet;

    if( fNum <= 2.0 )
    {
        double  fNum2 = fNum * 0.5;
        double  y = fNum2 * fNum2;

        fRet = -log( fNum2 ) * BesselI( fNum, 0 ) +
                ( -0.57721566 + y * ( 0.42278420 + y * ( 0.23069756 + y * ( 0.3488590e-1 +
                    y * ( 0.262698e-2 + y * ( 0.10750e-3 + y * 0.74e-5 ) ) ) ) ) );
    }
    else
    {
        double  y = 2.0 / fNum;

        fRet = exp( -fNum ) / sqrt( fNum ) * ( 1.25331414 + y * ( -0.7832358e-1 +
                y * ( 0.2189568e-1 + y * ( -0.1062446e-1 + y * ( 0.587872e-2 +
                y * ( -0.251540e-2 + y * 0.53208e-3 ) ) ) ) ) );
    }

    return fRet;
}

/// @throws IllegalArgumentException
/// @throws NoConvergenceException
static double Besselk1( double fNum )
{
    double  fRet;

    if( fNum <= 2.0 )
    {
        double  fNum2 = fNum * 0.5;
        double  y = fNum2 * fNum2;

        fRet = log( fNum2 ) * BesselI( fNum, 1 ) +
                ( 1.0 + y * ( 0.15443144 + y * ( -0.67278579 + y * ( -0.18156897 + y * ( -0.1919402e-1 +
                    y * ( -0.110404e-2 + y * -0.4686e-4 ) ) ) ) ) )
                / fNum;
    }
    else
    {
        double  y = 2.0 / fNum;

        fRet = exp( -fNum ) / sqrt( fNum ) * ( 1.25331414 + y * ( 0.23498619 +
                y * ( -0.3655620e-1 + y * ( 0.1504268e-1 + y * ( -0.780353e-2 +
                y * ( 0.325614e-2 + y * -0.68245e-3 ) ) ) ) ) );
    }

    return fRet;
}


double BesselK( double fNum, sal_Int32 nOrder )
{
    switch( nOrder )
    {
        case 0:     return Besselk0( fNum );
        case 1:     return Besselk1( fNum );
        default:
        {
            double      fTox = 2.0 / fNum;
            double      fBkm = Besselk0( fNum );
            double      fBk = Besselk1( fNum );

            for( sal_Int32 n = 1 ; n < nOrder ; n++ )
            {
                const double fBkp = fBkm + double( n ) * fTox * fBk;
                fBkm = fBk;
                fBk = fBkp;
            }

            return fBk;
        }
    }
}


// BESSEL Y


/*  The BESSEL function, second kind, unmodified:
    The algorithm for order 0 and for order 1 follows
    http://www.reference-global.com/isbn/978-3-11-020354-7
    Numerical Mathematics 1 / Numerische Mathematik 1,
    An algorithm-based introduction / Eine algorithmisch orientierte Einfuehrung
    Deuflhard, Peter; Hohmann, Andreas
    Berlin, New York (Walter de Gruyter) 2008
    4. ueberarb. u. erw. Aufl. 2008
    eBook ISBN: 978-3-11-020355-4
    Chapter 6.3.2 , algorithm 6.24
    The source is in German.
    See #i31656# for a commented version of the implementation, attachment #desc6
    https://bz.apache.org/ooo/attachment.cgi?id=63609
*/

/// @throws IllegalArgumentException
/// @throws NoConvergenceException
static double Bessely0( double fX )
{
    // If fX > 2^64 then sin and cos fail
    if (fX <= 0 || !rtl::math::isValidArcArg(fX))
        throw IllegalArgumentException();
    const double fMaxIteration = 9000000.0; // should not be reached
    if (fX > 5.0e+6) // iteration is not considerably better than approximation
        return sqrt(1/M_PI/fX)
                *(std::sin(fX)-std::cos(fX));
    const double epsilon = 1.0e-15;
    double alpha = log(fX/2.0)+std::numbers::egamma;
    double u = alpha;

    double k = 1.0;
    double g_bar_delta_u = 0.0;
    double g_bar = -2.0 / fX;
    double delta_u = g_bar_delta_u / g_bar;
    double g = -1.0/g_bar;
    double f_bar = -1 * g;

    double sign_alpha = 1.0;
    bool bHasFound = false;
    k = k + 1;
    do
    {
        double km1mod2 = fmod(k-1.0, 2.0);
        double m_bar = (2.0*km1mod2) * f_bar;
        if (km1mod2 == 0.0)
            alpha = 0.0;
        else
        {
           alpha = sign_alpha * (4.0/k);
           sign_alpha = -sign_alpha;
        }
        g_bar_delta_u = f_bar * alpha - g * delta_u - m_bar * u;
        g_bar = m_bar - (2.0*k)/fX + g;
        delta_u = g_bar_delta_u / g_bar;
        u = u+delta_u;
        g = -1.0 / g_bar;
        f_bar = f_bar*g;
        bHasFound = (fabs(delta_u)<=fabs(u)*epsilon);
        k=k+1;
    }
    while (!bHasFound && k<fMaxIteration);
    if (!bHasFound)
        throw NoConvergenceException(); // not likely to happen
    return u*M_2_PI;
}

// See #i31656# for a commented version of this implementation, attachment #desc6
// https://bz.apache.org/ooo/attachment.cgi?id=63609
/// @throws IllegalArgumentException
/// @throws NoConvergenceException
static double Bessely1( double fX )
{
    // If fX > 2^64 then sin and cos fail
    if (fX <= 0 || !rtl::math::isValidArcArg(fX))
        throw IllegalArgumentException();
    const double fMaxIteration = 9000000.0; // should not be reached
    if (fX > 5.0e+6) // iteration is not considerably better than approximation
        return - sqrt(1/M_PI/fX)
                *(std::sin(fX)+std::cos(fX));
    const double epsilon = 1.0e-15;
    double alpha = 1.0/fX;
    double f_bar = -1.0;
    double u = alpha;
    double k = 1.0;
    alpha = 1.0 - std::numbers::egamma - log(fX/2.0);
    double g_bar_delta_u = -alpha;
    double g_bar = -2.0 / fX;
    double delta_u = g_bar_delta_u / g_bar;
    u = u + delta_u;
    double g = -1.0/g_bar;
    f_bar = f_bar * g;
    double sign_alpha = -1.0;
    bool bHasFound = false;
    k = k + 1.0;
    do
    {
        double km1mod2 = fmod(k-1.0,2.0);
        double m_bar = (2.0*km1mod2) * f_bar;
        double q = (k-1.0)/2.0;
        if (km1mod2 == 0.0) // k is odd
        {
           alpha = sign_alpha * (1.0/q + 1.0/(q+1.0));
           sign_alpha = -sign_alpha;
        }
        else
            alpha = 0.0;
        g_bar_delta_u = f_bar * alpha - g * delta_u - m_bar * u;
        g_bar = m_bar - (2.0*k)/fX + g;
        delta_u = g_bar_delta_u / g_bar;
        u = u+delta_u;
        g = -1.0 / g_bar;
        f_bar = f_bar*g;
        bHasFound = (fabs(delta_u)<=fabs(u)*epsilon);
        k=k+1;
    }
    while (!bHasFound && k<fMaxIteration);
    if (!bHasFound)
        throw NoConvergenceException();
    return -u*2.0/M_PI;
}

double BesselY( double fNum, sal_Int32 nOrder )
{
    switch( nOrder )
    {
        case 0:     return Bessely0( fNum );
        case 1:     return Bessely1( fNum );
        default:
        {
            double      fTox = 2.0 / fNum;
            double      fBym = Bessely0( fNum );
            double      fBy = Bessely1( fNum );

            for( sal_Int32 n = 1 ; n < nOrder ; n++ )
            {
                const double fByp = double( n ) * fTox * fBy - fBym;
                fBym = fBy;
                fBy = fByp;
            }

            return fBy;
        }
    }
}

// weboffice: Excel's BESSELJ/Y/I/K are the rational and polynomial approximations with Miller's
// recurrence (as in Numerical Recipes); its results differ from the exact values in the 8th or
// 9th digit (BESSELJ(0;0) is 1.0000000028), and these give Excel's values to the last digit.
namespace {

const double fExcelBesselAcc = 40.0;
const double fExcelBesselBig = 1.0e10;
const double fExcelBesselBigInv = 1.0e-10;

double ExcelBesselJ0( double x )
{
    const double ax = std::fabs( x );
    if (ax < 8.0)
    {
        const double y = x * x;
        const double a1 = 57568490574.0 + y * (-13362590354.0 + y * (651619640.7 + y * (-11214424.18 + y * (77392.33017 + y * (-184.9052456)))));
        const double a2 = 57568490411.0 + y * (1029532985.0 + y * (9494680.718 + y * (59272.64853 + y * (267.8532712 + y * 1.0))));
        return a1 / a2;
    }
    const double z = 8.0 / ax, y = z * z, xx = ax - 0.785398164;
    const double a1 = 1.0 + y * (-0.1098628627e-2 + y * (0.2734510407e-4 + y * (-0.2073370639e-5 + y * 0.2093887211e-6)));
    const double a2 = -0.1562499995e-1 + y * (0.1430488765e-3 + y * (-0.6911147651e-5 + y * (0.7621095161e-6 - y * 0.934935152e-7)));
    return std::sqrt( 0.636619772 / ax ) * (std::cos( xx ) * a1 - z * std::sin( xx ) * a2);
}

double ExcelBesselJ1( double x )
{
    const double ax = std::fabs( x );
    if (ax < 8.0)
    {
        const double y = x * x;
        const double a1 = x * (72362614232.0 + y * (-7895059235.0 + y * (242396853.1 + y * (-2972611.439 + y * (15704.48260 + y * (-30.16036606))))));
        const double a2 = 144725228442.0 + y * (2300535178.0 + y * (18583304.74 + y * (99447.43394 + y * (376.9991397 + y * 1.0))));
        return a1 / a2;
    }
    const double z = 8.0 / ax, y = z * z, xx = ax - 2.356194491;
    const double a1 = 1.0 + y * (0.183105e-2 + y * (-0.3516396496e-4 + y * (0.2457520174e-5 + y * (-0.240337019e-6))));
    const double a2 = 0.04687499995 + y * (-0.2002690873e-3 + y * (0.8449199096e-5 + y * (-0.88228987e-6 + y * 0.105787412e-6)));
    const double ans = std::sqrt( 0.636619772 / ax ) * (std::cos( xx ) * a1 - z * std::sin( xx ) * a2);
    return x < 0.0 ? -ans : ans;
}

double ExcelBesselY0( double x )
{
    if (x < 8.0)
    {
        const double y = x * x;
        const double a1 = -2957821389.0 + y * (7062834065.0 + y * (-512359803.6 + y * (10879881.29 + y * (-86327.92757 + y * 228.4622733))));
        const double a2 = 40076544269.0 + y * (745249964.8 + y * (7189466.438 + y * (47447.26470 + y * (226.1030244 + y * 1.0))));
        return a1 / a2 + 0.636619772 * ExcelBesselJ0( x ) * std::log( x );
    }
    const double z = 8.0 / x, y = z * z, xx = x - 0.785398164;
    const double a1 = 1.0 + y * (-0.1098628627e-2 + y * (0.2734510407e-4 + y * (-0.2073370639e-5 + y * 0.2093887211e-6)));
    const double a2 = -0.1562499995e-1 + y * (0.1430488765e-3 + y * (-0.6911147651e-5 + y * (0.7621095161e-6 + y * (-0.934945152e-7))));
    return std::sqrt( 0.636619772 / x ) * (std::sin( xx ) * a1 + z * std::cos( xx ) * a2);
}

double ExcelBesselY1( double x )
{
    if (x < 8.0)
    {
        const double y = x * x;
        const double a1 = x * (-0.4900604943e13 + y * (0.1275274390e13 + y * (-0.5153438139e11 + y * (0.7349264551e9 + y * (-0.4237922726e7 + y * 0.8511937935e4)))));
        const double a2 = 0.2499580570e14 + y * (0.4244419664e12 + y * (0.3733650367e10 + y * (0.2245904002e8 + y * (0.1020426050e6 + y * (0.3549632885e3 + y)))));
        return a1 / a2 + 0.636619772 * (ExcelBesselJ1( x ) * std::log( x ) - 1.0 / x);
    }
    const double z = 8.0 / x, y = z * z, xx = x - 2.356194491;
    const double a1 = 1.0 + y * (0.183105e-2 + y * (-0.3516396496e-4 + y * (0.2457520174e-5 + y * (-0.240337019e-6))));
    const double a2 = 0.04687499995 + y * (-0.2002690873e-3 + y * (0.8449199096e-5 + y * (-0.88228987e-6 + y * 0.105787412e-6)));
    return std::sqrt( 0.636619772 / x ) * (std::sin( xx ) * a1 + z * std::cos( xx ) * a2);
}

double ExcelBesselI0( double x )
{
    const double ax = std::fabs( x );
    if (ax < 3.75)
    {
        double y = x / 3.75;
        y *= y;
        return 1.0 + y * (3.5156229 + y * (3.0899424 + y * (1.2067492 + y * (0.2659732 + y * (0.360768e-1 + y * 0.45813e-2)))));
    }
    const double y = 3.75 / ax;
    return (std::exp( ax ) / std::sqrt( ax )) * (0.39894228 + y * (0.1328592e-1 + y * (0.225319e-2 + y * (-0.157565e-2 + y * (0.916281e-2 + y * (-0.2057706e-1 + y * (0.2635537e-1 + y * (-0.1647633e-1 + y * 0.392377e-2))))))));
}

double ExcelBesselI1( double x )
{
    const double ax = std::fabs( x );
    double ans;
    if (ax < 3.75)
    {
        double y = x / 3.75;
        y *= y;
        ans = ax * (0.5 + y * (0.87890594 + y * (0.51498869 + y * (0.15084934 + y * (0.2658733e-1 + y * (0.301532e-2 + y * 0.32411e-3))))));
    }
    else
    {
        const double y = 3.75 / ax;
        ans = 0.2282967e-1 + y * (-0.2895312e-1 + y * (0.1787654e-1 - y * 0.420059e-2));
        ans = 0.39894228 + y * (-0.3988024e-1 + y * (-0.362018e-2 + y * (0.163801e-2 + y * (-0.1031555e-1 + y * ans))));
        ans *= std::exp( ax ) / std::sqrt( ax );
    }
    return x < 0.0 ? -ans : ans;
}

double ExcelBesselK0( double x )
{
    if (x <= 2.0)
    {
        const double y = x * x / 4.0;
        return (-std::log( x / 2.0 ) * ExcelBesselI0( x )) + (-0.57721566 + y * (0.42278420 + y * (0.23069756 + y * (0.3488590e-1 + y * (0.262698e-2 + y * (0.10750e-3 + y * 0.74e-5))))));
    }
    const double y = 2.0 / x;
    return (std::exp( -x ) / std::sqrt( x )) * (1.25331414 + y * (-0.7832358e-1 + y * (0.2189568e-1 + y * (-0.1062446e-1 + y * (0.587872e-2 + y * (-0.251540e-2 + y * 0.53208e-3))))));
}

double ExcelBesselK1( double x )
{
    if (x <= 2.0)
    {
        const double y = x * x / 4.0;
        return (std::log( x / 2.0 ) * ExcelBesselI1( x )) + (1.0 / x) * (1.0 + y * (0.15443144 + y * (-0.67278579 + y * (-0.18156897 + y * (-0.1919402e-1 + y * (-0.110404e-2 + y * (-0.4686e-4)))))));
    }
    const double y = 2.0 / x;
    return (std::exp( -x ) / std::sqrt( x )) * (1.25331414 + y * (0.23498619 + y * (-0.3655620e-1 + y * (0.1504268e-1 + y * (-0.780353e-2 + y * (0.325614e-2 + y * (-0.68245e-3)))))));
}

}

double ExcelBesselJ( double x, sal_Int32 n )
{
    if (n < 0)
        throw IllegalArgumentException();
    if (n == 0)
        return ExcelBesselJ0( x );
    if (n == 1)
        return ExcelBesselJ1( x );
    const double ax = std::fabs( x );
    if (ax == 0.0)
        return 0.0;
    double ans;
    if (ax > static_cast<double>(n))
    {
        const double tox = 2.0 / ax;
        double bjm = ExcelBesselJ0( ax ), bj = ExcelBesselJ1( ax );
        for (sal_Int32 j = 1; j < n; ++j)
        {
            const double bjp = j * tox * bj - bjm;
            bjm = bj;
            bj = bjp;
        }
        ans = bj;
    }
    else
    {
        const double tox = 2.0 / ax;
        const sal_Int32 m = 2 * ((n + static_cast<sal_Int32>(std::sqrt( fExcelBesselAcc * n ))) / 2);
        bool bSum = false;
        double bjp = 0.0, sum = 0.0, bj = 1.0;
        ans = 0.0;
        for (sal_Int32 j = m; j > 0; --j)
        {
            const double bjm = j * tox * bj - bjp;
            bjp = bj;
            bj = bjm;
            if (std::fabs( bj ) > fExcelBesselBig)
            {
                bj *= fExcelBesselBigInv;
                bjp *= fExcelBesselBigInv;
                ans *= fExcelBesselBigInv;
                sum *= fExcelBesselBigInv;
            }
            if (bSum)
                sum += bj;
            bSum = !bSum;
            if (j == n)
                ans = bjp;
        }
        sum = 2.0 * sum - bj;
        ans /= sum;
    }
    return (x < 0.0 && (n & 1)) ? -ans : ans;
}

double ExcelBesselI( double x, sal_Int32 n )
{
    if (n < 0)
        throw IllegalArgumentException();
    if (n == 0)
        return ExcelBesselI0( x );
    if (n == 1)
        return ExcelBesselI1( x );
    if (x == 0.0)
        return 0.0;
    const double tox = 2.0 / std::fabs( x );
    double bip = 0.0, ans = 0.0, bi = 1.0;
    for (sal_Int32 j = 2 * (n + static_cast<sal_Int32>(std::sqrt( fExcelBesselAcc * n ))); j > 0; --j)
    {
        const double bim = bip + j * tox * bi;
        bip = bi;
        bi = bim;
        if (std::fabs( bi ) > fExcelBesselBig)
        {
            ans *= fExcelBesselBigInv;
            bi *= fExcelBesselBigInv;
            bip *= fExcelBesselBigInv;
        }
        if (j == n)
            ans = bip;
    }
    ans *= ExcelBesselI0( x ) / bi;
    return (x < 0.0 && (n & 1)) ? -ans : ans;
}

double ExcelBesselK( double x, sal_Int32 n )
{
    if (n == 0)
        return ExcelBesselK0( x );
    if (n == 1)
        return ExcelBesselK1( x );
    const double tox = 2.0 / x;
    double bkm = ExcelBesselK0( x ), bk = ExcelBesselK1( x );
    for (sal_Int32 j = 1; j < n; ++j)
    {
        const double bkp = bkm + j * tox * bk;
        bkm = bk;
        bk = bkp;
    }
    return bk;
}

double ExcelBesselY( double x, sal_Int32 n )
{
    if (n == 0)
        return ExcelBesselY0( x );
    if (n == 1)
        return ExcelBesselY1( x );
    const double tox = 2.0 / x;
    double bym = ExcelBesselY0( x ), by = ExcelBesselY1( x );
    for (sal_Int32 j = 1; j < n; ++j)
    {
        const double byp = j * tox * by - bym;
        bym = by;
        by = byp;
    }
    return by;
}

} // namespace sca::analysis

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
