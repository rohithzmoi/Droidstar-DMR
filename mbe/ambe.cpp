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

#include <stdio.h>
#include <string.h>
#include <float.h>
#include <limits.h>
#include <math.h>
#include "mbevocoder.h"
#include "vocoder_tables.h"
	
inline float make_f0(int b0) {
	return mbe_ambe2400_f0(b0);
}

int
mbe_dequantizeAmbeParms (mbe_parms * cur_mp, mbe_parms * prev_mp, const int *b, int dstar)
{

  int ji, i, j, k, l, L, m, am, ak;
  int intkl[57];
  int b0, b1, b2, b3, b4, b5, b6, b7, b8;
  float f0, Cik[5][18], flokl[57], deltal[57];
  float Sum42, Sum43, Tl[57], Gm[9], Ri[9], sum, c1, c2;
  //char tmpstr[13];
  int silence;
  int Ji[5], jl;
  float deltaGamma, BigGamma;
  float unvc, rconst;

  b0 = b[0];
  b1 = b[1];
  b2 = b[2];
  b3 = b[3];
  b4 = b[4];
  b5 = b[5];
  b6 = b[6];
  b7 = b[7];
  b8 = b[8];

  silence = 0;

#ifdef AMBE_DEBUG
  fprintf (stderr, "\n");
#endif

  // copy repeat from prev_mp
  cur_mp->repeat = prev_mp->repeat;

  if ((b0 >= 120) && (b0 <= 123))
    {
#ifdef AMBE_DEBUG
      fprintf (stderr, "AMBE Erasure Frame\n");
#endif
      return (2);
    }
  else if ((b0 == 124) || (b0 == 125))
    {
#ifdef AMBE_DEBUG
      fprintf (stderr, "AMBE Silence Frame\n");
#endif
      // Silence frame: preserve prev_mp prediction history (TIA-102.BABA-1 §4.3)
      cur_mp->w0 = ((float) 2 * (float) M_PI) / (float) 32;
      cur_mp->L = 14;
      for (l = 0; l <= 56; l++)
        {
          cur_mp->Vl[l] = 0;
          cur_mp->Ml[l] = 0.0f;
        }
      return (0);
    }
  else if ((b0 == 126) || (b0 == 127))
    {
#ifdef AMBE_DEBUG
      fprintf (stderr, "AMBE Tone Frame\n");
#endif
      return (3);
    }

  if (silence == 0)
    {
      if (dstar)
        f0 = make_f0(b0);
      else
        f0 = mbe_ambe2450_f0(b0);
      cur_mp->w0 = f0 * (float) 2 * (float) M_PI;
    }

  unvc = (float) 0.2046 / sqrtf (cur_mp->w0);

  // decode L
  if (silence == 0)
    {
      if (dstar)
        L = mbe_ambe2400_harmonic_count(b0);
      else
        L = mbe_ambe2450_harmonic_count(b0);
      cur_mp->L = L;
    }

  // decode V/UV parameters
  for (l = 1; l <= L; l++)
    {
      // jl from specification document
      jl = (int) ((float) l * (float) 16.0 * f0);
      if (jl < 0) jl = 0;
      else if (jl > 7) jl = 7;

      if (silence == 0)
        {
          if (dstar)
            cur_mp->Vl[l] = mbe_ambe2400_harmonic_voiced(b1, l, f0);
          else
            cur_mp->Vl[l] = AmbeVuv[b1][jl];
        }
#ifdef AMBE_DEBUG
      fprintf (stderr, "jl[%i]:%i Vl[%i]:%i\n", l, jl, l, cur_mp->Vl[l]);
#endif
    }
#ifdef AMBE_DEBUG
  fprintf (stderr, "\nb0:%i w0:%f L:%i b1:%i\n", b0, cur_mp->w0, L, b1);
#endif
  if (dstar) {
    deltaGamma = AmbePlusDg[b2];
    cur_mp->gamma = deltaGamma + ((float) 0.5 * prev_mp->gamma);
  } else {
    deltaGamma = AmbeDg[b2];
    cur_mp->gamma = deltaGamma + ((float) 0.5 * prev_mp->gamma);
  }
#ifdef AMBE_DEBUG
  fprintf (stderr, "b2: %i, deltaGamma: %f gamma: %f gamma-1: %f\n", b2, deltaGamma, cur_mp->gamma, prev_mp->gamma);
#endif


  // decode PRBA vectors
  Gm[1] = 0;

  if (dstar) {
    Gm[2] = AmbePlusPRBA24[b3][0];
    Gm[3] = AmbePlusPRBA24[b3][1];
    Gm[4] = AmbePlusPRBA24[b3][2];

    Gm[5] = AmbePlusPRBA58[b4][0];
    Gm[6] = AmbePlusPRBA58[b4][1];
    Gm[7] = AmbePlusPRBA58[b4][2];
    Gm[8] = AmbePlusPRBA58[b4][3];
  } else {
    Gm[2] = AmbePRBA24[b3][0];
    Gm[3] = AmbePRBA24[b3][1];
    Gm[4] = AmbePRBA24[b3][2];

    Gm[5] = AmbePRBA58[b4][0];
    Gm[6] = AmbePRBA58[b4][1];
    Gm[7] = AmbePRBA58[b4][2];
    Gm[8] = AmbePRBA58[b4][3];
  }

#ifdef AMBE_DEBUG
  fprintf (stderr, "b3: %i Gm[2]: %f Gm[3]: %f Gm[4]: %f b4: %i Gm[5]: %f Gm[6]: %f Gm[7]: %f Gm[8]: %f\n", b3, Gm[2], Gm[3], Gm[4], b4, Gm[5], Gm[6], Gm[7], Gm[8]);
#endif

  // compute Ri
  const struct ambe_dct_cache *dct_cache = mbe_get_dct_cache();
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
      fprintf (stderr, "R%i: %f ", i, Ri[i]);
#endif
    }
#ifdef AMBE_DEBUG
  fprintf (stderr, "\n");
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

  // lookup Ji
  if (dstar) {
    Ji[1] = AmbePlusLmprbl[L][0];
    Ji[2] = AmbePlusLmprbl[L][1];
    Ji[3] = AmbePlusLmprbl[L][2];
    Ji[4] = AmbePlusLmprbl[L][3];
  } else {
    Ji[1] = AmbeLmprbl[L][0];
    Ji[2] = AmbeLmprbl[L][1];
    Ji[3] = AmbeLmprbl[L][2];
    Ji[4] = AmbeLmprbl[L][3];
  }
#ifdef AMBE_DEBUG
  fprintf (stderr, "Ji[1]: %i Ji[2]: %i Ji[3]: %i Ji[4]: %i\n", Ji[1], Ji[2], Ji[3], Ji[4]);
  fprintf (stderr, "b5: %i b6: %i b7: %i b8: %i\n", b5, b6, b7, b8);
#endif

  // Load Ci,k with the values from the HOC tables
  // there appear to be a couple typos in eq. 37 so we will just do what makes sense
  // (3 <= k <= Ji and k<=6)
  for (k = 3; k <= Ji[1]; k++)
    {
      if (k > 6)
        {
          Cik[1][k] = 0;
        }
      else
        {
          if (dstar)
            Cik[1][k] = AmbePlusHOCb5[b5][k - 3];
          else
            Cik[1][k] = AmbeHOCb5[b5][k - 3];
#ifdef AMBE_DEBUG
          fprintf (stderr, "C1,%i: %f ", k, Cik[1][k]);
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
          if (dstar)
            Cik[2][k] = AmbePlusHOCb6[b6][k - 3];
          else
            Cik[2][k] = AmbeHOCb6[b6][k - 3];
#ifdef AMBE_DEBUG
          fprintf (stderr, "C2,%i: %f ", k, Cik[2][k]);
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
          if (dstar)
            Cik[3][k] = AmbePlusHOCb7[b7][k - 3];
          else
            Cik[3][k] = AmbeHOCb7[b7][k - 3];
#ifdef AMBE_DEBUG
          fprintf (stderr, "C3,%i: %f ", k, Cik[3][k]);
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
          if (dstar)
            Cik[4][k] = AmbePlusHOCb8[b8][k - 3];
          else
            Cik[4][k] = AmbeHOCb8[b8][k - 3];
#ifdef AMBE_DEBUG
          fprintf (stderr, "C4,%i: %f ", k, Cik[4][k]);
#endif
        }
    }
