// analyser_uploader.c
// Cross-platform C port of the Node.js batch uploader
// - Scans a directory every 10 seconds
// - Parses analyser outputs
// - Sends JSON via HTTP POST using libcurl
// - Deletes file on successful upload (HTTP 2xx)
// - Uses MachineID/MAC from env (defaults provided)

#define _CRT_SECURE_NO_WARNINGS

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>

#ifdef _WIN32
  #include <windows.h>
  #include <io.h>
  #include <direct.h>
  #define PATH_SEP '\\'
#else
  #include <dirent.h>
  #include <sys/stat.h>
  #include <unistd.h>
  #define PATH_SEP '/'
#endif

#include <curl/curl.h>

// -------- Portable tokenization shim (strtok_r on POSIX, strtok_s on Windows) -----
#ifdef _WIN32
  #define STRTOK_CTX char*
  #define STRTOK(str, delim, ctx) strtok_s((str), (delim), (ctx))
#else
  #define STRTOK_CTX char*
  #define STRTOK(str, delim, ctx) strtok_r((str), (delim), (ctx))
#endif

// ===================== Config =====================
static const char* API_ANALYSER1 = "https://api.superceuticals.in/test-one/saveCbc";
static const char* API_ANALYSER2 = "https://api.superceuticals.in/test-two/saveResults";
static const char* API_ANALYSER3 = "https://api.superceuticals.in/test-three/saveUrine";

// Default scan folder (matches Node sample)
static const char* DEFAULT_SCAN_DIR =
#ifdef _WIN32
  "C:\\ss";
#else
  "/ss";
#endif

// ===================== Small utils =====================
static int starts_with(const char* s, const char* prefix) {
  if (!s || !prefix) return 0;
  size_t a = strlen(s), b = strlen(prefix);
  return a >= b && strncmp(s, prefix, b) == 0;
}

static void trim_inplace(char* s) {
  if (!s) return;
  size_t len = strlen(s);
  size_t i = 0;
  while (i < len && isspace((unsigned char)s[i])) i++;
  size_t j = len;
  while (j > i && isspace((unsigned char)s[j-1])) j--;
  if (i > 0) memmove(s, s + i, j - i);
  s[j - i] = '\0';
}

// Split by a single char delimiter, preserving empty tokens (like JS split on '|')
// Returns heap-allocated array of char* (also heap allocated). *count is set.
// Caller must free each token and the array.
static char** split_preserve_empty(const char* s, char delim, int* count) {
  if (!s) { *count = 0; return NULL; }
  int cap = 16, n = 0;
  char** out = (char**)malloc(cap * sizeof(char*));
  const char* p = s;
  const char* start = p;
  while (1) {
    if (*p == delim || *p == '\0') {
      int len = (int)(p - start);
      char* tok = (char*)malloc((size_t)len + 1);
      memcpy(tok, start, (size_t)len);
      tok[len] = '\0';
      if (n >= cap) { cap *= 2; out = (char**)realloc(out, cap * sizeof(char*)); }
      out[n++] = tok;
      if (*p == '\0') break;
      p++; start = p;
    } else {
      p++;
    }
  }
  *count = n;
  return out;
}

static void free_split(char** arr, int count) {
  if (!arr) return;
  for (int i = 0; i < count; i++) free(arr[i]);
  free(arr);
}

// Remove spaces and asterisks (like s.replace(/[ *]+/g,""))
static void remove_spaces_and_asterisks(char* s) {
  if (!s) return;
  char* w = s;
  for (char* r = s; *r; ++r) {
    if (*r != ' ' && *r != '*') *w++ = *r;
  }
  *w = '\0';
}

