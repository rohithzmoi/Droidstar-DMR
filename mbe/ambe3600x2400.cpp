/*
 * Copyright (C) 2010 mbelib Author
 * GPG Key ID: 0xEA5EFE2C (9E7A 5527 9CDC EBF7 BF1B  D772 4F98 E863 EA5E FE2C)
 * Modified Copyright (C) 2024-2026 Rohith Namboothiri
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND ISC DISCLAIMS ALL WARRANTIES WITH
 * REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS.  IN NO EVENT SHALL ISC BE LIABLE FOR ANY SPECIAL, DIRECT,
 * INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
 * LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE
 * OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#include "mbevocoder.h"
#include "vocoder_tables.h"

void
mbe_dumpAmbe2400Data (char *ambe_d)
{

  int i;
  char *ambe;

  ambe = ambe_d;
  for (i = 0; i < 49; i++)
    {
      printf ("%i", *ambe);
      ambe++;
    }
  printf (" ");
}

void
mbe_dumpAmbe3600x2400Frame (char ambe_fr[4][24])
{

  int j;

  // c0
  printf ("ambe_fr c0: ");
  for (j = 23; j >= 0; j--)
    {
      printf ("%i", ambe_fr[0][j]);
    }
  printf (" ");
  // c1
  printf ("ambe_fr c1: ");
  for (j = 22; j >= 0; j--)
    {
      printf ("%i", ambe_fr[1][j]);
    }
  printf (" ");
  // c2
  printf ("ambe_fr c2: ");
  for (j = 10; j >= 0; j--)
    {
      printf ("%i", ambe_fr[2][j]);
    }
  printf (" ");
  // c3
  printf ("ambe_fr c3: ");
  for (j = 13; j >= 0; j--)
    {
      printf ("%i", ambe_fr[3][j]);
    }
  printf (" ");
}

int
mbe_eccAmbe3600x2400C0 (char ambe_fr[4][24])
{
  return mbe_eccAmbe3600C0Counted (ambe_fr);
}

int
mbe_eccAmbe3600x2400Data (char ambe_fr[4][24], char *ambe_d)
{
  return mbe_eccAmbe3600DataCounted (ambe_fr, ambe_d);
}

int
mbe_decodeAmbe2400Parms (char *ambe_d, mbe_parms * cur_mp, mbe_parms * prev_mp)
{

  int ji, i, j, k, l, L, m, am, ak;
  int intkl[57];
  int b0, b1, b2, b3, b4, b5, b6, b7, b8;
  float f0, Cik[5][18], flokl[57], deltal[57];
  float Sum42, Sum43, Tl[57], Gm[9], Ri[9], sum, c1, c2;
  int silence;
  int Ji[5];
  float deltaGamma, BigGamma;
  float unvc, rconst;
  const struct ambe_dct_cache *dct_cache = mbe_get_dct_cache();

  silence = 0;

#ifdef AMBE_DEBUG
  printf ("\n");
#endif

  // copy repeat from prev_mp
  cur_mp->repeat = prev_mp->repeat;

  // decode fundamental frequency w0 from b0
  b0 = 0;
  b0 |= ambe_d[0]<<6;
  b0 |= ambe_d[1]<<5;
  b0 |= ambe_d[2]<<4;
  b0 |= ambe_d[3]<<3;
  b0 |= ambe_d[4]<<2;
  b0 |= ambe_d[5]<<1;
  b0 |= ambe_d[48];

  // DVSI AMBE-3000 hardware behavior (mbelib-neo): - b0 == 124 (including the
  if (b0 == 125 || b0 == 127)
    {
      return (2);
    }

  if (b0 == 126) // frame is tone
    {
      int t7tab[8] = {1,0,0,0,0,1,1,1};
      int t6tab[8] = {0,0,0,1,1,1,1,0};
      int t5tab[8] = {0,0,1,0,1,1,0,1};
      int sel = ((ambe_d[6]<<2)|(ambe_d[7]<<1)|ambe_d[8]) & 7;
      b1 = 0;
      b1 |= t7tab[sel]<<7;
      b1 |= t6tab[sel]<<6;
      b1 |= t5tab[sel]<<5;
      b1 |= ambe_d[9]<<4;
      b1 |= ambe_d[42]<<3;
      b1 |= ambe_d[43]<<2;
      b1 |= ambe_d[10]<<1;
      b1 |= ambe_d[11];

      if ((b1 >= 5 && b1 <= 122) || (b1 >= 128 && b1 <= 163))
        {
          return (3);
        }
      return (2);
    }

  if (b0 == 124)
    {
      // D-STAR silence / null frame: 14-harmonic all-unvoiced silence model. Preserve prev_mp
      cur_mp->w0 = ((float) 2 * (float) M_PI) / (float) 32;
      cur_mp->L = 14;
      for (l = 0; l <= 56; l++)
        {
          cur_mp->Vl[l] = 0;
          cur_mp->Ml[l] = 0.0f;
        }
      return (4);
    }
  else
    {
      f0 = mbe_ambe2400_f0 (b0);
      cur_mp->w0 = f0 * (float) 2 * (float) M_PI;
      L = mbe_ambe2400_harmonic_count (b0);
      cur_mp->L = L;
    }

  unvc = (float) 0.2046 / sqrtf (cur_mp->w0);

  // decode V/UV parameters with 250 Hz downward band overlap (DVSI AMBE-3000 parity)
  b1 = 0;
  b1 |= ambe_d[38]<<3;
  b1 |= ambe_d[39]<<2;
  b1 |= ambe_d[40]<<1;
  b1 |= ambe_d[41];

  for (l = 1; l <= L; l++)
    {
      if (silence == 0)
        {
          cur_mp->Vl[l] = mbe_ambe2400_harmonic_voiced (b1, l, f0);
        }
    }
#ifdef AMBE_DEBUG
  printf ("\nb0:%i w0:%f L:%i b1:%i\n", b0, cur_mp->w0, L, b1);
#endif

  // decode gain vector
  // load b2 from ambe_d
  b2 = 0;
  b2 |= ambe_d[6]<<5;
  b2 |= ambe_d[7]<<4;
  b2 |= ambe_d[8]<<3;
  b2 |= ambe_d[9]<<2;
  b2 |= ambe_d[42]<<1;
  b2 |= ambe_d[43];
  //fprintf(stderr,"Gain = %d,\n", b2);
  deltaGamma = AmbePlusDg[b2];
  cur_mp->gamma = deltaGamma + ((float) 0.5 * prev_mp->gamma);
#ifdef AMBE_DEBUG
  printf ("b2: %i, deltaGamma: %f gamma: %f gamma-1: %f\n", b2, deltaGamma, cur_mp->gamma, prev_mp->gamma);
#endif


  // decode PRBA vectors
  Gm[1] = 0;

  // load b3 from ambe_d
  b3 = 0;
  b3 |= ambe_d[10]<<8;
  b3 |= ambe_d[11]<<7;
  b3 |= ambe_d[12]<<6;
  b3 |= ambe_d[13]<<5;
  b3 |= ambe_d[14]<<4;
  b3 |= ambe_d[15]<<3;
  b3 |= ambe_d[16]<<2;
  b3 |= ambe_d[44]<<1;
  b3 |= ambe_d[45];
  Gm[2] = AmbePlusPRBA24[b3][0];
  Gm[3] = AmbePlusPRBA24[b3][1];
  Gm[4] = AmbePlusPRBA24[b3][2];

  // load b4 from ambe_d
  b4 = 0;
  b4 |= ambe_d[17]<<6;
  b4 |= ambe_d[18]<<5;
  b4 |= ambe_d[19]<<4;
  b4 |= ambe_d[20]<<3;
  b4 |= ambe_d[21]<<2;
  b4 |= ambe_d[46]<<1;
  b4 |= ambe_d[47];
  Gm[5] = AmbePlusPRBA58[b4][0];
  Gm[6] = AmbePlusPRBA58[b4][1];
  Gm[7] = AmbePlusPRBA58[b4][2];
  Gm[8] = AmbePlusPRBA58[b4][3];

#ifdef AMBE_DEBUG
  printf ("b3: %i Gm[2]: %f Gm[3]: %f Gm[4]: %f b4: %i Gm[5]: %f Gm[6]: %f Gm[7]: %f Gm[8]: %f\n", b3, Gm[2], Gm[3], Gm[4], b4, Gm[5], Gm[6], Gm[7], Gm[8]);
#endif

  // compute Ri
  for (i = 1; i <= 8; i++)
    {
      sum = 0;
      for (m = 1; m <= 8; m++)
        {
          if (m == 1)
            {
              am = 1;
            }
          else
            {
              am = 2;
            }
          sum = sum + ((float) am * Gm[m] * dct_cache->ri_cos[m][i]);
        }
      Ri[i] = sum;
#ifdef AMBE_DEBUG
      printf ("R%i: %f ", i, Ri[i]);
#endif
    }
#ifdef AMBE_DEBUG
  printf ("\n");
#endif

  // generate first to elements of each Ci,k block from PRBA vector
  rconst = ((float) 1 / ((float) 2 * M_SQRT2));
  Cik[1][1] = (float) 0.5 *(Ri[1] + Ri[2]);
  Cik[1][2] = rconst * (Ri[1] - Ri[2]);
  Cik[2][1] = (float) 0.5 *(Ri[3] + Ri[4]);
  Cik[2][2] = rconst * (Ri[3] - Ri[4]);
  Cik[3][1] = (float) 0.5 *(Ri[5] + Ri[6]);
  Cik[3][2] = rconst * (Ri[5] - Ri[6]);
  Cik[4][1] = (float) 0.5 *(Ri[7] + Ri[8]);
  Cik[4][2] = rconst * (Ri[7] - Ri[8]);

  // decode HOC

  // load b5 from ambe_d
  b5 = 0;
  b5 |= ambe_d[22]<<3;
  b5 |= ambe_d[23]<<2;
  b5 |= ambe_d[25]<<1;
  b5 |= ambe_d[26];

  // load b6 from ambe_d
  b6 = 0;
  b6 |= ambe_d[27]<<3;
  b6 |= ambe_d[28]<<2;
  b6 |= ambe_d[29]<<1;
  b6 |= ambe_d[30];

  // load b7 from ambe_d
  b7 = 0;
  b7 |= ambe_d[31]<<3;
  b7 |= ambe_d[32]<<2;
  b7 |= ambe_d[33]<<1;
  b7 |= ambe_d[34];

  // load b8 from ambe_d
  b8 = 0;
  b8 |= ambe_d[35]<<3;
  b8 |= ambe_d[36]<<2;
  b8 |= ambe_d[37]<<1;
  // b8 |= 0; // least significant bit of hoc3 unused here, and according to

  // lookup Ji
  Ji[1] = AmbePlusLmprbl[L][0];
  Ji[2] = AmbePlusLmprbl[L][1];
  Ji[3] = AmbePlusLmprbl[L][2];
  Ji[4] = AmbePlusLmprbl[L][3];
#ifdef AMBE_DEBUG
  printf ("Ji[1]: %i Ji[2]: %i Ji[3]: %i Ji[4]: %i\n", Ji[1], Ji[2], Ji[3], Ji[4]);
  printf ("b5: %i b6: %i b7: %i b8: %i\n", b5, b6, b7, b8);
#endif

  // Load Ci,k with the values from the HOC tables there appear to be a
  for (k = 3; k <= Ji[1]; k++)
    {
      if (k > 6)
        {
          Cik[1][k] = 0;
        }
      else
        {
          Cik[1][k] = AmbePlusHOCb5[b5][k - 3];
#ifdef AMBE_DEBUG
          printf ("C1,%i: %f ", k, Cik[1][k]);
#endif
        }
    }
  for (k = 3; k <= Ji[2]; k++)
    {
      if (k > 6)
        {
          Cik[2][k] = 0;
        }
      else
        {
          Cik[2][k] = AmbePlusHOCb6[b6][k - 3];
#ifdef AMBE_DEBUG
          printf ("C2,%i: %f ", k, Cik[2][k]);
#endif
        }
    }
  for (k = 3; k <= Ji[3]; k++)
    {
      if (k > 6)
        {
          Cik[3][k] = 0;
        }
      else
        {
          Cik[3][k] = AmbePlusHOCb7[b7][k - 3];
#ifdef AMBE_DEBUG
          printf ("C3,%i: %f ", k, Cik[3][k]);
#endif
        }
    }
  for (k = 3; k <= Ji[4]; k++)
    {
      if (k > 6)
        {
          Cik[4][k] = 0;
        }
      else
        {
          Cik[4][k] = AmbePlusHOCb8[b8][k - 3];
#ifdef AMBE_DEBUG
          printf ("C4,%i: %f ", k, Cik[4][k]);
#endif
        }
    }
#ifdef AMBE_DEBUG
  printf ("\n");
#endif

  // inverse DCT each Ci,k to give ci,j (Tl)
  l = 1;
  for (i = 1; i <= 4; i++)
    {
      ji = Ji[i];
      for (j = 1; j <= ji; j++)
        {
          sum = 0;
          for (k = 1; k <= ji; k++)
            {
              if (k == 1)
                {
                  ak = 1;
                }
              else
                {
                  ak = 2;
                }
#ifdef AMBE_DEBUG
              printf ("j: %i Cik[%i][%i]: %f ", j, i, k, Cik[i][k]);
#endif
              sum = sum + ((float) ak * Cik[i][k] * dct_cache->idct_cos[ji][j][k]);
            }
          Tl[l] = sum;
#ifdef AMBE_DEBUG
          printf ("Tl[%i]: %f\n", l, Tl[l]);
#endif
          l++;
        }
    }

  // determine log2Ml by applying ci,j to previous log2Ml

  // fix for when L > L(-1)
  if (cur_mp->L > prev_mp->L)
    {
      for (l = (prev_mp->L) + 1; l <= cur_mp->L; l++)
        {
          prev_mp->Ml[l] = prev_mp->Ml[prev_mp->L];
          prev_mp->log2Ml[l] = prev_mp->log2Ml[prev_mp->L];
        }
    }
  prev_mp->log2Ml[0] = prev_mp->log2Ml[1];
  prev_mp->Ml[0] = prev_mp->Ml[1];

  // Part 1
  Sum43 = 0;
  for (l = 1; l <= cur_mp->L; l++)
    {

      // eq. 40
      flokl[l] = ((float) prev_mp->L / (float) cur_mp->L) * (float) l;
      intkl[l] = (int) (flokl[l]);
      if (intkl[l] < 0) intkl[l] = 0;
      if (intkl[l] > 56) intkl[l] = 56;
#ifdef AMBE_DEBUG
      printf ("flok%i: %f, intk%i: %i ", l, flokl[l], l, intkl[l]);
#endif
      // eq. 41
      deltal[l] = flokl[l] - (float) intkl[l];
#ifdef AMBE_DEBUG
      printf ("delta%i: %f ", l, deltal[l]);
#endif
      // eq 43: clamp upper tap to 56 so log2Ml[57] is never read out-of-bounds
      int upper = (intkl[l] + 1 > 56) ? 56 : (intkl[l] + 1);
      Sum43 = Sum43 + ((((float) 1 - deltal[l]) * prev_mp->log2Ml[intkl[l]]) + (deltal[l] * prev_mp->log2Ml[upper]));
    }
  Sum43 = ((DSTAR_SPEC_PRED / (float) cur_mp->L) * Sum43);
#ifdef AMBE_DEBUG
  printf ("\n");
  printf ("Sum43: %f\n", Sum43);
#endif

  // Part 2
  Sum42 = 0;
  for (l = 1; l <= cur_mp->L; l++)
    {
      Sum42 += Tl[l];
    }
  Sum42 = Sum42 / (float) cur_mp->L;
  BigGamma = cur_mp->gamma - ((float) 0.5 * log2f ((float) cur_mp->L)) - Sum42;

  // Part 3
  for (l = 1; l <= cur_mp->L; l++)
    {
      int upper = (intkl[l] + 1 > 56) ? 56 : (intkl[l] + 1);
      c1 = (DSTAR_SPEC_PRED * ((float) 1 - deltal[l]) * prev_mp->log2Ml[intkl[l]]);
      c2 = (DSTAR_SPEC_PRED * deltal[l] * prev_mp->log2Ml[upper]);
      cur_mp->log2Ml[l] = Tl[l] + c1 + c2 - Sum43 + BigGamma;
      // inverse log to generate spectral amplitudes
      if (cur_mp->Vl[l] == 1)
        {
          cur_mp->Ml[l] = exp2f (cur_mp->log2Ml[l]);
        }
      else
        {
          cur_mp->Ml[l] = unvc * exp2f (cur_mp->log2Ml[l]);
        }
#ifdef AMBE_DEBUG
      printf ("flokl[%i]: %f, intkl[%i]: %i ", l, flokl[l], l, intkl[l]);
      printf ("deltal[%i]: %f ", l, deltal[l]);
      printf ("prev_mp->log2Ml[%i]: %f\n", l, prev_mp->log2Ml[intkl[l]]);
      printf ("BigGamma: %f c1: %f c2: %f Sum43: %f Tl[%i]: %f log2Ml[%i]: %f Ml[%i]: %f\n", BigGamma, c1, c2, Sum43, l, Tl[l], l, cur_mp->log2Ml[l], l, cur_mp->Ml[l]);
#endif
    }

  return (0);
}

void
mbe_demodulateAmbe3600x2400Data (char ambe_fr[4][24])
{
  int i, j, k;
  unsigned short pr[115];
  unsigned short foo = 0;

  // create pseudo-random modulator
  for (i = 23; i >= 12; i--)
    {
      foo <<= 1;
      foo |= ambe_fr[0][i];
    }
  pr[0] = (16 * foo);
  for (i = 1; i < 24; i++)
    {
      pr[i] = (173 * pr[i - 1]) + 13849 - (65536 * (((173 * pr[i - 1]) + 13849) / 65536));
    }
  for (i = 1; i < 24; i++)
    {
      pr[i] = pr[i] / 32768;
    }

  // demodulate ambe_fr with pr
  k = 1;
  for (j = 22; j >= 0; j--)
    {
      ambe_fr[1][j] = ((ambe_fr[1][j]) ^ pr[k]);
      k++;
    }
}

void
mbe_processAmbe2400Dataf (float *aout_buf, int *errs2, char *err_str, char ambe_d[49], mbe_parms * cur_mp, mbe_parms * prev_mp, mbe_parms * prev_mp_enhanced, int uvquality)
{

  int i, bad;
  const int c0_uncorrectable = (cur_mp->errorCount4 != 0);
  cur_mp->errorCount4 = 0;

  for (i = 0; i < *errs2; i++)
    {
      *err_str = '=';
      err_str++;
    }

  const float prior_err_level = (prev_mp->PSIl[0] >= 0.0f && prev_mp->PSIl[0] < 1000000.0f) ? prev_mp->PSIl[0] : 0.0f;
  const float err_level = (0.99f * prior_err_level) + (float) (*errs2);
  const float err_rate = (0.95f * prev_mp->errorRate) + (0.001064f * (float) (*errs2));

  bad = mbe_decodeAmbe2400Parms (ambe_d, cur_mp, prev_mp);
  cur_mp->errorCountTotal = *errs2;
  cur_mp->errorRate = err_rate;
  cur_mp->PSIl[0] = err_level;

  if (bad == 3)
    {
      // Tone Frame
      *err_str = 'T';
      err_str++;
      cur_mp->repeat = 0;
      prev_mp->repeat = 0;
      prev_mp->errorCountTotal = *errs2;
      prev_mp->errorRate = err_rate;
      prev_mp->PSIl[0] = err_level;
      mbe_synthesizeSilencef (aout_buf);
      *err_str = 0;
      return;
    }

  if (bad == 2 || c0_uncorrectable)
    {
      // Lost / Erasure frame (b0 125, 127, invalid tone, or 4-bit C0 error)
      *err_str = (bad == 2) ? 'E' : 'R';
      err_str++;

      // When C0 had 4 errors on a voice frame, DVSI's AMBE-3000 still updates the
      if (c0_uncorrectable && bad == 0)
        {
          for (int l = 1; l <= cur_mp->L && l <= 56; l++)
            {
              prev_mp->log2Ml[l] = cur_mp->log2Ml[l];
            }
          prev_mp->L = cur_mp->L;
          prev_mp->gamma = cur_mp->gamma;
        }

      const int next_repeat = prev_mp->repeat + 1;
      prev_mp->repeat = next_repeat;
      prev_mp->errorCountTotal = *errs2;
      prev_mp->errorRate = err_rate;
      prev_mp->PSIl[0] = err_level;

      if (next_repeat <= 3)
        {
          // Repeat the already-enhanced model without re-running spectral enhancement
          mbe_useLastMbeParms (cur_mp, prev_mp_enhanced);
          cur_mp->repeat = next_repeat;
          cur_mp->errorCountTotal = *errs2;
          cur_mp->errorRate = err_rate;
          cur_mp->PSIl[0] = err_level;
          mbe_synthesizeSpeechWithRm0f (aout_buf, cur_mp, prev_mp_enhanced, uvquality, 0.0f, 1);
          mbe_moveMbeParms (cur_mp, prev_mp_enhanced);
          if (err_level > 342.0f)
            {
              mbe_synthesizeSilencef (aout_buf);
            }
        }
      else if (next_repeat == 4)
        {
          // 4th consecutive repeat: fade the last enhanced frame out to silence
          // while preserving prev_mp prediction history (DVSI AMBE-3000 parity)
          *err_str = 'M';
          err_str++;
          mbe_parms silent = *prev_mp_enhanced;
          memset (silent.Ml, 0, sizeof (silent.Ml));
          silent.repeat = next_repeat;
          silent.errorCountTotal = *errs2;
          silent.errorRate = err_rate;
          silent.PSIl[0] = err_level;
          mbe_synthesizeSpeechWithRm0f (aout_buf, &silent, prev_mp_enhanced, uvquality, 0.0f, 1);
          mbe_moveMbeParms (&silent, prev_mp_enhanced);
          mbe_moveMbeParms (&silent, cur_mp);
          if (err_level > 342.0f)
            {
              mbe_synthesizeSilencef (aout_buf);
            }
        }
      else
        {
          *err_str = 'M';
          err_str++;
          mbe_synthesizeSilencef (aout_buf);
        }
      *err_str = 0;
      return;
    }

  // Valid voice or b0=124 silence frame
  if (bad == 4)
    {
      // Silence frame (b0 == 124): synthesize so any voiced tail windows out smoothly,
      // while keeping prev_mp prediction history untouched.
      cur_mp->repeat = 0;
      prev_mp->repeat = 0;
      prev_mp->errorCountTotal = *errs2;
      prev_mp->errorRate = err_rate;
      prev_mp->PSIl[0] = err_level;
      if (err_level > 342.0f)
        {
          *err_str = 'M';
          err_str++;
          memset (prev_mp_enhanced->Ml, 0, sizeof (prev_mp_enhanced->Ml));
          mbe_synthesizeSilencef (aout_buf);
        }
      else
        {
          mbe_synthesizeSpeechWithRm0f (aout_buf, cur_mp, prev_mp_enhanced, uvquality, 0.0f, 1);
          mbe_moveMbeParms (cur_mp, prev_mp_enhanced);
        }
      *err_str = 0;
      return;
    }

  cur_mp->repeat = 0;
  mbe_moveMbeParms (cur_mp, prev_mp);
  const float pre_enh_rm0 = mbe_spectralAmpEnhanceWithRm0 (cur_mp, 1.33352143f);
  {
    int l;
    const float gv = powf (10.0f, DSTAR_DEC_VOICED_DB / 20.0f);
    const float gu = powf (10.0f, DSTAR_DEC_UNVOICED_DB / 20.0f);
    const float fhz = cur_mp->w0 / (2.0f * (float) M_PI) * 8000.0f;
    const float gp = powf (10.0f, DSTAR_DEC_PITCH_DB_PER_OCT * log2f (fhz / DSTAR_DEC_PITCH_REF_HZ) / 20.0f);
    for (l = 1; l <= cur_mp->L; l++)
      cur_mp->Ml[l] *= (cur_mp->Vl[l] ? gv : gu) * gp;
  }
  mbe_synthesizeSpeechWithRm0f (aout_buf, cur_mp, prev_mp_enhanced, uvquality, pre_enh_rm0, 0);
  mbe_moveMbeParms (cur_mp, prev_mp_enhanced);
  if (err_level > 342.0f)
    {
      *err_str = 'M';
      err_str++;
      mbe_synthesizeSilencef (aout_buf);
    }
  *err_str = 0;
}

void
mbe_processAmbe2400Data (short *aout_buf, int *errs2, char *err_str, char ambe_d[49], mbe_parms * cur_mp, mbe_parms * prev_mp, mbe_parms * prev_mp_enhanced, int uvquality)
{
  float float_buf[160];

  mbe_processAmbe2400Dataf (float_buf, errs2, err_str, ambe_d, cur_mp, prev_mp, prev_mp_enhanced, uvquality);
  mbe_floattoshort (float_buf, aout_buf);
}

void
mbe_processAmbe3600x2400Framef (float *aout_buf, int *errs2, char *err_str, char ambe_fr[4][24], char ambe_d[49], mbe_parms * cur_mp, mbe_parms * prev_mp, mbe_parms * prev_mp_enhanced, int uvquality)
{

  int errs = 0;
  *errs2 = 0;
  errs = mbe_eccAmbe3600x2400C0 (ambe_fr);
  cur_mp->errorCount4 = (errs >= 4) ? 1 : 0;
  mbe_demodulateAmbe3600x2400Data (ambe_fr);
  *errs2 = errs;
  *errs2 += mbe_eccAmbe3600x2400Data (ambe_fr, ambe_d);

  mbe_processAmbe2400Dataf (aout_buf, errs2, err_str, ambe_d, cur_mp, prev_mp, prev_mp_enhanced, uvquality);
}

void
mbe_processAmbe3600x2400Frame (short *aout_buf, int *errs2, char *err_str, char ambe_fr[4][24], char ambe_d[49], mbe_parms * cur_mp, mbe_parms * prev_mp, mbe_parms * prev_mp_enhanced, int uvquality)
{
  float float_buf[160];

  mbe_processAmbe3600x2400Framef (float_buf, errs2, err_str, ambe_fr, ambe_d, cur_mp, prev_mp, prev_mp_enhanced, uvquality);
  mbe_floattoshort (float_buf, aout_buf);
}
