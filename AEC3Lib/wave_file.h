#include <cstdint>
#include <cstdio>
#ifndef WAVFILE_H
#define WAVFILE_H

//#include "platform.h"

typedef enum {
    FormatTag_PCM = 0x1,      // PCM
    FormatTag_ADPCM = 0x2,      // DAPCM
    FormatTag_IEEE_float = 0x3,      // IEEE float
    FormatTag_VSELP = 0x4,      // VSELP
    FormatTag_IBM_CVSD = 0x5,      // IBM CVSD
    FormatTag_a_Law = 0x6,      // a-Law
    FormatTag_u_Law = 0x7,      // u-Law 
    FormatTag_DTS = 0x8,      // DTS
    FormatTag_DRM = 0x9,      // DRM
    FormatTag_OKI_ADPCM = 0x10,     // OKI-ADPCM
    FormatTag_IMA_ADPCM = 0x11,     // IMA-ADPCM
    FormatTag_Mediaspace_ADPCM = 0x12,     // Mediaspace ADPCM
    FormatTag_Sierra_ADPCM = 0x13,     // Sierra ADPCM
    FormatTag_G723_ADPCM = 0x14,     // G723 ADPCM
    FormatTag_DIGISTD = 0x15,     // DIGISTD
    FormatTag_DIGIFIX = 0x16,     // DIGIFIX
    FormatTag_Dolby_AC2 = 0x30,     // Dolby AC2
    FormatTag_GSM610 = 0x31,     // GSM610
    FormatTag_Rockwell_ADPCM = 0x3b,     // Rockwell ADPCM
    FormatTag_Rockwell_DIGITALK = 0x3c,     // Rockwell DIGITALK
    FormatTag_G721_ADPCM = 0x40,     // G721 ADPCM
    FormatTag_G728_CELP = 0x41,     // G728 CELP
    FormatTag_MPEG = 0x50,     // MPEG
    FormatTag_RT24 = 0x52,     // RT24
    FormatTag_PAC = 0x53,     // PAC
    FormatTag_MP3 = 0x55,     // MP3
    FormatTag_G726_ADPCM = 0x64,     // G726 ADPCM
    FormatTag_G722_ADPCM = 0x65,     // G722 ADPCM
    FormatTag_IBM_u_Law = 0x101,    // IBM u-Law
    FormatTag_IBM_a_Law = 0x102,    // IBM a-Law
    FormatTag_IBM_ADPCM = 0x103,    // IBM ADPCM
    FormatTag_Dev = 0xffff,   // Development
} WavFormatTag;

/**
 * @brief   Wav文件读取器
 */
class WavReader
{
public:
    WavReader();
    ~WavReader();

    bool Open(const char* inFileName);
    void Close();
    bool IsOpen();
    bool Reset();
    int Read(short* ioData, int inLen);
    int Read(float* ioData, int inLen);

    uint16_t FormatTag() { return m_nFormatTag; }
    uint32_t SampleRate() { return m_nSampleRate; }
    uint16_t Channels() { return m_nChannels; }
    uint16_t BitsPerSample() { return m_nBitsPerSample; }
    uint32_t Length() { return m_nLength; }
private:
    uint32_t readString();
    int readInt32();
    short readInt16();
private:
    FILE* m_pFile;
    uint32_t    m_nLength;      // 文件采样数目
    uint32_t    m_nStart;       // 文件开始位置
    uint16_t    m_nFormatTag;   // format type
    uint16_t    m_nChannels;
    uint32_t    m_nSampleRate;
    uint32_t    m_nAvgBytesPerSec;
    uint16_t    m_nBlockAlign;
    uint16_t    m_nBitsPerSample;
};

/**
 * @brief   Wav文件生成器
 */
class WavWriter
{
public:
    WavWriter();
    ~WavWriter();

    bool Open(const char* inFileName,
        int inSampleRate,
        short inChannels,
        short inBitsPerSamples = 16,
        short inFormatTag = FormatTag_PCM);
    void Close();
    bool IsOpen();
    void Write(short* inData, int inLen);
    void Write(float* inData, int inLen);

    uint32_t SampleRate() { return m_nSampleRate; }
    uint16_t Channels() { return m_nChannels; }
private:
    void writeString(const char* inStr);
    void writeInt32(int inValue);
    void writeInt16(int inValue);
    void writeHeader(int inLength);

    FILE* m_pFile;
    int m_nDataLen;
    int m_nSampleRate;
    short m_nBitsPerSamples;
    int m_nChannels;
    short m_nFormatTag;
};

#endif#pragma once
#pragma once
