/* Minimal gRPC-over-HTTP/2 server, hand-rolled. Proves the reverse direction:
 * my HPACK *encoding* must satisfy a real client, and my decoder must handle
 * whatever grpcio's CLIENT encoder emits (which need not match its server's).
 * Huffman in a request is refused loudly with a distinct exit code rather than
 * mis-decoded. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/time.h>
#include "hpack.h"

static void put24(unsigned char *p, unsigned v) { p[0]=(unsigned char)(v>>16); p[1]=(unsigned char)(v>>8); p[2]=(unsigned char)v; }
static void put32(unsigned char *p, unsigned v) { p[0]=(unsigned char)(v>>24); p[1]=(unsigned char)(v>>16); p[2]=(unsigned char)(v>>8); p[3]=(unsigned char)v; }
static int rdall(int fd, unsigned char *b, size_t n) { size_t g=0; while(g<n){ ssize_t k=read(fd,b+g,n-g); if(k<=0) return -1; g+=(size_t)k; } return 0; }
static int wrall(int fd, const void *b, size_t n) { const char *p=b; size_t g=0; while(g<n){ ssize_t k=write(fd,p+g,n-g); if(k<=0) return -1; g+=(size_t)k; } return 0; }
static size_t hdr_lit(unsigned char *o, const char *n, const char *v) {
  size_t i=0, ln=strlen(n), lv=strlen(v);
  o[i++]=0x00; o[i++]=(unsigned char)ln; memcpy(o+i,n,ln); i+=ln;
  o[i++]=(unsigned char)lv; memcpy(o+i,v,lv); i+=lv;
  return i;
}
static char g_path[256] = "";
static char g_timeout[64] = "";
static int g_nhdr = 0;
static void on_hdr(const char *n, const char *v, void *u) {
  (void)u; g_nhdr++;
  fprintf(stderr, "      req hdr: %-20s %s\n", n, v);
  if (strcmp(n, ":path") == 0) snprintf(g_path, sizeof g_path, "%s", v);
  if (strcmp(n, "grpc-timeout") == 0) snprintf(g_timeout, sizeof g_timeout, "%s", v);
}

int main(int argc, char **argv) {
  int port = argc > 1 ? atoi(argv[1]) : 50600;
  int ls = socket(AF_INET, SOCK_STREAM, 0);
  int one = 1;
  setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
  struct sockaddr_in sa; memset(&sa,0,sizeof sa);
  sa.sin_family=AF_INET; sa.sin_port=htons((unsigned short)port);
  sa.sin_addr.s_addr=inet_addr("127.0.0.1");
  if (bind(ls,(struct sockaddr*)&sa,sizeof sa)<0) { perror("bind"); return 2; }
  if (listen(ls,4)<0) { perror("listen"); return 2; }
  printf("READY %d\n", port); fflush(stdout);

  int fd = accept(ls, 0, 0);
  if (fd < 0) { perror("accept"); return 2; }
  setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
  struct timeval tv = { 20, 0 };
  setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);

  unsigned char pre[24];
  if (rdall(fd,pre,24)<0 || memcmp(pre,"PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n",24)!=0) {
    fprintf(stderr,"   BAD PREFACE\n"); return 1; }
  fprintf(stderr,"   preface OK\n");
  unsigned char f[9];
  put24(f,0); f[3]=0x4; f[4]=0; put32(f+5,0);
  if (wrall(fd,f,9)<0) return 1;

  Dyn dyn; memset(&dyn,0,sizeof dyn); dyn.cap = 4096;
  hpack_on_header(on_hdr, 0);
  unsigned char msg[65536]; size_t msglen = 0;
  int nrecv = 0;
  size_t recvbytes = 0;
  int hdrs_sent_stream = 0;
  int served = 0, huffman = 0;

  for (int i = 0; i < 512; i++) {
    unsigned char h[9];
    if (rdall(fd,h,9) < 0) break;
    unsigned len=((unsigned)h[0]<<16)|((unsigned)h[1]<<8)|h[2];
    unsigned t=h[3], fl=h[4];
    unsigned sid=(((unsigned)h[5]&0x7f)<<24)|((unsigned)h[6]<<16)|((unsigned)h[7]<<8)|h[8];
    unsigned char *p = malloc(len?len:1);
    if (len && rdall(fd,p,len)<0) { free(p); break; }
    fprintf(stderr,"   <- type=%u len=%-5u flags=0x%02x stream=%u\n", t, len, fl, sid);
    if (t==0x4 && !(fl&1)) { unsigned char a[9]; put24(a,0); a[3]=0x4; a[4]=0x1; put32(a+5,0); if(wrall(fd,a,9)<0){free(p);break;} }
    if (t==0x6 && !(fl&1) && len==8) { unsigned char pk[17]; put24(pk,8); pk[3]=0x6; pk[4]=0x1; put32(pk+5,0); memcpy(pk+9,p,8); if(wrall(fd,pk,17)<0){free(p);break;} }
    if (t==0x1) {
      int cnt=0;
      int r = hpack_decode(p,len,&dyn,&cnt);
      if (r == -2) { fprintf(stderr,"   HUFFMAN in client request headers\n"); huffman=1; free(p); break; }
      if (r < 0)   { fprintf(stderr,"   HPACK DECODE FAILED\n"); free(p); close(fd); return 3; }
    }
    if (t==0x0) {
      /* A DATA frame may carry several length-prefixed gRPC messages, and a
       * client-streaming call sends many frames before END_STREAM. Walk the
       * whole frame and keep every message, rather than the last one only. */
      size_t off = 0;
      while (off + 5 <= len) {
        unsigned ml=((unsigned)p[off+1]<<24)|((unsigned)p[off+2]<<16)|((unsigned)p[off+3]<<8)|p[off+4];
        if (off + 5 + (size_t)ml > len) break;
        if (ml <= sizeof msg) { memcpy(msg, p+off+5, ml); msglen = ml; }
        recvbytes += ml;
        nrecv++;
        /* Bidi: reply to each message as it arrives. Waiting for END_STREAM
         * deadlocks against a ping-pong client that sends one and waits for
         * one back, so the reply cannot be deferred to the end. */
        if (strstr(g_path, "/Bidi")) {
          if (hdrs_sent_stream != (int)sid) {
            unsigned char rh[128]; size_t rn=0;
            rh[rn++] = 0x88;
            rn += hdr_lit(rh+rn, "content-type", "application/grpc");
            put24(f,(unsigned)rn); f[3]=0x1; f[4]=0x4; put32(f+5,sid);
            if (wrall(fd,f,9)<0 || wrall(fd,rh,rn)<0) { free(p); goto finish; }
            hdrs_sent_stream = (int)sid;
          }
          unsigned char *bd = malloc(ml+5);
          bd[0]=0; put32(bd+1,ml); memcpy(bd+5,p+off+5,ml);
          put24(f,(unsigned)(5+ml)); f[3]=0x0; f[4]=0x0; put32(f+5,sid);
          int bb = (wrall(fd,f,9)<0 || wrall(fd,bd,5+ml)<0);
          free(bd);
          if (bb) { free(p); goto finish; }
        }
        off += 5 + (size_t)ml;
      }
    }
    int end = (fl & 0x1) && (t==0x0 || t==0x1);
    free(p);
    if (end && sid) {
      int bidi = strstr(g_path, "/Bidi") != NULL;
      unsigned char hb[256]; size_t hn=0;
      hb[hn++] = 0x88;                                   /* :status: 200 (static 8) */
      hn += hdr_lit(hb+hn, "content-type", "application/grpc");
      if (!bidi) {
        put24(f,(unsigned)hn); f[3]=0x1; f[4]=0x4; put32(f+5,sid);
        if (wrall(fd,f,9)<0 || wrall(fd,hb,hn)<0) break;
      }
      /* Server-streaming: /interop.Echo/Stream emits several messages on the
       * one stream before the trailers. Unary is just the n==1 case, so both
       * shapes go through the same path rather than two copies of it. */
      int nresp = (bidi || strstr(g_path, "/Fail")) ? 0
                : (strstr(g_path, "/Stream") ? 3 : 1);
      int bad = 0;
      for (int k = 0; k < nresp && !bad; k++) {
        unsigned char *db = malloc(msglen+6);
        db[0]=0;
        put32(db+1,(unsigned)msglen);
        memcpy(db+5,msg,msglen);
        if (nresp > 1 && msglen > 0) db[5+msglen-1] = (unsigned char)(msg[msglen-1] + k);
        put24(f,(unsigned)(5+msglen)); f[3]=0x0; f[4]=0x0; put32(f+5,sid);
        bad = (wrall(fd,f,9)<0 || wrall(fd,db,5+msglen)<0);
        free(db);
      }
      if (bad) break;
      unsigned char tb[256]; size_t tn=0;
      /* Every interop suite checks a non-OK status carrying a message; a server
       * that can only say OK passes nothing but the happy path. */
      if (strstr(g_path, "/Fail")) {
        tn += hdr_lit(tb+tn, "grpc-status", "9");            /* FAILED_PRECONDITION */
        tn += hdr_lit(tb+tn, "grpc-message", "deliberate failure from c-reference");
      } else {
        tn += hdr_lit(tb+tn, "grpc-status", "0");
      }
      tn += hdr_lit(tb+tn, "x-served-by", "c-reference");
      put24(f,(unsigned)tn); f[3]=0x1; f[4]=0x5; put32(f+5,sid);  /* END_HEADERS|END_STREAM */
      if (wrall(fd,f,9)<0 || wrall(fd,tb,tn)<0) break;
      fprintf(stderr,"   -> responded stream %u  path=%s  recv=%d msg(s) %zu B total  timeout=%s\n",
              sid, g_path, nrecv, recvbytes, g_timeout[0] ? g_timeout : "(none)");
      nrecv = 0; recvbytes = 0; hdrs_sent_stream = 0;
      served++;
      g_timeout[0] = 0;
      continue;
    }
  }
finish:
  close(fd); close(ls);
  fprintf(stderr,"   served=%d  request-headers-decoded=%d  huffman=%d\n", served, g_nhdr, huffman);
  if (huffman) return 4;
  return served > 0 ? 0 : 1;
}
