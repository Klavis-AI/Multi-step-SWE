/* Minimal HTTP/2 + gRPC unary client, hand-rolled: no third-party library.
 * Proves framing / HPACK *encoding* / gRPC message framing are accepted by an
 * independent server. Does NOT decode response headers -- HPACK decoding
 * (Huffman + dynamic table) is the harder direction and is deliberately not
 * claimed here. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>

static void put24(unsigned char *p, unsigned v) { p[0] = (unsigned char)(v >> 16); p[1] = (unsigned char)(v >> 8); p[2] = (unsigned char)v; }
static void put32(unsigned char *p, unsigned v) { p[0] = (unsigned char)(v >> 24); p[1] = (unsigned char)(v >> 16); p[2] = (unsigned char)(v >> 8); p[3] = (unsigned char)v; }

/* HPACK literal header field without indexing, new name (RFC 7541 6.2.2):
 * 0x00, then length-prefixed name and value with the Huffman bit clear. */
static size_t hdr_lit(unsigned char *o, const char *n, const char *v) {
  size_t i = 0, ln = strlen(n), lv = strlen(v);
  o[i++] = 0x00;
  o[i++] = (unsigned char)ln; memcpy(o + i, n, ln); i += ln;
  o[i++] = (unsigned char)lv; memcpy(o + i, v, lv); i += lv;
  return i;
}
static int rdall(int fd, unsigned char *b, size_t n) {
  size_t g = 0;
  while (g < n) { ssize_t k = read(fd, b + g, n - g); if (k <= 0) return -1; g += (size_t)k; }
  return 0;
}

int main(int argc, char **argv) {
  const char *port = argc > 1 ? argv[1] : "50551";
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  struct sockaddr_in sa; memset(&sa, 0, sizeof sa);
  sa.sin_family = AF_INET; sa.sin_port = htons((unsigned short)atoi(port));
  sa.sin_addr.s_addr = inet_addr("127.0.0.1");
  if (connect(fd, (struct sockaddr *)&sa, sizeof sa) < 0) { perror("   connect"); return 2; }
  int one = 1; setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);

  unsigned char f[9];
  if (write(fd, "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n", 24) != 24) return 2;
  put24(f, 0); f[3] = 0x4; f[4] = 0; put32(f + 5, 0);
  if (write(fd, f, 9) != 9) return 2;

  unsigned char hb[512]; size_t hn = 0;
  char auth[80]; snprintf(auth, sizeof auth, "127.0.0.1:%s", port);
  hn += hdr_lit(hb + hn, ":method", "POST");
  hn += hdr_lit(hb + hn, ":scheme", "http");
  hn += hdr_lit(hb + hn, ":path", "/interop.Echo/Unary");
  hn += hdr_lit(hb + hn, ":authority", auth);
  hn += hdr_lit(hb + hn, "content-type", "application/grpc");
  hn += hdr_lit(hb + hn, "te", "trailers");
  hn += hdr_lit(hb + hn, "x-echo-trailer", "pong");
  put24(f, (unsigned)hn); f[3] = 0x1; f[4] = 0x4; put32(f + 5, 1);
  if (write(fd, f, 9) != 9 || write(fd, hb, hn) != (ssize_t)hn) return 2;

  /* gRPC length-prefixed message: compressed flag, 4-byte BE length, payload */
  unsigned char db[16]; db[0] = 0; put32(db + 1, 2); db[5] = 0x08; db[6] = 0x2a;
  put24(f, 7); f[3] = 0x0; f[4] = 0x1; put32(f + 5, 1);
  if (write(fd, f, 9) != 9 || write(fd, db, 7) != 7) return 2;

  const char *nm[] = {"DATA","HEADERS","PRIORITY","RST_STREAM","SETTINGS",
                      "PUSH_PROMISE","PING","GOAWAY","WINDOW_UPDATE","CONTINUATION"};
  int sawdata = 0, frames = 0, hdrseq = 0;
  const char *outdir = argc > 2 ? argv[2] : ".";
  for (int i = 0; i < 24; i++) {
    unsigned char h[9];
    if (rdall(fd, h, 9) < 0) break;
    unsigned len = ((unsigned)h[0] << 16) | ((unsigned)h[1] << 8) | h[2];
    unsigned t = h[3], fl = h[4];
    unsigned sid = (((unsigned)h[5] & 0x7f) << 24) | ((unsigned)h[6] << 16) | ((unsigned)h[7] << 8) | h[8];
    unsigned char *p = malloc(len ? len : 1);
    if (len && rdall(fd, p, len) < 0) { free(p); break; }
    frames++;
    printf("   <- %-13s len=%-5u flags=0x%02x stream=%u", t < 10 ? nm[t] : "?", len, fl, sid);
    if (t == 0x0 && len >= 5) {
      unsigned ml = ((unsigned)p[1] << 24) | ((unsigned)p[2] << 16) | ((unsigned)p[3] << 8) | p[4];
      printf("  grpc-msg len=%u bytes=", ml);
      for (unsigned k = 0; k < ml && 5 + k < len; k++) printf("%02x", p[5 + k]);
      if (ml == 2 && len >= 7 && p[5] == 0x08 && p[6] == 0x2a) sawdata = 1;
    }
    if (t == 0x1) {
      char fn[256];
      int need = snprintf(fn, sizeof fn, "%s/hdrblock_%d.bin", outdir, ++hdrseq);
      int ok = 0;
      if (need > 0 && (size_t)need < sizeof fn) {
        FILE *hf = fopen(fn, "wb");
        if (hf) {
          ok = (fwrite(p, 1, len, hf) == len);
          if (fclose(hf) != 0) ok = 0;
        }
      }
      /* Report what actually happened. An unconditional "[saved]" here
       * announced success twice while writing nothing. */
      printf(ok ? "  [saved hdrblock_%d.bin]" : "  [SAVE FAILED hdrblock_%d]", hdrseq);
    }
    if (t == 0x6 && !(fl & 1) && len == 8) {
      /* PING must be answered with PING ACK; ignoring it eventually costs the
       * connection. Caught only because every frame is printed. */
      unsigned char pk[17];
      put24(pk, 8); pk[3] = 0x6; pk[4] = 0x1; put32(pk + 5, 0);
      memcpy(pk + 9, p, 8);
      if (write(fd, pk, 17) != 17) { free(p); break; }
      printf("  [PING ACK sent]");
    }
    if (t == 0x7 && len >= 8) printf("  GOAWAY err=%u", ((unsigned)p[4] << 24) | ((unsigned)p[5] << 16) | ((unsigned)p[6] << 8) | p[7]);
    if (t == 0x3 && len >= 4) printf("  RST err=%u", ((unsigned)p[0] << 24) | ((unsigned)p[1] << 16) | ((unsigned)p[2] << 8) | p[3]);
    printf("\n");
    if (t == 0x4 && !(fl & 1)) { unsigned char a[9]; put24(a, 0); a[3] = 0x4; a[4] = 0x1; put32(a + 5, 0); if (write(fd, a, 9) != 9) { free(p); break; } }
    free(p);
    if (sawdata && t == 0x1 && sid == 1 && (fl & 0x1)) break;
  }
  close(fd);
  printf("   frames read: %d\n", frames);
  printf("%s", sawdata ? "   => ECHO MATCHED: framing + HPACK encode + gRPC framing accepted\n"
                       : "   => NO MATCHING DATA FRAME\n");
  return sawdata ? 0 : 1;
}
