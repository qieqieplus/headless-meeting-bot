package server

import (
	"context"
	"net/http"
	"strings"
)

type contextKey string

const paramsContextKey contextKey = "path_params"

// Params holds path parameters extracted by ParamRouter
type Params map[string]string

// GetPathParam retrieves a path parameter from the request context
func GetPathParam(r *http.Request, name string) string {
	params, _ := r.Context().Value(paramsContextKey).(Params)
	if params == nil {
		return ""
	}
	return params[name]
}

// ParamRouter is a tiny router supporting patterns with {param} segments
type ParamRouter struct {
	routes []route
}

type route struct {
	pattern string
	parts   []string
	handler http.HandlerFunc
}

// NewParamRouter creates a new ParamRouter instance
func NewParamRouter() *ParamRouter {
	return &ParamRouter{routes: make([]route, 0)}
}

// Handle registers a handler for a pattern like "/ws/audio/{meeting_id}"
func (rtr *ParamRouter) Handle(pattern string, handler http.HandlerFunc) {
	pattern = strings.TrimSuffix(pattern, "/")
	parts := splitPath(pattern)
	rtr.routes = append(rtr.routes, route{pattern: pattern, parts: parts, handler: handler})
}

// ServeHTTP matches the incoming request path and dispatches to the first matching handler
func (rtr *ParamRouter) ServeHTTP(w http.ResponseWriter, r *http.Request) {
	path := strings.TrimSuffix(r.URL.Path, "/")
	inParts := splitPath(path)

	for _, rt := range rtr.routes {
		if len(rt.parts) != len(inParts) {
			continue
		}
		params := make(Params)
		matched := true
		for i := 0; i < len(rt.parts); i++ {
			pp := rt.parts[i]
			ip := inParts[i]
			if isParam(pp) {
				name := strings.TrimSuffix(strings.TrimPrefix(pp, "{"), "}")
				params[name] = ip
				continue
			}
			if pp != ip {
				matched = false
				break
			}
		}
		if matched {
			ctx := context.WithValue(r.Context(), paramsContextKey, params)
			rt.handler(w, r.WithContext(ctx))
			return
		}
	}

	http.NotFound(w, r)
}

func splitPath(p string) []string {
	if p == "" || p == "/" {
		return []string{""}
	}
	if !strings.HasPrefix(p, "/") {
		p = "/" + p
	}
	// Avoid leading empty element by trimming then re-adding leading slash part
	parts := strings.Split(p, "/")
	// Keep leading empty string for the root slash to align with pattern split
	return parts
}

func isParam(seg string) bool {
	return strings.HasPrefix(seg, "{") && strings.HasSuffix(seg, "}") && len(seg) > 2
}
