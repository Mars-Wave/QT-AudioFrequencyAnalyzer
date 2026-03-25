# Threaded Audio Frequency Analyzer

Leverages multithreadding and adaptative window resizing to analyze audio packets using Fourier and RMS in every kind of microphone or otherwise audio input. 


<img width="1367" height="958" alt="image" src="https://github.com/user-attachments/assets/d1436741-6b07-4982-b862-df8e30ebe344" />


# The Goal: Seeing Audio Levels Instantly

Our `IPAudioInput` is designed to listen to an IP audio stream (like from a security camera via RTSP) and show its volume level in real-time. We want the meter on the screen to react immediately to any sound changes, **both with a sound energy level and an equalizer**.

## Using: Qt's `QAudioDecoder, QMediaPlayer`

To get the audio from the network and understand it, our class uses a tool provided by the Qt framework called `QAudioDecoder`, alternatively we can use `QMediaPlayer`. These give us raw audio sound waves our program can measure. But if you try to process the audio in just **one thread** in tiny, tiny chunks, the level meter seems to update in bursts, maybe every 0.3 seconds or so, rather than smoothly and continuously.

The main reason for this is **receiving,** **buffering and processing**, in that order.

### What is Buffering in this context?

Buffering in audio streaming:

1. **Network Sputters:** Audio data travelling over the internet (or even a local network) doesn't arrive perfectly smoothly. Packets might get slightly delayed, arrive out of order, or even get lost momentarily (like the hose sputtering).
2. **The Decoder's Bucket:** `QAudioDecoder` and `QMediaPlayer`, like most audio tools dealing with streams, collects incoming data in a temporary holding area – a **buffer**. It waits until it has a reasonable amount of audio collected in its "bucket".
3. **The Smoother Pour:** Only when the buffer has a decent chunk of audio (enough to ensure smooth playback or processing without gaps), `QAudioDecoder` or `QMediaPlayer`tells our `IPAudioInputService`, "Okay, I have audio ready for you!" (This is the `bufferReady` signal).

### BufferReady signal: Why the ~0.3 Second Gaps?

The ~0.3-second delay we observe comes from two sources: First, the time it takes for `QAudioDecoder` or `QMediaPlayer` (and the underlying network layers) to fill its "bucket" sufficiently before passing data to our code. Second, after receiving the `bufferReady` signal, our code processes a **0.01-second chunk** of audio. While calculating the **sound energy level is quick, computing the equalizer levels is much slower**. This processing can take up to 0.3 seconds, creating burst updates and causing data starvation during equalizer calculations. The system then attempts to process the next chunk, but several chunks have already been lost in the meantime.

This buffering strategy is **very common** in the world of audio and video streaming, for **Reliability and Efficiency,** that processing strategy is simply much too slow.

## Can We Get *Truly* Instant Updates?

- **With Other Tools:** Using much more complex, lower-level C++ libraries (like FFmpeg or GStreamer directly) already have the infrastructure set up to reduce the delay from ~0.3s down to maybe ~0.05s-0.1s (50-100 milliseconds). However, this adds too much complexity when it can be achieved **purely with a few Qt + C++ native classes**. 
- **With `QAudioDecoder` or `QMediaPlayer` & Network Streams:** It's impossible to analyze while receiving the data in the same process. Receiving data and processing it need to be happening **at the same time**.
- What is the solution? A **Qt-Native Multithreadded Audio Analysis architecture.** 😇

## `ThreadedAudioFrequencyAnalyzer`: Real-Time Audio Analysis

## Addressing Audio Input Characteristics

Audio data using `QT`'s data structures, arrives in discrete chunks (e.g., `QAudioBuffer`). While we cannot alter this input behaviour, **we can create an architecture** focused on rapid, parallel processing of these chunks once they are received, minimizing latency from the point of data arrival to the point of analysis output.

## System Overview

The `ThreadedAudioFrequencyAnalyzer` processes incoming audio buffers using a multi-threaded approach to achieve the following two primary outputs:

1. **Root Mean Square (RMS) Values:** Representing the overall energy or volume of the audio. 
2. **Fast Fourier Transform (FFT) Band Values:** Breaking down the audio signal into distinct frequency bands (e.g., sub-bass, bass, low-mid, mid, high-mid, treble, air).

He **also** runs on **his own thread.** So all copies of the batches, dispatch and receival of data is **pulled away from the main thread as well.**

## Core Components

The system's workflow is designed to efficiently transform raw audio buffers into meaningful RMS and FFT data, leveraging multi-threading and intelligent data processing.

### 1. Receiving and Preparing Audio (`analyzeAudioBuffer`)

When an audio buffer arrives:

