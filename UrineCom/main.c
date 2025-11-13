// serial_logger.c  — version with clear error when device not connected
#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

#ifdef _WIN32
  #include <windows.h>
  #include <direct.h>
  #define DEFAULT_PORT "COM3"
  #define DEFAULT_SS_DIR "C:\\ss\\serial_data.txt"
  static HANDLE hSerial = INVALID_HANDLE_VALUE;
#else
  #include <unistd.h>
  #include <fcntl.h>
  #include <termios.h>
  #include <sys/stat.h>
  #define DEFAULT_PORT "/dev/ttyUSB0"
  #define DEFAULT_SS_DIR "./ss/serial_data.txt"
  static int serial_fd = -1;
#endif

static volatile int keepRunning = 1;

void intHandler(int dummy) {
  keepRunning = 0;
  printf("\n🔌 Closing serial port...\n");
}

#ifdef _WIN32
int open_serial(const char* portName, int baudRate) {
  char fullPortName[32];
  snprintf(fullPortName, sizeof(fullPortName), "\\\\.\\%s", portName);
  hSerial = CreateFileA(fullPortName, GENERIC_READ | GENERIC_WRITE, 0, 0, OPEN_EXISTING, 0, 0);
  if (hSerial == INVALID_HANDLE_VALUE) {
    fprintf(stderr, "❌ ERROR: Unable to open %s — check USB connection or COM port.\n", portName);
    return 0;
  }

  DCB dcb = {0};
  dcb.DCBlength = sizeof(dcb);
  if (!GetCommState(hSerial, &dcb)) {
    fprintf(stderr, "❌ GetCommState failed.\n");
    CloseHandle(hSerial); hSerial = INVALID_HANDLE_VALUE;
    return 0;
  }

  dcb.BaudRate = baudRate;
  dcb.ByteSize = 8;
  dcb.StopBits = ONESTOPBIT;
  dcb.Parity   = NOPARITY;
  dcb.fDtrControl = DTR_CONTROL_ENABLE;
  if (!SetCommState(hSerial, &dcb)) {
    fprintf(stderr, "❌ SetCommState failed.\n");
    CloseHandle(hSerial); hSerial = INVALID_HANDLE_VALUE;
    return 0;
  }

  COMMTIMEOUTS t = {0};
  t.ReadIntervalTimeout = 50;
  t.ReadTotalTimeoutConstant = 50;
  t.ReadTotalTimeoutMultiplier = 10;
  SetCommTimeouts(hSerial, &t);

  printf("✅ Opened %s @ %d baud\n", portName, baudRate);
  return 1;
}

void close_serial() {
  if (hSerial != INVALID_HANDLE_VALUE) {
    CloseHandle(hSerial);
    hSerial = INVALID_HANDLE_VALUE;
    printf("🔒 Port closed.\n");
  }
}

int read_serial_line(char* buf, size_t maxlen) {
  DWORD bytes = 0; char ch; size_t i = 0;
  while (i < maxlen - 1) {
    if (!ReadFile(hSerial, &ch, 1, &bytes, NULL)) return -1;
    if (bytes == 0) continue;
    if (ch == '\n') break;
    buf[i++] = ch;
  }
  buf[i] = '\0';
  return (int)i;
}

#else   // ----------------- macOS / Linux -----------------
int open_serial(const char* portName, int baudRate) {
  serial_fd = open(portName, O_RDWR | O_NOCTTY | O_SYNC);
  if (serial_fd < 0) {
    perror("❌ ERROR opening port");
    fprintf(stderr, "⚠️  Check if the USB device is connected or use correct /dev/cu.* path.\n");
    return 0;
  }

  struct termios tty;
  if (tcgetattr(serial_fd, &tty) != 0) {
    perror("tcgetattr");
    close(serial_fd); serial_fd = -1;
    return 0;
  }

  cfsetospeed(&tty, B19200);
  cfsetispeed(&tty, B19200);

  tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8;
  tty.c_iflag &= ~IGNBRK;
  tty.c_lflag = 0;
  tty.c_oflag = 0;
  tty.c_cc[VMIN] = 1;
  tty.c_cc[VTIME] = 1;
  tty.c_iflag &= ~(IXON | IXOFF | IXANY);
  tty.c_cflag |= (CLOCAL | CREAD);
  tty.c_cflag &= ~(PARENB | PARODD | CSTOPB | CRTSCTS);

  if (tcsetattr(serial_fd, TCSANOW, &tty) != 0) {
    perror("tcsetattr");
    close(serial_fd); serial_fd = -1;
    return 0;
  }

  printf("✅ Opened %s @ %d baud\n", portName, baudRate);
  return 1;
}

void close_serial() {
  if (serial_fd >= 0) {
    close(serial_fd);
    serial_fd = -1;
    printf("🔒 Port closed.\n");
  }
}

int read_serial_line(char* buf, size_t maxlen) {
  char ch; size_t i = 0;
  while (i < maxlen - 1) {
    int n = read(serial_fd, &ch, 1);
    if (n < 0) return -1;
    if (ch == '\r') continue;
    if (ch == '\n') break;
    buf[i++] = ch;
  }
  buf[i] = '\0';
  return (int)i;
}
#endif
// --------------------------------------------------------

int main(int argc, char* argv[]) {
  const char* portName = (argc > 1) ? argv[1] : DEFAULT_PORT;
  const char* filePath = DEFAULT_SS_DIR;
  const int baudRate = 19200;

  signal(SIGINT, intHandler);

#ifndef _WIN32
  mkdir("ss", 0755);   // ensure directory exists
#else
  _mkdir("C:\\ss");
#endif

  // Try to open the port
  if (!open_serial(portName, baudRate)) {
    fprintf(stderr, "❌ Exiting: serial device not found.\n");
    return 1;
  }

  FILE* fout = fopen(filePath, "a");
  if (!fout) {
    fprintf(stderr, "❌ Cannot open output file %s\n", filePath);
    close_serial();
    return 1;
  }

  printf("📡 Listening on %s ... writing to %s\n", portName, filePath);

  char line[512];
  while (keepRunning) {
    int len = read_serial_line(line, sizeof(line));
    if (len > 0) {
      printf("Received data: %s\n", line);
      fprintf(fout, "%s\n", line);
      fflush(fout);
    }
  }

  fclose(fout);
  close_serial();
  printf("✅ Exiting gracefully.\n");
  return 0;
}
