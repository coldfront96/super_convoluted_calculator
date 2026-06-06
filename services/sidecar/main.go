// The generic sidecar: wraps any CLI stage behind a tiny HTTP service.
// Usage: sidecar <port> <shell-command>
// POST /        -> request body is piped to the command's stdin; stdout returned
// GET  /health  -> "ok"
package main

import (
	"bytes"
	"io"
	"net/http"
	"os"
	"os/exec"
)

func main() {
	if len(os.Args) < 3 {
		os.Stderr.WriteString("usage: sidecar <port> <command>\n")
		os.Exit(2)
	}
	port := os.Args[1]
	command := os.Args[2]

	mux := http.NewServeMux()
	mux.HandleFunc("/health", func(w http.ResponseWriter, r *http.Request) {
		w.Write([]byte("ok"))
	})
	mux.HandleFunc("/", func(w http.ResponseWriter, r *http.Request) {
		body, _ := io.ReadAll(r.Body)
		cmd := exec.Command("sh", "-c", command)
		cmd.Stdin = bytes.NewReader(body)
		var out bytes.Buffer
		cmd.Stdout = &out
		err := cmd.Run()
		if err != nil && out.Len() == 0 {
			http.Error(w, "stage failed", http.StatusInternalServerError)
			return
		}
		w.Write(out.Bytes())
	})

	if err := http.ListenAndServe("127.0.0.1:"+port, mux); err != nil {
		os.Stderr.WriteString("sidecar: " + err.Error() + "\n")
		os.Exit(1)
	}
}
