#include "AudioFFTworker.h"

#include <QDebug>
#include <algorithm>
#include <cmath>
#include <numeric>

namespace {

inline bool isPowerOfTwo(uint32_t n) {
    return (n > 0) && ((n & (n - 1)) == 0);
}

/// --- Frequency Band Ranges (Hz) ---
// Updated for NUM_BANDS bands
inline const std::vector<std::pair<float, float>>& getDefaultBandRanges()
{
    static const std::vector<std::pair<float, float>> RANGES = {
        { 20.0f   , 60.0f   }, // SubBass
        { 60.0f   , 250.0f  }, // Bass
        { 250.0f  , 500.0f  }, // LowMid
        { 500.0f  , 2000.0f }, // Mid
        { 2000.0f , 4000.0f }, // HighMid
        { 4000.0f , 6000.0f }, // Presence
        { 6000.0f , 20000.0f}  // Air (Upper limit clamped by Nyquist later)
    };
    return RANGES;
}

constexpr uint32_t INVALID_BAND_RANGE = UINT32_MAX;

/// Threshold for when to use proportional bands
constexpr float MIN_NYQUIST_FOR_IDEAL_LIKE_BANDS = 10000.0f; // e.g., 10 kHz Nyquist (20kHz sample rate)

/// --- Proportions for bands if Nyquist is very low ---
const std::vector<float>& getLowNyquistBandProportions()
{
    static const std::vector<float> LOW_NYQUIST_BAND_PROPORTIONS = {
        0.0f,    // Start of SubBass
        0.03f,   // End of SubBass   (3% of Nyquist)
        0.10f,   // End of Bass      (next NUM_BANDS%)
        0.20f,   // End of LowMid    (next 10%)
        0.40f,   // End of Mid       (next 20%)
        0.60f,   // End of HighMid   (next 20%)
        0.80f,   // End of Presence  (next 20%)
        1.0f     // End of Air       (remaining 20% to Nyquist)
    };
    return LOW_NYQUIST_BAND_PROPORTIONS;
}

using Complex = std::complex<float>;
using ComplexVector = std::vector<Complex>;

/// --- Iterative In-Place FFT Implementation (Cooley-Tukey Radix-2 DIT) ---
void fftIterative(ComplexVector& buffer) {
    const size_t N = buffer.size();
    if (N <= 1) return;

    unsigned int numBits = 0;
    if (N > 1) {
        size_t tempN = N;
        while(tempN > 1) {
            tempN >>= 1;
            numBits++;
        }
    }

    for (size_t i = 0; i < N; ++i) {
        size_t rev_i = 0;
        size_t temp_i = i;
        for (unsigned int j = 0; j < numBits; ++j) {
            rev_i = (rev_i << 1) | (temp_i & 1);
            temp_i >>= 1;
        }
        if (rev_i > i) {
            std::swap(buffer[i], buffer[rev_i]);
        }
    }

    for (unsigned int s = 1; s <= numBits; ++s) {
        size_t m = static_cast<size_t>(1) << s;
        size_t m_half = m >> 1;
        Complex w_m = std::exp(Complex(0.0f, -2.0f * static_cast<float>(M_PI) / static_cast<float>(m)));
        for (size_t j = 0; j < N; j += m) {
            Complex w = Complex(1.0f, 0.0f);
            for (size_t k = 0; k < m_half; ++k) {
                Complex t = w * buffer[j + k + m_half];
                Complex u = buffer[j + k];
                buffer[j + k]           = u + t;
                buffer[j + k + m_half] = u - t;
                w *= w_m;
            }
        }
    }
}
} // namespace


//-----------------------------------------------------------------------------------------------

void AudioFFTworker::initialize(const uint32_t sampleRate, const uint32_t chunkSize)
{
    if (sampleRate == 0 || chunkSize == 0) {
        m_isInitialized = false;
        return;
    }

    Q_ASSERT(isPowerOfTwo(chunkSize));

    if (m_isInitialized && m_sampleRate == sampleRate && m_chunkSize == chunkSize) { return; }

    qDebug() << "AudioFrequencyAnalyzer initializing with Sample Rate:" << sampleRate << "Chunk Size:" << chunkSize;

    m_sampleRate     = sampleRate;
    m_chunkSize      = chunkSize;
    m_freqResolution = (m_chunkSize > 0) ? (static_cast<float>(m_sampleRate) / static_cast<float>(m_chunkSize)) : 0.0f;

    m_bandLevels.assign(NumBands, 0.0f);
    m_bandBinRanges.resize(NumBands);

    m_fftBuffer.resize(m_chunkSize);
    m_window.resize(m_chunkSize);

    if (m_chunkSize > 1) {
        for (uint32_t i = 0; i < m_chunkSize; ++i) {
            m_window[i] = 0.5f * (1.0f - cosf(2.0f * static_cast<float>(M_PI) * static_cast<float>(i) / static_cast<float>(m_chunkSize - 1)));
        }
    } else if (m_chunkSize == 1) {
        m_window[0] = 1.0f;
    }

    m_magnitudes.resize(m_chunkSize / 2 + 1);
    std::fill(m_magnitudes.begin(), m_magnitudes.end(), 0.0f);

    setupBandFrequencyRanges();
    calculateBandBinRanges  ();

    m_isInitialized = true;
}

