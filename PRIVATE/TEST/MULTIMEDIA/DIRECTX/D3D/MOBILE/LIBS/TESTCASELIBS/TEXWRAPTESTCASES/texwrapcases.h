//
// Copyright (c) Microsoft Corporation.  All rights reserved.
//
//
// Use of this source code is subject to the terms of the Microsoft shared
// source or premium shared source license agreement under which you licensed
// this source code. If you did not accept the terms of the license agreement,
// you are not authorized to use this source code. For the terms of the license,
// please see the license agreement between you and Microsoft or, if applicable,
// see the SOURCE.RTF on your install media or the root of your tools installation.
// THE SOURCE CODE IS PROVIDED "AS IS", WITH NO WARRANTIES.
//
#pragma once
#include <windows.h>
#include <d3dm.h>
#include "TestCases.h"

#define _M(_a) D3DM_MAKE_D3DMVALUE(_a)

#define D3DMQA_TWTEXWIDTH 64
#define D3DMQA_TWTEXHEIGHT 64

HRESULT CreateAndPrepareTexture(
    LPDIRECT3DMOBILEDEVICE   pDevice, 
    DWORD                    dwTableIndex, 
    LPDIRECT3DMOBILETEXTURE *ppTexture);

HRESULT CreateAndPrepareVertexBuffer(
    LPDIRECT3DMOBILEDEVICE        pDevice, 
    HWND                          hWnd,
    DWORD                         dwTableIndex, 
    LPDIRECT3DMOBILEVERTEXBUFFER *ppVertexBuffer, 
    UINT                         *pVertexBufferStride);

HRESULT SetupTextureStages(
    LPDIRECT3DMOBILEDEVICE pDevice, 
    DWORD                  dwTableIndex);

HRESULT SetupTextureTransformFlag(
    LPDIRECT3DMOBILEDEVICE pDevice, 
    DWORD                  dwTableIndex);

HRESULT SetupRenderState(
    LPDIRECT3DMOBILEDEVICE pDevice, 
    DWORD                  dwTableIndex);


#define FLOAT_DONTCARE 1.0f

//////////////////////////////////////////////
//
// Geometry definitions
//
/////////////////////////////////////////////

#define D3DMQA_NUMVERTS 4
#define D3DMQA_NUMPRIM  2
#define D3DMQA_PRIMTYPE D3DMPT_TRIANGLESTRIP



//    (1)        (2) 
//     +--------+  +
//     |       /  /|
//     |      /  / |
//     |     /  /  |
//     |    /  /   |
//     |   /  /    |
//     |  /  /     |
//     | /  /      | 
//     |/  /       |
//     +  +--------+
//    (3)         (4)
//

//
// These positions are untransformed. They work appropriately with the 
// transformation matrices set to the identity matrix (for simplicity).
//
#define POSX1  -1.0f
#define POSY1  1.0f
#define POSZ1  0.0f

#define POSX2  1.0f
#define POSY2  1.0f
#define POSZ2  0.0f

#define POSX3  -1.0f
#define POSY3  -1.0f
#define POSZ3  0.0f

#define POSX4  1.0f
#define POSY4  -1.0f
#define POSZ4  0.0f

//
// The offset used to clip the primitives.
//
#define POSHALFWIDTH (((POSX2)-(POSX1))/2)
#define POSHALFHEIGHT (((POSY1)-(POSY3))/2)

//
// The minimum and maximum values for the texture coordinates u and v.
// 0.201 is chosen so that there aren't differences caused by float inconsistencies.
//
#define TEXTUREMIN 0.201f
#define TEXTUREMAX 0.8f

#define D3DMQA_WRAP0   (D3DMWRAPCOORD_0)
#define D3DMQA_WRAP1   (D3DMWRAPCOORD_1)
#define D3DMQA_WRAP01  (D3DMWRAPCOORD_0 | D3DMWRAPCOORD_1)

#include "OneDTextures.h"
#include "TwoDTextures.h"

typedef struct _TEXWRAP_TESTS {
    DWORD dwFVF;
    UINT  uiFVFSize;
    PBYTE pVertexData;
    DWORD uiNumVerts;
    DWORD dwWrapCoord;
    BOOL  bProjected;
    UINT  uiTTFStage;
    DWORD dwTTF;
} TEXWRAP_TESTS;


