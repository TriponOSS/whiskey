/* whiskey/glue.c - the one pointer island.
 *
 * Tripon v0 has no address-of outside extern fences, so the syscalls that
 * need real struct memory (sockaddr_in, read/write buffers) live here.
 * Everything else - HTTP parsing, routing, response building - is pure
 * Tripon above this file.
 *
 * ABI contract (must match whiskey/io.tp `struct Buf`):
 *   struct Buf { int32_t n; char data[8192]; }  (passed by value)
 */

#define _GNU_SOURCE
#define _PATHMAX_ 1
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <limits.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>

#define WBUF 8192

typedef struct {
    int32_t n;
    char data[WBUF];      /* exactly io.tp's Buf layout */
} BGhBuf;

/* listening socket on 0.0.0.0:port, SOREUSEADDR, backlog 64. -1 on fail */
int32_t wsk_listen(int32_t port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((uint16_t)port);
    if (bind(fd, (struct sockaddr *)&addr, sizeof addr) < 0) return -1;
    if (listen(fd, 64) < 0) return -1;
    return fd;
}

int32_t wsk_accept(int32_t lfd) {
    return (int32_t)accept((int)lfd, NULL, NULL);
}

/* fork-per-request helper: auto-reap children so the accept loop never
 * collects zombies by hand. call once at startup. */
int32_t wsk_nozombies(void) {
    signal(SIGCHLD, SIG_IGN);
    return 0;
}

/* read one full HTTP request: loops until "\r\n\r\n" seen AND any declared
 * Content-Length satisfied, or out of room. returns the Buf; n==0 = EOF. */
BGhBuf wsk_recv(int32_t fd) {
    BGhBuf b;
    memset(&b, 0, sizeof b);
    ssize_t r;
    while (b.n < WBUF - 1) {
        int header_done = 0;
        long body_start = b.n;
        for (int i = 0; i + 4 <= b.n; i++) {
            if (b.data[i] == '\r' && b.data[i+1] == '\n' &&
                b.data[i+2] == '\r' && b.data[i+3] == '\n') {
                header_done = 1;
                body_start = i + 4;
                break;
            }
        }
        if (header_done) {
            b.data[b.n] = 0;
            const char *cl = strcasestr(b.data, "content-length:");
            long want = 0;
            if (cl) {
                const char *p = cl + 15;
                while (*p == ' ') p++;
                want = strtol(p, NULL, 10);
                if (want < 0) want = 0;
                if (want > WBUF - b.n - 1) want = WBUF - b.n - 1;
            }
            if ((long)b.n >= body_start + want) return b;
        }
        char tmp[512];
        r = read((int)fd, tmp, sizeof tmp);
        if (r <= 0) break;   /* EOF or error */
        if (b.n + r > WBUF - 1) r = WBUF - 1 - b.n;
        memcpy(b.data + b.n, tmp, (size_t)r);
        b.n += (int32_t)r;
        b.data[b.n] = 0;
    }
    return b;
}

/* send the first n bytes of buf to fd. negative on failure. */
int32_t wsk_send(int32_t fd, BGhBuf b) {
    size_t off = 0;
    long n = b.n;
    if (n < 0 || n > WBUF) return -1;
    while (off < (size_t)n) {
        ssize_t w = write((int)fd, b.data + off, (size_t)n - off);
        if (w <= 0) return -1;
        off += (size_t)w;
    }
    return (int32_t)n;
}

/* static file serve: path comes in a Buf (by value). Rejects traversal,
 * absolute paths, and serving directories. Streams file bytes out.
 * returns 0 ok, 1 not found, 2 forbidden. */
int32_t wsk_serve_file(int32_t fd, BGhBuf pathbuf) {
    char path[WBUF];
    long pn = pathbuf.n;
    if (pn <= 0 || pn >= WBUF) return 2;
    memcpy(path, pathbuf.data, (size_t)pn);
    path[pn] = 0;
    if (path[0] == '/') return 2;               /* no absolute paths */
    if (strstr(path, "../")) return 2;          /* no traversal */
    if (strstr(path, "..\\")) return 2;
    FILE *f = fopen(path, "rb");
    if (!f) return 1;
    {
        struct stat st = {0};
        if (fstat(fileno(f), &st) == 0 && S_ISDIR(st.st_mode)) { fclose(f); return 2; }
    }
    /* small mime table by suffix */
    const char *mime = "application/octet-stream";
    size_t plen = strlen(path);
    if (plen >= 5 && strcmp(path + plen - 5, ".html") == 0) mime = "text/html; charset=utf-8";
    else if (plen >= 4 && strcmp(path + plen - 4, ".txt") == 0) mime = "text/plain; charset=utf-8";
    else if (plen >= 4 && strcmp(path + plen - 4, ".css") == 0) mime = "text/css";
    else if (plen >= 3 && strcmp(path + plen - 3, ".js") == 0) mime = "text/javascript";
    else if (plen >= 4 && strcmp(path + plen - 4, ".png") == 0) mime = "image/png";
    else if (plen >= 4 && strcmp(path + plen - 4, ".jpg") == 0) mime = "image/jpeg";

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < 0) { fclose(f); return 1; }

    dprintf(fd, "HTTP/1.1 200 OK\r\nContent-Type: %s\r\nContent-Length: %ld\r\nConnection: close\r\n\r\n", mime, sz);
    char buf[4096];
    size_t k;
    while ((k = fread(buf, 1, sizeof buf, f)) > 0) {
        if (write(fd, buf, k) <= 0) break;
    }
    fclose(f);
    return 0;
}
