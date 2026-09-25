/* wels_rt_win.c — Windows 版 WELS-C runtime */

typedef void *HANDLE;
typedef unsigned long DWORD;
typedef int BOOL;

__declspec(dllimport) HANDLE __stdcall GetStdHandle(DWORD nStdHandle);
__declspec(dllimport) BOOL   __stdcall WriteFile(HANDLE, const void *, DWORD, DWORD *, void *);
__declspec(dllimport) void   __stdcall ExitProcess(unsigned int);

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
    int ret = main();
    ExitProcess((unsigned)ret);
}
