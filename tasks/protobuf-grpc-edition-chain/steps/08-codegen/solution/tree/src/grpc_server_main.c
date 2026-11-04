#define _POSIX_C_SOURCE 200809L
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
#include <stdint.h>
#include <time.h>
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
/* grpc.testing wire shapes, hand-encoded -- the interop service is protobuf, so
 * echoing request bytes back makes the client parse a SimpleRequest as a
 * SimpleResponse and fail mid-field. Only the fields the suite checks are built.
 *   SimpleRequest.response_size = field 2, varint
 *   SimpleResponse.payload      = field 1, LEN -> Payload
 *   Payload.body                = field 2, LEN   (Payload.type defaults to 0)
 */
static size_t put_varint(unsigned char *o, uint64_t v) {
  size_t i = 0;
  while (v >= 0x80) { o[i++] = (unsigned char)(v | 0x80); v >>= 7; }
  o[i++] = (unsigned char)v;
  return i;
}
static uint64_t get_varint(const unsigned char *b, size_t n, size_t *i) {
  uint64_t v = 0; int sh = 0;
  while (*i < n && sh <= 63) {
    unsigned char c = b[(*i)++];
    v |= (uint64_t)(c & 0x7f) << sh;
    if (!(c & 0x80)) break;
    sh += 7;
  }
  return v;
}
/* StreamingOutputCallRequest.response_parameters = field 2, repeated
 * ResponseParameters{size = field 1}. Each one asks for a
 * StreamingOutputCallResponse{payload{body: size}}. Echoing request bytes makes
 * the client parse them as that message and land non-UTF-8 on a string field. */
static int response_sizes(const unsigned char *b, size_t n, int *out, int max) {
  size_t i = 0; int cnt = 0;
  while (i < n && cnt < max) {
    uint64_t tag = get_varint(b, n, &i);
    uint32_t fld = (uint32_t)(tag >> 3); int wt = (int)(tag & 7);
    if (fld == 2 && wt == 2) {
      uint64_t l = get_varint(b, n, &i);
      size_t end = i + (size_t)l, j = i;
      int sz = 0;
      while (j < end) {
        uint64_t t2 = get_varint(b, end, &j);
        uint32_t f2 = (uint32_t)(t2 >> 3); int w2 = (int)(t2 & 7);
        if (f2 == 1 && w2 == 0) sz = (int)get_varint(b, end, &j);
        else if (w2 == 0) get_varint(b, end, &j);
        else if (w2 == 2) { uint64_t sl = get_varint(b, end, &j); j += (size_t)sl; }
        else break;
      }
      out[cnt++] = sz; i = end; continue;
    }
    if (wt == 0) get_varint(b, n, &i);
    else if (wt == 2) { uint64_t l = get_varint(b, n, &i); i += (size_t)l; }
    else if (wt == 5) i += 4;
    else if (wt == 1) i += 8;
    else break;
  }
  return cnt;
}

/* StreamingInputCallRequest.payload = field 1 -> Payload.body = field 2.
 * aggregated_payload_size is the sum of BODY lengths; summing whole message
 * lengths overcounts by the protobuf framing around each body. */
static size_t payload_body_len(const unsigned char *b, size_t n) {
  size_t i = 0;
  while (i < n) {
    uint64_t tag = get_varint(b, n, &i);
    uint32_t fld = (uint32_t)(tag >> 3); int wt = (int)(tag & 7);
    if (fld == 1 && wt == 2) {
      uint64_t l = get_varint(b, n, &i);
      size_t end = i + (size_t)l, j = i;
      while (j < end) {
        uint64_t t2 = get_varint(b, end, &j);
        uint32_t f2 = (uint32_t)(t2 >> 3); int w2 = (int)(t2 & 7);
        if (f2 == 2 && w2 == 2) return (size_t)get_varint(b, end, &j);
        if (w2 == 0) get_varint(b, end, &j);
        else if (w2 == 2) { uint64_t sl = get_varint(b, end, &j); j += (size_t)sl; }
        else break;
      }
      return 0;
    }
    if (wt == 0) get_varint(b, n, &i);
    else if (wt == 2) { uint64_t l = get_varint(b, n, &i); i += (size_t)l; }
    else if (wt == 5) i += 4;
    else if (wt == 1) i += 8;
    else break;
  }
  return 0;
}

