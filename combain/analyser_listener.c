#define _CRT_SECURE_NO_WARNINGS

#include "analyser_listener.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #include <process.h>
  #pragma comment(lib, "ws2_32.lib")
#else
  #include <unistd.h>
  #include <pthread.h>
  #include <sys/types.h>
  #include <sys/socket.h>
  #include <arpa/inet.h>
#endif

// ===============================================================
//  Per-instance state
// ===============================================================

struct AnalyserListenerHandle {
    volatile int running;       // 1 = keep running, 0 = stop

    char ip[64];
    int  port;
    char outPath[512];

#ifdef _WIN32
    HANDLE thread;
#else
    pthread_t thread;
#endif
};

// ===============================================================
//  Small cross-platform helpers
// ===============================================================

#ifdef _WIN32

static int socket_init(void) {
    WSADATA wsa;
    return WSAStartup(MAKEWORD(2, 2), &wsa);
}

static void socket_cleanup(void) {
    WSACleanup();
}

static void sleep_ms(unsigned int ms) {
    Sleep(ms);
}

#else // POSIX

static int socket_init(void) { return 0; }
static void socket_cleanup(void) { /* no-op */ }

static void sleep_ms(unsigned int ms) {
    usleep(ms * 1000);
}

#endif

// ===============================================================
//  Core worker logic (per instance)
// ===============================================================

static int connect_to_analyser(struct AnalyserListenerHandle *h) {
    int sockfd = -1;

#ifdef _WIN32
    sockfd = (int)socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == INVALID_SOCKET) {
        fprintf(stderr, "[listener %s:%d] socket() failed: %d\n",
                h->ip, h->port, WSAGetLastError());
        return -1;
    }
#else
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("[listener] socket");
        return -1;
    }
#endif

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port   = htons((unsigned short)h->port);

    if (inet_pton(AF_INET, h->ip, &addr.sin_addr) <= 0) {
        fprintf(stderr, "[listener %s:%d] inet_pton failed\n", h->ip, h->port);
#ifdef _WIN32
        closesocket(sockfd);
#else
        close(sockfd);
#endif
        return -1;
    }

    fprintf(stderr, "[listener %s:%d] Connecting...\n", h->ip, h->port);

    if (connect(sockfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
#ifdef _WIN32
        fprintf(stderr, "[listener %s:%d] connect() failed: %d\n",
                h->ip, h->port, WSAGetLastError());
        closesocket(sockfd);
#else
        perror("[listener] connect");
        close(sockfd);
#endif
        return -1;
    }

    fprintf(stderr, "[listener %s:%d] Connected.\n", h->ip, h->port);
    return sockfd;
}

static void read_loop(struct AnalyserListenerHandle *h, int sockfd) {
    FILE *fp = fopen(h->outPath, "ab");  // append binary
    if (!fp) {
        perror("[listener] fopen outPath");
        return;
    }

    fprintf(stderr, "[listener %s:%d] Writing to %s ...\n",
            h->ip, h->port, h->outPath);

    char buf[4096];

    while (h->running) {
#ifdef _WIN32
        int n = recv(sockfd, buf, sizeof(buf), 0);
        if (n == SOCKET_ERROR) {
            fprintf(stderr, "[listener %s:%d] recv() failed: %d\n",
                    h->ip, h->port, WSAGetLastError());
            break;
        }
#else
        ssize_t n = recv(sockfd, buf, sizeof(buf), 0);
        if (n < 0) {
            perror("[listener] recv");
            break;
        }
#endif
        if (n == 0) {
            fprintf(stderr, "[listener %s:%d] Connection closed by remote.\n",
                    h->ip, h->port);
            break;
        }

        size_t written = fwrite(buf, 1, (size_t)n, fp);
        if (written != (size_t)n) {
            perror("[listener] fwrite");
            break;
        }

        fflush(fp); // let other processes see data
    }

    fclose(fp);
    fprintf(stderr, "[listener %s:%d] read_loop finished.\n", h->ip, h->port);
}

// ===============================================================
//  Thread entry (per instance)
// ===============================================================

#ifdef _WIN32
static unsigned __stdcall listener_thread(void *arg)
#else
static void *listener_thread(void *arg)
#endif
{
    struct AnalyserListenerHandle *h = (struct AnalyserListenerHandle *)arg;

    if (socket_init() != 0) {
        fprintf(stderr, "[listener %s:%d] socket_init failed.\n",
                h->ip, h->port);
#ifdef _WIN32
        return 0;
#else
        return NULL;
#endif
    }

    while (h->running) {
        int sockfd = connect_to_analyser(h);
        if (sockfd < 0) {
            // wait and retry
            if (!h->running) break;
            fprintf(stderr, "[listener %s:%d] Reconnect in 3s...\n",
                    h->ip, h->port);
            sleep_ms(3000);
            continue;
        }

        read_loop(h, sockfd);

#ifdef _WIN32
        closesocket(sockfd);
#else
        close(sockfd);
#endif

        if (!h->running) break;

        fprintf(stderr, "[listener %s:%d] Reconnect in 3s...\n",
                h->ip, h->port);
        sleep_ms(3000);
    }

    socket_cleanup();

    fprintf(stderr, "[listener %s:%d] Thread exiting.\n", h->ip, h->port);

#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
}

// ===============================================================
//  Public API
// ===============================================================

AnalyserListenerHandle *start_analyser_listener(const AnalyserListenerConfig *cfg) {
    if (!cfg || !cfg->ip || !cfg->outPath || cfg->port <= 0) {
        fprintf(stderr, "[listener] Invalid config.\n");
        return NULL;
    }

    struct AnalyserListenerHandle *h =
        (struct AnalyserListenerHandle *)calloc(1, sizeof(*h));
    if (!h) {
        perror("[listener] calloc");
        return NULL;
    }

    h->running = 1;

    strncpy(h->ip, cfg->ip, sizeof(h->ip) - 1);
    h->ip[sizeof(h->ip) - 1] = '\0';

    h->port = cfg->port;

    strncpy(h->outPath, cfg->outPath, sizeof(h->outPath) - 1);
    h->outPath[sizeof(h->outPath) - 1] = '\0';

#ifdef _WIN32
    uintptr_t handle = _beginthreadex(
        NULL,
        0,
        listener_thread,
        h,
        0,
        NULL
    );
    if (handle == 0) {
        fprintf(stderr, "[listener %s:%d] _beginthreadex failed.\n",
                h->ip, h->port);
        free(h);
        return NULL;
    }
    h->thread = (HANDLE)handle;
#else
    int err = pthread_create(&h->thread, NULL, listener_thread, h);
    if (err != 0) {
        fprintf(stderr, "[listener %s:%d] pthread_create failed: %s\n",
                h->ip, h->port, strerror(err));
        free(h);
        return NULL;
    }
    // We keep the thread joinable so we can wait on stop()
#endif

    fprintf(stderr, "[listener %s:%d] Started, output: %s\n",
            h->ip, h->port, h->outPath);

    return h;
}

void stop_analyser_listener(AnalyserListenerHandle *h) {
    if (!h) return;

    h->running = 0;

#ifdef _WIN32
    if (h->thread) {
        WaitForSingleObject(h->thread, 3000);
        CloseHandle(h->thread);
        h->thread = NULL;
    }
#else
    // Wait for thread to exit
    pthread_join(h->thread, NULL);
#endif

    free(h);
}
