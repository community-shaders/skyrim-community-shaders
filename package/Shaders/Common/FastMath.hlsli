/******************************************************************************
    Shader Fast Math Lib (v0.41)
    A shader math library for optimized approximate transcendental functions.
    Optimized and tested on AMD GCN architecture.
    Release notes:
    v0.41   minor bug fixes, missing references
    
    v0.4    new constants calculated for new ranges, minor optimization and precision improvements
            Developed during production of : Far Cry 4, Ubisoft Montreal
    v0.3    added Newton Raphson 1 and 2 iterations
            Newton Raphson methods provided for reference purpose (some code / architectures might still benefit from single NR).
    v0.2    fast IEEE float math sqrt() rsqrt() rcp()
    v0.1    4th order polynomial approximations for atan() asin() acos()
            Developed during production of : Killzone : Shadow Fall, Guerrilla Games, SCEE
    Ubisoft and Guerrilla Games granted permission for open source distribution.
    Contact information:
    Michal Drobot - @MichalDrobot
    hello@drobot.org
    Presented publicly part of a course: 
    Low Level Optimizations for AMD GCN
    (available @ http://michaldrobot.com/publications/)
********************************************************************************/

/******************************************************************************
    The MIT License (MIT)
    Copyright (c) <2014> <Michal Drobot>
    Permission is hereby granted, free of charge, to any person obtaining a copy
    of this software and associated documentation files (the "Software"), to deal
    in the Software without restriction, including without limitation the rights
    to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
    copies of the Software, and to permit persons to whom the Software is
    furnished to do so, subject to the following conditions:
    The above copyright notice and this permission notice shall be included in
    all copies or substantial portions of the Software.
    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
    AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
    OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
    THE SOFTWARE.
********************************************************************************/

#ifndef SHADER_FAST_MATH_INC_FX
#define SHADER_FAST_MATH_INC_FX

#include "Common/Math.hlsli"

// Define switch for PC compilation
#ifdef _PC
#	define asint(_x) *reinterpret_cast<int*>(&_x);
#	define asfloat(_x) *reinterpret_cast<float*>(&_x);
#	include <math.h>
#endif

// Derived from batch testing
// TODO : Should be improved
#define IEEE_INT_RCP_CONST_NR0 0x7EF311C2
#define IEEE_INT_RCP_CONST_NR1 0x7EF311C3
#define IEEE_INT_RCP_CONST_NR2 0x7EF312AC

// Derived from batch testing
#define IEEE_INT_SQRT_CONST_NR0 0x1FBD1DF5

// Biases for global ranges
// 0-1 or 1-2 specific ranges might improve from different bias
// Derived from batch testing
// TODO : Should be improved
#define IEEE_INT_RCP_SQRT_CONST_NR0 0x5f3759df
#define IEEE_INT_RCP_SQRT_CONST_NR1 0x5F375A86
#define IEEE_INT_RCP_SQRT_CONST_NR2 0x5F375A86

//
// Normalized range [0,1] Constants
//
#define IEEE_INT_RCP_CONST_NR0_SNORM 0x7EEF370B
#define IEEE_INT_SQRT_CONST_NR0_SNORM 0x1FBD1DF5
#define IEEE_INT_RCP_SQRT_CONST_NR0_SNORM 0x5F341A43

//
// Distance [0,1000] based constants
//
//#define IEEE_INT_RCP_CONST_NR0_SNORM        0x7EF3210C
//#define IEEE_INT_SQRT_CONST_NR0_SNORM       0x1FBD22DF
//#define IEEE_INT_RCP_SQRT_CONST_NR0_SNORM   0x5F33E79F

namespace FastMath
{

	//
	// RCP SQRT
	//

	// Approximate guess using integer float arithmetics based on IEEE floating point standard
	float rcpSqrtIEEEIntApproximation(float inX, const int inRcpSqrtConst)
	{
		int x = asint(inX);
		x = inRcpSqrtConst - (x >> 1);
		return asfloat(x);
	}