#ifdef AMBE_DEBUG
  fprintf (stderr, "\n");
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
              fprintf (stderr, "j: %i Cik[%i][%i]: %f ", j, i, k, Cik[i][k]);
#endif
              sum = sum + ((float) ak * Cik[i][k] * dct_cache->idct_cos[ji][j][k]);
            }
          Tl[l] = sum;
#ifdef AMBE_DEBUG
          fprintf (stderr, "Tl[%i]: %f\n", l, Tl[l]);
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
      fprintf (stderr, "flok%i: %f, intk%i: %i ", l, flokl[l], l, intkl[l]);
#endif
      // eq. 41
      deltal[l] = flokl[l] - (float) intkl[l];
#ifdef AMBE_DEBUG
      fprintf (stderr, "delta%i: %f ", l, deltal[l]);
#endif
      // eq 43: clamp upper tap to 56 so log2Ml[57] is never read out-of-bounds
      int upper = (intkl[l] + 1 > 56) ? 56 : (intkl[l] + 1);
      Sum43 = Sum43 + ((((float) 1 - deltal[l]) * prev_mp->log2Ml[intkl[l]]) + (deltal[l] * prev_mp->log2Ml[upper]));
    }
  Sum43 = (((dstar ? DSTAR_SPEC_PRED : 0.65f) / (float) cur_mp->L) * Sum43);
#ifdef AMBE_DEBUG
  fprintf (stderr, "\n");
  fprintf (stderr, "Sum43: %f\n", Sum43);
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
      c1 = ((dstar ? DSTAR_SPEC_PRED : 0.65f) * ((float) 1 - deltal[l]) * prev_mp->log2Ml[intkl[l]]);
      c2 = ((dstar ? DSTAR_SPEC_PRED : 0.65f) * deltal[l] * prev_mp->log2Ml[upper]);
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
      fprintf (stderr, "flokl[%i]: %f, intkl[%i]: %i ", l, flokl[l], l, intkl[l]);
      fprintf (stderr, "deltal[%i]: %f ", l, deltal[l]);
      fprintf (stderr, "prev_mp->log2Ml[%i]: %f\n", l, prev_mp->log2Ml[intkl[l]]);
      fprintf (stderr, "BigGamma: %f c1: %f c2: %f Sum43: %f Tl[%i]: %f log2Ml[%i]: %f Ml[%i]: %f\n", BigGamma, c1, c2, Sum43, l, Tl[l], l, cur_mp->log2Ml[l], l, cur_mp->Ml[l]);
#endif
    }

  return (0);
}

int
mbe_dequantizeAmbe2400Parms (mbe_parms * cur_mp, mbe_parms * prev_mp, const int *b){
	int dstar = 1;
	return (mbe_dequantizeAmbeParms (cur_mp, prev_mp, b, dstar));
}

int
mbe_dequantizeAmbe2450Parms (mbe_parms * cur_mp, mbe_parms * prev_mp, const int *b){
	int dstar = 0;
	return (mbe_dequantizeAmbeParms (cur_mp, prev_mp, b, dstar));
}

