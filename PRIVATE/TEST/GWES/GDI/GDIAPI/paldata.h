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
static RGBQUAD NaturalPal[256] = {

    { 0x00, 0x00, 0x00, 0 },    /*   0 */	// Black
    { 0x00, 0x00, 0x80, 0 },    /*   1 */	// Dark Red    
    { 0x00, 0x80, 0x00, 0 },    /*   2 */	// Dark Green  
    { 0x00, 0x80, 0x80, 0 },    /*   3 */	// Dark Yellow 
    { 0x80, 0x00, 0x00, 0 },    /*   4 */	// Dark Blue   
    { 0x80, 0x00, 0x80, 0 },    /*   5 */	// Dark Magenta
    { 0x80, 0x80, 0x00, 0 },    /*   6 */	// Dark Cyan   
    { 0xc0, 0xc0, 0xc0, 0 },    /*   7 */	// Light Grey  
    { 0xc0, 0xdc, 0xc0, 0 },    /*   8 */	// system color
    { 0xf0, 0xca, 0xa6, 0 },    /*   9 */	// system color
    { 0x04, 0x04, 0x04, 0 },    /*  10 */
    { 0xf0, 0xf0, 0xc6, 0 },    /*  11 */
    { 0x00, 0x07, 0x00, 0 },    /*  12 */
    { 0x00, 0x00, 0x0d, 0 },    /*  13 */
    { 0x24, 0x00, 0x00, 0 },    /*  14 */
    { 0xc2, 0x1d, 0x20, 0 },    /*  15 */
    { 0x08, 0x08, 0x08, 0 },    /*  16 */
    { 0x00, 0x0e, 0x00, 0 },    /*  17 */
    { 0x00, 0x00, 0x1b, 0 },    /*  18 */
    { 0x49, 0x00, 0x00, 0 },    /*  19 */
    { 0x0c, 0x0c, 0x0c, 0 },    /*  20 */
    { 0x00, 0x14, 0x00, 0 },    /*  21 */
    { 0x00, 0x00, 0x28, 0 },    /*  22 */
    { 0xff, 0xe7, 0xff, 0 },    /*  23 */
    { 0x2b, 0x09, 0x08, 0 },    /*  24 */
    { 0x3f, 0x4e, 0x8d, 0 },    /*  25 */
    { 0x11, 0x11, 0x11, 0 },    /*  26 */
    { 0x00, 0x1d, 0x00, 0 },    /*  27 */
    { 0x00, 0x00, 0x39, 0 },    /*  28 */
    { 0x9b, 0x00, 0x00, 0 },    /*  29 */
    { 0x2c, 0x0e, 0x0e, 0 },    /*  30 */
    { 0x16, 0x16, 0x16, 0 },    /*  31 */
    { 0x00, 0x25, 0x00, 0 },    /*  32 */
    { 0x00, 0x00, 0x49, 0 },    /*  33 */
    { 0xff, 0xff, 0xd0, 0 },    /*  34 */
    { 0x3b, 0x11, 0x11, 0 },    /*  35 */
    { 0x1c, 0x1c, 0x1c, 0 },    /*  36 */
    { 0x00, 0x2f, 0x00, 0 },    /*  37 */
    { 0x00, 0x00, 0x5d, 0 },    /*  38 */
    { 0x45, 0x17, 0x17, 0 },    /*  39 */
    { 0x22, 0x22, 0x22, 0 },    /*  40 */
    { 0x00, 0x3a, 0x00, 0 },    /*  41 */
    { 0x80, 0xff, 0xff, 0 },    /*  42 */
    { 0xf1, 0xf1, 0xf1, 0 },    /*  43 */
    { 0x11, 0x11, 0x49, 0 },    /*  44 */
    { 0x53, 0x1c, 0x1c, 0 },    /*  45 */
    { 0x29, 0x29, 0x29, 0 },    /*  46 */
    { 0x00, 0x45, 0x00, 0 },    /*  47 */
    { 0xff, 0x16, 0x00, 0 },    /*  48 */
    { 0xff, 0x00, 0x2b, 0 },    /*  49 */
    { 0x6c, 0x21, 0x21, 0 },    /*  50 */
    { 0x14, 0x14, 0x59, 0 },    /*  51 */
    { 0x30, 0x30, 0x30, 0 },    /*  52 */
    { 0x00, 0x51, 0x00, 0 },    /*  53 */
    { 0xff, 0xd0, 0xff, 0 },    /*  54 */
    { 0xff, 0xff, 0xa2, 0 },    /*  55 */
    { 0xe3, 0xe3, 0xe3, 0 },    /*  56 */
    { 0x00, 0x39, 0x31, 0 },    /*  57 */
    { 0x1d, 0x21, 0x56, 0 },    /*  58 */
    { 0x6a, 0x1a, 0x47, 0 },    /*  59 */
    { 0x67, 0x32, 0x19, 0 },    /*  60 */
    { 0x39, 0x39, 0x39, 0 },    /*  61 */
    { 0x00, 0x61, 0x00, 0 },    /*  62 */
    { 0x00, 0x00, 0xbe, 0 },    /*  63 */
    { 0xff, 0x31, 0x00, 0 },    /*  64 */
    { 0xff, 0x00, 0x61, 0 },    /*  65 */
    { 0x13, 0x28, 0x6a, 0 },    /*  66 */
    { 0x7b, 0x20, 0x53, 0 },    /*  67 */
    { 0x67, 0x43, 0x16, 0 },    /*  68 */
    { 0x01, 0x3e, 0x45, 0 },    /*  69 */
    { 0xe2, 0x2e, 0x2e, 0 },    /*  70 */
    { 0x42, 0x42, 0x42, 0 },    /*  71 */
    { 0x6a, 0xe9, 0xc8, 0 },    /*  72 */
    { 0xff, 0xb3, 0xff, 0 },    /*  73 */
    { 0xff, 0xff, 0x69, 0 },    /*  74 */
    { 0x00, 0xe2, 0xff, 0 },    /*  75 */
    { 0x65, 0x4c, 0x22, 0 },    /*  76 */
    { 0x16, 0x59, 0x26, 0 },    /*  77 */
    { 0x04, 0x46, 0x51, 0 },    /*  78 */
    { 0x49, 0x2e, 0x68, 0 },    /*  79 */
    { 0x85, 0x32, 0x4a, 0 },    /*  80 */
    { 0x8f, 0x52, 0x07, 0 },    /*  81 */
    { 0xb8, 0x18, 0x6a, 0 },    /*  82 */
    { 0x15, 0x23, 0x90, 0 },    /*  83 */
    { 0x4d, 0x4d, 0x4d, 0 },    /*  84 */
    { 0xff, 0x53, 0x00, 0 },    /*  85 */
    { 0xff, 0x00, 0xa3, 0 },    /*  86 */
    { 0x12, 0x4a, 0x6a, 0 },    /*  87 */
    { 0x6c, 0x33, 0x75, 0 },    /*  88 */
    { 0x9a, 0x41, 0x4a, 0 },    /*  89 */
    { 0x60, 0x5f, 0x24, 0 },    /*  90 */
    { 0x0b, 0x65, 0x37, 0 },    /*  91 */
    { 0x15, 0x2c, 0xa4, 0 },    /*  92 */
    { 0xb1, 0x1f, 0x83, 0 },    /*  93 */
    { 0xff, 0x2c, 0x4e, 0 },    /*  94 */
    { 0xb6, 0x51, 0x20, 0 },    /*  95 */
    { 0x92, 0x64, 0x08, 0 },    /*  96 */
    { 0x55, 0x55, 0x55, 0 },    /*  97 */
    { 0x00, 0xff, 0xc7, 0 },    /*  98 */
    { 0xbe, 0x2f, 0x1d, 0 },    /*  99 */
    { 0x90, 0xec, 0x7e, 0 },    /* 100 */
    { 0x37, 0xdc, 0xc3, 0 },    /* 101 */
    { 0x82, 0xc0, 0xdf, 0 },    /* 102 */
    { 0x0b, 0x56, 0x6f, 0 },    /* 103 */
    { 0x6a, 0x39, 0x85, 0 },    /* 104 */
    { 0xad, 0x43, 0x59, 0 },    /* 105 */
    { 0x76, 0x66, 0x28, 0 },    /* 106 */
    { 0x12, 0x72, 0x36, 0 },    /* 107 */
    { 0x85, 0x77, 0x02, 0 },    /* 108 */
    { 0xf8, 0x48, 0x34, 0 },    /* 109 */
    { 0xb7, 0x22, 0x96, 0 },    /* 110 */
    { 0x17, 0x33, 0xb0, 0 },    /* 111 */
    { 0x5f, 0x5f, 0x5f, 0 },    /* 112 */
    { 0x00, 0xa1, 0x00, 0 },    /* 113 */
    { 0x00, 0x1f, 0xff, 0 },    /* 114 */
    { 0xa8, 0x00, 0xff, 0 },    /* 115 */
    { 0xff, 0x71, 0x00, 0 },    /* 116 */
    { 0xff, 0x00, 0xdf, 0 },    /* 117 */
    { 0x1f, 0x5f, 0x77, 0 },    /* 118 */
    { 0x71, 0x47, 0x89, 0 },    /* 119 */
    { 0xac, 0x4f, 0x62, 0 },    /* 120 */
    { 0x7b, 0x6e, 0x36, 0 },    /* 121 */
    { 0x23, 0x78, 0x44, 0 },    /* 122 */
    { 0x1c, 0x43, 0xb0, 0 },    /* 123 */
    { 0xa5, 0xec, 0xe9, 0 },    /* 124 */
    { 0x7d, 0x2d, 0xb7, 0 },    /* 125 */
    { 0xe0, 0x24, 0xa5, 0 },    /* 126 */
    { 0x95, 0x86, 0x00, 0 },    /* 127 */
    { 0xed, 0x5a, 0x36, 0 },    /* 128 */
    { 0x69, 0x69, 0x69, 0 },    /* 129 */
    { 0x81, 0xd8, 0xb0, 0 },    /* 130 */
    { 0xd8, 0xd6, 0x94, 0 },    /* 131 */
    { 0xff, 0x98, 0xff, 0 },    /* 132 */
    { 0x23, 0x6e, 0x7a, 0 },    /* 133 */
    { 0x70, 0x52, 0x94, 0 },    /* 134 */
    { 0xb5, 0x56, 0x73, 0 },    /* 135 */
    { 0x90, 0x75, 0x44, 0 },    /* 136 */
    { 0x36, 0x84, 0x48, 0 },    /* 137 */
    { 0x00, 0x9f, 0x26, 0 },    /* 138 */
    { 0x89, 0x94, 0x0a, 0 },    /* 139 */
    { 0xf9, 0x6f, 0x2a, 0 },    /* 140 */
    { 0x01, 0x5b, 0xac, 0 },    /* 141 */
    { 0x82, 0x32, 0xcc, 0 },    /* 142 */
    { 0xfe, 0x2e, 0xa7, 0 },    /* 143 */
    { 0x32, 0x3f, 0xd1, 0 },    /* 144 */
    { 0x77, 0x77, 0x77, 0 },    /* 145 */
    { 0x00, 0xca, 0x00, 0 },    /* 146 */
    { 0x00, 0x48, 0xff, 0 },    /* 147 */
    { 0xff, 0x9a, 0x00, 0 },    /* 148 */
    { 0xff, 0x18, 0xff, 0 },    /* 149 */
    { 0x33, 0x7d, 0x85, 0 },    /* 150 */
    { 0x7a, 0x62, 0xa0, 0 },    /* 151 */
    { 0xbf, 0x64, 0x83, 0 },    /* 152 */
    { 0x9f, 0x81, 0x56, 0 },    /* 153 */
    { 0x4a, 0x91, 0x56, 0 },    /* 154 */
    { 0xff, 0x7d, 0x3a, 0 },    /* 155 */
    { 0xa2, 0x9b, 0x22, 0 },    /* 156 */
    { 0x4c, 0xaa, 0x24, 0 },    /* 157 */
    { 0x00, 0x79, 0xa0, 0 },    /* 158 */
    { 0x52, 0x58, 0xc2, 0 },    /* 159 */
    { 0xba, 0x43, 0xc6, 0 },    /* 160 */
    { 0x70, 0x41, 0xe4, 0 },    /* 161 */
    { 0x86, 0x86, 0x86, 0 },    /* 162 */
    { 0x65, 0x9a, 0xff, 0 },    /* 163 */
    { 0x18, 0xd0, 0xb1, 0 },    /* 164 */
    { 0x69, 0xf1, 0x51, 0 },    /* 165 */
    { 0xe0, 0xde, 0x4c, 0 },    /* 166 */
    { 0x41, 0x8c, 0x95, 0 },    /* 167 */
    { 0x8b, 0x71, 0xaf, 0 },    /* 168 */
    { 0xcd, 0x74, 0x91, 0 },    /* 169 */
    { 0xae, 0x91, 0x63, 0 },    /* 170 */
    { 0x58, 0x9f, 0x67, 0 },    /* 171 */
    { 0xc9, 0xa2, 0x38, 0 },    /* 172 */
    { 0x8c, 0xb7, 0x25, 0 },    /* 173 */
    { 0x42, 0xbc, 0x36, 0 },    /* 174 */
    { 0x01, 0xa9, 0x73, 0 },    /* 175 */
    { 0x3c, 0x6f, 0xd0, 0 },    /* 176 */
    { 0x7f, 0x59, 0xe2, 0 },    /* 177 */
    { 0xce, 0x53, 0xd1, 0 },    /* 178 */
    { 0x96, 0x96, 0x96, 0 },    /* 179 */
    { 0x00, 0x7d, 0xff, 0 },    /* 180 */
    { 0xff, 0xcf, 0x00, 0 },    /* 181 */
    { 0xff, 0x4d, 0xff, 0 },    /* 182 */
    { 0x54, 0x9e, 0xa0, 0 },    /* 183 */
    { 0x93, 0x83, 0xbd, 0 },    /* 184 */
    { 0xd3, 0x83, 0xa4, 0 },    /* 185 */
    { 0xc1, 0x9e, 0x78, 0 },    /* 186 */
    { 0x70, 0xae, 0x76, 0 },    /* 187 */
    { 0xcd, 0xb8, 0x3f, 0 },    /* 188 */
    { 0x4d, 0xcf, 0x42, 0 },    /* 189 */
    { 0x01, 0xca, 0x68, 0 },    /* 190 */
    { 0x00, 0xa9, 0xa9, 0 },    /* 191 */
    { 0x3e, 0x85, 0xd9, 0 },    /* 192 */
    { 0x81, 0x70, 0xea, 0 },    /* 193 */
    { 0x95, 0xd3, 0x20, 0 },    /* 194 */
    { 0x42, 0x5e, 0x8e, 0 },    /* 195 */
    { 0xb3, 0xc8, 0x88, 0 },    /* 196 */
    { 0xb3, 0x9c, 0xdd, 0 },    /* 197 */
    { 0x66, 0xc1, 0xb2, 0 },    /* 198 */
    { 0xff, 0xa4, 0xb2, 0 },    /* 199 */
    { 0xff, 0x7c, 0xff, 0 },    /* 200 */
    { 0x5a, 0xac, 0xb1, 0 },    /* 201 */
    { 0xa3, 0x8e, 0xd1, 0 },    /* 202 */
    { 0xee, 0x8f, 0xb3, 0 },    /* 203 */
    { 0xd2, 0xae, 0x81, 0 },    /* 204 */
    { 0x78, 0xbf, 0x80, 0 },    /* 205 */
    { 0xdb, 0xca, 0x46, 0 },    /* 206 */
    { 0x67, 0xdc, 0x4c, 0 },    /* 207 */
    { 0x0b, 0xcf, 0x89, 0 },    /* 208 */
    { 0x3d, 0x9a, 0xdf, 0 },    /* 209 */
    { 0x80, 0x80, 0xf9, 0 },    /* 210 */
    { 0xb2, 0xb2, 0xb2, 0 },    /* 211 */
    { 0x00, 0xff, 0x5c, 0 },    /* 212 */
    { 0x00, 0xac, 0xff, 0 },    /* 213 */
    { 0xcd, 0xf6, 0xf5, 0 },    /* 214 */
    {   10,   10,   10, 0 },    /* 215 */   // -- free slot --
    {   20,   20,   20, 0 },    /* 216 */   // -- free slot --
    {   30,   30,   30, 0 },    /* 217 */   // -- free slot --
    {   40,   40,   40, 0 },    /* 218 */   // -- free slot --
    {   50,   50,   50, 0 },    /* 219 */   // -- free slot --
    {   60,   60,   60, 0 },    /* 220 */   // -- free slot --
    {   70,   70,   70, 0 },    /* 221 */   // -- free slot --
    {   80,   80,   80, 0 },    /* 222 */   // -- free slot --
    {   90,   90,   90, 0 },    /* 223 */   // -- free slot --
    {  100,  100,  100, 0 },    /* 224 */   // -- free slot --
    {  110,  110,  110, 0 },    /* 225 */   // -- free slot --
    {  120,  120,  120, 0 },    /* 226 */   // -- free slot --
    {  130,  130,  130, 0 },    /* 227 */   // -- free slot --
    {  140,  140,  140, 0 },    /* 228 */   // -- free slot --
    {  150,  150,  150, 0 },    /* 229 */   // -- free slot --
    {  160,  160,  160, 0 },    /* 230 */   // -- free slot --
    {  170,  170,  170, 0 },    /* 231 */   // -- free slot --
    {  180,  180,  180, 0 },    /* 232 */   // -- free slot --
    {  190,  190,  190, 0 },    /* 233 */   // -- free slot --
    {  200,  200,  200, 0 },    /* 234 */   // -- free slot --
    {  210,  210,  210, 0 },    /* 235 */   // -- free slot --
    {  220,  220,  220, 0 },    /* 236 */   // -- free slot --
    {  230,  230,  230, 0 },    /* 237 */   // -- free slot --
    {  240,  240,  240, 0 },    /* 238 */   // -- free slot --
    {  250,  250,  250, 0 },    /* 239 */   // -- free slot --
    {  210,  210,  210, 0 },    /* 240 */   // -- free slot --
    {  220,  220,  220, 0 },    /* 241 */   // -- free slot --
    {  230,  230,  230, 0 },    /* 242 */   // -- free slot --
    {  240,  240,  240, 0 },    /* 243 */   // -- free slot --
    {  250,  250,  250, 0 },    /* 244 */   // -- free slot --
    { 0xf7, 0x00, 0xf7, 0 },    /* 245 */   // Transparent color
    { 0xf0, 0xfb, 0xff, 0 },    /* 246 */	// system color  
    { 0xa4, 0xa0, 0xa0, 0 },    /* 247 */	// system color  
    { 0x80, 0x80, 0x80, 0 },    /* 248 */	// Dark Grey     
    { 0x00, 0x00, 0xff, 0 },    /* 249 */	// Bright Red    
    { 0x00, 0xff, 0x00, 0 },    /* 250 */	// Bright Green  
    { 0x00, 0xff, 0xff, 0 },    /* 251 */	// Bright Yellow 
    { 0xff, 0x00, 0x00, 0 },    /* 252 */	// Bright Blue   
    { 0xff, 0x00, 0xff, 0 },    /* 253 */	// Bright Magenta
    { 0xff, 0xff, 0x00, 0 },    /* 254 */	// Bright Cyan   
    { 0xff, 0xff, 0xff, 0 }     /* 255 */	// White      
};







