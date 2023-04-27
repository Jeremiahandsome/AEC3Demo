#include "api/audio/echo_canceller3_factory.h"
#include "api/audio/echo_canceller3_config.h"
#include "api/audio/audio_frame.h"
#include "audio_processing/aec3/echo_canceller3.h"
#include "audio_processing/include/audio_processing.h"
#include "audio_processing/audio_buffer.h"
#include "audio_processing/high_pass_filter.h"

#include "wave_file.h"

#include <iostream>
#include <memory>
#include <vector>

void print_progress(int current, int total) {
    int percentage = current / static_cast<float>(total) * 100;
    static constexpr int p_bar_length = 50;
    int progress = percentage * p_bar_length / 100;
    std::cout << "        " << current << "/" << total << "    " << percentage << "%";
    std::cout << "|";
    for (int i = 0; i < progress; ++i) {
        std::cout << "=";
    }
    std::cout << ">";
    for (int i = progress; i < p_bar_length; ++i) {
        std::cout << " ";
    }
    std::cout << "|";
    std::cout << "\r";
}

int main(int argc, char* argv[]) {
    //const char* audio_file_1 = "echo2.wav";
    //const char* audio_file_2 = "rec2.wav";

   char* audio_file_1 = argv[1];
   char* audio_file_2 = argv[2];

    WavReader wav_reader1;
    WavReader wav_reader2;

    if (!wav_reader1.Open(audio_file_1) || !wav_reader2.Open(audio_file_2)) {
        std::cerr << "Error opening audio files." << std::endl;
        return 1;
    }

    int sample_rate1 = wav_reader1.SampleRate();
    int num_channels1 = wav_reader1.Channels();
    int num_samples1 = wav_reader1.Length();
    int bits_per_sample1 = wav_reader1.BitsPerSample();

    int sample_rate2 = wav_reader2.SampleRate();
    int num_channels2 = wav_reader2.Channels();
    int num_samples2 = wav_reader2.Length();
    int bits_per_sample2 = wav_reader2.BitsPerSample();

    if (num_channels1 != num_channels2 || sample_rate1 != sample_rate2 || bits_per_sample1 != bits_per_sample2) {
        std::cerr << "ref file format != rec file format" << std::endl;
        return -1;
    }

    int audio1_samples = num_samples1;
    int audio2_samples = num_samples2;
    int samples_per_frame = sample_rate1 * num_channels1 / 100;
    int total = audio1_samples > audio2_samples ? audio2_samples / samples_per_frame: 
        audio1_samples / samples_per_frame;
    //long long bytes_per_frame = samples_per_frame * bits_per_sample1 / 8;


    webrtc::EchoCanceller3Config config;
    config.filter.export_linear_aec_output = true;
    webrtc::EchoCanceller3Factory aec_factory(config);
    std::unique_ptr<webrtc::EchoControl> aec3 = aec_factory.Create(sample_rate1, num_channels1, num_channels2);
    std::unique_ptr<webrtc::HighPassFilter> hp_filter = std::make_unique<webrtc::HighPassFilter>(sample_rate1, num_channels1);

    webrtc::StreamConfig stream_config(sample_rate1, num_channels1);
    std::unique_ptr<webrtc::AudioBuffer> audio_buffer1 = std::make_unique<webrtc::AudioBuffer>(
        stream_config.sample_rate_hz(), stream_config.num_channels(),
        stream_config.sample_rate_hz(), stream_config.num_channels(),
        stream_config.sample_rate_hz(), stream_config.num_channels());
    std::unique_ptr<webrtc::AudioBuffer> audio_buffer2 = std::make_unique<webrtc::AudioBuffer>(
        stream_config.sample_rate_hz(), stream_config.num_channels(),
        stream_config.sample_rate_hz(), stream_config.num_channels(),
        stream_config.sample_rate_hz(), stream_config.num_channels());
    int kLinearOutputRateHz = 16000;
    std::unique_ptr<webrtc::AudioBuffer> aec_linear_audio = std::make_unique<webrtc::AudioBuffer>(
        kLinearOutputRateHz, stream_config.num_channels(),
        kLinearOutputRateHz, stream_config.num_channels(),
        kLinearOutputRateHz, stream_config.num_channels());
    webrtc::StreamConfig output_config(sample_rate1, num_channels1);

    WavWriter output_file;
    output_file.Open("output.wav", sample_rate1, num_channels1, bits_per_sample1, FormatTag_PCM);

    short* audio_data1 = new short[samples_per_frame];
    short* audio_data2 = new short[samples_per_frame];

    int current = 0;
    while (current++ < total) {
        print_progress(current, total);
        wav_reader1.Read(audio_data1,samples_per_frame);
        wav_reader2.Read(audio_data2,samples_per_frame);

        audio_buffer1->CopyFrom(audio_data1, stream_config);
        audio_buffer2->CopyFrom(audio_data2, stream_config);

        audio_buffer1->SplitIntoFrequencyBands();
        aec3->AnalyzeRender(audio_buffer1.get());
        audio_buffer1->MergeFrequencyBands();

        aec3->AnalyzeCapture(audio_buffer2.get());
        audio_buffer2->SplitIntoFrequencyBands();
        hp_filter->Process(audio_buffer2.get(), true);
        aec3->SetAudioBufferDelay(0);
        aec3->ProcessCapture(audio_buffer2.get(), aec_linear_audio.get(), false);
        audio_buffer2->MergeFrequencyBands();

        audio_buffer2->CopyTo(output_config, audio_data2);
        output_file.Write(audio_data2, samples_per_frame);

    }

    output_file.Close();
    wav_reader1.Close();
    wav_reader2.Close();
    delete[] audio_data1;
    delete[] audio_data2;

    return 0;
}