void encode_ambe(const IMBE_PARAM *imbe_param, int b[], mbe_parms*cur_mp, mbe_parms*prev_mp, bool dstar, float gain_adjust, float *gain_state) {
	static const float SQRT_2 = sqrtf(2.0);
	static const int b0_lmax = sizeof(b0_lookup) / sizeof(b0_lookup[0]);
	// int b[9];

	int L;
	// The harmonics the rest of this function codes: their count (nh), their
	// amplitudes (sa_w) and voicing (vuv_w). For DMR they are the IMBE
	// analysis' own values, unchanged.
	int nh = imbe_param->num_harms;
	float max_in_sa = 0.0f;
	for (int i1 = 0; i1 < nh && i1 < NUM_HARMS_MAX; i1++) {
		if ((float)imbe_param->sa[i1] > max_in_sa) {
			max_in_sa = (float)imbe_param->sa[i1];
		}
	}
	if (nh <= 0 || max_in_sa < 1.5f) {
		b[0] = 124;
		b[1] = dstar ? 15 : 16;
		b[2] = 0;
		b[3] = 0;
		b[4] = 0;
		b[5] = 0;
		b[6] = 0;
		b[7] = 0;
		b[8] = 0;
		if (gain_state) {
			*gain_state = -99.0f;
		}
		// Keep prev_mp prediction history intact across silence frames so encoder
		// and decoder predictor states remain 100% synchronized.
		mbe_moveMbeParms(prev_mp, cur_mp);
		cur_mp->w0 = ((float) 2 * (float) M_PI) / (float) 32;
		cur_mp->L = 14;
		for (int l = 0; l <= 56; l++) {
			cur_mp->Ml[l] = 0.0f;
			cur_mp->Vl[l] = 0;
		}
		return;
	}
	float sa_w[NUM_HARMS_MAX];
	short vuv_w[NUM_HARMS_MAX];
	if (dstar) {
		float f0_in = 256.0f / (float) imbe_param->ref_pitch;	// cycles per sample
		int b0 = (int) lrintf((-log2f(f0_in) - DSTAR_F0_C0) / DSTAR_F0_C1 - 0.5f);	// nearest code on the chip-measured scale
		if (b0 < 0) b0 = 0;
		if (b0 > 119) b0 = 119;	// 120..127 are erasure / silence / tone codes
		b[0] = b0;
		L = DSTAR_L(b0);
		float f0_c = make_f0(b0);
		const int n_in = imbe_param->num_harms;
		for (int l = 1; l <= L; l++) {
			// position of the decoder's harmonic l on the IMBE harmonic axis (1-based)
			float k = (float) l * f0_c / f0_in;
			float a;
			if (k <= 1.0f) {
				a = (float) imbe_param->sa[0];
			} else if (k >= (float) n_in) {
				a = (float) imbe_param->sa[n_in - 1] * exp2f(-1.0f * (k - (float) n_in));
			} else {
				int k0 = (int) k;	// 1-based lower neighbour
				float fr = k - (float) k0;
				float a0 = (float) imbe_param->sa[k0 - 1];
				float a1 = (float) imbe_param->sa[k0];
				if (a0 < 1.0f) a0 = 1.0f;
				if (a1 < 1.0f) a1 = 1.0f;
				a = exp2f((1.0f - fr) * log2f(a0) + fr * log2f(a1));
			}
			sa_w[l - 1] = a;
			int kn = (int) lrintf(k);
			if (kn < 1) kn = 1;
			if (kn > n_in) kn = n_in;
			vuv_w[l - 1] = imbe_param->v_uv_dsn[kn - 1];
		}
		nh = L;
	} else {
		// Smooth AMBE+2 pitch law of DVSI's AMBE-3000 (mbelib-neo)
		float f0_in = 256.0f / (float) imbe_param->ref_pitch;
		int b0 = (int) lrintf((MBE_AMBE2450_LOG2_F0_OFFSET - log2f(f0_in)) / MBE_AMBE2450_LOG2_F0_SLOPE);
		if (b0 < 0) b0 = 0;
		if (b0 > 119) b0 = 119;
		b[0] = b0;
		L = mbe_ambe2450_harmonic_count(b0);
		float f0_c = mbe_ambe2450_f0(b0);
		const int n_in = imbe_param->num_harms;
		for (int l = 1; l <= L; l++) {
			float k = (float) l * f0_c / f0_in;
			float a;
			if (k <= 1.0f) {
				a = (float) imbe_param->sa[0];
			} else if (k >= (float) n_in) {
				a = (float) imbe_param->sa[n_in - 1] * exp2f(-1.0f * (k - (float) n_in));
			} else {
				int k0 = (int) k;
				float fr = k - (float) k0;
				float a0 = (float) imbe_param->sa[k0 - 1];
				float a1 = (float) imbe_param->sa[k0];
				if (a0 < 1.0f) a0 = 1.0f;
				if (a1 < 1.0f) a1 = 1.0f;
				a = exp2f((1.0f - fr) * log2f(a0) + fr * log2f(a1));
			}
			sa_w[l - 1] = a;
			int kn = (int) lrintf(k);
			if (kn < 1) kn = 1;
			if (kn > n_in) kn = n_in;
			vuv_w[l - 1] = imbe_param->v_uv_dsn[kn - 1];
		}
		nh = L;
	}
	float m_float2[NUM_HARMS_MAX];
	float max_sa = 0.0f;
	for (int l=1; l <= L; l++) {
		if (sa_w[l-1] > max_sa) max_sa = sa_w[l-1];
		m_float2[l-1] = sa_w[l-1] ;
		m_float2[l-1] = m_float2[l-1] * m_float2[l-1];
	}

	// Emit an AMBE silence frame (b[0] = 124) when spectral energy is below threshold
	if (max_sa < 1.5f) {
		b[0] = 124;
		b[1] = dstar ? 15 : 16;
		b[2] = 0;
		b[3] = 0;
		b[4] = 0;
		b[5] = 0;
		b[6] = 0;
		b[7] = 0;
		b[8] = 0;
		if (gain_state) {
			*gain_state = -99.0f;
		}
		mbe_moveMbeParms(prev_mp, cur_mp);
		cur_mp->w0 = ((float) 2 * (float) M_PI) / (float) 32;
		cur_mp->L = 14;
		for (int l = 0; l <= 56; l++) {
			cur_mp->Ml[l] = 0.0f;
			cur_mp->Vl[l] = 0;
		}
		return;
	}

	const float f0_quant = dstar ? make_f0(b[0]) : mbe_ambe2450_f0(b[0]);

	// Low-pitch (< 95 Hz) background hum unvoicing guard (mbelib-neo #149):
	// At pitches below ~95 Hz, 60 Hz hum or low-level room noise can falsely pass
	// the IMBE voicing test in the lowest 500 Hz column. When pitch < 95 Hz and
	// frame spectral amplitude is low, mark weak sub-500 Hz harmonics unvoiced.
	if (f0_quant * 8000.0f < 95.0f && max_sa < 18.0f) {
		for (int l = 1; l <= L; l++) {
			if ((float)l * f0_quant * 8000.0f <= 500.0f && sa_w[l - 1] < 8.0f) {
				vuv_w[l - 1] = 0;
			}
		}
	}

	float en_min = 0;
	b[1] = 0;
	int vuv_max = (dstar) ? 16 : 17;
	for (int n=0; n < vuv_max; n++) {
		float En = 0;
		for (int l=1; l <= L; l++) {
			if (dstar) {
				if (vuv_w[l-1] != mbe_ambe2400_harmonic_voiced(n, l, f0_quant))
					En += m_float2[l-1];
			} else {
				int jl = (int) ((float) l * (float) 16.0 * f0_quant);
				if (jl < 0) jl = 0;
				if (jl > 7) jl = 7;
				if (vuv_w[l-1] != AmbeVuv[n][jl])
					En += m_float2[l-1];
			}
		}
		if (n == 0)
			en_min = En;
		else if (En < en_min) {
			b[1] = n;
			en_min = En;
		}
	}

	// log spectral amplitudes
	float num_harms_f = (float) nh;
	float log_l_2 = 0.5f * log2f(num_harms_f);
	float log_l_w0 = 0.5f * log2f(num_harms_f * f0_quant * 2.0f * (float)M_PI) + 1.40f;
	float lsa[NUM_HARMS_MAX];
	float lsa_sum=0.0;

	for (int i1 = 0; i1 < nh; i1++) {
		float sa = sa_w[i1];
		if (sa < 0.25f) sa = 0.25f;
		int quant_v;
		if (dstar) {
			quant_v = mbe_ambe2400_harmonic_voiced(b[1], i1 + 1, f0_quant);
		} else {
			int jl = (int) ((float) (i1 + 1) * (float) 16.0 * f0_quant);
			if (jl < 0) jl = 0;
			if (jl > 7) jl = 7;
			quant_v = AmbeVuv[b[1]][jl];
		}
		if (vuv_w[i1] || quant_v)
			lsa[i1] = log_l_2 + log2f(sa);
		else
			lsa[i1] = log_l_w0 + log2f(sa);
		lsa_sum += lsa[i1];
	}

	float gain = lsa_sum / num_harms_f;
	if (gain_state) {
		if (dstar) {
			if (*gain_state > -98.0f && fabsf(gain - *gain_state) < 2.5f) {
				gain = DSTAR_GAIN_SMOOTH * (*gain_state) + (1.0f - DSTAR_GAIN_SMOOTH) * gain;
			}
			*gain_state = gain;
		} else {
			*gain_state = gain;
		}
	}
	float diff_gain;
	(void)dstar;
	diff_gain = gain - 0.5f * prev_mp->gamma;

	diff_gain -= gain_adjust;

	float error = 0.0;
	int error_index = 0;
	int max_dg = (dstar) ? 64 : 32;
	for (int i1 = 0; i1 < max_dg; i1++) {
		float diff;
		if (dstar)
			diff = fabsf(diff_gain - AmbePlusDg[i1]);
		else
			diff = fabsf(diff_gain - AmbeDg[i1]);
		//fprintf(stderr, "%2.4f:%2.4f ", diff, error);
		if ((i1 == 0) || (diff < error)) {
			error = diff;
			error_index = i1;
		}
	}
	//fprintf(stderr, "\n");
	b[2] = error_index;

	// prediction residuals (mirror decoder's L > prev_mp->L edge extension)
	int prev_L = prev_mp->L;
	if (prev_L < 1) prev_L = 1;
	if (prev_L > 56) prev_L = 56;
	float prev_log2Ml[57];
	memcpy(prev_log2Ml, prev_mp->log2Ml, sizeof(prev_log2Ml));
	if (nh > prev_L) {
		for (int l = prev_L + 1; l <= nh && l <= 56; l++) {
			prev_log2Ml[l] = prev_log2Ml[prev_L];
		}
	}
	prev_log2Ml[0] = prev_log2Ml[1];

	float l_prev_l = (float) prev_L / num_harms_f;
	float T[NUM_HARMS_MAX];
	for (int i1 = 0; i1 < nh; i1++) {
		float kl = l_prev_l * (float)(i1+1);
		int kl_floor = (int) kl;
		if (kl_floor < 0) kl_floor = 0;
		if (kl_floor > 56) kl_floor = 56;
		int kl_upper = (kl_floor + 1 > 56) ? 56 : (kl_floor + 1);
		float kl_frac = kl - (float) kl_floor;
		const float pc = dstar ? DSTAR_SPEC_PRED : 0.65f;
		T[i1] = lsa[i1] - pc * (1.0f - kl_frac) * prev_log2Ml[kl_floor]
				- pc * kl_frac * prev_log2Ml[kl_upper];
	}

	// DCT
	const struct ambe_dct_cache *dct_cache = mbe_get_dct_cache();
	const int * J;
	if (dstar)
		J = AmbePlusLmprbl[nh];
	else
		J = AmbeLmprbl[nh];
	float * c[4];
	int acc = 0;
	for (int i=0; i<4; i++) {
		c[i] = &T[acc];
		acc += J[i];
	}
	float C[4][17];
	for (int i=1; i<=4; i++) {
		const int ji = J[i-1];
		for (int k=1; k<=ji; k++) {
			float s = 0.0;
			for (int j=1; j<=ji; j++) {
				s += (c[i-1][j-1] * dct_cache->idct_cos[ji][j][k]);
			}
			C[i-1][k-1] = s / (float)ji;
		}
	}
	float R[8];
	R[0] = C[0][0] + SQRT_2 * C[0][1];
	R[1] = C[0][0] - SQRT_2 * C[0][1];
	R[2] = C[1][0] + SQRT_2 * C[1][1];
	R[3] = C[1][0] - SQRT_2 * C[1][1];
	R[4] = C[2][0] + SQRT_2 * C[2][1];
	R[5] = C[2][0] - SQRT_2 * C[2][1];
	R[6] = C[3][0] + SQRT_2 * C[3][1];
	R[7] = C[3][0] - SQRT_2 * C[3][1];

	// encode PRBA
	float G[8];
	for (int m=1; m<=8; m++) {
		G[m-1] = 0.0;
		for (int i=1; i<=8; i++) {
			G[m-1] += (R[i-1] * dct_cache->ri_cos[m][i]);
		}
		G[m-1] /= 8.0;
	}
	for (int i=0; i<512; i++) {
		float err=0.0;
		float diff;
		if (dstar) {
			diff = G[1] - AmbePlusPRBA24[i][0];
			err += (diff * diff);
			diff = G[2] - AmbePlusPRBA24[i][1];
			err += (diff * diff);
			diff = G[3] - AmbePlusPRBA24[i][2];
			err += (diff * diff);
		} else {
			diff = G[1] - AmbePRBA24[i][0];
			err += (diff * diff);
			diff = G[2] - AmbePRBA24[i][1];
			err += (diff * diff);
			diff = G[3] - AmbePRBA24[i][2];
			err += (diff * diff);
		}
		if (i == 0 || err < error) {
			error = err;
			error_index = i;
		}
	}
	b[3] = error_index;

	// PRBA58
	for (int i=0; i<128; i++) {
		float err=0.0;
		float diff;
		if (dstar) {
			diff = G[4] - AmbePlusPRBA58[i][0];
			err += (diff * diff);
			diff = G[5] - AmbePlusPRBA58[i][1];
			err += (diff * diff);
			diff = G[6] - AmbePlusPRBA58[i][2];
			err += (diff * diff);
			diff = G[7] - AmbePlusPRBA58[i][3];
			err += (diff * diff);
		} else {
			diff = G[4] - AmbePRBA58[i][0];
			err += (diff * diff);
			diff = G[5] - AmbePRBA58[i][1];
			err += (diff * diff);
			diff = G[6] - AmbePRBA58[i][2];
			err += (diff * diff);
			diff = G[7] - AmbePRBA58[i][3];
			err += (diff * diff);
		}
		if (i == 0 || err < error) {
			error = err;
			error_index = i;
		}
	}
	b[4] = error_index;

	// higher order coeffs b5
	int ii = 1;
	if (J[ii-1] <= 2) {
		b[4+ii] = 0.0;
	} else {
		int max_5 = (dstar) ? 16 : 32;
		for (int n=0; n < max_5; n++) {
			float err=0.0;
			float diff;
			for (int j=1; j <= J[ii-1]-2 && j <= 4; j++) {
				if (dstar)
					diff = AmbePlusHOCb5[n][j-1] - C[ii-1][j+2-1];
				else
					diff = AmbeHOCb5[n][j-1] - C[ii-1][j+2-1];
				err += (diff * diff);
			}
			if (n == 0 || err < error) {
				error = err;
				error_index = n;
			}
		}
		b[4+ii] = error_index;
	}

	// higher order coeffs b6
	ii = 2;
	if (J[ii-1] <= 2) {
		b[4+ii] = 0.0;
	} else {
		for (int n=0; n < 16; n++) {
			float err=0.0;
			float diff;
			for (int j=1; j <= J[ii-1]-2 && j <= 4; j++) {
				if (dstar)
					diff = AmbePlusHOCb6[n][j-1] - C[ii-1][j+2-1];
				else
					diff = AmbeHOCb6[n][j-1] - C[ii-1][j+2-1];
				err += (diff * diff);
			}
			if (n == 0 || err < error) {
				error = err;
				error_index = n;
			}
		}
		b[4+ii] = error_index;
	}

	// higher order coeffs b7
	ii = 3;
	if (J[ii-1] <= 2) {
		b[4+ii] = 0.0;
	} else {
		for (int n=0; n < 16; n++) {
			float err=0.0;
			float diff;
			for (int j=1; j <= J[ii-1]-2 && j <= 4; j++) {
				if (dstar)
					diff = AmbePlusHOCb7[n][j-1] - C[ii-1][j+2-1];
				else
					diff = AmbeHOCb7[n][j-1] - C[ii-1][j+2-1];
				err += (diff * diff);
			}
			if (n == 0 || err < error) {
				error = err;
				error_index = n;
			}
		}
		b[4+ii] = error_index;
	}

	// higher order coeffs b8
	ii = 4;
	if (J[ii-1] <= 2) {
		b[4+ii] = 0.0;
	} else {
		int max_8 = (dstar) ? 16 : 8;
		const int step_8 = (dstar) ? 2 : 1;
		for (int n=0; n < max_8; n += step_8) {
			float err=0.0;
			float diff;
			for (int j=1; j <= J[ii-1]-2 && j <= 4; j++) {
				if (dstar)
					diff = AmbePlusHOCb8[n][j-1] - C[ii-1][j+2-1];
				else
					diff = AmbeHOCb8[n][j-1] - C[ii-1][j+2-1];
				err += (diff * diff);
			}
			if (n == 0 || err < error) {
				error = err;
				error_index = n;
			}
		}
		b[4+ii] = error_index;
	}
	//fprintf (stderr, "B\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\n", b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7], b[8]);
	//int rc;
	if (dstar)
		mbe_dequantizeAmbe2400Parms (cur_mp, prev_mp, b);
	else
		mbe_dequantizeAmbe2450Parms (cur_mp, prev_mp, b);
	mbe_moveMbeParms (cur_mp, prev_mp);
}

