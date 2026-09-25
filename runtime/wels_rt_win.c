/* wels_rt_win.c — Windows 版 WELS-C runtime */

typedef void *HANDLE;
typedef unsigned long DWORD;
typedef int BOOL;

__declspec(dllimport) HANDLE __stdcall GetStdHandle(DWORD nStdHandle);
__declspec(dllimport) BOOL   __stdcall WriteFile(HANDLE, const void *, DWORD, DWORD *, void *);
__declspec(dllimport) void   __stdcall ExitProcess(unsigned int);
__declspec(dllimport) char * __stdcall GetCommandLineA(void);

static long   g_argc = 0;
static char **g_argv = 0;
static char  *g_argv_buf[64];

long wels_argc(void) { return g_argc; }

char *wels_argv(long i) {
    if (i < 0 || i >= g_argc) return 0;
    return g_argv[i];
}

/* 简易解析：按空格切，不处理引号 */
static void parse_cmdline(void) {
    char *s = GetCommandLineA();
    int in_tok = 0;
    g_argc = 0;
    for (; *s; s++) {
        if (*s == ' ' || *s == '\t') {
            if (in_tok) { *s = 0; in_tok = 0; }
        } else {
            if (!in_tok) {
                if (g_argc < 63) g_argv_buf[g_argc++] = s;
                in_tok = 1;
            }
        }
    }
    g_argv = g_argv_buf;
}

#define STD_OUTPUT_HANDLE ((DWORD)-11)

static DWORD slen(const char *s) {
    DWORD n = 0;
    while (s[n]) n++;
    return n;
}

void wels_print(const char *s) {
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD w;
    WriteFile(h, s, slen(s), &w, 0);
}

void wels_print_int(long n) {
    char buf[24];
    int i = 23;
    buf[i] = 0;
    if (n == 0) {
        buf[--i] = '0';
    } else {
        int neg = 0;
        if (n < 0) { neg = 1; n = -n; }
        while (n > 0 && i > 0) {
            buf[--i] = '0' + (n % 10);
            n /= 10;
        }
        if (neg && i > 0) buf[--i] = '-';
    }
    wels_print(buf + i);
}

void wels_print_float(double d) {
    long ip = (long)d;
    wels_print_int(ip);
    wels_print(".");
    double frac = d - (double)ip;
    if (frac < 0) frac = -frac;
    for (int k = 0; k < 6; k++) {
        frac *= 10;
        int digit = (int)frac;
        char c = '0' + digit;
        HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
        DWORD w;
        WriteFile(h, &c, 1, &w, 0);
        frac -= digit;
    }
}

void wels_print_char(int c) {
    char ch = (char)c;
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD w;
    WriteFile(h, &ch, 1, &w, 0);
}

void wels_exit(int code) {
    ExitProcess((unsigned)code);
}

extern int main(void);
void mainCRTStartup(void) {
    parse_cmdline();
    int ret = main();
    ExitProcess((unsigned)ret);
}
