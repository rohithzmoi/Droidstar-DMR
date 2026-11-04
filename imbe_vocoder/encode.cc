/*
 * Project 25 IMBE Encoder/Decoder Fixed-Point implementation
 * Developed by Pavel Yazev E-mail: pyazev@gmail.com
 * Version 1.0 (c) Copyright 2009
 * Modified Copyright (C) 2024-2026 Rohith Namboothiri
 * 
 * This is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3, or (at your option)
 * any later version.
 * 
 * The software is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this; see the file COPYING.  If not, write to the Free
 * Software Foundation, Inc., 51 Franklin Street, Boston, MA
 * 02110-1301, USA.
 */


#include <cmath>
#include "typedef.h"
#include "globals.h"
#include "imbe.h"
#include "basic_op.h"
#include "dsp_sub.h"
#include "aux_sub.h"
#include "tbls.h"
#include "encode.h"
#include "dc_rmv.h"
#include "pe_lpf.h"
#include "pitch_est.h"
#include "pitch_ref.h"
#include "v_uv_det.h"
#include "sa_encode.h"
#include "ch_encode.h"
#include "imbe_vocoder_impl.h"

const uint8_t  BIT_MASK_TABLE8[]  = { 0x80U, 0x40U, 0x20U, 0x10U, 0x08U, 0x04U, 0x02U, 0x01U };
#define WRITE_BIT(p,i,b)   p[(i)>>3] = (b) ? (p[(i)>>3] | BIT_MASK_TABLE8[(i)&7]) : (p[(i)>>3] & ~BIT_MASK_TABLE8[(i)&7])


void imbe_vocoder_impl::encode_init(void)
{
	v_zap(pitch_est_buf, PITCH_EST_BUF_SIZE);
	v_zap(pitch_ref_buf, PITCH_EST_BUF_SIZE);
	v_zap(pe_lpf_mem, PE_LPF_ORD);
	pitch_est_init();
	robust_prev_lag = 0;
	fft_init();
	dc_rmv_mem = 0;
	sa_encode_init();
	pitch_ref_init();
}

// Robust coarse-pitch override. The stock IMBE pitch tracker (pitch_est) was measured
void imbe_vocoder_impl::robust_pitch_override(IMBE_PARAM *imbe_param)
{
	constexpr int kMinLag = 20;   // 400 Hz
	constexpr int kMaxLag = 127;  // 63 Hz
	constexpr int kLen = 160;     // correlation length (20 ms)
	constexpr float kMinConf = 0.70f;  // only override the stock tracker when clearly periodic

	float ncc[kMaxLag + 2];
	float best = 0.0f;
	int bestLag = 0;
	for (int L = kMinLag; L <= kMaxLag; ++L) {
		const int s = 70 - (L / 2);   // both segments centred on sample 150
		double sxy = 0.0, sxx = 0.0, syy = 0.0;
		const Word16 *a = &pitch_est_buf[s];
		const Word16 *b = &pitch_est_buf[s + L];
		for (int n = 0; n < kLen; ++n) {
			const double x = a[n];
			const double y = b[n];
			sxy += x * y;
			sxx += x * x;
			syy += y * y;
		}
		ncc[L] = (sxx > 1.0 && syy > 1.0) ? static_cast<float>(sxy / sqrt(sxx * syy)) : 0.0f;
		if (ncc[L] > best) {
			best = ncc[L];
			bestLag = L;
		}
	}
	ncc[kMinLag - 1 < 0 ? 0 : kMinLag - 1] = 0.0f;
	ncc[kMaxLag + 1] = 0.0f;

	if (best < kMinConf || bestLag == 0) {
		robust_prev_lag = 0;
		return;
	}

	// Shortest local maximum nearly as strong as the global one: the
	int lag = bestLag;
	for (int c = kMinLag; c < bestLag; ++c) {
		const float prev = (c > kMinLag) ? ncc[c - 1] : 0.0f;
		if (ncc[c] >= best - 0.08f && ncc[c] >= kMinConf && ncc[c] >= prev && ncc[c] >= ncc[c + 1]) {
			lag = c;
			break;
		}
	}

	// Continuity with the previous frame (+-15 %) when that candidate is nearly as strong.
	if (robust_prev_lag > 0) {
		const int lo = (int)(robust_prev_lag * 0.85f);
		const int hi = (int)(robust_prev_lag * 1.15f + 1.0f);
		int cl = 0;
		float cb = 0.0f;
		for (int c = (lo < kMinLag ? kMinLag : lo); c <= (hi > kMaxLag ? kMaxLag : hi); ++c) {
			if (ncc[c] > cb) {
				cb = ncc[c];
				cl = c;
			}
		}
		if (cl > 0 && cb >= best - 0.10f && cb >= kMinConf) {
			lag = cl;
		}
	}

	// Parabolic interpolation for half-sample precision.
	float delta = 0.0f;
	if (lag > kMinLag && lag < kMaxLag) {
		const float ym = ncc[lag - 1], y0 = ncc[lag], yp = ncc[lag + 1];
		const float den = ym - 2.0f * y0 + yp;
		if (fabsf(den) > 1e-6f) {
			delta = 0.5f * (ym - yp) / den;
			if (delta > 0.5f) delta = 0.5f;
			if (delta < -0.5f) delta = -0.5f;
		}
	}
	float period = (float)lag + delta;
	if (period < 21.0f) period = 21.0f;     // IMBE pitch range: 21..122 samples
	if (period > 122.0f) period = 122.0f;

	const Word16 pitch_q15_1 = (Word16)lrintf(period * 2.0f);
	float conf = ncc[lag];
	if (conf < 0.0f) conf = 0.0f;
	if (conf > 1.0f) conf = 1.0f;
	Word16 ep = (Word16)lrintf((1.0f - conf) * 4096.0f * 0.9f);   // Q4.12
	if (ep < 0) ep = 0;
	if (ep > 4095) ep = 4095;

	imbe_param->pitch = pitch_q15_1;
	imbe_param->e_p = ep;
	// Keep the stock tracker's look-back state coherent with what we actually sent.
	prev_pitch = pitch_q15_1 - 42;
	if (prev_pitch < 0) prev_pitch = 0;
	if (prev_pitch > 202) prev_pitch = 202;
	prev_e_p = ep;
	robust_prev_lag = lag;
}


