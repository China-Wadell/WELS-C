/* wels_rt.c — WELS-C 最小运行时
 * 直接调 Linux syscall，不依赖 libc。
 */

static long sys_write(int fd, const void *buf, unsigned long count) {
    long ret;
    __asm__ volatile ("syscall"
        : "=a"(ret)
        : "a"(1), "D"(fd), "S"(buf), "d"(count)
        : "rcx", "r11", "memory");
    return ret;
}

static void sys_exit(int code) {
    __asm__ volatile ("syscall"
        : : "a"(60), "D"(code) : "rcx", "r11", "memory");
    __builtin_unreachable();
}

static unsigned long str_len(const char *s) {
    unsigned long n = 0;
    while (s[n]) n++;
    return n;
}

void wels_print(const char *s) {
    sys_write(1, s, str_len(s));
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
    sys_write(1, buf + i, 23 - i);
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
        sys_write(1, &c, 1);
        frac -= digit;
    }
}

void wels_exit(int code) {
    sys_exit(code);
}

/* 程序入口 */
extern int main(void);
void _start(void) {
    int ret = main();
    sys_exit(ret);
}
