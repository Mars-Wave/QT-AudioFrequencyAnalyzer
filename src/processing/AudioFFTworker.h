#pragma once

#include <QVector>
#include <complex>
#include <QObject>
#include "ArithmeticCircularQueue.h"

/**
 * @brief Encapsulates FFT calculation and frequency band analysis.
 *        It is intended to run on another thread because of >O(n) cost
 */
class AudioFFTworker : public QObject
{
    Q_OBJECT
public:
    // NUM_BANDS standard frequency bands
    enum FrequencyBand {
        SubBass,    // e.g., 20-60 Hz
        Bass,       // e.g., 60-250 Hz
        LowMid,     // e.g., 250-500 Hz
        Mid,        // e.g., 500-2 kHz
        HighMid,    // e.g., 2-4 kHz
        Treble,     // e.g., 4-6 kHz
        Air,        // e.g., 6-20 kHz
        NumBands    // Keep last
    };
    static_assert(FrequencyBand::NumBands == NUM_BANDS, "AudioFrequencyAnalyzer::FrequencyBand offers NUM_BANDS bands");

    AudioFFTworker(QObject* parent = nullptr) : QObject(parent) {};
    virtual ~AudioFFTworker() {};

    /// -- Multithreading --
    inline bool isMarkedForShutdown() const {
        return m_markedForShutdown;
    }

public slots:
    /**
     * @brief Processes a chunk of audio samples to calculate frequency band levels.
     * @param samples ArithmeticCircularQueue containing the normalized float audio samples. 0 < Size <= chunkSize, ideally == chunkSize.
     * @param sequenceNumber A unique number identifying this processing task for ordered output.
     */
    void processSamples(const ArithmeticCircularQueue<float> &samples, qint64 sequenceNumber);

    /**
     * @brief Initializes the analyzer with audio format parameters.
     * @param sampleRate The sample rate of the audio in Hz.
     * @param chunkSize The number of samples per chunk to be processed. Must be a power of 2.
     */
    void initialize(const uint32_t sampleRate, const uint32_t chunkSize);

    /// -- Multithreading --
    inline void markForShutdown() {
        m_markedForShutdown = true;
    }

signals:
    /// Analogue to processSamples, providing NUM_BANDS band levels
    void fftValueCalculated(uint64_t sequenceNumber,
                            float subBass, float bass, float lowMid, float mid,
                            float highMid, float treble, float air);

private:
    using Complex       = std::complex<float>;
    using ComplexVector = std::vector<Complex>;

    // --- Configuration ---
    uint32_t m_sampleRate   = 0  ;
    uint32_t m_chunkSize    = 0  ; // Expected to be power of 2
    float m_freqResolution = 0.0; // sampleRate / chunkSize

    // --- Band Definitions (Hz) ---
    std::vector<std::pair<float,       float>> m_bandFrequencyRanges;
    std::vector<std::pair<uint32_t, uint32_t>> m_bandBinRanges      ; // Calculated FFT bin index ranges

    // --- Calculation Results ---
    std::vector<float> m_bandLevels; // Stores the latest calculated level for each band (0-100)

    // --- FFT Related Members ---
    bool                  m_isInitialized     = false;
    ComplexVector         m_fftBuffer                ;
    std::vector<float>    m_window                   ; // Windowing function (e.g., Hann) buffer
    std::vector<float>    m_magnitudes               ; // Buffer for calculated magnitudes (size = chunkSize/2 + 1)
    bool                  m_markedForShutdown = false;
    /** Calculates the FFT bin index ranges for each frequency band */
    void calculateBandBinRanges();

    /** Calculate the band ranges for some samplesize, chunksize*/
    void setupBandFrequencyRanges();
};