- **Initial Setup:** The system first determines the audio's format (sample rate, channels, etc.) from the initial buffer. This information is crucial for all subsequent processing. Incoming samples are then converted to a standard floating-point format for consistent calculations. (`processMono/processStereo`)
- **Data Duplication for Parallel Paths:** Each incoming normalized sample is then directed down two distinct processing paths: one for RMS analysis and one for FFT analysis.

### 2. The RMS Path: Quick Volume Assessment

- **Dedicated RMS Worker (`AudioRMSworker`):** To provide frequent and low-latency volume updates, RMS calculation is handled by a single, dedicated worker thread (`m_rmsThread`). This isolates RMS processing from the more computationally demanding FFT analysis and the main application thread.
- **Batching for Efficiency:** Samples destined for RMS are collected into small batches (`m_rmsBatchBuffer`). Once a batch reaches a configurable size (`m_rmsChunkSize`), it's sent to the `AudioRMSworker`. This batching balances the overhead of inter-thread communication with the desire for frequent updates.
- **Output:** The `AudioRMSworker` computes the RMS value for the batch and signals it back, resulting in the `newRMSValue` emission.

### 3. The FFT Path: EQ Frequency Analysis

This path involves more complex steps to break down the audio into its constituent frequencies:

- **Data Accumulation (`m_fftSample` - Circular Queue):** Samples are continuously added to a circular queue (`m_fftSample`). This queue acts as a rolling window, ensuring that FFT analysis is always performed on the most recent segment of audio.
- **Chunking for FFT:** When enough samples have accumulated in the circular queue to form a complete "FFT chunk" (size defined by `m_currentFFTChunkSize`), this chunk is ready for analysis.
- **Parallel Processing with a Pool of FFT Workers (`AudioFFTworker`):**
    - **Multi-threading for Performance:** FFT is computationally more intensive than RMS. To handle this without blocking, the system maintains a pool of `AudioFFTworker` instances, each operating in its own thread. This allows multiple FFT chunks to be processed concurrently if the CPU has available cores.
    - **Managed Dispatch (`m_availableSlotsSemaphore`):** A semaphore controls access to the FFT worker pool. When an FFT chunk is ready, the system attempts to acquire a "slot" from the semaphore. **If a slot (an idle worker) is available, the data chunk is dispatched to that worker.** If all workers are busy, the system will wait and count these samples as lost until a worker becomes free. This mechanism prevents the system from being overwhelmed with too many concurrent FFT tasks and also **understanding if we need more threads to not lose samples**.
    - **Round-Robin Dispatch:** Work is distributed among the available FFT workers in a round-robin fashion to balance the load.
- **Worker Task:** Each `AudioFFTworker` performs the Fast Fourier Transform on its assigned chunk, calculating the energy levels for predefined frequency bands (e.g., sub-bass, bass, low-mid, mid, high-mid, treble, air).
- **NOTE**: It is a radix-2 FFT analysis, it is fast but the least reliable. In our case, this is the best compromise, since we just want a level per band, and not the most incredibly accurate audio fidelity.

### 4. Ensuring Output Order and Thread Safety

- **Sequence Numbers:** To handle results that might complete out-of-order due to parallel processing, FFT tasks are dispatched with sequence numbers. The system ensures that FFT results are emitted in the correct chronological order.
- **Mutexes and Semaphores:** Throughout the system, mutexes (`m_dispatchMutex`, `m_resultsMutex`) are used to protect shared data structures from concurrent access issues, while semaphores manage the pool of worker threads. This is essential for maintaining data integrity and stability in a multi-threaded environment.

## Output Signals

- `newRMSValue(float rmsValue)`: Emitted when a new RMS value is calculated.
- `newFFTValue(float subBass, float bass, ... float treble, float air, uint16_t fftSamplesLost)`: Emitted with normalized frequency band values and the count of FFT samples lost since the last emission.
- `analyzerError(const QString& statusMessage)`: Emitted if unsupported audio format, initialization failures errors occur.

## Key Advantages of the Design

- **Responsiveness:** By offloading RMS and FFT calculations to dedicated worker threads, the main application thread remains unburdened.
- **Detailed Insight:** Simultaneous calculation of RMS (overall volume) and multi-band FFT (frequency spectrum) provides lots of audio-based signals.
- **Scalability and Robustness:** The multi-threaded design, managed with semaphores and thread-safe data handling, provides a scalable foundation that can be adapted to different hardware capabilities (e.g., by adjusting `numFFTthreads`).

## UI Integration Note

While this system provides processed and (for FFT) normalized data, UIs consuming these signals typically benefit from additional temporal smoothing (e.g., averaging, easing functions) on the received values. This final presentation-layer smoothing helps create fluid visual animations for meters and spectrum displays, complementing the real-time data provided by the analyzer.
