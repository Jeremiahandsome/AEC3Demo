#include "wave_file.h"
#include <cassert>


#define MakeFourCc(inChar0, inChar1, inChar2, inChar3)                  \
  ((uint32_t)(uint8_t)(inChar3) | ((uint32_t)(uint8_t)(inChar2) << 8) | \
   ((uint32_t)(uint8_t)(inChar1) << 16) |                               \
   ((uint32_t)(uint8_t)(inChar0) << 24))

WavReader::WavReader()
    : m_pFile(0)
    , m_nLength(0)
    , m_nStart(0)
    , m_nFormatTag(0)
    , m_nChannels(0)
    , m_nSampleRate(0)
    , m_nAvgBytesPerSec(0)
    , m_nBlockAlign(0)
    , m_nBitsPerSample(0)
{
}

WavReader::~WavReader()
{
    Close();
}

bool WavReader::Open(const char* inFileName)
{
    m_pFile = fopen(inFileName, "rb");
    if (!m_pFile) return false;
    fseek(m_pFile, 0, SEEK_END);
    int nTotal = ftell(m_pFile);
    assert(0 != nTotal);
    fseek(m_pFile, 0, SEEK_SET);

    if (MakeFourCc('R', 'I', 'F', 'F') != readString())
    {
        return false;
    }
    uint32_t nFmtLen = readInt32();

    if (MakeFourCc('W', 'A', 'V', 'E') != readString())
    {
        return false;
    }

    int nRet = fseek(m_pFile, 16, SEEK_SET);
    assert(0 == nRet);
    if (0 != nRet)
    {
        Close();
        return false;
    }
    nFmtLen = readInt32();
    m_nFormatTag = readInt16();
    m_nChannels = readInt16();
    m_nSampleRate = readInt32();
    m_nAvgBytesPerSec = readInt32();
    m_nBlockAlign = readInt16();
    m_nBitsPerSample = readInt16();

    uint32_t data_flags = MakeFourCc('d', 'a', 't', 'a');
    while (nTotal > 0)
    {
        if (data_flags == readString())
        {
            break;
        }
        nRet = fseek(m_pFile, -3, SEEK_CUR);
        assert(0 == nRet);
        if (0 != nRet)
        {
            Close();
            return false;
        }
        nTotal--;
    }
    m_nLength = readInt32() / (m_nBitsPerSample / 8);
    m_nStart = ftell(m_pFile);
    return true;
}

void WavReader::Close()
{
    if (m_pFile)
    {
        fclose(m_pFile);
        m_pFile = 0;
    }
}

bool WavReader::IsOpen() {
    return m_pFile != nullptr;
}

bool WavReader::Reset()
{
    assert(0 != m_pFile);
    if (!m_pFile) return false;

    return (0 == fseek(m_pFile, m_nStart, SEEK_SET));
}

int WavReader::Read(short* ioData, int inLen)
{
    assert(0 != m_pFile);
    assert(16 == m_nBitsPerSample);
    return fread(ioData, 2, inLen, m_pFile);
}

int WavReader::Read(float* ioData, int inLen)
{
    assert(32 == m_nBitsPerSample);

    fread(ioData, 4, inLen, m_pFile);
    return inLen;
}

uint32_t WavReader::readString()
{
    uint8_t nBytes[4];
    fread(nBytes, 1, 4, m_pFile);
    return ((nBytes[0] << 24) | (nBytes[1] << 16) | (nBytes[2] << 8) | nBytes[3]);
}

int WavReader::readInt32()
{
    uint32_t nBytes = 0;
    fread(&nBytes, 4, 1, m_pFile);
    return nBytes;
}

short WavReader::readInt16()
{
    uint16_t nBytes = 0;
    fread(&nBytes, 2, 1, m_pFile);
    return nBytes;
}

WavWriter::WavWriter()
    : m_pFile(0)
    , m_nDataLen(0)
    , m_nSampleRate(0)
    , m_nBitsPerSamples(16)
    , m_nChannels(0)
    , m_nFormatTag(FormatTag_PCM)
{
}

WavWriter::~WavWriter()
{
    Close();
}

bool WavWriter::Open(const char* inFileName,
    int inSampleRate,
    short inChannels,
    short inBitsPerSamples,
    short inFormatTag)
{
    m_pFile = fopen(inFileName, "wb");
    if (m_pFile == NULL) return false;
    m_nDataLen = 0;

    m_nSampleRate = inSampleRate;
    m_nBitsPerSamples = inBitsPerSamples;
    m_nChannels = inChannels;
    m_nFormatTag = inFormatTag;

    writeHeader(m_nDataLen);
    return true;
}

void WavWriter::Close()
{
    if (0 == m_pFile) return;
    fseek(m_pFile, 0, SEEK_SET);
    writeHeader(m_nDataLen);
    if (m_pFile)
    {
        fclose(m_pFile);
        m_pFile = 0;
    }
}

bool WavWriter::IsOpen() {
    return m_pFile != NULL;
}

void WavWriter::Write(short* inData, int inLen)
{
    assert(0 != m_pFile);
    assert(16 == m_nBitsPerSamples);
    assert(FormatTag_PCM == m_nFormatTag);

    if (m_pFile == NULL) return;
    fwrite(inData, inLen, 2, m_pFile);
    m_nDataLen += inLen * 2;
}

void WavWriter::Write(float* inData, int inLen)
{
    assert(0 != m_pFile);
    assert(32 == m_nBitsPerSamples);
    assert(FormatTag_IEEE_float == m_nFormatTag);

    if (m_pFile == NULL) return;
    fwrite(inData, 4, inLen, m_pFile);
    m_nDataLen += inLen * 4;
}

void WavWriter::writeString(const char* inStr)
{
    fputc(inStr[0], m_pFile);
    fputc(inStr[1], m_pFile);
    fputc(inStr[2], m_pFile);
    fputc(inStr[3], m_pFile);
}

void WavWriter::writeInt32(int inValue)
{
    fputc((inValue >> 0) & 0xff, m_pFile);
    fputc((inValue >> 8) & 0xff, m_pFile);
    fputc((inValue >> 16) & 0xff, m_pFile);
    fputc((inValue >> 24) & 0xff, m_pFile);
}

void WavWriter::writeInt16(int inValue)
{
    fputc((inValue >> 0) & 0xff, m_pFile);
    fputc((inValue >> 8) & 0xff, m_pFile);
}

void WavWriter::writeHeader(int inLen)
{
    writeString("RIFF");
    writeInt32(4 + 8 + 16 + 8 + inLen);
    writeString("WAVE");

    writeString("fmt ");
    writeInt32(16);

    int nBytesPerFrame = m_nBitsPerSamples / 8 * m_nChannels;

    writeInt16(m_nFormatTag);                   // Format
    writeInt16(m_nChannels);                    // Channels
    writeInt32(m_nSampleRate);                  // Samplerate
    writeInt32(nBytesPerFrame * m_nSampleRate);   // Bytes per sec
    writeInt16(nBytesPerFrame);                 // Bytes per frame
    writeInt16(m_nBitsPerSamples);              // Bits per sample

    writeString("data");
    writeInt32(inLen);
}