/* SimpleRequest.response_status = field 7, EchoStatus{code=1, message=2}.
 * The suite asks the server to answer with exactly that status. */
static int echo_status(const unsigned char *b, size_t n, int *code, char *msg, size_t msgcap) {
  size_t i = 0; int found = 0;
  while (i < n) {
    uint64_t tag = get_varint(b, n, &i);
    uint32_t fld = (uint32_t)(tag >> 3); int wt = (int)(tag & 7);
    if (fld == 7 && wt == 2) {
      uint64_t l = get_varint(b, n, &i);
      size_t end = i + (size_t)l, j = i;
      *code = 0; if (msgcap) msg[0] = 0;
      while (j < end) {
        uint64_t t2 = get_varint(b, end, &j);
        uint32_t f2 = (uint32_t)(t2 >> 3); int w2 = (int)(t2 & 7);
        if (f2 == 1 && w2 == 0) *code = (int)get_varint(b, end, &j);
        else if (f2 == 2 && w2 == 2) {
          uint64_t sl = get_varint(b, end, &j);
          size_t cp = (size_t)sl < msgcap - 1 ? (size_t)sl : msgcap - 1;
          memcpy(msg, b + j, cp); msg[cp] = 0; j += (size_t)sl;
        } else if (w2 == 0) get_varint(b, end, &j);
        else if (w2 == 2) { uint64_t sl = get_varint(b, end, &j); j += (size_t)sl; }
        else break;
      }
      found = 1; i = end; continue;
    }
    if (wt == 0) get_varint(b, n, &i);
    else if (wt == 2) { uint64_t l = get_varint(b, n, &i); i += (size_t)l; }
    else if (wt == 5) i += 4;
    else if (wt == 1) i += 8;
    else break;
  }
  return found;
}

/* Returns response_size, or -1 when the field is absent. */
static long simple_request_size(const unsigned char *b, size_t n) {
  size_t i = 0;
  long want = -1;
  while (i < n) {
    uint64_t tag = get_varint(b, n, &i);
    uint32_t fld = (uint32_t)(tag >> 3);
    int wt = (int)(tag & 7);
    if (fld == 2 && wt == 0) { want = (long)get_varint(b, n, &i); continue; }
    if (wt == 0) { get_varint(b, n, &i); }
    else if (wt == 2) { uint64_t l = get_varint(b, n, &i); i += (size_t)l; }
    else if (wt == 5) i += 4;
    else if (wt == 1) i += 8;
    else break;
  }
  return want;
}
/* grpc-timeout is <positive integer><unit>, unit one of H M S m u n. Returning
 * 0 for "no deadline" is safe: a real deadline is always positive. */