// Read entire file as text into heap buffer. Returns NULL on error.
// Caller must free(*out).
static int read_file_text(const char* path, char** out) {
  FILE* f = fopen(path, "rb");
  if (!f) return 0;
  fseek(f, 0, SEEK_END);
  long sz = ftell(f);
  if (sz < 0) { fclose(f); return 0; }
  fseek(f, 0, SEEK_SET);
  char* buf = (char*)malloc((size_t)sz + 1);
  if (!buf) { fclose(f); return 0; }
  size_t rd = fread(buf, 1, (size_t)sz, f);
  fclose(f);
  buf[rd] = '\0';
  *out = buf;
  return 1;
}

// Delete a file
static void delete_file(const char* path) {
#ifdef _WIN32
  _unlink(path);
#else
  unlink(path);
#endif
}

// ===================== HTTP (libcurl) =====================
struct mem_sink { char* data; size_t size; };
static size_t write_cb(void* contents, size_t size, size_t nmemb, void* userp) {
  size_t real = size * nmemb;
  struct mem_sink* ms = (struct mem_sink*)userp;
  char* ptr = (char*)realloc(ms->data, ms->size + real + 1);
  if (!ptr) return 0;
  ms->data = ptr;
  memcpy(ms->data + ms->size, contents, real);
  ms->size += real;
  ms->data[ms->size] = '\0';
  return real;
}

// Returns 1 on success (HTTP 2xx), 0 otherwise. On success, caller may delete file.
static int http_post_json(const char* url, const char* json_body, char** response_out) {
  CURL* curl = curl_easy_init();
  if (!curl) return 0;

  struct curl_slist* headers = NULL;
  headers = curl_slist_append(headers, "Content-Type: application/json");

  struct mem_sink sink = {0};

  curl_easy_setopt(curl, CURLOPT_URL, url);
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt(curl, CURLOPT_POST, 1L);
  curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_body);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &sink);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);

  // If you ship a cacert.pem, uncomment the next line and set the path:
  // curl_easy_setopt(curl, CURLOPT_CAINFO, "cacert.pem");

  CURLcode res = curl_easy_perform(curl);
  long code = 0;
  if (res == CURLE_OK) curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);

  if (response_out) *response_out = sink.data; else free(sink.data);
  curl_slist_free_all(headers);
  curl_easy_cleanup(curl);

  return (res == CURLE_OK && code >= 200 && code < 300);
}

// ===================== JSON building helpers =====================
// (Simple snprintf-based composition to avoid external JSON libs)
static void json_escape_copy(char* dst, size_t dst_cap, const char* src) {
  // Minimal escape: backslash and quotes; control chars -> skip
  size_t w = 0;
  for (size_t i = 0; src && src[i] && w + 2 < dst_cap; i++) {
    unsigned char c = (unsigned char)src[i];
    if (c == '\\' || c == '\"') {
      if (w + 2 >= dst_cap) break;
      dst[w++] = '\\'; dst[w++] = (char)c;
    } else if (c >= 0x20) {
      dst[w++] = (char)c;
    }
  }
  if (w < dst_cap) dst[w] = '\0';
}

// Safe append to a buffer
static void sb_append(char** buf, size_t* len, size_t* cap, const char* s) {
  size_t L = strlen(s);
  if (*len + L + 1 > *cap) {
    size_t newcap = (*cap == 0 ? 1024 : *cap * 2);
    while (newcap < *len + L + 1) newcap *= 2;
    *buf = (char*)realloc(*buf, newcap);
    *cap = newcap;
  }
  memcpy(*buf + *len, s, L);
  *len += L;
  (*buf)[*len] = '\0';
}

// Append a quoted JSON string with escaping
static void sb_append_json_str(char** buf, size_t* len, size_t* cap, const char* s) {
  sb_append(buf, len, cap, "\"");
  char tmp[4096]; tmp[0] = '\0';
  json_escape_copy(tmp, sizeof(tmp), s ? s : "");
  sb_append(buf, len, cap, tmp);
  sb_append(buf, len, cap, "\"");
}

// ===================== Analyser parsers =====================

