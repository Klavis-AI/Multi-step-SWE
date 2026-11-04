package main

import (
	"strconv"
	"strings"
)

func itoa(v int64) string { return strconv.FormatInt(v, 10) }

func render(ids []int64) string {
	parts := make([]string, 0, len(ids))
	for _, id := range ids {
		parts = append(parts, itoa(id))
	}
	return strings.Join(parts, ",")
}
