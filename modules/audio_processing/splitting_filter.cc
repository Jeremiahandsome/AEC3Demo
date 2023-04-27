/*
 *  Copyright (c) 2014 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "modules/audio_processing/splitting_filter.h"

#include <array>

#include "api/array_view.h"
#include "common_audio/channel_buffer.h"
#include "common_audio/signal_processing/include/signal_processing_library.h"
#include "rtc_base/checks.h"

namespace webrtc {
    namespace {

        constexpr size_t kSamplesPerBand = 160;
        constexpr size_t kTwoBandFilterSamplesPerFrame = 320;

    }  // namespace
    enum {
        kMaxBandFrameLength = 320  // 10 ms at 64 kHz.
    };
    static const uint16_t WebRtcSpl_kAllPassFilter1[3] = { 6418, 36982, 57261 };
    static const uint16_t WebRtcSpl_kAllPassFilter2[3] = { 21333, 49062, 63010 };
    static void WebRtcSpl_AllPassQMF(int32_t* in_data,
        size_t data_length,
        int32_t* out_data,
        const uint16_t* filter_coefficients,
        int32_t* filter_state)
    {
        // The procedure is to filter the input with three first order all pass
        // filters (cascade operations).
        //
        //         a_3 + q^-1    a_2 + q^-1    a_1 + q^-1
        // y[n] =  -----------   -----------   -----------   x[n]
        //         1 + a_3q^-1   1 + a_2q^-1   1 + a_1q^-1
        //
        // The input vector `filter_coefficients` includes these three filter
        // coefficients. The filter state contains the in_data state, in_data[-1],
        // followed by the out_data state, out_data[-1]. This is repeated for each
        // cascade. The first cascade filter will filter the `in_data` and store
        // the output in `out_data`. The second will the take the `out_data` as
        // input and make an intermediate storage in `in_data`, to save memory. The
        // third, and final, cascade filter operation takes the `in_data` (which is
        // the output from the previous cascade filter) and store the output in
        // `out_data`. Note that the input vector values are changed during the
        // process.
        size_t k;
        int32_t diff;
        // First all-pass cascade; filter from in_data to out_data.

        // Let y_i[n] indicate the output of cascade filter i (with filter
        // coefficient a_i) at vector position n. Then the final output will be
        // y[n] = y_3[n]

        // First loop, use the states stored in memory.
        // "diff" should be safe from wrap around since max values are 2^25
        // diff = (x[0] - y_1[-1])
        diff = WebRtcSpl_SubSatW32(in_data[0], filter_state[1]);
        // y_1[0] =  x[-1] + a_1 * (x[0] - y_1[-1])
        out_data[0] = WEBRTC_SPL_SCALEDIFF32(filter_coefficients[0], diff, filter_state[0]);

        // For the remaining loops, use previous values.
        for (k = 1; k < data_length; k++)
        {
            // diff = (x[n] - y_1[n-1])
            diff = WebRtcSpl_SubSatW32(in_data[k], out_data[k - 1]);
            // y_1[n] =  x[n-1] + a_1 * (x[n] - y_1[n-1])
            out_data[k] = WEBRTC_SPL_SCALEDIFF32(filter_coefficients[0], diff, in_data[k - 1]);
        }

        // Update states.
        filter_state[0] = in_data[data_length - 1]; // x[N-1], becomes x[-1] next time
        filter_state[1] = out_data[data_length - 1]; // y_1[N-1], becomes y_1[-1] next time

        // Second all-pass cascade; filter from out_data to in_data.
        // diff = (y_1[0] - y_2[-1])
        diff = WebRtcSpl_SubSatW32(out_data[0], filter_state[3]);
        // y_2[0] =  y_1[-1] + a_2 * (y_1[0] - y_2[-1])
        in_data[0] = WEBRTC_SPL_SCALEDIFF32(filter_coefficients[1], diff, filter_state[2]);
        for (k = 1; k < data_length; k++)
        {
            // diff = (y_1[n] - y_2[n-1])
            diff = WebRtcSpl_SubSatW32(out_data[k], in_data[k - 1]);
            // y_2[0] =  y_1[-1] + a_2 * (y_1[0] - y_2[-1])
            in_data[k] = WEBRTC_SPL_SCALEDIFF32(filter_coefficients[1], diff, out_data[k - 1]);
        }

        filter_state[2] = out_data[data_length - 1]; // y_1[N-1], becomes y_1[-1] next time
        filter_state[3] = in_data[data_length - 1]; // y_2[N-1], becomes y_2[-1] next time

        // Third all-pass cascade; filter from in_data to out_data.
        // diff = (y_2[0] - y[-1])
        diff = WebRtcSpl_SubSatW32(in_data[0], filter_state[5]);
        // y[0] =  y_2[-1] + a_3 * (y_2[0] - y[-1])
        out_data[0] = WEBRTC_SPL_SCALEDIFF32(filter_coefficients[2], diff, filter_state[4]);
        for (k = 1; k < data_length; k++)
        {
            // diff = (y_2[n] - y[n-1])
            diff = WebRtcSpl_SubSatW32(in_data[k], out_data[k - 1]);
            // y[n] =  y_2[n-1] + a_3 * (y_2[n] - y[n-1])
            out_data[k] = WEBRTC_SPL_SCALEDIFF32(filter_coefficients[2], diff, in_data[k - 1]);
        }
        filter_state[4] = in_data[data_length - 1]; // y_2[N-1], becomes y_2[-1] next time
        filter_state[5] = out_data[data_length - 1]; // y[N-1], becomes y[-1] next time
    }
    void WebRtcSpl_AnalysisQMF(const int16_t* in_data, size_t in_data_length,
        int16_t* low_band, int16_t* high_band,
        int32_t* filter_state1, int32_t* filter_state2)
    {
        size_t i;
        int16_t k;
        int32_t tmp;
        int32_t half_in1[kMaxBandFrameLength];
        int32_t half_in2[kMaxBandFrameLength];
        int32_t filter1[kMaxBandFrameLength];
        int32_t filter2[kMaxBandFrameLength];
        const size_t band_length = in_data_length / 2;
        RTC_DCHECK_EQ(0, in_data_length % 2);
        RTC_DCHECK_LE(band_length, kMaxBandFrameLength);

        // Split even and odd samples. Also shift them to Q10.
        for (i = 0, k = 0; i < band_length; i++, k += 2)
        {
            half_in2[i] = ((int32_t)in_data[k]) * (1 << 10);
            half_in1[i] = ((int32_t)in_data[k + 1]) * (1 << 10);
        }

        // All pass filter even and odd samples, independently.
        WebRtcSpl_AllPassQMF(half_in1, band_length, filter1,
            WebRtcSpl_kAllPassFilter1, filter_state1);
        WebRtcSpl_AllPassQMF(half_in2, band_length, filter2,
            WebRtcSpl_kAllPassFilter2, filter_state2);

        // Take the sum and difference of filtered version of odd and even
        // branches to get upper & lower band.
        for (i = 0; i < band_length; i++)
        {
            tmp = (filter1[i] + filter2[i] + 1024) >> 11;
            low_band[i] = WebRtcSpl_SatW32ToW16(tmp);

            tmp = (filter1[i] - filter2[i] + 1024) >> 11;
            high_band[i] = WebRtcSpl_SatW32ToW16(tmp);
        }
    }

    void WebRtcSpl_SynthesisQMF(const int16_t* low_band, const int16_t* high_band,
        size_t band_length, int16_t* out_data,
        int32_t* filter_state1, int32_t* filter_state2)
    {
        int32_t tmp;
        int32_t half_in1[kMaxBandFrameLength];
        int32_t half_in2[kMaxBandFrameLength];
        int32_t filter1[kMaxBandFrameLength];
        int32_t filter2[kMaxBandFrameLength];
        size_t i;
        int16_t k;
        RTC_DCHECK_LE(band_length, kMaxBandFrameLength);

        // Obtain the sum and difference channels out of upper and lower-band channels.
        // Also shift to Q10 domain.
        for (i = 0; i < band_length; i++)
        {
            tmp = (int32_t)low_band[i] + (int32_t)high_band[i];
            half_in1[i] = tmp * (1 << 10);
            tmp = (int32_t)low_band[i] - (int32_t)high_band[i];
            half_in2[i] = tmp * (1 << 10);
        }

        // all-pass filter the sum and difference channels
        WebRtcSpl_AllPassQMF(half_in1, band_length, filter1,
            WebRtcSpl_kAllPassFilter2, filter_state1);
        WebRtcSpl_AllPassQMF(half_in2, band_length, filter2,
            WebRtcSpl_kAllPassFilter1, filter_state2);

        // The filtered signals are even and odd samples of the output. Combine
        // them. The signals are Q10 should shift them back to Q0 and take care of
        // saturation.
        for (i = 0, k = 0; i < band_length; i++)
        {
            tmp = (filter2[i] + 512) >> 10;
            out_data[k++] = WebRtcSpl_SatW32ToW16(tmp);

            tmp = (filter1[i] + 512) >> 10;
            out_data[k++] = WebRtcSpl_SatW32ToW16(tmp);
        }

    }


    SplittingFilter::SplittingFilter(size_t num_channels,
        size_t num_bands,
        size_t num_frames)
        : num_bands_(num_bands),
        two_bands_states_(num_bands_ == 2 ? num_channels : 0),
        three_band_filter_banks_(num_bands_ == 3 ? num_channels : 0) {
        RTC_CHECK(num_bands_ == 2 || num_bands_ == 3);
    }

    SplittingFilter::~SplittingFilter() = default;

    void SplittingFilter::Analysis(const ChannelBuffer<float>* data,
        ChannelBuffer<float>* bands) {
        RTC_DCHECK_EQ(num_bands_, bands->num_bands());
        RTC_DCHECK_EQ(data->num_channels(), bands->num_channels());
        RTC_DCHECK_EQ(data->num_frames(),
            bands->num_frames_per_band() * bands->num_bands());
        if (bands->num_bands() == 2) {
            TwoBandsAnalysis(data, bands);
        }
        else if (bands->num_bands() == 3) {
            ThreeBandsAnalysis(data, bands);
        }
    }

    void SplittingFilter::Synthesis(const ChannelBuffer<float>* bands,
        ChannelBuffer<float>* data) {
        RTC_DCHECK_EQ(num_bands_, bands->num_bands());
        RTC_DCHECK_EQ(data->num_channels(), bands->num_channels());
        RTC_DCHECK_EQ(data->num_frames(),
            bands->num_frames_per_band() * bands->num_bands());
        if (bands->num_bands() == 2) {
            TwoBandsSynthesis(bands, data);
        }
        else if (bands->num_bands() == 3) {
            ThreeBandsSynthesis(bands, data);
        }
    }

    void SplittingFilter::TwoBandsAnalysis(const ChannelBuffer<float>* data,
        ChannelBuffer<float>* bands) {
        RTC_DCHECK_EQ(two_bands_states_.size(), data->num_channels());
        RTC_DCHECK_EQ(data->num_frames(), kTwoBandFilterSamplesPerFrame);

        for (size_t i = 0; i < two_bands_states_.size(); ++i) {
            std::array<std::array<int16_t, kSamplesPerBand>, 2> bands16;
            std::array<int16_t, kTwoBandFilterSamplesPerFrame> full_band16;
            FloatS16ToS16(data->channels(0)[i], full_band16.size(), full_band16.data());
            WebRtcSpl_AnalysisQMF(full_band16.data(), data->num_frames(),
                bands16[0].data(), bands16[1].data(),
                two_bands_states_[i].analysis_state1,
                two_bands_states_[i].analysis_state2);
            S16ToFloatS16(bands16[0].data(), bands16[0].size(), bands->channels(0)[i]);
            S16ToFloatS16(bands16[1].data(), bands16[1].size(), bands->channels(1)[i]);
        }
    }

    void SplittingFilter::TwoBandsSynthesis(const ChannelBuffer<float>* bands,
        ChannelBuffer<float>* data) {
        RTC_DCHECK_LE(data->num_channels(), two_bands_states_.size());
        RTC_DCHECK_EQ(data->num_frames(), kTwoBandFilterSamplesPerFrame);
        for (size_t i = 0; i < data->num_channels(); ++i) {
            std::array<std::array<int16_t, kSamplesPerBand>, 2> bands16;
            std::array<int16_t, kTwoBandFilterSamplesPerFrame> full_band16;
            FloatS16ToS16(bands->channels(0)[i], bands16[0].size(), bands16[0].data());
            FloatS16ToS16(bands->channels(1)[i], bands16[1].size(), bands16[1].data());
            WebRtcSpl_SynthesisQMF(bands16[0].data(), bands16[1].data(),
                bands->num_frames_per_band(), full_band16.data(),
                two_bands_states_[i].synthesis_state1,
                two_bands_states_[i].synthesis_state2);
            S16ToFloatS16(full_band16.data(), full_band16.size(), data->channels(0)[i]);
        }
    }

    void SplittingFilter::ThreeBandsAnalysis(const ChannelBuffer<float>* data,
        ChannelBuffer<float>* bands) {
        RTC_DCHECK_EQ(three_band_filter_banks_.size(), data->num_channels());
        RTC_DCHECK_LE(data->num_channels(), three_band_filter_banks_.size());
        RTC_DCHECK_LE(data->num_channels(), bands->num_channels());
        RTC_DCHECK_EQ(data->num_frames(), ThreeBandFilterBank::kFullBandSize);
        RTC_DCHECK_EQ(bands->num_frames(), ThreeBandFilterBank::kFullBandSize);
        RTC_DCHECK_EQ(bands->num_bands(), ThreeBandFilterBank::kNumBands);
        RTC_DCHECK_EQ(bands->num_frames_per_band(),
            ThreeBandFilterBank::kSplitBandSize);

        for (size_t i = 0; i < three_band_filter_banks_.size(); ++i) {
            three_band_filter_banks_[i].Analysis(
                rtc::ArrayView<const float, ThreeBandFilterBank::kFullBandSize>(
                    data->channels_view()[i].data(),
                    ThreeBandFilterBank::kFullBandSize),
                rtc::ArrayView<const rtc::ArrayView<float>,
                ThreeBandFilterBank::kNumBands>(
                    bands->bands_view(i).data(), ThreeBandFilterBank::kNumBands));
        }
    }

    void SplittingFilter::ThreeBandsSynthesis(const ChannelBuffer<float>* bands,
        ChannelBuffer<float>* data) {
        RTC_DCHECK_LE(data->num_channels(), three_band_filter_banks_.size());
        RTC_DCHECK_LE(data->num_channels(), bands->num_channels());
        RTC_DCHECK_LE(data->num_channels(), three_band_filter_banks_.size());
        RTC_DCHECK_EQ(data->num_frames(), ThreeBandFilterBank::kFullBandSize);
        RTC_DCHECK_EQ(bands->num_frames(), ThreeBandFilterBank::kFullBandSize);
        RTC_DCHECK_EQ(bands->num_bands(), ThreeBandFilterBank::kNumBands);
        RTC_DCHECK_EQ(bands->num_frames_per_band(),
            ThreeBandFilterBank::kSplitBandSize);

        for (size_t i = 0; i < data->num_channels(); ++i) {
            three_band_filter_banks_[i].Synthesis(
                rtc::ArrayView<const rtc::ArrayView<float>,
                ThreeBandFilterBank::kNumBands>(
                    bands->bands_view(i).data(), ThreeBandFilterBank::kNumBands),
                rtc::ArrayView<float, ThreeBandFilterBank::kFullBandSize>(
                    data->channels_view()[i].data(),
                    ThreeBandFilterBank::kFullBandSize));
        }
    }

}  // namespace webrtc
