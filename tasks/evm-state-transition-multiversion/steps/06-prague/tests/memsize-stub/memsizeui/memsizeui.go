// Package memsizeui is a build-compatibility stub (see parent package).
package memsizeui

import (
	"net/http"
	"sync"
)

type Handler struct {
	mu   sync.Mutex
	inst map[string]interface{}
}

func (h *Handler) Add(name string, v interface{}) {
	h.mu.Lock()
	defer h.mu.Unlock()
	if h.inst == nil {
		h.inst = make(map[string]interface{})
	}
	h.inst[name] = v
}

func (h *Handler) ServeHTTP(w http.ResponseWriter, r *http.Request) {
	http.Error(w, "memsize disabled in this build", http.StatusNotImplemented)
}