void
mbe_moveMbeParms (mbe_parms * cur_mp, mbe_parms * prev_mp)
{
  if (!cur_mp || !prev_mp) {
    return;
  }
  *prev_mp = *cur_mp;
  prev_mp->Ml[0] = 0.0f;
}

void
mbe_useLastMbeParms (mbe_parms * cur_mp, mbe_parms * prev_mp)
{
  if (!cur_mp || !prev_mp) {
    return;
  }
  *cur_mp = *prev_mp;
  cur_mp->Ml[0] = 0.0f;
}

void
mbe_initMbeParms (mbe_parms * cur_mp, mbe_parms * prev_mp, mbe_parms * prev_mp_enhanced)
{
  int l;
  if (!cur_mp || !prev_mp || !prev_mp_enhanced) {
    return;
  }
  memset (prev_mp, 0, sizeof (mbe_parms));
  prev_mp->w0 = 0.09378f;
  prev_mp->L = 30;
  prev_mp->K = 10;
  prev_mp->gamma = 0.0f;
  for (l = 0; l <= 56; l++)
    {
      prev_mp->Ml[l] = 0.0f;
      prev_mp->Vl[l] = 0;
      prev_mp->log2Ml[l] = 0.0f;
      prev_mp->PHIl[l] = 0.0f;
      prev_mp->PSIl[l] = ((float) M_PI / 2.0f);
    }
  prev_mp->repeat = 0;
  prev_mp->noiseSeed = -1.0f;
  prev_mp->errorRate = 0.0f;
  prev_mp->errorCountTotal = 0;
  prev_mp->errorCount4 = 0;
  prev_mp->localEnergy = 75000.0f;
  prev_mp->amplitudeThreshold = 20480;
  prev_mp->mutingThreshold = 0.096f;
  mbe_moveMbeParms (prev_mp, cur_mp);
  mbe_moveMbeParms (prev_mp, prev_mp_enhanced);
}