// Analyser 1: takes mydataArray (already split by comma)
static int analyser_1(char** arr, int n, const char* filePath,
                      const char* MachineID, const char* MAC) {
  int start = 6, end = 28;
  if (n < start) return 0;

  char* json = NULL; size_t len = 0, cap = 0;
  sb_append(&json, &len, &cap, "{\"mydata\":[");
  int first = 1;

  for (int idx = start; idx < n && idx < end; idx++) {
    int token_count = 0;
    char** tokens = split_preserve_empty(arr[idx], '|', &token_count);
    if (!tokens || token_count == 0) { free_split(tokens, token_count); continue; }

    int slice_pos = idx - start;
    int base = (slice_pos >= 4 && slice_pos < 22) ? 3 : 0;
    if (base >= token_count) { free_split(tokens, token_count); continue; }

    int pcount = 0;
    char** parts = split_preserve_empty(tokens[base + 0], '^', &pcount);

    const char* test_code = (pcount > 0 ? parts[0] : "");
    const char* name      = (pcount > 1 ? parts[1] : "");
    const char* system    = (pcount > 2 ? parts[2] : "");

    const char* result        = (base + 1 < token_count ? tokens[base + 1] : "");
    const char* units         = (base + 2 < token_count ? tokens[base + 2] : "");
    const char* normal_range  = (base + 4 < token_count ? tokens[base + 4] : "");
    const char* flag          = (base + 5 < token_count ? tokens[base + 5] : "");

    if (!first) sb_append(&json, &len, &cap, ",");
    first = 0;

    sb_append(&json, &len, &cap, "{");
    sb_append(&json, &len, &cap, "\"test_code\":"); sb_append_json_str(&json, &len, &cap, test_code); sb_append(&json, &len, &cap, ",");
    sb_append(&json, &len, &cap, "\"name\":");      sb_append_json_str(&json, &len, &cap, name);      sb_append(&json, &len, &cap, ",");
    sb_append(&json, &len, &cap, "\"system\":");    sb_append_json_str(&json, &len, &cap, system);    sb_append(&json, &len, &cap, ",");
    sb_append(&json, &len, &cap, "\"result\":");    sb_append_json_str(&json, &len, &cap, result);    sb_append(&json, &len, &cap, ",");
    sb_append(&json, &len, &cap, "\"units\":");     sb_append_json_str(&json, &len, &cap, units);     sb_append(&json, &len, &cap, ",");
    sb_append(&json, &len, &cap, "\"normal_range\":"); sb_append_json_str(&json, &len, &cap, normal_range); sb_append(&json, &len, &cap, ",");
    sb_append(&json, &len, &cap, "\"flag\":");      sb_append_json_str(&json, &len, &cap, flag);
    sb_append(&json, &len, &cap, "}");

    free_split(parts, pcount);
    free_split(tokens, token_count);
  }

  sb_append(&json, &len, &cap, "],\"MachineID\":");
  sb_append_json_str(&json, &len, &cap, MachineID);
  sb_append(&json, &len, &cap, ",\"MAC\":");
  sb_append_json_str(&json, &len, &cap, MAC);
  sb_append(&json, &len, &cap, "}");

  char* resp = NULL;
  int ok = http_post_json(API_ANALYSER1, json, &resp);
  if (!ok) {
    fprintf(stderr, "❌ Failed to upload [%s]: %s\n", API_ANALYSER1, resp ? resp : "(no response)");
  } else {
    printf("✅ Upload successful (Analyser1): %s\n", filePath);
    delete_file(filePath);
  }
  free(resp);
  free(json);
  return ok;
}

