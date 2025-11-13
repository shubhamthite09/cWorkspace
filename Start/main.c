#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
    #include <windows.h>
    #include <direct.h>
#else
    #include <unistd.h>
    #include <limits.h>
#endif

// Cross-platform get current working directory
static int get_current_dir(char *buf, size_t size) {
#ifdef _WIN32
    return _getcwd(buf, (int)size) != NULL;
#else
    return getcwd(buf, size) != NULL;
#endif
}

// Cross-platform sleep 20 seconds
static void sleep_20_seconds(void) {
#ifdef _WIN32
    Sleep(20000);
#else
    sleep(20);
#endif
}

void runCommand(const char *cmd) {
    int result = system(cmd);
    if (result != 0) {
        printf("Failed: %s\n", cmd);
    }
}

void openURL(const char *url) {
#ifdef _WIN32
    // Windows: open in default browser via ShellExecute
    HINSTANCE result = ShellExecuteA(NULL, "open", url, NULL, NULL, SW_SHOWNORMAL);
    if ((int)result <= 32) {
        printf("Failed to open URL: %s\n", url);
    } else {
        printf("Opened URL: %s\n", url);
    }
#elif __APPLE__
    // macOS: use `open` command (default browser)
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "open \"%s\"", url);
    int res = system(cmd);
    if (res != 0) {
        printf("Failed to open URL: %s\n", url);
    } else {
        printf("Opened URL: %s\n", url);
    }
#else
    printf("openURL not supported on this platform.\n");
#endif
}

void openListeners() {
    char cwd[1024];
    if (!get_current_dir(cwd, sizeof(cwd))) {
        printf("Failed to get current working directory.\n");
        return;
    }

#ifdef _WIN32
    // ---------- WINDOWS PATHS ----------
    char F200Application[1024];
    char h360Application[1024];
    char UrineCom[1024];
    char Parser[1024];

    snprintf(F200Application, sizeof(F200Application),
             "\"%s\\SC_Data_Receive\\SUPERCUTICALSPVTLTD_1_0_0_21_F200\\SUPERCUTICALSPVTLTD.exe\"",
             cwd);

    snprintf(h360Application, sizeof(h360Application),
             "\"%s\\SC_Data_Receive\\SUPERCUTICALSPVTLTD_1_0_0_23_h360\\SUPERCUTICALSPVTLTD.exe\"",
             cwd);

    snprintf(UrineCom, sizeof(UrineCom),
             "\"%s\\urincom\\com.js\"", cwd);

    snprintf(Parser, sizeof(Parser),
             "\"%s\\parser\\Analyser_node_1.js\"", cwd);

    char cmdBuf[2048];

    // Launch executables in new windows
    snprintf(cmdBuf, sizeof(cmdBuf), "start \"\" %s", F200Application);
    runCommand(cmdBuf);

    snprintf(cmdBuf, sizeof(cmdBuf), "start \"\" %s", h360Application);
    runCommand(cmdBuf);

    // Launch JS scripts in Node terminals that remain open
    snprintf(cmdBuf, sizeof(cmdBuf), "start \"\" cmd /k node %s", UrineCom);
    runCommand(cmdBuf);

    snprintf(cmdBuf, sizeof(cmdBuf), "start \"\" cmd /k node %s", Parser);
    runCommand(cmdBuf);

#elif __APPLE__
    // ---------- macOS PATHS ----------
    // NOTE: adjust paths if your mac folder structure differs
    char F200Application[1024];
    char h360Application[1024];
    char UrineComPath[1024];
    char ParserPath[1024];

    snprintf(F200Application, sizeof(F200Application),
             "%s/SC_Data_Receive/SUPERCUTICALSPVTLTD_1_0_0_21_F200/SUPERCUTICALSPVTLTD",
             cwd);

    snprintf(h360Application, sizeof(h360Application),
             "%s/SC_Data_Receive/SUPERCUTICALSPVTLTD_1_0_0_23_h360/SUPERCUTICALSPVTLTD",
             cwd);

    snprintf(UrineComPath, sizeof(UrineComPath),
             "%s/urincom/com.js", cwd);

    snprintf(ParserPath, sizeof(ParserPath),
             "%s/parser/Analyser_node_1.js", cwd);

    char cmdBuf[4096];

    // Try to open the “exe equivalents” (if you have .app or binary, adjust path)
    snprintf(cmdBuf, sizeof(cmdBuf),
             "open \"%s\"", F200Application);
    runCommand(cmdBuf);

    snprintf(cmdBuf, sizeof(cmdBuf),
             "open \"%s\"", h360Application);
    runCommand(cmdBuf);

    // Launch JS scripts in new Terminal windows that stay open
    // Uses AppleScript to tell Terminal to run the command
    snprintf(cmdBuf, sizeof(cmdBuf),
             "osascript -e 'tell application \"Terminal\" to do script \"cd %s && node %s\"'",
             cwd, UrineComPath);
    runCommand(cmdBuf);

    snprintf(cmdBuf, sizeof(cmdBuf),
             "osascript -e 'tell application \"Terminal\" to do script \"cd %s && node %s\"'",
             cwd, ParserPath);
    runCommand(cmdBuf);

#else
    printf("openListeners not implemented for this platform.\n");
#endif
}

int main(void) {
    printf("Welcome to Superceuticals!\n");

#if defined(_WIN32) || defined(__APPLE__)
    openListeners();

    printf("Waiting 20 seconds before opening the browser...\n");
    sleep_20_seconds();

    openURL("https://app.superceuticals.in/");
#else
    printf("Platform not supported (only Windows and macOS are handled here).\n");
#endif

    return 0;
}