/*
 * High-band shelf compensation (mbelib-neo):
 * 0 dB below 2.5 kHz, rising linearly to `gain` at 3.0 kHz, flat to 3.6 kHz,
 * and returning to 0 dB at 3.8 kHz.
 */
static inline float
mbe_high_band_gain (float hz, float gain)
{
  if (gain == 1.0f || hz <= 2500.0f || hz >= 3800.0f) {
    return 1.0f;
  }
  float t;
  if (hz < 3000.0f) {
    t = (hz - 2500.0f) / 500.0f;
  } else if (hz <= 3600.0f) {
    t = 1.0f;
  } else {
    t = (3800.0f - hz) / 200.0f;
  }
  return 1.0f + ((gain - 1.0f) * t);
}

/*
 * Overflow-safe spectral amplitude enhancement (mbelib-neo):
 * Evaluates W_l using normalized r = Rm1 / Rm0 and inv_sqrt_rm0 = 1 / sqrt(Rm0)
 * with harmonic cosine recurrence, eliminating the O(Rm0^3) float overflow on
 * loud frames, with double-precision fallback for near-singular spectra.
 */
float
mbe_spectralAmpEnhanceWithRm0 (mbe_parms * cur_mp, float high_band_gain)
{
  if (!cur_mp || cur_mp->L < 1 || cur_mp->L > 56 || !(cur_mp->w0 > 0.0f)) {
    return 0.0f;
  }

  const int L = cur_mp->L;
  float cos_tab[57];
  const float c_step = cosf (cur_mp->w0);
  const float s_step = sinf (cur_mp->w0);
  float c = 1.0f;
  float s = 0.0f;
  for (int l = 1; l <= L; ++l) {
    const float cn = (c * c_step) - (s * s_step);
    const float sn = (s * c_step) + (c * s_step);
    c = cn;
    s = sn;
    cos_tab[l] = c;
  }

  double rm0_d = 0.0;
  double rm1_d = 0.0;
  for (int l = 1; l <= L; ++l) {
    const double ml = (double) cur_mp->Ml[l];
    const double p = ml * ml;
    rm0_d += p;
    rm1_d += p * (double) cos_tab[l];
  }

  if (!(rm0_d > 0.0)) {
    return 0.0f;
  }

  const double r = rm1_d / rm0_d;
  const double denom_factor = 1.0 - (r * r);
  const int singular = !(cur_mp->w0 * denom_factor > 1e-12);
  const double shape = (0.96 * M_PI) / (singular ? (double) cur_mp->w0 : ((double) cur_mp->w0 * denom_factor));
  const double inv_sqrt_rm0 = 1.0 / sqrt (rm0_d);

  double sum_d = 0.0;
  for (int l = 1; l <= L; ++l) {
    const double ml = (double) cur_mp->Ml[l];
    if (ml != 0.0 && (8 * l) > L) {
      const double spread = singular ? 1.0 : fmax ((1.0 + (r * r)) - (2.0 * r * (double) cos_tab[l]), 0.0);
      double wl = sqrt (fabs (ml) * inv_sqrt_rm0) * sqrt (sqrt (shape * spread));
      if (wl > 1.2) wl = 1.2;
      else if (wl < 0.5) wl = 0.5;
      cur_mp->Ml[l] = (float) (ml * wl);
    }
    const double m_enh = (double) cur_mp->Ml[l];
    sum_d += m_enh * m_enh;
  }

  const float gamma = (sum_d > 0.0) ? (float) sqrt (rm0_d / sum_d) : 1.0f;
  const float hz_per_harm = cur_mp->w0 * (8000.0f / (2.0f * (float) M_PI));
  for (int l = 1; l <= L; ++l) {
    cur_mp->Ml[l] *= gamma * mbe_high_band_gain (hz_per_harm * (float) l, high_band_gain);
  }

  return (rm0_d > (double) FLT_MAX) ? FLT_MAX : (float) rm0_d;
}

void
mbe_spectralAmpEnhance (mbe_parms * cur_mp)
{
  (void) mbe_spectralAmpEnhanceWithRm0 (cur_mp, 1.0f);
}

/*
 * TIA-102.BABA Equations 111-116 Adaptive Smoothing under channel bit errors.
 */
static void
mbe_applyAdaptiveSmoothingWithRm0 (mbe_parms * cur_mp, const mbe_parms * prev_mp, float rm0)
{
  if (!cur_mp || !prev_mp || cur_mp->L < 1 || cur_mp->L > 56) {
    return;
  }
  float prior_energy = prev_mp->localEnergy;
  int prior_threshold = prev_mp->amplitudeThreshold;
  if (prior_energy < 10000.0f) {
    prior_energy = 75000.0f;
  }
  if (prior_threshold <= 0) {
    prior_threshold = 20480;
  }

  float energy = (0.95f * prior_energy) + (0.05f * rm0);
  if (energy < 10000.0f) {
    energy = 10000.0f;
  }
  cur_mp->localEnergy = energy;

  if (cur_mp->errorRate <= 0.0125f && cur_mp->errorCountTotal <= 4) {
    cur_mp->amplitudeThreshold = 20480;
    return;
  }

  float threshold = FLT_MAX;
  if (cur_mp->errorRate > 0.005f || cur_mp->errorCountTotal > 4) {
    const float root = sqrtf (sqrtf (sqrtf (energy)));
    const float energy_power = (root * root) * root;
    if (cur_mp->errorRate <= 0.0125f && cur_mp->errorCount4 == 0) {
      threshold = (45.255f * energy_power) / expf (277.26f * cur_mp->errorRate);
    } else {
      threshold = 1.414f * energy_power;
    }
  }

  double amplitude_sum = 0.0;
  for (int l = 1; l <= cur_mp->L; ++l) {
    if (cur_mp->Ml[l] > threshold) {
      cur_mp->Vl[l] = 1;
    }
    amplitude_sum += (double) cur_mp->Ml[l];
  }

  int updated_thresh = 20480;
  if (cur_mp->errorRate > 0.005f || cur_mp->errorCountTotal > 6) {
    int64_t t64 = (int64_t) prior_threshold + 6000 - (int64_t) 300 * cur_mp->errorCountTotal;
    if (t64 > INT_MAX) t64 = INT_MAX;
    if (t64 < INT_MIN) t64 = INT_MIN;
    updated_thresh = (int) t64;
  }
  cur_mp->amplitudeThreshold = updated_thresh;

  float scale = 1.0f;
  if (amplitude_sum > 0.0 && (double) updated_thresh <= amplitude_sum) {
    scale = (float) ((double) updated_thresh / amplitude_sum);
  }
  if (scale < 0.0f) {
    scale = 0.0f;
  }
  for (int l = 1; l <= cur_mp->L; ++l) {
    cur_mp->Ml[l] *= scale;
  }
}

void
mbe_synthesizeSilencef (float *aout_buf)
{
  if (!aout_buf) {
    return;
  }
  memset (aout_buf, 0, 160 * sizeof (float));
}

void
mbe_synthesizeSilence (short *aout_buf)
{
  if (!aout_buf) {
    return;
  }
  memset (aout_buf, 0, 160 * sizeof (short));
}

/*
 * DVSI AMBE-3000 100 Hz 2nd-order Butterworth output high-pass magnitude & phase
 * response (mbelib-neo).
 */
#define MBE_LOW_BAND_CORNER_HZ 100.0f