// Analyser 2: arr[0] is the big line; split on '|' (preserving empties)
static int analyser_2(char** arr, int n, const char* filePath,
                      const char* MachineID, const char* MAC) {
  if (n <= 0) return 0;

  int rcount = 0;
  char** resultParts = split_preserve_empty(arr[0], '|', &rcount);
  if (!resultParts || rcount == 0) { free_split(resultParts, rcount); return 0; }

  int pcount0 = 0;
  char** parts0 = split_preserve_empty(resultParts[0], '^', &pcount0);

  int pcount1 = 0;
  char** parts1 = NULL;
  if (rcount > 3) parts1 = split_preserve_empty(resultParts[3], '^', &pcount1);

  const char* test_code = (pcount0 > 0 ? parts0[0] : "");
  const char* test_name = (pcount0 > 1 ? parts0[1] : "");
  const char* system    = (pcount0 > 2 ? parts0[2] : "");

  const char* result = (rcount > 2 ? resultParts[2] : "");
  const char* units  = (pcount1 > 0 ? parts1[0] : "");

  char units_system[512] = {0};
  if (pcount1 > 1) strncat(units_system, parts1[1], sizeof(units_system)-1);
  if (pcount1 > 2) strncat(units_system, parts1[2], sizeof(units_system)-1);

  char* json = NULL; size_t len = 0, cap = 0;
  sb_append(&json, &len, &cap, "{\"mydata\":[{");
  sb_append(&json, &len, &cap, "\"test_code\":");   sb_append_json_str(&json, &len, &cap, test_code); sb_append(&json, &len, &cap, ",");
  sb_append(&json, &len, &cap, "\"test_name\":");   sb_append_json_str(&json, &len, &cap, test_name); sb_append(&json, &len, &cap, ",");
  sb_append(&json, &len, &cap, "\"system\":");      sb_append_json_str(&json, &len, &cap, system);    sb_append(&json, &len, &cap, ",");
  sb_append(&json, &len, &cap, "\"result\":");      sb_append_json_str(&json, &len, &cap, result);    sb_append(&json, &len, &cap, ",");
  sb_append(&json, &len, &cap, "\"units\":");       sb_append_json_str(&json, &len, &cap, units);     sb_append(&json, &len, &cap, ",");
  sb_append(&json, &len, &cap, "\"units_system\":");sb_append_json_str(&json, &len, &cap, units_system);
  sb_append(&json, &len, &cap, "}],\"MachineID\":");
  sb_append_json_str(&json, &len, &cap, MachineID);
  sb_append(&json, &len, &cap, ",\"MAC\":");
  sb_append_json_str(&json, &len, &cap, MAC);
  sb_append(&json, &len, &cap, "}");

  char* resp = NULL;
  int ok = http_post_json(API_ANALYSER2, json, &resp);
  if (!ok) {
    fprintf(stderr, "❌ Failed to upload [%s]: %s\n", API_ANALYSER2, resp ? resp : "(no response)");
  } else {
    printf("✅ Upload successful (Analyser2): %s\n", filePath);
    delete_file(filePath);
  }
  free(resp);
  free(json);
  free_split(parts0, pcount0);
  free_split(parts1, pcount1);
  free_split(resultParts, rcount);
  return ok;
}

