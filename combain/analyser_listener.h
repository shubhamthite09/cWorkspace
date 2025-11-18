#ifndef ANALYSER_LISTENER_H
#define ANALYSER_LISTENER_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *ip;       // e.g. "192.168.0.173"
    int         port;     // e.g. 50001
    const char *outPath;  // e.g. "c:\\ss\\out_f200.txt"
} AnalyserListenerConfig;

// Opaque handle type for a single listener instance
typedef struct AnalyserListenerHandle AnalyserListenerHandle;

/**
 * Start a new background listener instance for one analyser.
 *
 * Returns:
 *   - non-NULL pointer on success
 *   - NULL on error
 */
AnalyserListenerHandle *start_analyser_listener(const AnalyserListenerConfig *cfg);

/**
 * Stop a listener instance and free its resources.
 * Safe to call with NULL (no-op).
 */
void stop_analyser_listener(AnalyserListenerHandle *handle);

#ifdef __cplusplus
}
#endif

#endif // ANALYSER_LISTENER_H