static inline float
mbe_low_band_gain (float hz)
{
  if (!(hz > 0.0f)) {
    return 0.0f;
  }
  const float hz2 = hz * hz;
  const float corner2 = MBE_LOW_BAND_CORNER_HZ * MBE_LOW_BAND_CORNER_HZ;
  return hz2 / sqrtf ((hz2 * hz2) + (corner2 * corner2));
}

static inline float
mbe_low_band_phase (float hz)
{
  if (!(hz > 0.0f)) {
    return 0.0f;
  }
  const double x = (double) hz / (double) MBE_LOW_BAND_CORNER_HZ;
  return (float) (M_PI - atan2 (M_SQRT2 * x, 1.0 - (x * x)));
}

/*
 * Regenerated voiced harmonic phases (US Patent 5,701,390 Eqs. 7-9):
 * 19-tap odd 1/m edge-detection kernel over the log2 spectral envelope.
 */
static void
mbe_regenerate_harmonic_phases (const mbe_parms * cur_mp, float phi[57])
{
  constexpr int D = 19;
  const int L = (cur_mp->L < 1) ? 1 : ((cur_mp->L > 56) ? 56 : cur_mp->L);
  static const float inv_odd[10] = {
    1.0f, 1.0f / 3.0f, 1.0f / 5.0f, 1.0f / 7.0f, 1.0f / 9.0f,
    1.0f / 11.0f, 1.0f / 13.0f, 1.0f / 15.0f, 1.0f / 17.0f, 1.0f / 19.0f
  };
  float B[56 + 2 * D + 1];
  B[D] = 0.0f;
  for (int l = 1; l <= L; ++l) {
    B[D + l] = log2f (fmaxf (cur_mp->Ml[l], 1e-6f));
  }
  for (int l = L + 1; l <= 56 + D; ++l) {
    B[D + l] = B[D + L] - 0.72f * (float) (l - L);
  }
  for (int l = 1; l <= D; ++l) {
    B[D - l] = B[D + l];
  }
  for (int l = 1; l <= 56; ++l) {
    float acc = 0.0f;
    for (int k = 0; k < 10; ++k) {
      const int m = 2 * k + 1;
      acc += (B[D + l + m] - B[D + l - m]) * inv_odd[k];
    }
    phi[l] = 0.44f * acc;
  }
}

/*
 * Self-contained, zero-allocation 256-point radix-2 FFT / IFFT and TIA-102.BABA
 * Algorithms #117-126 WOLA unvoiced synthesis (mbelib-neo).
 */
struct mbe_fft256_tables
{
  int inited;
  uint8_t bitrev[256];
  float cos_tab[128];
  float sin_tab[128];
};

static inline const struct mbe_fft256_tables *
mbe_get_fft256_tables (void)
{
  static struct mbe_fft256_tables s_fft = { 0, {0}, {0.0f}, {0.0f} };
  if (!s_fft.inited) {
    for (int i = 0; i < 256; ++i) {
      uint8_t r = 0;
      for (int b = 0; b < 8; ++b) {
        r = (uint8_t) ((r << 1) | ((i >> b) & 1));
      }
      s_fft.bitrev[i] = r;
    }
    for (int k = 0; k < 128; ++k) {
      const double ang = -2.0 * M_PI * (double) k / 256.0;
      s_fft.cos_tab[k] = (float) cos (ang);
      s_fft.sin_tab[k] = (float) sin (ang);
    }
    s_fft.inited = 1;
  }
  return &s_fft;
}

static void
mbe_fft256_complex (float re[256], float im[256], int inverse)
{
  const struct mbe_fft256_tables *t = mbe_get_fft256_tables ();
  for (int i = 0; i < 256; ++i) {
    const int j = t->bitrev[i];
    if (i < j) {
      const float tr = re[i]; re[i] = re[j]; re[j] = tr;
      const float ti = im[i]; im[i] = im[j]; im[j] = ti;
    }
  }
  const float sign = inverse ? -1.0f : 1.0f;
  for (int len = 2, step = 128; len <= 256; len <<= 1, step >>= 1) {
    const int half = len >> 1;
    for (int i = 0; i < 256; i += len) {
      for (int k = 0, tw = 0; k < half; ++k, tw += step) {
        const float wr = t->cos_tab[tw];
        const float wi = sign * t->sin_tab[tw];
        const int u = i + k;
        const int v = u + half;
        const float tr = wr * re[v] - wi * im[v];
        const float ti = wr * im[v] + wi * re[v];
        re[v] = re[u] - tr;
        im[v] = im[u] - ti;
        re[u] += tr;
        im[u] += ti;
      }
    }
  }
}

static inline float
mbe_unvoiced_synth_window (int n)
{
  if (n <= -105 || n >= 105) {
    return 0.0f;
  }
  if (n < 0) {
    n = -n;
  }
  if (n <= 55) {
    return 1.0f;
  }
  return 0.02f * (float) (105 - n);
}

static void
mbe_generate_noise_with_overlap (float buffer[256], float *seed, float overlap[96])
{
  if (*seed < 0.0f) {
    memset (buffer, 0, 256 * sizeof (float));
    memset (overlap, 0, 96 * sizeof (float));
    *seed = 3147.0f;
    return;
  }
  memcpy (buffer, overlap, 96 * sizeof (float));
  uint32_t state = ((uint32_t) (*seed)) % 53125u;
  for (int i = 96; i < 256; ++i) {
    buffer[i] = (float) state;
    state = (171u * state + 11213u) % 53125u;
  }
  *seed = (float) state;
  memcpy (overlap, buffer + 160, 96 * sizeof (float));
}

static void
mbe_synthesizeUnvoicedFFT (float aout_buf[160], mbe_parms * cur_mp, const mbe_parms * prev_mp, const float noise_buffer[256])
{
  float re[256];
  float im[256];
  float shaped_re[256];
  float shaped_im[256];

  for (int i = 0; i < 256; ++i) {
    re[i] = noise_buffer[i] * mbe_unvoiced_synth_window (i - 128);
    im[i] = 0.0f;
  }
  mbe_fft256_complex (re, im, 0);

  memset (shaped_re, 0, sizeof (shaped_re));
  memset (shaped_im, 0, sizeof (shaped_im));

  const float spacing = (256.0f / (2.0f * (float) M_PI)) * cur_mp->w0;
  for (int l = 1; l <= cur_mp->L; ++l) {
    if (cur_mp->Vl[l] != 0) {
      continue;
    }
    const float f_low = ((float) l - 0.5f) * spacing;
    const float f_high = ((float) l + 0.5f) * spacing;
    int lower = (f_low >= 128.0f) ? 128 : (int) ceilf (f_low);
    int upper = (f_high >= 128.0f) ? 128 : (int) ceilf (f_high);
    if (lower < 0) lower = 0;
    if (upper <= lower) {
      continue;
    }
    float power = 0.0f;
    for (int k = lower; k < upper; ++k) {
      const float r = re[k];
      const float m = (k == 0) ? 0.0f : im[k];
      power += (r * r) + (m * m);
    }
    if (power <= 1.0e-10f) {
      continue;
    }
    // Scale unvoiced bins on TIA-102.BABA Algorithm #120 scale (146.17696)
    const float scale = (146.17696f * cur_mp->Ml[l]) / sqrtf (power / (float) (upper - lower));
    for (int k = lower; k < upper; ++k) {
      shaped_re[k] = re[k] * scale;
      if (k > 0 && k < 128) {
        shaped_im[k] = im[k] * scale;
        shaped_re[256 - k] = shaped_re[k];
        shaped_im[256 - k] = -shaped_im[k];
      }
    }
  }

  mbe_fft256_complex (shaped_re, shaped_im, 1);
  for (int i = 0; i < 256; ++i) {
    shaped_re[i] *= (1.0f / 256.0f);
  }

  // Algorithm #126: Weighted Overlap-Add (WOLA)
  // Only read the buffers where the corresponding window is non-zero: for n < 56 the current-frame
  // index (128 + n - 160) is negative and for n > 127 the previous-frame index (128 + n) is past the
  // end of previousUw. Those terms carry zero weight, but reading them pulled in adjacent stack/heap
  // garbage, and a NaN bit-pattern there (NaN * 0 == NaN) saturated the whole output frame.
  for (int n = 0; n < 160; ++n) {
    const float w_prev = mbe_unvoiced_synth_window (n);
    const float w_curr = mbe_unvoiced_synth_window (n - 160);
    const float weight = (w_prev * w_prev) + (w_curr * w_curr);
    if (weight > 0.0f) {
      float sample = 0.0f;
      if (w_prev > 0.0f && (128 + n) < 256) {
        sample += w_prev * prev_mp->previousUw[128 + n];
      }
      if (w_curr > 0.0f && (128 + n - 160) >= 0) {
        sample += w_curr * shaped_re[128 + n - 160];
      }
      aout_buf[n] += sample / weight;
    }
  }

  memcpy (cur_mp->previousUw, shaped_re, sizeof (cur_mp->previousUw));
}