//-----------------------------------------------------------------------------------------------

void AudioFFTworker::calculateBandBinRanges()
{
    if (m_freqResolution <= 0.0f) {
        qWarning() << "Cannot calculate bin ranges, frequency resolution is zero.";
        for(int i=0; i<NumBands; ++i) m_bandBinRanges[i] = {INVALID_BAND_RANGE, INVALID_BAND_RANGE};
        Q_ASSERT(0);
        return;
    }

    uint32_t maxBinIndex = static_cast<uint32_t>(m_chunkSize / 2); // Max valid bin index for magnitude array (excluding DC if m_chunkSize/2+1 bins total)

    for (int i = 0; i < NumBands; ++i) {
        float lowerFreq = m_bandFrequencyRanges[i].first;
        float upperFreq = m_bandFrequencyRanges[i].second;

        if (lowerFreq >= upperFreq || lowerFreq < 0) {
            m_bandBinRanges[i] = { INVALID_BAND_RANGE, INVALID_BAND_RANGE };
            continue;
        }

        // Bin 0 is DC. First AC bin is index 1.
        // Bin k corresponds to frequency k * m_freqResolution.
        // So frequency f corresponds to bin f / m_freqResolution.
        uint32_t startBin = std::clamp(static_cast<uint32_t>(std::ceil (lowerFreq / m_freqResolution)), 1u, maxBinIndex);
        uint32_t endBin   = std::min  (static_cast<uint32_t>(std::floor(upperFreq / m_freqResolution)),     maxBinIndex);

        if (startBin > endBin) { // Can happen if band is too narrow or falls between bins
            m_bandBinRanges[i] = { INVALID_BAND_RANGE, INVALID_BAND_RANGE };
        } else {
            m_bandBinRanges[i] = { startBin, endBin };
        }
    }
}

//-----------------------------------------------------------------------------------------------

void AudioFFTworker::setupBandFrequencyRanges()
{
    m_bandFrequencyRanges.resize(NumBands);
    float nyquist = static_cast<float>(m_sampleRate) / 2.0f;

    const auto& idealRanges = getDefaultBandRanges();
    Q_ASSERT(idealRanges.size() == static_cast<size_t>(NumBands));
    Q_ASSERT(getLowNyquistBandProportions().size() == static_cast<size_t>(NumBands + 1));

    if ( nyquist < MIN_NYQUIST_FOR_IDEAL_LIKE_BANDS ) {
        qDebug() << "Nyquist frequency" << nyquist << "is low. Using proportional bands.";
        for (int i = 0; i < NumBands; ++i) {
            float lowerFreq = nyquist * getLowNyquistBandProportions()[i];
            float upperFreq = nyquist * getLowNyquistBandProportions()[i+1];
            upperFreq = std::min(upperFreq, nyquist);
            if (lowerFreq >= upperFreq && i < NumBands -1) {
                upperFreq = lowerFreq + (m_freqResolution > 0.0f ? m_freqResolution : 1.0f);
                upperFreq = std::min(upperFreq, nyquist);
            }
            if (lowerFreq >= upperFreq) {
                m_bandFrequencyRanges[i] = {std::max(0.0f, upperFreq - (m_freqResolution > 0.0f ? m_freqResolution : 1.0f)), upperFreq};
            } else {
                m_bandFrequencyRanges[i] = {lowerFreq, upperFreq};
            }
        }
    } else {
        qDebug() << "Nyquist frequency" << nyquist << "is sufficient. Adapting ideal bands.";
        float currentMaxFreq = 0.0;
        for (int i = 0; i < NumBands; ++i) {
            float lowerFreq = std::max(currentMaxFreq, idealRanges[i].first);
            float upperFreq = idealRanges[i].second;

            lowerFreq = std::min(lowerFreq, nyquist);
            upperFreq = std::min(upperFreq, nyquist);

            if (lowerFreq >= upperFreq) {
                if (currentMaxFreq >= nyquist) {
                    m_bandFrequencyRanges[i] = {nyquist - (m_freqResolution > 0 ? m_freqResolution : 1.0f) , nyquist };
                    m_bandFrequencyRanges[i].first = std::max(0.0f, m_bandFrequencyRanges[i].first);
                } else {
                    m_bandFrequencyRanges[i] = {currentMaxFreq, nyquist};
                }
                if (m_bandFrequencyRanges[i].first >= m_bandFrequencyRanges[i].second) {
                    m_bandFrequencyRanges[i].first = std::max(0.0f, m_bandFrequencyRanges[i].second - (m_freqResolution > 0.0f ? m_freqResolution : 1.0f));
                }
            } else {
                m_bandFrequencyRanges[i] = {lowerFreq, upperFreq};
            }
            currentMaxFreq = m_bandFrequencyRanges[i].second;
        }
    }
}