__declspec(selectany) TEXWRAP_TESTS TexWrapCases [D3DMQA_TEXWRAPTEST_COUNT] = {      

/*  0 */      D3DMFVFTEST_ONED01_FVF,   sizeof(D3DMTEXWRAPTEST_ONED),   (PBYTE)TexWrapOneD01, D3DMQA_NUMVERTS,   D3DMQA_WRAP0, FALSE, 0, 0,
/*  1 */      D3DMFVFTEST_ONED01_FVF,   sizeof(D3DMTEXWRAPTEST_ONED),   (PBYTE)TexWrapOneD02, D3DMQA_NUMVERTS,   D3DMQA_WRAP0, FALSE, 0, 0,


/*  2 */      D3DMFVFTEST_TWOD01_FVF,   sizeof(D3DMTEXWRAPTEST_TWOD),   (PBYTE)TexWrapTwoD01, D3DMQA_NUMVERTS,   D3DMQA_WRAP0, FALSE, 0, 0,
/*  3 */      D3DMFVFTEST_TWOD01_FVF,   sizeof(D3DMTEXWRAPTEST_TWOD),   (PBYTE)TexWrapTwoD01, D3DMQA_NUMVERTS,   D3DMQA_WRAP1, FALSE, 0, 0,
/*  4 */      D3DMFVFTEST_TWOD01_FVF,   sizeof(D3DMTEXWRAPTEST_TWOD),   (PBYTE)TexWrapTwoD01, D3DMQA_NUMVERTS,  D3DMQA_WRAP01, FALSE, 0, 0,
    
/*  5 */      D3DMFVFTEST_TWOD01_FVF,   sizeof(D3DMTEXWRAPTEST_TWOD),   (PBYTE)TexWrapTwoD02, D3DMQA_NUMVERTS,   D3DMQA_WRAP0, FALSE, 0, 0,
/*  6 */      D3DMFVFTEST_TWOD01_FVF,   sizeof(D3DMTEXWRAPTEST_TWOD),   (PBYTE)TexWrapTwoD02, D3DMQA_NUMVERTS,   D3DMQA_WRAP1, FALSE, 0, 0,
/*  7 */      D3DMFVFTEST_TWOD01_FVF,   sizeof(D3DMTEXWRAPTEST_TWOD),   (PBYTE)TexWrapTwoD02, D3DMQA_NUMVERTS,  D3DMQA_WRAP01, FALSE, 0, 0,

/*  8 */      D3DMFVFTEST_TWOD01_FVF,   sizeof(D3DMTEXWRAPTEST_TWOD),   (PBYTE)TexWrapTwoD03, D3DMQA_NUMVERTS,   D3DMQA_WRAP0, FALSE, 0, 0,
/*  9 */      D3DMFVFTEST_TWOD01_FVF,   sizeof(D3DMTEXWRAPTEST_TWOD),   (PBYTE)TexWrapTwoD03, D3DMQA_NUMVERTS,   D3DMQA_WRAP1, FALSE, 0, 0,
/* 10 */      D3DMFVFTEST_TWOD01_FVF,   sizeof(D3DMTEXWRAPTEST_TWOD),   (PBYTE)TexWrapTwoD03, D3DMQA_NUMVERTS,  D3DMQA_WRAP01, FALSE, 0, 0,

/* 11 */      D3DMFVFTEST_TWOD01_FVF,   sizeof(D3DMTEXWRAPTEST_TWOD),   (PBYTE)TexWrapTwoD04, D3DMQA_NUMVERTS,   D3DMQA_WRAP0, FALSE, 0, 0,
/* 12 */      D3DMFVFTEST_TWOD01_FVF,   sizeof(D3DMTEXWRAPTEST_TWOD),   (PBYTE)TexWrapTwoD04, D3DMQA_NUMVERTS,   D3DMQA_WRAP1, FALSE, 0, 0,
/* 13 */      D3DMFVFTEST_TWOD01_FVF,   sizeof(D3DMTEXWRAPTEST_TWOD),   (PBYTE)TexWrapTwoD04, D3DMQA_NUMVERTS,  D3DMQA_WRAP01, FALSE, 0, 0,


///* 14 */    D3DMFVFTEST_THREED01_FVF, sizeof(D3DMTEXWRAPTEST_THREED), (PBYTE)TexWrapThreeD01, D3DMQA_NUMVERTS,   D3DMQA_WRAP0,  TRUE, 0, D3DMTTFF_PROJECTED |  D3DMTTFF_COUNT3,
///* 15 */    D3DMFVFTEST_THREED01_FVF, sizeof(D3DMTEXWRAPTEST_THREED), (PBYTE)TexWrapThreeD01, D3DMQA_NUMVERTS,   D3DMQA_WRAP1,  TRUE, 0, D3DMTTFF_PROJECTED |  D3DMTTFF_COUNT3,
///* 17 */    D3DMFVFTEST_THREED01_FVF, sizeof(D3DMTEXWRAPTEST_THREED), (PBYTE)TexWrapThreeD01, D3DMQA_NUMVERTS,  D3DMQA_WRAP01,  TRUE, 0, D3DMTTFF_PROJECTED |  D3DMTTFF_COUNT3,
//    
///* 21 */    D3DMFVFTEST_THREED01_FVF, sizeof(D3DMTEXWRAPTEST_THREED), (PBYTE)TexWrapThreeD02, D3DMQA_NUMVERTS,   D3DMQA_WRAP0,  TRUE, 0, D3DMTTFF_PROJECTED |  D3DMTTFF_COUNT3,
///* 22 */    D3DMFVFTEST_THREED01_FVF, sizeof(D3DMTEXWRAPTEST_THREED), (PBYTE)TexWrapThreeD02, D3DMQA_NUMVERTS,   D3DMQA_WRAP1,  TRUE, 0, D3DMTTFF_PROJECTED |  D3DMTTFF_COUNT3,
///* 23 */    D3DMFVFTEST_THREED01_FVF, sizeof(D3DMTEXWRAPTEST_THREED), (PBYTE)TexWrapThreeD02, D3DMQA_NUMVERTS,   D3DMQA_WRAP2,  TRUE, 0, D3DMTTFF_PROJECTED |  D3DMTTFF_COUNT3,
///* 24 */    D3DMFVFTEST_THREED01_FVF, sizeof(D3DMTEXWRAPTEST_THREED), (PBYTE)TexWrapThreeD02, D3DMQA_NUMVERTS,  D3DMQA_WRAP01,  TRUE, 0, D3DMTTFF_PROJECTED |  D3DMTTFF_COUNT3,
///* 25 */    D3DMFVFTEST_THREED01_FVF, sizeof(D3DMTEXWRAPTEST_THREED), (PBYTE)TexWrapThreeD02, D3DMQA_NUMVERTS,  D3DMQA_WRAP02,  TRUE, 0, D3DMTTFF_PROJECTED |  D3DMTTFF_COUNT3,
///* 26 */    D3DMFVFTEST_THREED01_FVF, sizeof(D3DMTEXWRAPTEST_THREED), (PBYTE)TexWrapThreeD02, D3DMQA_NUMVERTS,  D3DMQA_WRAP12,  TRUE, 0, D3DMTTFF_PROJECTED |  D3DMTTFF_COUNT3,
///* 27 */    D3DMFVFTEST_THREED01_FVF, sizeof(D3DMTEXWRAPTEST_THREED), (PBYTE)TexWrapThreeD02, D3DMQA_NUMVERTS, D3DMQA_WRAP012,  TRUE, 0, D3DMTTFF_PROJECTED |  D3DMTTFF_COUNT3,
//    
///* 28 */    D3DMFVFTEST_THREED01_FVF, sizeof(D3DMTEXWRAPTEST_THREED), (PBYTE)TexWrapThreeD03, D3DMQA_NUMVERTS,   D3DMQA_WRAP0,  TRUE, 0, D3DMTTFF_PROJECTED |  D3DMTTFF_COUNT3,
///* 29 */    D3DMFVFTEST_THREED01_FVF, sizeof(D3DMTEXWRAPTEST_THREED), (PBYTE)TexWrapThreeD03, D3DMQA_NUMVERTS,   D3DMQA_WRAP1,  TRUE, 0, D3DMTTFF_PROJECTED |  D3DMTTFF_COUNT3,
///* 30 */    D3DMFVFTEST_THREED01_FVF, sizeof(D3DMTEXWRAPTEST_THREED), (PBYTE)TexWrapThreeD03, D3DMQA_NUMVERTS,   D3DMQA_WRAP2,  TRUE, 0, D3DMTTFF_PROJECTED |  D3DMTTFF_COUNT3,
///* 31 */    D3DMFVFTEST_THREED01_FVF, sizeof(D3DMTEXWRAPTEST_THREED), (PBYTE)TexWrapThreeD03, D3DMQA_NUMVERTS,  D3DMQA_WRAP01,  TRUE, 0, D3DMTTFF_PROJECTED |  D3DMTTFF_COUNT3,
///* 32 */    D3DMFVFTEST_THREED01_FVF, sizeof(D3DMTEXWRAPTEST_THREED), (PBYTE)TexWrapThreeD03, D3DMQA_NUMVERTS,  D3DMQA_WRAP02,  TRUE, 0, D3DMTTFF_PROJECTED |  D3DMTTFF_COUNT3,
///* 33 */    D3DMFVFTEST_THREED01_FVF, sizeof(D3DMTEXWRAPTEST_THREED), (PBYTE)TexWrapThreeD03, D3DMQA_NUMVERTS,  D3DMQA_WRAP12,  TRUE, 0, D3DMTTFF_PROJECTED |  D3DMTTFF_COUNT3,
///* 34 */    D3DMFVFTEST_THREED01_FVF, sizeof(D3DMTEXWRAPTEST_THREED), (PBYTE)TexWrapThreeD03, D3DMQA_NUMVERTS, D3DMQA_WRAP012,  TRUE, 0, D3DMTTFF_PROJECTED |  D3DMTTFF_COUNT3,
//   
///* 35 */    D3DMFVFTEST_THREED01_FVF, sizeof(D3DMTEXWRAPTEST_THREED), (PBYTE)TexWrapThreeD04, D3DMQA_NUMVERTS,   D3DMQA_WRAP0,  TRUE, 0, D3DMTTFF_PROJECTED |  D3DMTTFF_COUNT3,
///* 36 */    D3DMFVFTEST_THREED01_FVF, sizeof(D3DMTEXWRAPTEST_THREED), (PBYTE)TexWrapThreeD04, D3DMQA_NUMVERTS,   D3DMQA_WRAP1,  TRUE, 0, D3DMTTFF_PROJECTED |  D3DMTTFF_COUNT3,
///* 37 */    D3DMFVFTEST_THREED01_FVF, sizeof(D3DMTEXWRAPTEST_THREED), (PBYTE)TexWrapThreeD04, D3DMQA_NUMVERTS,   D3DMQA_WRAP2,  TRUE, 0, D3DMTTFF_PROJECTED |  D3DMTTFF_COUNT3,
///* 38 */    D3DMFVFTEST_THREED01_FVF, sizeof(D3DMTEXWRAPTEST_THREED), (PBYTE)TexWrapThreeD04, D3DMQA_NUMVERTS,  D3DMQA_WRAP01,  TRUE, 0, D3DMTTFF_PROJECTED |  D3DMTTFF_COUNT3,
///* 39 */    D3DMFVFTEST_THREED01_FVF, sizeof(D3DMTEXWRAPTEST_THREED), (PBYTE)TexWrapThreeD04, D3DMQA_NUMVERTS,  D3DMQA_WRAP02,  TRUE, 0, D3DMTTFF_PROJECTED |  D3DMTTFF_COUNT3,
///* 40 */    D3DMFVFTEST_THREED01_FVF, sizeof(D3DMTEXWRAPTEST_THREED), (PBYTE)TexWrapThreeD04, D3DMQA_NUMVERTS,  D3DMQA_WRAP12,  TRUE, 0, D3DMTTFF_PROJECTED |  D3DMTTFF_COUNT3,
///* 41 */    D3DMFVFTEST_THREED01_FVF, sizeof(D3DMTEXWRAPTEST_THREED), (PBYTE)TexWrapThreeD04, D3DMQA_NUMVERTS, D3DMQA_WRAP012,  TRUE, 0, D3DMTTFF_PROJECTED |  D3DMTTFF_COUNT3,
};


