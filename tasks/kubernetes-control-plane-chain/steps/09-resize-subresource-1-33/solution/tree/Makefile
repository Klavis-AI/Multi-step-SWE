# kube-apiserver, kube-controller-manager and kube-scheduler, built the way
# upstream builds them: `make` puts the binaries in /out/bin, outside the
# workspace, which holds sources only.
CC      ?= cc
CFLAGS  ?= -std=gnu11 -O2 -pthread -Wall -DKCP_RELEASE=33
LDLIBS   = -lssl -lcrypto -lpthread -lm
LDFLAGS += -rdynamic

# The workspace itself stays text-only: build products go outside it.
OUT     ?= /out/bin
COMMON   = src/json.c src/util.c src/http.c

APISERVER_SRC = src/apiserver.c src/schema.c src/store.c src/etcd.c src/auth.c \
                src/admission.c src/cel.c src/patch.c src/extras.c src/crd.c src/openapi.c \
                src/blobs.c src/protobuf.c $(COMMON)
KCM_SRC       = src/kcm.c src/client.c $(COMMON)
SCHED_SRC     = src/scheduler.c src/client.c $(COMMON)

all: $(OUT)/kube-apiserver $(OUT)/kube-controller-manager $(OUT)/kube-scheduler

$(OUT):
	mkdir -p $(OUT)

$(OUT)/kube-apiserver: $(APISERVER_SRC) | $(OUT)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(APISERVER_SRC) $(LDLIBS)

$(OUT)/kube-controller-manager: $(KCM_SRC) | $(OUT)
	$(CC) $(CFLAGS) -o $@ $(KCM_SRC) $(LDLIBS)

$(OUT)/kube-scheduler: $(SCHED_SRC) | $(OUT)
	$(CC) $(CFLAGS) -o $@ $(SCHED_SRC) $(LDLIBS)

clean:
	rm -rf _output

.PHONY: all clean