//-----------------------------------------------------------------------------------------------

void AudioFFTworker::processSamples(const ArithmeticCircularQueue<float>& samples, qint64 sequenceNumber)
{
    if (m_bandLevels.size() != static_cast<size_t>(NumBands)) { // NumBands is NUM_BANDS
        m_bandLevels.assign(NumBands, 0.0f);
    }

    if (!m_isInitialized) {
        qWarning() << "AudioFrequencyAnalyzer::processSamples (seq:" << sequenceNumber << ") called when not initialized. Emitting empty result.";
        std::fill(m_bandLevels.begin(), m_bandLevels.end(), 0.0f);
        emit fftValueCalculated(sequenceNumber, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
        return;
    }

    ArithmeticCircularQueue<float> fftSamples = samples;

    uint32_t numSamples = fftSamples.size();
    if (numSamples == 0) {
        qWarning() << "AudioFrequencyAnalyzer::processSamples (seq:" << sequenceNumber << ") called with empty samples. Emitting empty result.";
        std::fill(m_bandLevels.begin(), m_bandLevels.end(), 0.0f);
        emit fftValueCalculated(sequenceNumber, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
        return;
    }

    Q_ASSERT(m_chunkSize > 0);
    Q_ASSERT(m_fftBuffer.size() == m_chunkSize);
    Q_ASSERT(m_window.size() == m_chunkSize);
    Q_ASSERT(m_magnitudes.size() == (m_chunkSize / 2 + 1));

    fftSamples.prepareIndexes();
    uint32_t processLength = std::min(numSamples, m_chunkSize);
    for (uint32_t i = 0; i < processLength; ++i) {
        m_fftBuffer[i] = Complex(static_cast<float>(fftSamples.data(i) * m_window[i]), 0.0f);
    }
    std::fill(m_fftBuffer.begin() + processLength, m_fftBuffer.end(), Complex(0.0f, 0.0f));

    fftIterative(m_fftBuffer);

    uint32_t numFftBins = m_chunkSize / 2 + 1; // Number of bins in m_magnitudes (0 to N/2)

    // Calculate power (magnitude squared) for bins k=1 up to N/2.
    for (uint32_t k = 1; k < numFftBins; ++k) { // k=1 to exclude DC component's direct calculation here
        m_magnitudes[k] = std::norm(m_fftBuffer[k]);
    }
    if (numFftBins > 0) { // Ensure m_magnitudes has at least one element before accessing index 0
        m_magnitudes[0] = 0.0f; // Explicitly set DC component power to zero after FFT
    }

    std::fill(m_bandLevels.begin(), m_bandLevels.end(), 0.0f);

    for (int bandIdx = 0; bandIdx < NumBands; ++bandIdx) {
        uint32_t startBin = m_bandBinRanges[bandIdx].first;
        uint32_t endBin   = m_bandBinRanges[bandIdx].second;

        // Validate bin range
        if (startBin == INVALID_BAND_RANGE || endBin == INVALID_BAND_RANGE ||
            startBin >= numFftBins || endBin >= numFftBins || startBin > endBin) {
            m_bandLevels[bandIdx] = 0.0f; // Invalid or empty band range
            continue;
        }

        float bandPower = std::accumulate(m_magnitudes.begin() + startBin,
                                          m_magnitudes.begin() + endBin + 1, // end iterator is one past the last element
                                          0.0);

        m_bandLevels[bandIdx] = bandPower; // NOT NORMALIZED YET
    }

    // Emit analysis results for NUM_BANDS bands
    emit fftValueCalculated(sequenceNumber,
                            m_bandLevels[FrequencyBand::SubBass],
                            m_bandLevels[FrequencyBand::Bass],
                            m_bandLevels[FrequencyBand::LowMid],
                            m_bandLevels[FrequencyBand::Mid],
                            m_bandLevels[FrequencyBand::HighMid],
                            m_bandLevels[FrequencyBand::Treble],
                            m_bandLevels[FrequencyBand::Air]);
}