void
mbe_synthesizeSpeechWithRm0f (float *aout_buf, mbe_parms * cur_mp, mbe_parms * prev_mp, int uvquality, float pre_enh_rm0, int is_repeat)
{
  (void) uvquality;
  const int N = 160;

  if (!aout_buf) {
    return;
  }
  if (!cur_mp || !prev_mp || cur_mp->L < 1 || cur_mp->L > 56 || prev_mp->L < 1 || prev_mp->L > 56) {
    mbe_synthesizeSilencef (aout_buf);
    return;
  }

  if (!is_repeat) {
    if (pre_enh_rm0 > 0.0f) {
      mbe_applyAdaptiveSmoothingWithRm0 (cur_mp, prev_mp, pre_enh_rm0);
    } else {
      float rm0 = 0.0f;
      for (int l = 1; l <= cur_mp->L; ++l) {
        rm0 += cur_mp->Ml[l] * cur_mp->Ml[l];
      }
      mbe_applyAdaptiveSmoothingWithRm0 (cur_mp, prev_mp, rm0);
    }
  }

  // Algorithm #117: generate unvoiced LCG noise buffer with 96-sample overlap
  float noise_buffer[256];
  mbe_generate_noise_with_overlap (noise_buffer, &cur_mp->noiseSeed, cur_mp->noiseOverlap);

  memset (aout_buf, 0, N * sizeof (float));

  // eq 128 and 129: reconcile different harmonic counts between frames
  int maxl;
  if (cur_mp->L > prev_mp->L) {
    maxl = cur_mp->L;
    for (int l = prev_mp->L + 1; l <= maxl; ++l) {
      prev_mp->Ml[l] = 0.0f;
      prev_mp->Vl[l] = 1;
    }
  } else {
    maxl = prev_mp->L;
    for (int l = cur_mp->L + 1; l <= maxl; ++l) {
      cur_mp->Ml[l] = 0.0f;
      cur_mp->Vl[l] = 1;
    }
  }

  const float cw0 = cur_mp->w0;
  const float pw0 = prev_mp->w0;
  const float two_pi = 2.0f * (float) M_PI;

  // Wrap integrated phase PSIl modulo 2*pi and add regenerated spectral-envelope phase
  float phi[57];
  mbe_regenerate_harmonic_phases (cur_mp, phi);
  for (int l = 1; l <= 56; ++l) {
    float prev_psil_wrapped = fmodf (prev_mp->PSIl[l], two_pi);
    if (prev_psil_wrapped < 0.0f) {
      prev_psil_wrapped += two_pi;
    }
    prev_mp->PSIl[l] = prev_psil_wrapped;
    cur_mp->PSIl[l] = prev_psil_wrapped + ((pw0 + cw0) * ((float) (l * N) / 2.0f));
    cur_mp->PHIl[l] = cur_mp->PSIl[l] + phi[l];
  }

  // Temporarily apply DVSI 100 Hz output high-pass magnitude/phase and voiced/unvoiced
  // render gains (+0.45 dB voiced / -0.4 dB unvoiced), restoring model state after synthesis
  float cur_saved_Ml[57], cur_saved_PHIl[57];
  float prev_saved_Ml[57], prev_saved_PHIl[57];
  const int shared_model = (prev_mp == cur_mp);
  constexpr float kVoicedGainDvsi = 1.05317369f;   // +0.45 dB
  constexpr float kUnvoicedGainDvsi = 0.95499259f; // -0.40 dB

  const float cur_hz_per_harm = cw0 * (8000.0f / two_pi);
  for (int l = 1; l <= 56; ++l) {
    const float hz = cur_hz_per_harm * (float) l;
    const int voiced = (l <= cur_mp->L) && (cur_mp->Vl[l] == 1);
    cur_saved_Ml[l] = cur_mp->Ml[l];
    cur_saved_PHIl[l] = cur_mp->PHIl[l];
    cur_mp->Ml[l] *= mbe_low_band_gain (hz) * (voiced ? kVoicedGainDvsi : kUnvoicedGainDvsi);
    if (l <= maxl && cur_mp->Vl[l] == 1) {
      cur_mp->PHIl[l] += mbe_low_band_phase (hz);
    }
  }

  if (!shared_model) {
    const float prev_hz_per_harm = pw0 * (8000.0f / two_pi);
    for (int l = 1; l <= 56; ++l) {
      const float hz = prev_hz_per_harm * (float) l;
      const int voiced = (l <= prev_mp->L) && (prev_mp->Vl[l] == 1);
      prev_saved_Ml[l] = prev_mp->Ml[l];
      prev_saved_PHIl[l] = prev_mp->PHIl[l];
      prev_mp->Ml[l] *= mbe_low_band_gain (hz) * (voiced ? kVoicedGainDvsi : kUnvoicedGainDvsi);
      if (l <= maxl && prev_mp->Vl[l] == 1) {
        prev_mp->PHIl[l] += mbe_low_band_phase (hz);
      }
    }
  }

  // Synthesize voiced harmonics (DVSI AMBE-3000 fourth-root amplitude interpolation
  // and quadratic phase interpolation on all steady-pitch harmonics; 2-multiply
  // oscillator recurrence with Ws[] windowing on transitions)
  for (int l = 1; l <= maxl; ++l) {
    const float cw0l = cw0 * (float) l;
    const float pw0l = pw0 * (float) l;
    const int cur_voiced = (cur_mp->Vl[l] == 1) && (cw0l < (float) M_PI);
    const int prev_voiced = (prev_mp->Vl[l] == 1) && (pw0l < (float) M_PI);

    if (!cur_voiced && !prev_voiced) {
      continue;
    }

    const int both_positive = (prev_mp->Ml[l] > 0.0f) && (cur_mp->Ml[l] > 0.0f);
    if (cur_voiced && prev_voiced && (fabsf (cw0 - pw0) < (0.1f * cw0)) && (both_positive || l < 8)) {
      const float deltaphil = cur_mp->PHIl[l] - prev_mp->PHIl[l] - (((pw0 + cw0) * (float) (l * N)) / 2.0f);
      const float deltawl = (1.0f / (float) N) * (deltaphil - (two_pi * floorf ((deltaphil + (float) M_PI) / two_pi)));
      const float prev_coord = both_positive ? sqrtf (sqrtf (prev_mp->Ml[l])) : prev_mp->Ml[l];
      const float cur_coord = both_positive ? sqrtf (sqrtf (cur_mp->Ml[l])) : cur_mp->Ml[l];
      const float diff_coord = cur_coord - prev_coord;
      const float inv_N = 1.0f / (float) N;
      const float freq = pw0l + deltawl;
      const float phase_quad = ((cw0 - pw0) * (float) l) / (float) (2 * N);

      for (int n = 0; n < N; ++n) {
        const float fn = (float) n;
        const float thetaln = prev_mp->PHIl[l] + (freq * fn) + (phase_quad * fn * fn);
        float aln = prev_coord + (fn * inv_N * diff_coord);
        if (both_positive) {
          const float sq = aln * aln;
          aln = sq * sq;
        }
        aout_buf[n] += aln * cosf (thetaln);
      }
    } else {
      if (prev_voiced) {
        const float gain_prev = prev_mp->Ml[l];
        const float cd_p = cosf (pw0l);
        const float sd_p = sinf (pw0l);
        float c_p = cosf (prev_mp->PHIl[l]);
        float s_p = sinf (prev_mp->PHIl[l]);
        for (int n = 0; n < N; ++n) {
          aout_buf[n] += Ws[n + N] * gain_prev * c_p;
          const float next_c = (c_p * cd_p) - (s_p * sd_p);
          s_p = (s_p * cd_p) + (c_p * sd_p);
          c_p = next_c;
        }
      }
      if (cur_voiced) {
        const float gain_cur = cur_mp->Ml[l];
        const float cd_c = cosf (cw0l);
        const float sd_c = sinf (cw0l);
        const float init_phase = cur_mp->PHIl[l] - (cw0l * (float) N);
        float c_c = cosf (init_phase);
        float s_c = sinf (init_phase);
        for (int n = 0; n < N; ++n) {
          aout_buf[n] += Ws[n] * gain_cur * c_c;
          const float next_c = (c_c * cd_c) - (s_c * sd_c);
          s_c = (s_c * cd_c) + (c_c * sd_c);
          c_c = next_c;
        }
      }
    }
  }

  // Synthesize unvoiced bands via 256-point FFT + WOLA (Algorithms #117-126)
  mbe_synthesizeUnvoicedFFT (aout_buf, cur_mp, prev_mp, noise_buffer);

  // Restore decoded model amplitudes and phases
  for (int l = 1; l <= 56; ++l) {
    cur_mp->Ml[l] = cur_saved_Ml[l];
    cur_mp->PHIl[l] = cur_saved_PHIl[l];
  }
  if (!shared_model) {
    for (int l = 1; l <= 56; ++l) {
      prev_mp->Ml[l] = prev_saved_Ml[l];
      prev_mp->PHIl[l] = prev_saved_PHIl[l];
    }
  }
}