	float rcpSqrtNewtonRaphson(float inXHalf, float inRcpX)
	{
		return inRcpX * (-inXHalf * (inRcpX * inRcpX) + 1.5f);
	}

	//
	// Using 0 Newton Raphson iterations
	// Relative error : ~3.4% over full
	// Precise format : ~small float
	// 2 ALU
	//
	float fastRcpSqrtNR0(float inX)
	{
		float xRcpSqrt = rcpSqrtIEEEIntApproximation(inX, IEEE_INT_RCP_SQRT_CONST_NR0);
		return xRcpSqrt;
	}

	//
	// Using 1 Newton Raphson iterations
	// Relative error : ~0.2% over full
	// Precise format : ~half float
	// 6 ALU
	//
	float fastRcpSqrtNR1(float inX)
	{
		float xhalf = 0.5f * inX;
		float xRcpSqrt = rcpSqrtIEEEIntApproximation(inX, IEEE_INT_RCP_SQRT_CONST_NR1);
		xRcpSqrt = rcpSqrtNewtonRaphson(xhalf, xRcpSqrt);
		return xRcpSqrt;
	}

	//
	// Using 2 Newton Raphson iterations
	// Relative error : ~4.6e-004%  over full
	// Precise format : ~full float
	// 9 ALU
	//
	float fastRcpSqrtNR2(float inX)
	{
		float xhalf = 0.5f * inX;
		float xRcpSqrt = rcpSqrtIEEEIntApproximation(inX, IEEE_INT_RCP_SQRT_CONST_NR2);
		xRcpSqrt = rcpSqrtNewtonRaphson(xhalf, xRcpSqrt);
		xRcpSqrt = rcpSqrtNewtonRaphson(xhalf, xRcpSqrt);
		return xRcpSqrt;
	}

	//
	// SQRT
	//
	float sqrtIEEEIntApproximation(float inX, const int inSqrtConst)
	{
		int x = asint(inX);
		x = inSqrtConst + (x >> 1);
		return asfloat(x);
	}

	//
	// Using 0 Newton Raphson iterations
	// Relative error : < 0.7% over full
	// Precise format : ~small float
	// 1 ALU
	//
	float fastSqrtNR0(float inX)
	{
		float xRcp = sqrtIEEEIntApproximation(inX, IEEE_INT_SQRT_CONST_NR0);
		return xRcp;
	}

	//
	// Use inverse Rcp Sqrt
	// Using 1 Newton Raphson iterations
	// Relative error : ~0.2% over full
	// Precise format : ~half float
	// 6 ALU
	//
	float fastSqrtNR1(float inX)
	{
		// Inverse Rcp Sqrt
		return inX * fastRcpSqrtNR1(inX);
	}

	//
	// Use inverse Rcp Sqrt
	// Using 2 Newton Raphson iterations
	// Relative error : ~4.6e-004%  over full
	// Precise format : ~full float
	// 9 ALU
	//
	float fastSqrtNR2(float inX)
	{
		// Inverse Rcp Sqrt
		return inX * fastRcpSqrtNR2(inX);
	}

	//
	// RCP
	//

	float rcpIEEEIntApproximation(float inX, const int inRcpConst)
	{
		int x = asint(inX);
		x = inRcpConst - x;
		return asfloat(x);
	}

	float rcpNewtonRaphson(float inX, float inRcpX)
	{
		return inRcpX * (-inRcpX * inX + 2.0f);
	}

	//
	// Using 0 Newton Raphson iterations
	// Relative error : < 0.4% over full
	// Precise format : ~small float
	// 1 ALU
	//
	float fastRcpNR0(float inX)
	{
		float xRcp = rcpIEEEIntApproximation(inX, IEEE_INT_RCP_CONST_NR0);
		return xRcp;
	}

	//
	// Using 1 Newton Raphson iterations
	// Relative error : < 0.02% over full
	// Precise format : ~half float
	// 3 ALU
	//
	float fastRcpNR1(float inX)
	{
		float xRcp = rcpIEEEIntApproximation(inX, IEEE_INT_RCP_CONST_NR1);
		xRcp = rcpNewtonRaphson(inX, xRcp);
		return xRcp;
	}