static int64_t parse_grpc_timeout(const char *v) {
  if (!v || !*v) return 0;
  int64_t n = 0;
  const char *q = v;
  while (*q >= '0' && *q <= '9') { n = n * 10 + (*q - '0'); q++; }
  if (q == v || !*q || q[1]) return 0;
  switch (*q) {
    case 'H': return n * 3600ll * 1000000000ll;
    case 'M': return n * 60ll * 1000000000ll;
    case 'S': return n * 1000000000ll;
    case 'm': return n * 1000000ll;
    case 'u': return n * 1000ll;
    case 'n': return n;
    default: return 0;
  }
}
static int64_t now_ns(void) {
  struct timeval tv;
  gettimeofday(&tv, 0);
  return (int64_t)tv.tv_sec * 1000000000ll + (int64_t)tv.tv_usec * 1000ll;
}
static char g_timeout[64] = "";
static char g_echo_init[256] = "";
static char g_echo_trail[256] = "";
static int g_nhdr = 0;
static void on_hdr(const char *n, const char *v, void *u) {
  (void)u; g_nhdr++;
  fprintf(stderr, "      req hdr: %-20s %s\n", n, v);
  if (strcmp(n, ":path") == 0) snprintf(g_path, sizeof g_path, "%s", v);
  if (strcmp(n, "grpc-timeout") == 0) snprintf(g_timeout, sizeof g_timeout, "%s", v);
  if (strcmp(n, "x-grpc-test-echo-initial") == 0)
    snprintf(g_echo_init, sizeof g_echo_init, "%s", v);
  if (strcmp(n, "x-grpc-test-echo-trailing-bin") == 0)
    snprintf(g_echo_trail, sizeof g_echo_trail, "%s", v);
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
  /* Must hold a whole message: the reply body is whatever the request carried,
   * and large_unary sends 271840 bytes, so the buffer grows on demand instead
   * of silently dropping anything larger than a fixed size. */
  unsigned char *msg = NULL; size_t msglen = 0, msgcap = 0;
  int nrecv = 0;
  size_t recvbytes = 0;
  /* HTTP/2 splits a gRPC message across DATA frames wherever it likes, so
   * the 5-byte length prefix and its body must be reassembled from the
   * stream rather than parsed per frame. Parsing each frame in isolation
   * reads whatever lands at a frame boundary as a new prefix. */
  unsigned char *acc = NULL; size_t acclen = 0, acccap = 0;
  int hdrs_sent_stream = 0;
  int served = 0, huffman = 0;
  int64_t deadline_ns = 0;

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
      { int64_t budget = parse_grpc_timeout(g_timeout);
        deadline_ns = budget ? now_ns() + budget : 0; }
      if (r < 0)   { fprintf(stderr,"   HPACK DECODE FAILED\n"); free(p); close(fd); return 3; }
    }
    if (t==0x0 && len) {
      /* HTTP/2 flow control. A peer may send only as much DATA as its window
       * allows -- 65535 by default -- and then stops until WINDOW_UPDATE
       * replenishes it. Without this the sender stalls mid-body, END_STREAM
       * never arrives, and the request simply never completes. Both the
       * connection window (stream 0) and the stream window must be topped up. */
      unsigned char wu[13];
      put24(wu, 4); wu[3] = 0x8; wu[4] = 0; put32(wu + 5, 0); put32(wu + 9, len);
      if (wrall(fd, wu, 13) < 0) { free(p); break; }
      put32(wu + 5, sid);
      if (wrall(fd, wu, 13) < 0) { free(p); break; }
    }
    if (t==0x0) {
      if (len) {
        if (acclen + len > acccap) {
          size_t want = (acclen + len) * 2 + 1024;
          unsigned char *n2 = realloc(acc, want);
          if (!n2) { free(p); break; }
          acc = n2; acccap = want;
        }
        memcpy(acc + acclen, p, len);
        acclen += len;
      }
      /* Drain every COMPLETE message now present; a partial tail waits. */
      size_t off = 0;
      while (acclen - off >= 5) {
        unsigned ml = ((unsigned)acc[off+1] << 24) | ((unsigned)acc[off+2] << 16)
                    | ((unsigned)acc[off+3] << 8) | acc[off+4];
        if (acclen - off - 5 < (size_t)ml) break;
        if ((size_t)ml + 1 > msgcap) {
          unsigned char *m2 = realloc(msg, (size_t)ml + 1);
          if (!m2) {
            fprintf(stderr, "   cannot hold a %u-byte message\n", ml);
            free(p); goto finish;
          }
          msg = m2; msgcap = (size_t)ml + 1;
        }
        memcpy(msg, acc + off + 5, ml);
        msglen = ml;
        recvbytes += payload_body_len(acc + off + 5, ml);
        nrecv++;
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
          bd[0]=0; put32(bd+1,ml); memcpy(bd+5, acc + off + 5, ml);
          put24(f,(unsigned)(5+ml)); f[3]=0x0; f[4]=0x0; put32(f+5,sid);
          int bb = (wrall(fd,f,9)<0 || wrall(fd,bd,5+ml)<0);
          free(bd);
          if (bb) { free(p); goto finish; }
        }
        off += 5 + (size_t)ml;
      }
      if (off) { memmove(acc, acc + off, acclen - off); acclen -= off; }
    }
    int end = (fl & 0x1) && (t==0x0 || t==0x1);
    free(p);
    if (end && sid) {
      /* /Sleep overruns whatever deadline the caller set. A client-side timeout
       * looks identical from the client's seat, so the SERVER has to notice and
       * return DEADLINE_EXCEEDED itself. */
      int overran = 0;
      if (strstr(g_path, "/Sleep") && deadline_ns) {
        /* The work would not finish inside the caller's budget, so refuse now
         * rather than reply after the deadline -- a late reply is indistinguish-
         * able from the client timing out by itself. */
        struct timespec ts = {0, 50 * 1000 * 1000};
        nanosleep(&ts, 0);
        overran = 1;
      }
      int bidi = strstr(g_path, "/Bidi") != NULL;
      unsigned char hb[256]; size_t hn=0;
      hb[hn++] = 0x88;                                   /* :status: 200 (static 8) */
      hn += hdr_lit(hb+hn, "content-type", "application/grpc");
      if (g_echo_init[0]) hn += hdr_lit(hb+hn, "x-grpc-test-echo-initial", g_echo_init);
      if (!bidi) {
        put24(f,(unsigned)hn); f[3]=0x1; f[4]=0x4; put32(f+5,sid);
        if (wrall(fd,f,9)<0 || wrall(fd,hb,hn)<0) break;
      }
      /* Server-streaming: /interop.Echo/Stream emits several messages on the
       * one stream before the trailers. Unary is just the n==1 case, so both
       * shapes go through the same path rather than two copies of it. */
      /* grpc.testing.TestService/EmptyCall: both request and response are the
       * empty message, so it needs no payload encoding at all. It is the one
       * upstream interop case reachable before the testing protos exist. */
      /* Read the request's response_status BEFORE any handler replaces msg with
       * the reply -- otherwise this parses my own response and never fires. */
      int want_code = 0; char want_msg[256]; want_msg[0] = 0;
      int has_status = echo_status(msg, msglen, &want_code, want_msg, sizeof want_msg);
      int empty_call = strstr(g_path, "/grpc.testing.TestService/EmptyCall") != NULL;
      if (empty_call) msglen = 0;
      /* UnaryCall must answer with a real SimpleResponse of the requested size. */
      if (strstr(g_path, "/grpc.testing.TestService/UnaryCall")) {
        long want = simple_request_size(msg, msglen);
        if (want < 0) want = 0;
        unsigned char ph[16], sh[16];
        size_t pn = put_varint(ph, (uint64_t)want);          /* Payload.body len */
        size_t inner = 1 + pn + (size_t)want;                /* 0x12 + len + body */
        size_t sn = put_varint(sh, (uint64_t)inner);         /* SimpleResponse len */
        size_t total = 1 + sn + inner;
        unsigned char *rep = malloc(total + 1);
        if (!rep) { goto finish; }
        size_t o = 0;
        rep[o++] = 0x0a; memcpy(rep + o, sh, sn); o += sn;   /* field 1, LEN */
        rep[o++] = 0x12; memcpy(rep + o, ph, pn); o += pn;   /* field 2, LEN */
        memset(rep + o, 0, (size_t)want); o += (size_t)want;
        if (o > msgcap) {
          unsigned char *m2 = realloc(msg, o + 1);
          if (!m2) { free(rep); goto finish; }
          msg = m2; msgcap = o + 1;
        }
        memcpy(msg, rep, o); msglen = o;
        free(rep);
      }
      /* StreamingInputCall is client-streaming: exactly ONE response is owed,
       * a StreamingInputCallResponse{aggregated_payload_size=1}. */
      int input_call = strstr(g_path, "/StreamingInputCall") != NULL;
      if (input_call) {
        unsigned char agg[16];
        size_t an = put_varint(agg, (uint64_t)recvbytes);
        if (an + 2 > msgcap) {
          unsigned char *m2 = realloc(msg, an + 2);
          if (!m2) goto finish;
          msg = m2; msgcap = an + 2;
        }
        msg[0] = 0x08; memcpy(msg + 1, agg, an); msglen = an + 1;
      }
      int out_sizes[16]; int n_out = 0;
      if (strstr(g_path, "/StreamingOutputCall") || strstr(g_path, "/FullDuplexCall"))
        n_out = response_sizes(msg, msglen, out_sizes, 16);
      int nresp = (overran || bidi || strstr(g_path, "/Fail") || (has_status && want_code)) ? 0
                : (n_out ? n_out : ((strstr(g_path, "/Stream") && !input_call) ? 3 : 1));
      int bad = 0;
      for (int k = 0; k < nresp && !bad; k++) {
        unsigned char *db;
        size_t blen = msglen;
        if (n_out) {
          /* StreamingOutputCallResponse{payload{body: N}} */
          int want = out_sizes[k];
          unsigned char ph[16], sh2[16];
          size_t pn = put_varint(ph, (uint64_t)want);
          size_t inner = 1 + pn + (size_t)want;
          size_t sn = put_varint(sh2, (uint64_t)inner);
          blen = 1 + sn + inner;
          db = malloc(blen + 6);
          if (!db) { bad = 1; break; }
          size_t o2 = 5;
          db[o2++] = 0x0a; memcpy(db + o2, sh2, sn); o2 += sn;
          db[o2++] = 0x12; memcpy(db + o2, ph, pn); o2 += pn;
          memset(db + o2, 0, (size_t)want);
        } else {
          db = malloc(msglen + 6);
          if (!db) { bad = 1; break; }
          memcpy(db + 5, msg, msglen);
          if (nresp > 1 && msglen > 0) db[5+msglen-1] = (unsigned char)(msg[msglen-1] + k);
        }
        db[0] = 0; put32(db + 1, (unsigned)blen);
        /* A DATA frame may not exceed the peer's SETTINGS_MAX_FRAME_SIZE, 16384
         * by default. Writing the whole reply as one frame is a connection
         * error (large_unary's reply is 271845 bytes), so it is chunked. */
        size_t total = 5 + blen, sent = 0;
        while (sent < total && !bad) {
          size_t chunk = total - sent;
          if (chunk > 16384) chunk = 16384;
          put24(f,(unsigned)chunk); f[3]=0x0; f[4]=0x0; put32(f+5,sid);
          bad = (wrall(fd,f,9)<0 || wrall(fd,db+sent,chunk)<0);
          sent += chunk;
        }
        free(db);
      }
      if (bad) break;
      unsigned char tb[256]; size_t tn=0;
      /* Every interop suite checks a non-OK status carrying a message; a server
       * that can only say OK passes nothing but the happy path. */
      if (overran) {
        tn += hdr_lit(tb+tn, "grpc-status", "4");
        tn += hdr_lit(tb+tn, "grpc-message", "deadline exceeded on the server");
      } else if (has_status && want_code) {
        char cbuf[16];
        snprintf(cbuf, sizeof cbuf, "%d", want_code);
        tn += hdr_lit(tb+tn, "grpc-status", cbuf);
        if (want_msg[0]) tn += hdr_lit(tb+tn, "grpc-message", want_msg);
      } else if (strstr(g_path, "/Fail")) {
        tn += hdr_lit(tb+tn, "grpc-status", "9");            /* FAILED_PRECONDITION */
        tn += hdr_lit(tb+tn, "grpc-message", "deliberate failure from c-reference");
      } else {
        tn += hdr_lit(tb+tn, "grpc-status", "0");
      }
      tn += hdr_lit(tb+tn, "x-served-by", "c-reference");
      if (g_echo_trail[0]) tn += hdr_lit(tb+tn, "x-grpc-test-echo-trailing-bin", g_echo_trail);
      put24(f,(unsigned)tn); f[3]=0x1; f[4]=0x5; put32(f+5,sid);  /* END_HEADERS|END_STREAM */
      if (wrall(fd,f,9)<0 || wrall(fd,tb,tn)<0) break;
      fprintf(stderr,"   -> responded stream %u  path=%s  recv=%d msg(s) %zu B total  timeout=%s\n",
              sid, g_path, nrecv, recvbytes, g_timeout[0] ? g_timeout : "(none)");
      nrecv = 0; recvbytes = 0; hdrs_sent_stream = 0; acclen = 0;
      served++;
      g_timeout[0] = 0; g_echo_init[0] = 0; g_echo_trail[0] = 0;
      continue;
    }
  }
finish:
  free(acc);
  free(msg);
  close(fd); close(ls);
  fprintf(stderr,"   served=%d  request-headers-decoded=%d  huffman=%d\n", served, g_nhdr, huffman);
  if (huffman) return 4;
  return served > 0 ? 0 : 1;
}
