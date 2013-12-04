/*
 * "$Id$"
 *
 *   sRGB lookup tables for CUPS.
 *
 *   Copyright 2007 by Apple Inc.
 *   Copyright 1993-2005 by Easy Software Products.
 *
 *   These coded instructions, statements, and computer programs are the
 *   property of Apple Inc. and are protected by Federal copyright
 *   law.  Distribution and use rights are outlined in the file "LICENSE.txt"
 *   which should have been included with this file.  If this file is
 *   file is missing or damaged, see the license at "http://www.cups.org/".
 */

/*
 * Include necessary headers.
 */

#include "driver.h"


/*
 * sRGB gamma lookup table.
 */

const unsigned char cups_srgb_lut[256] =
{
    0,  20,  28,  33,  38,  42,  46,  49,  52,  55,  58,  61,  63,  65,  68,
   70,  72,  74,  76,  78,  80,  81,  83,  85,  87,  88,  90,  91,  93,  94,
   96,  97,  99, 100, 102, 103, 104, 106, 107, 108, 109, 111, 112, 113, 114,
  115, 117, 118, 119, 120, 121, 122, 123, 124, 125, 126, 128, 129, 130, 131,
  132, 133, 134, 135, 136, 136, 137, 138, 139, 140, 141, 142, 143, 144, 145,
  146, 147, 147, 148, 149, 150, 151, 152, 153, 153, 154, 155, 156, 157, 158,
  158, 159, 160, 161, 162, 162, 163, 164, 165, 165, 166, 167, 168, 168, 169,
  170, 171, 171, 172, 173, 174, 174, 175, 176, 176, 177, 178, 178, 179, 180,
  181, 181, 182, 183, 183, 184, 185, 185, 186, 187, 187, 188, 189, 189, 190,
  190, 191, 192, 192, 193, 194, 194, 195, 196, 196, 197, 197, 198, 199, 199,
  200, 200, 201, 202, 202, 203, 203, 204, 205, 205, 206, 206, 207, 208, 208,
  209, 209, 210, 210, 211, 212, 212, 213, 213, 214, 214, 215, 216, 216, 217,
  217, 218, 218, 219, 219, 220, 220, 221, 222, 222, 223, 223, 224, 224, 225,
  225, 226, 226, 227, 227, 228, 228, 229, 229, 230, 230, 231, 231, 232, 232,
  233, 233, 234, 234, 235, 235, 236, 236, 237, 237, 238, 238, 239, 239, 240,
  240, 241, 241, 242, 242, 243, 243, 244, 244, 245, 245, 246, 246, 247, 247,
  248, 248, 249, 249, 249, 250, 250, 251, 251, 252, 252, 253, 253, 254, 254,
  255
};


/*
 * sRGB gamma lookup table (inverted output to map to CMYK...)
 */

const unsigned char cups_scmy_lut[256] =
{
  255, 235, 227, 222, 217, 213, 209, 206, 203, 200, 197, 194, 192, 190, 187,
  185, 183, 181, 179, 177, 175, 174, 172, 170, 168, 167, 165, 164, 162, 161,
  159, 158, 156, 155, 153, 152, 151, 149, 148, 147, 146, 144, 143, 142, 141,
  140, 138, 137, 136, 135, 134, 133, 132, 131, 130, 129, 127, 126, 125, 124,
  123, 122, 121, 120, 119, 119, 118, 117, 116, 115, 114, 113, 112, 111, 110,
  109, 108, 108, 107, 106, 105, 104, 103, 102, 102, 101, 100,  99,  98,  97,
   97,  96,  95,  94,  93,  93,  92,  91,  90,  90,  89,  88,  87,  87,  86,
   85,  84,  84,  83,  82,  81,  81,  80,  79,  79,  78,  77,  77,  76,  75,
   74,  74,  73,  72,  72,  71,  70,  70,  69,  68,  68,  67,  66,  66,  65,
   65,  64,  63,  63,  62,  61,  61,  60,  59,  59,  58,  58,  57,  56,  56,
   55,  55,  54,  53,  53,  52,  52,  51,  50,  50,  49,  49,  48,  47,  47,
   46,  46,  45,  45,  44,  43,  43,  42,  42,  41,  41,  40,  39,  39,  38,
   38,  37,  37,  36,  36,  35,  35,  34,  33,  33,  32,  32,  31,  31,  30,
   30,  29,  29,  28,  28,  27,  27,  26,  26,  25,  25,  24,  24,  23,  23,
   22,  22,  21,  21,  20,  20,  19,  19,  18,  18,  17,  17,  16,  16,  15,
   15,  14,  14,  13,  13,  12,  12,  11,  11,  10,  10,   9,   9,   8,   8,
    7,   7,   6,   6,   6,   5,   5,   4,   4,   3,   3,   2,   2,   1,   1,
    0
};


/*
 * End of "$Id$".
 */