// Analyser 3: read file raw, strip spaces/asterisks, then parse between the marker lines
static int analyser_3(const char* filePath, const char* MachineID, const char* MAC) {
  char* text = NULL;
  if (!read_file_text(filePath, &text)) {
    fprintf(stderr, "❌ Error in analyser_3: cannot read file\n");
    return 0;
  }

  remove_spaces_and_asterisks(text);

  int cap = 64, n = 0;
  char** lines = (char**)malloc(cap * sizeof(char*));
  STRTOK_CTX save = NULL;
  char* tok = STRTOK(text, "\n", &save);
  while (tok) {
    if (n >= cap) { cap *= 2; lines = (char**)realloc(lines, cap * sizeof(char*)); }
    lines[n++] = tok;
    tok = STRTOK(NULL, "\n", &save);
  }

  if (n > 7) {
    char* l7 = lines[7];
    size_t L = strlen(l7);
    if (L > 0 && l7[L-1] == '\r') l7[L-1] = '\0';
    if (strcmp(l7, "Measurementerror!") == 0) {
      fprintf(stderr, "⚠️  Test error found in %s\n", filePath);
      free(lines);
      free(text);
      return 0;
    }
  }

  int start = -1, end = -1;
  for (int i = 0; i < n; i++) {
    if (strcmp(lines[i], "........................") == 0) start = i;
    if (strcmp(lines[i], "------------------------") == 0) { end = i; break; }
  }
  if (start < 0 || end < 0 || start + 1 >= end) {
    fprintf(stderr, "❌ Error in analyser_3: markers not found\n");
    free(lines);
    free(text);
    return 0;
  }

  const char* labels[10] = {"BLD","LEU","BIL","UBG","KET","GLU","PRO","pH","NIT","SG"};
  const int expected = 10;
  int count = (end - (start + 1));
  if (count < expected) {
    fprintf(stderr, "❌ Error in analyser_3: insufficient result lines (%d)\n", count);
    free(lines);
    free(text);
    return 0;
  }

  char* json = NULL; size_t len = 0, capj = 0;
  sb_append(&json, &len, &capj, "{\"mydata\":{");
  for (int i = 0; i < expected; i++) {
    const char* row = lines[start + 1 + i];
    const char* key = labels[i];
    size_t klen = strlen(key);
    const char* val = row;
    if (starts_with(row, key)) val = row + klen;

    char tmp[512]; tmp[0] = '\0';
    size_t w = 0;
    size_t rlen = strlen(val);
    for (size_t r = 0; r < rlen && w + 1 < sizeof(tmp); r++) {
      if (r + 5 <= rlen && strncmp(val + r, "mg/dl", 5) == 0) { r += 4; continue; }
      tmp[w++] = val[r];
    }
    tmp[w] = '\0';

    if (i > 0) sb_append(&json, &len, &capj, ",");
    sb_append(&json, &len, &capj, "\""); sb_append(&json, &len, &capj, key); sb_append(&json, &len, &capj, "\":");
    sb_append_json_str(&json, &len, &capj, tmp);
  }
  sb_append(&json, &len, &capj, "},\"MachineID\":");
  sb_append_json_str(&json, &len, &capj, MachineID);
  sb_append(&json, &len, &capj, ",\"MAC\":");
  sb_append_json_str(&json, &len, &capj, MAC);
  sb_append(&json, &len, &capj, "}");

  char* resp = NULL;
  int ok = http_post_json(API_ANALYSER3, json, &resp);
  if (!ok) {
    fprintf(stderr, "❌ Failed to upload [%s]: %s\n", API_ANALYSER3, resp ? resp : "(no response)");
  } else {
    printf("✅ Upload successful (Analyser3): %s\n", filePath);
    delete_file(filePath);
  }
  free(resp);
  free(json);

  // 'lines' pointers reference 'text'; just free containers
  free(lines);
  free(text);
  return ok;
}

// ===================== Directory scan & dispatch =====================

// Load file, do JS-like slice(1, -2).trim(), then split by ','
// Returns array of tokens (arr), count (out_n), and backing storage (out_storage) to free later.
static int load_and_tokenize_csvish(const char* path, char*** out_arr, int* out_n, char** out_storage) {
  char* text = NULL;
  if (!read_file_text(path, &text)) return 0;

  size_t L = strlen(text);
  size_t start = (L > 0 ? 1 : 0);
  size_t end = (L >= 2 ? L - 2 : 0);
  if (end < start) end = start;

  char* sliced = (char*)malloc(end - start + 1);
  memcpy(sliced, text + start, end - start);
  sliced[end - start] = '\0';
  free(text);

  trim_inplace(sliced);

  int cap = 16, n = 0;
  char** arr = (char**)malloc(cap * sizeof(char*));
  STRTOK_CTX save = NULL;
  char* tok = STRTOK(sliced, ",", &save);
  while (tok) {
    if (n >= cap) { cap *= 2; arr = (char**)realloc(arr, cap * sizeof(char*)); }
    arr[n++] = tok;
    tok = STRTOK(NULL, ",", &save);
  }

  *out_arr = arr;
  *out_n = n;
  *out_storage = sliced; // caller must free
  return 1;
}