	//
	// Using 2 Newton Raphson iterations
	// Relative error : < 5.0e-005%  over full
	// Precise format : ~full float
	// 5 ALU
	//
	float fastRcpNR2(float inX)
	{
		float xRcp = rcpIEEEIntApproximation(inX, IEEE_INT_RCP_CONST_NR2);
		xRcp = rcpNewtonRaphson(inX, xRcp);
		xRcp = rcpNewtonRaphson(inX, xRcp);
		return xRcp;
	}

	//
	// Trigonometric functions
	//

	// 4th order polynomial approximation
	// 4 VGRP, 16 ALU Full Rate
	// 7 * 10^-5 radians precision
	// Reference : Handbook of Mathematical Functions (chapter : Elementary Transcendental Functions), M. Abramowitz and I.A. Stegun, Ed.
	float acosFast4(float inX)
	{
		float x1 = abs(inX);
		float x2 = x1 * x1;
		float x3 = x2 * x1;
		float s;

		s = -0.2121144f * x1 + 1.5707288f;
		s = 0.0742610f * x2 + s;
		s = -0.0187293f * x3 + s;
		s = sqrt(1.0f - x1) * s;

		// acos function mirroring
		// check per platform if compiles to a selector - no branch neeeded
		return inX >= 0.0f ? s : Math::PI - s;
	}

	// 4th order polynomial approximation
	// 4 VGRP, 16 ALU Full Rate
	// 7 * 10^-5 radians precision
	float asinFast4(float inX)
	{
		float x = inX;

		// asin is offset of acos
		return Math::HALF_PI - acosFast4(x);
	}

	// 4th order hyperbolical approximation
	// 4 VGRP, 12 ALU Full Rate
	// 7 * 10^-5 radians precision
	// Reference : Efficient approximations for the arctangent function, Rajan, S. Sichun Wang Inkol, R. Joyal, A., May 2006
	float atanFast4(float inX)
	{
		float x = inX;
		return x * (-0.1784f * abs(x) - 0.0663f * x * x + 1.0301f);
	}

	////////////////////////////////////////////////////////////////////////////////////////////////

	// https://seblagarde.wordpress.com/2014/12/01/inverse-trigonometric-functions-gpu-optimization-for-amd-gcn-architecture/

	// max absolute error 9.0x10^-3
	// Eberly's polynomial degree 1 - respect bounds
	// 4 VGPR, 12 FR (8 FR, 1 QR), 1 scalar
	// input [-1, 1] and output [0, PI]
	float ACos(float inX)
	{
		float x = abs(inX);
		float res = -0.156583f * x + Math::HALF_PI;
		res *= fastSqrtNR0(1.0f - x);
		return (inX >= 0) ? res : Math::PI - res;
	}

	// Same cost as Acos + 1 FR
	// Same error
	// input [-1, 1] and output [-PI/2, PI/2]
	float ASin(float x)
	{
		return Math::HALF_PI - ACos(x);
	}

	// max absolute error 1.3x10^-3
	// Eberly's odd polynomial degree 5 - respect bounds
	// 4 VGPR, 14 FR (10 FR, 1 QR), 2 scalar
	// input [0, infinity] and output [0, PI/2]
	float ATanPos(float x)
	{
		float t0 = (x < 1.0f) ? x : 1.0f / x;
		float t1 = t0 * t0;
		float poly = 0.0872929f;
		poly = -0.301895f + poly * t1;
		poly = 1.0f + poly * t1;
		poly = poly * t0;
		return (x < 1.0f) ? poly : Math::HALF_PI - poly;
	}

	// 4 VGPR, 16 FR (12 FR, 1 QR), 2 scalar
	// input [-infinity, infinity] and output [-PI/2, PI/2]
	float ATan(float x)
	{
		float t0 = ATanPos(abs(x));
		return (x < 0.0f) ? -t0 : t0;
	}

}

#endif  //SHADER_FAST_MATH_INC_FX