void imbe_vocoder_impl::encode(IMBE_PARAM *imbe_param, Word16 *frame_vector, Word16 *snd)
{
	Word16 i;
	Word16 *wr_ptr, *sig_ptr;
	
	for(i = 0; i < PITCH_EST_BUF_SIZE - FRAME; i++)
	{
		pitch_est_buf[i] = pitch_est_buf[i + FRAME];
		pitch_ref_buf[i] = pitch_ref_buf[i + FRAME];
	}
		
	dc_rmv(snd, &pitch_ref_buf[PITCH_EST_BUF_SIZE - FRAME], &dc_rmv_mem, FRAME);
	pe_lpf(&pitch_ref_buf[PITCH_EST_BUF_SIZE - FRAME], &pitch_est_buf[PITCH_EST_BUF_SIZE - FRAME], pe_lpf_mem, FRAME);

	// Keep pitch estimator state clean and output zero spectral amplitudes
	// when the current analysis window is silent.
	bool windowSilent = true;
	for (i = 40; i < 260; i++) {
		if (pitch_ref_buf[i] > 20 || pitch_ref_buf[i] < -20) {
			windowSilent = false;
			break;
		}
	}
	if (windowSilent) {
		pitch_est_init();
		imbe_param->pitch = 158;
		imbe_param->ref_pitch = 158 << 7;
		imbe_param->fund_freq = 0x03333333;
		imbe_param->num_harms = 30;
		imbe_param->num_bands = 10;
		imbe_param->l_uv = 30;
		for (i = 0; i < NUM_HARMS_MAX; i++) {
			imbe_param->sa[i] = 0;
			imbe_param->v_uv_dsn[i] = 0;
		}
		for (i = 0; i < 8; i++) {
			frame_vector[i] = 0;
		}
		return;
	}

	pitch_est(imbe_param, pitch_est_buf);
	robust_pitch_override(imbe_param);

    // Speech windowing and FFT calculation
	wr_ptr  = (Word16 *)wr;
	sig_ptr = &pitch_ref_buf[40];
	for(i = 146; i < 256; i++) 
	{
		fft_buf[i].re = mult(*sig_ptr++, *wr_ptr++); 
		fft_buf[i].im = 0;
	}
	fft_buf[0].re = *sig_ptr++;
	fft_buf[0].im = 0;
	wr_ptr--;
	for(i = 1; i < 111; i++) 
	{
		fft_buf[i].re = mult(*sig_ptr++, *wr_ptr--); 
		fft_buf[i].im = 0;
	}
	for(i = 111; i < 146; i++) 
		fft_buf[i].re = fft_buf[i].im = 0;

	fft((Word16 *)&fft_buf, FFTLENGTH, 1);

	pitch_ref(imbe_param, fft_buf);
	v_uv_det(imbe_param, fft_buf);
	sa_encode(imbe_param);
	encode_frame_vector(imbe_param, frame_vector);
}

void imbe_vocoder_impl::encode_4400(int16_t *pcm, uint8_t *imbe)
{
	int16_t frame_vector[8];
	memset(imbe, 0, 11);

	imbe_encode(frame_vector, pcm);
	uint32_t offset = 0U;
	int16_t mask = 0x0800;

	for (uint32_t i = 0U; i < 12U; i++, mask >>= 1, offset++)
		WRITE_BIT(imbe, offset, (frame_vector[0U] & mask) != 0);

	mask = 0x0800;
	for (uint32_t i = 0U; i < 12U; i++, mask >>= 1, offset++)
		WRITE_BIT(imbe, offset, (frame_vector[1U] & mask) != 0);

	mask = 0x0800;
	for (uint32_t i = 0U; i < 12U; i++, mask >>= 1, offset++)
		WRITE_BIT(imbe, offset, (frame_vector[2U] & mask) != 0);

	mask = 0x0800;
	for (uint32_t i = 0U; i < 12U; i++, mask >>= 1, offset++)
		WRITE_BIT(imbe, offset, (frame_vector[3U] & mask) != 0);

	mask = 0x0400;
	for (uint32_t i = 0U; i < 11U; i++, mask >>= 1, offset++)
		WRITE_BIT(imbe, offset, (frame_vector[4U] & mask) != 0);

	mask = 0x0400;
	for (uint32_t i = 0U; i < 11U; i++, mask >>= 1, offset++)
		WRITE_BIT(imbe, offset, (frame_vector[5U] & mask) != 0);

	mask = 0x0400;
	for (uint32_t i = 0U; i < 11U; i++, mask >>= 1, offset++)
		WRITE_BIT(imbe, offset, (frame_vector[6U] & mask) != 0);

	mask = 0x0040;
	for (uint32_t i = 0U; i < 7U; i++, mask >>= 1, offset++)
		WRITE_BIT(imbe, offset, (frame_vector[7U] & mask) != 0);
}