static void free_tokenized_csvish(char** arr, char* storage) {
  free(arr);
  free(storage);
}

static void process_directory(const char* dirPath, const char* MachineID, const char* MAC) {
#ifdef _WIN32
  char pattern[MAX_PATH];
  snprintf(pattern, sizeof(pattern), "%s\\*.txt", dirPath);

  WIN32_FIND_DATAA ffd;
  HANDLE hFind = FindFirstFileA(pattern, &ffd);
  if (hFind == INVALID_HANDLE_VALUE) return;

  do {
    if (!(ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
      char filePath[MAX_PATH];
      snprintf(filePath, sizeof(filePath), "%s\\%s", dirPath, ffd.cFileName);

      char** arr = NULL; int n = 0; char* storage = NULL;
      if (!load_and_tokenize_csvish(filePath, &arr, &n, &storage)) continue;

      if (n > 0 && starts_with(arr[0], "\\\\SCAN\n")) {
        printf("📥 Processing %s → Analyser 3\n", ffd.cFileName);
        analyser_3(filePath, MachineID, MAC);
      } else if (n > 0 && starts_with(arr[0], "02001^Take Mode")) {
        printf("📥 Processing %s → Analyser 1\n", ffd.cFileName);
        analyser_1(arr, n, filePath, MachineID, MAC);
      } else {
        printf("📥 Processing %s → Analyser 2\n", ffd.cFileName);
        analyser_2(arr, n, filePath, MachineID, MAC);
      }
      free_tokenized_csvish(arr, storage);
    }
  } while (FindNextFileA(hFind, &ffd));
  FindClose(hFind);

#else
  DIR* d = opendir(dirPath);
  if (!d) return;
  struct dirent* ent;
  while ((ent = readdir(d)) != NULL) {
    if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
    const char* dot = strrchr(ent->d_name, '.');
    if (!dot || strcmp(dot, ".txt") != 0) continue;

    char filePath[4096];
    snprintf(filePath, sizeof(filePath), "%s/%s", dirPath, ent->d_name);

    char** arr = NULL; int n = 0; char* storage = NULL;
    if (!load_and_tokenize_csvish(filePath, &arr, &n, &storage)) continue;

    if (n > 0 && starts_with(arr[0], "\\\\SCAN\n")) {
      printf("📥 Processing %s → Analyser 3\n", ent->d_name);
      analyser_3(filePath, MachineID, MAC);
    } else if (n > 0 && starts_with(arr[0], "02001^Take Mode")) {
      printf("📥 Processing %s → Analyser 1\n", ent->d_name);
      analyser_1(arr, n, filePath, MachineID, MAC);
    } else {
      printf("📥 Processing %s → Analyser 2\n", ent->d_name);
      analyser_2(arr, n, filePath, MachineID, MAC);
    }
    free_tokenized_csvish(arr, storage);
  }
  closedir(d);
#endif
}

// ===================== Main loop =====================
int main(void) {
  const char* envMachine = getenv("MachineID");
  const char* envMAC     = getenv("MAC");

  const char* MachineID = (envMachine && *envMachine) ? envMachine : "MC0003";
  const char* MAC       = (envMAC && *envMAC) ? envMAC : "00:11:22:33:44:55";

  const char* scanDir = DEFAULT_SCAN_DIR;

  curl_global_init(CURL_GLOBAL_DEFAULT);

  while (1) {
    printf("⏳ Running analyser scan...\n");
    process_directory(scanDir, MachineID, MAC);
    printf("✅ Finished batch\n");

#ifdef _WIN32
    Sleep(10000); // 10 seconds
#else
    sleep(10);
#endif
  }

  curl_global_cleanup();
  return 0;
}