void
mbe_synthesizeSpeechf (float *aout_buf, mbe_parms * cur_mp, mbe_parms * prev_mp, int uvquality)
{
  mbe_synthesizeSpeechWithRm0f (aout_buf, cur_mp, prev_mp, uvquality, 0.0f, 0);
}

void
mbe_synthesizeSpeech (short *aout_buf, mbe_parms * cur_mp, mbe_parms * prev_mp, int uvquality)
{
  float float_buf[160];

  mbe_synthesizeSpeechf (float_buf, cur_mp, prev_mp, uvquality);
  mbe_floattoshort (float_buf, aout_buf);
}

void
mbe_floattoshort (float *float_buf, short *aout_buf)
{

  short *aout_buf_p;
  float *float_buf_p;
  int i, again;
  float audio;

  again = 7;
  aout_buf_p = aout_buf;
  float_buf_p = float_buf;
  for (i = 0; i < 160; i++)
    {
      audio = again * *float_buf_p;
      if (audio > 32760)
        {
#ifdef MBE_DEBUG
          printf ("audio clip: %f\n", audio);
#endif
          audio = 32760;
        }
      else if (audio < -32760)
        {
#ifdef MBE_DEBUG
          printf ("audio clip: %f\n", audio);
#endif
          audio = -32760;
        }
      *aout_buf_p = (short) (audio);
      aout_buf_p++;
      float_buf_p++;
    }
}

void
mbe_checkGolayBlock (long int *block)
{

  static int i, syndrome, eccexpected, eccbits, databits;
  long int mask, block_l;

  block_l = *block;

  mask = 0x400000l;
  eccexpected = 0;
  for (i = 0; i < 12; i++)
    {
      if ((block_l & mask) != 0l)
        {
          eccexpected ^= golayGenerator[i];
        }
      mask = mask >> 1;
    }
  eccbits = (int) (block_l & 0x7ffl);
  syndrome = eccexpected ^ eccbits;

  databits = (int) (block_l >> 11);
  databits = databits ^ golayMatrix[syndrome];

  *block = (long) databits;
}

int
mbe_golay2312 (char *in, char *out)
{

  int i, errs;
  long block;

  block = 0;
  for (i = 22; i >= 0; i--)
    {
      block = block << 1;
      block = block + in[i];
    }

  mbe_checkGolayBlock (&block);

  for (i = 22; i >= 11; i--)
    {
      out[i] = (block & 2048) >> 11;
      block = block << 1;
    }
  for (i = 10; i >= 0; i--)
    {
      out[i] = in[i];
    }

  errs = 0;
  for (i = 22; i >= 11; i--)
    {
      if (out[i] != in[i])
        {
          errs++;
        }
    }
  return (errs);
}

void
mbe_golay2312_encode (const char in[12], char out[23])
{
  int i;
  int data = 0;
  int ecc = 0;
  for (i = 0; i < 12; i++)
    {
      int bit = in[i] & 1;
      data = (data << 1) | bit;
      if (bit)
        {
          ecc ^= golayGenerator[i];
        }
      out[22 - i] = (char) bit;
    }
  for (i = 10; i >= 0; i--)
    {
      out[i] = (char) ((ecc >> i) & 1);
    }
}

int
mbe_eccAmbe3600C0Counted (char ambe_fr[4][24])
{
  int i, j;
  char in[23], out[23], data[12], codeword[23];
  for (j = 0; j < 23; j++)
    {
      in[j] = ambe_fr[0][j + 1];
    }
  (void) mbe_golay2312 (in, out);
  for (i = 0; i < 12; i++)
    {
      data[i] = out[22 - i];
    }
  mbe_golay2312_encode (data, codeword);
  int errs = 0;
  for (i = 0; i < 23; i++)
    {
      errs += ((codeword[i] & 1) != (in[i] & 1));
    }
  int parity = 0;
  for (j = 0; j < 23; j++)
    {
      ambe_fr[0][j + 1] = codeword[j];
      parity ^= (codeword[j] & 1);
    }
  if ((ambe_fr[0][0] & 1) != parity)
    {
      ambe_fr[0][0] = (char) parity;
      errs++;
    }
  return errs;
}

int
mbe_eccAmbe3600DataCounted (char ambe_fr[4][24], char *ambe_d)
{
  int j;
  char *ambe, gin[23], gout[23], codeword[23];

  memcpy (gin, ambe_fr[1], 23);

  ambe = ambe_d;
  // copy C0 data bits
  for (j = 23; j > 11; j--)
    {
      *ambe = ambe_fr[0][j];
      ambe++;
    }

  // ecc and copy C1
  (void) mbe_golay2312 (gin, gout);
  for (j = 22; j > 10; j--)
    {
      *ambe = gout[j];
      ambe++;
    }

  // copy C2
  for (j = 10; j >= 0; j--)
    {
      *ambe = ambe_fr[2][j];
      ambe++;
    }

  // copy C3
  for (j = 13; j >= 0; j--)
    {
      *ambe = ambe_fr[3][j];
      ambe++;
    }

  // Re-encode C1's 12 decoded data bits (in ambe_d[12..23], MSB first)
  // to count parity errors too (TIA-102.BABA-1 §5.5).
  mbe_golay2312_encode (ambe_d + 12, codeword);
  int errs = 0;
  for (j = 0; j < 23; j++)
    {
      errs += ((codeword[j] & 1) != (gin[j] & 1));
    }
  return errs;
}
