/*
MIT License

Copyright (c) 2026 wisp-code@proton.me

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

/*
 * relay.c — browser-driven relay that fronts a web chat UI as an
 * OpenAI-shaped HTTP endpoint.
 *
 * Platform branches (selected by the same macros CMake already
 * passes to webview2.cpp / webview.c):
 *
 *   WEBVIEW_GTK       — Linux (X11) and Termux/Android
 *   WEBVIEW_WEBVIEW2  — Windows (Chromium via the WebView2 SDK)
 *   WEBVIEW_COCOA     — macOS (Cocoa + WebKit)
 *
 * Shared pieces (state, JSON helpers, OpenAI response shapes,
 * transcript builder, HTTP handler, mongoose thread) are in the
 * "common" section below. The platform sections only own the
 * webview, the window, the event loop, and the stdin reader.
 *
 * Fixes carried over:
 *   - shim errors (input missing, chips timeout, send disabled) post
 *     back as "error:<reason>", which unblocks the HTTP wait.
 *   - the HTTP wait has no wall-clock deadline; only a shim error or
 *     an explicit END marker ends it.
 *   - the stale-request force-reset has been removed. A concurrent
 *     second request is rejected with 503.
 *   - the outgoing prompt is the full OpenAI-style transcript, not
 *     just the last user message.
 
 G_DEBUG=fatal-warnings,fatal-criticals gdb --args ./build/wisp-relay
 run

 NOTE: If you started in a VScode terminal and got a TLS error do this
 export GIO_EXTRA_MODULES=/usr/lib/x86_64-linux-gnu/gio/modules/ 
 wisp-relay
 
 */

#include "compat/posix.h"
#include "compat/threads.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <signal.h>
#include <errno.h>

/* unistd.h on Windows is provided by compat/posix.h (via <io.h>). On
 * POSIX it gives us getline(), read(), write(), close(), _exit(). */
#if !defined(_WIN32)
#include <unistd.h>
#else
  #pragma comment(linker, "/SUBSYSTEM:windows /ENTRY:mainCRTStartup")
#endif

#include "webview.h"
#include "mongoose.h"
#include "cJSON.h"
#include "relay.h"

/* ================================================================== */
/*  HTTP listener config                                              */
/* ================================================================== */
// http://127.0.0.1:35673/v1/chat/completions
#define RELAY_HTTP_PORT_BASE   35673
#define RELAY_HTTP_PORT_RANGE  16      /* try base .. base+15 */
#define RELAY_HTTP_HOST_DEFAULT "127.0.0.1"

/* Chosen port base and bind address. Overridable via argv or
 * RELAY_PORT / RELAY_HOST env vars. The listener walks
 * RELAY_HTTP_PORT_RANGE consecutive ports and binds the first free
 * one, so N relay instances can coexist. */
static int  g_relay_port_base = RELAY_HTTP_PORT_BASE;
static char g_relay_host[64]  = RELAY_HTTP_HOST_DEFAULT;

/* Set to 1 once the --- TOOLS --- preamble has been delivered in this
 * browser session. Cleared whenever the provider page navigates
 * (connect or reload), because that wipes the browser's conversation
 * history and the model forgets the tools. */
static int g_tools_preamble_sent = 0;

static void parse_args(int argc, char **argv) {
  /* Seed from env first, so flags override them. */
  const char *env_port = getenv("RELAY_PORT");
  if (env_port && env_port[0]) {
    int p = atoi(env_port);
    if (p > 0 && p < 65535) g_relay_port_base = p;
  }
  const char *env_host = getenv("RELAY_HOST");
  if (env_host && env_host[0]) {
    snprintf(g_relay_host, sizeof(g_relay_host), "%s", env_host);
  }

  for (int i = 1; i < argc; i++) {
    const char *a = argv[i];

    if (strcmp(a, "-h") == 0 || strcmp(a, "--help") == 0) {
      printf(
        "usage: %s [PORT] [--host ADDR] [--port PORT] [--listen-all]\n"
        "\n"
        "  PORT              base port (same as --port)\n"
        "  --port PORT       base port for the HTTP listener\n"
        "  --host ADDR       bind address (default " RELAY_HTTP_HOST_DEFAULT ")\n"
        "  --listen-all      shortcut for --host 0.0.0.0\n"
        "  -h, --help        show this help\n"
        "\n"
        "env: RELAY_PORT, RELAY_HOST\n"
        "\n"
        "The listener walks %d consecutive ports from the base and binds\n"
        "the first free one. The bound address is printed to stdout and\n"
        "shown in the window title.\n",
        argv[0], RELAY_HTTP_PORT_RANGE);
      _exit(0);
    }

    if (strcmp(a, "--port") == 0 && i + 1 < argc) {
      int p = atoi(argv[++i]);
      if (p > 0 && p < 65535) g_relay_port_base = p;
      continue;
    }
    if (strcmp(a, "--host") == 0 && i + 1 < argc) {
      snprintf(g_relay_host, sizeof(g_relay_host), "%s", argv[++i]);
      continue;
    }
    if (strcmp(a, "--listen-all") == 0) {
      snprintf(g_relay_host, sizeof(g_relay_host), "0.0.0.0");
      continue;
    }
    if (a[0] != '-' && a[0] != '\0') {
      /* positional: base port */
      int p = atoi(a);
      if (p > 0 && p < 65535) g_relay_port_base = p;
      continue;
    }
  }

  if (strcmp(g_relay_host, "0.0.0.0") == 0) {
    printf("[relay] WARNING: --listen-all exposes the relay on every "
           "interface with no authentication\n");
    fflush(stdout);
  }
}

/* Implemented by the per-platform section at the bottom of this file.
 * Declared here so http_thread() can call it before the platform
 * section is reached. */
void relay_platform_deliver_prompt(const char *text, cJSON *attachments);
void relay_platform_load_url(const char *url);
void relay_platform_set_title(const char *title);

/* ================================================================== */
/*  Signal traps                                                      */
/* ================================================================== */

static void on_exit_signal(int sig) {
  (void)sig;
  const char *msg = "[relay] signal received, exiting\n";
  ssize_t n = write(2, msg, ( unsigned int )strlen(msg));
  (void)n;
  _exit(0);
}

static void install_signal_traps(void) {
  signal(SIGTERM, on_exit_signal);
  signal(SIGINT,  on_exit_signal);
#ifdef SIGHUP
  signal(SIGHUP,  on_exit_signal);
#endif
#ifdef SIGQUIT
  signal(SIGQUIT, on_exit_signal);
#endif
}

/* ================================================================== */
/*  JSON string encoder                                               */
/* ================================================================== */

static char *json_quote(const char *s) {
  if (!s) s = "";
  size_t n = strlen(s);

  char *out = (char *)malloc(n * 6 + 3);
  if (!out) return NULL;

  char *p = out;
  *p++ = '"';
  for (size_t i = 0; i < n; i++) {
    unsigned char c = (unsigned char)s[i];
    switch (c) {
      case '"':  *p++ = '\\'; *p++ = '"';  break;
      case '\\': *p++ = '\\'; *p++ = '\\'; break;
      case '\b': *p++ = '\\'; *p++ = 'b';  break;
      case '\f': *p++ = '\\'; *p++ = 'f';  break;
      case '\n': *p++ = '\\'; *p++ = 'n';  break;
      case '\r': *p++ = '\\'; *p++ = 'r';  break;
      case '\t': *p++ = '\\'; *p++ = 't';  break;
      default:
        if (c < 0x20) {
          static const char hex[] = "0123456789abcdef";
          *p++ = '\\'; *p++ = 'u'; *p++ = '0'; *p++ = '0';
          *p++ = hex[(c >> 4) & 0xf];
          *p++ = hex[c & 0xf];
        } else {
          *p++ = (char)c;
        }
        break;
    }
  }
  *p++ = '"';
  *p   = '\0';
  return out;
}

/* ================================================================== */
/*  State shared between UI thread and HTTP thread                    */
/* ================================================================== */

static pthread_mutex_t g_req_mutex      = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  g_reply_cond     = PTHREAD_COND_INITIALIZER;
static int             g_request_busy   = 0;
static struct timespec g_request_started_at = { 0, 0 };
static char           *g_reply          = NULL;

static pthread_mutex_t g_gate_mutex   = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  g_gate_cond    = PTHREAD_COND_INITIALIZER;
static int             g_gate_stage   = 0;

static void gate_set(int stage) {
  pthread_mutex_lock(&g_gate_mutex);
  if (stage > g_gate_stage) g_gate_stage = stage;
  pthread_cond_broadcast(&g_gate_cond);
  pthread_mutex_unlock(&g_gate_mutex);
}

static int gate_wait_at_least(int stage) {
  pthread_mutex_lock(&g_gate_mutex);
  while (g_gate_stage < stage) {
    pthread_cond_wait(&g_gate_cond, &g_gate_mutex);
  }
  int s = g_gate_stage;
  pthread_mutex_unlock(&g_gate_mutex);
  return s;
}

/* Convenience: release the in-flight request with a given reply.
 * NULL or "" both mean "shim failed"; the HTTP handler turns that
 * into a 502. */
static void deliver_reply(const char *reply) {
  pthread_mutex_lock(&g_req_mutex);
  free(g_reply);
  g_reply = strdup(reply ? reply : "");
  g_request_busy = 0;
  g_request_started_at.tv_sec = 0;
  g_request_started_at.tv_nsec = 0;
  pthread_cond_signal(&g_reply_cond);
  pthread_mutex_unlock(&g_req_mutex);
}

/* ================================================================== */
/*  OpenAI-shaped JSON responses                                      */
/* ================================================================== */

static void send_openai_error(struct mg_connection *c, int code,
                              const char *type, const char *msg) {
  cJSON *root = cJSON_CreateObject();
  cJSON *err  = cJSON_CreateObject();
  cJSON_AddStringToObject(err, "message", msg ? msg : "error");
  cJSON_AddStringToObject(err, "type", type ? type : "invalid_request_error");
  cJSON_AddItemToObject(root, "error", err);
  char *s = cJSON_PrintUnformatted(root);
  mg_http_reply(c, code,
      "Content-Type: application/json\r\n"
      "Access-Control-Allow-Origin: *\r\n",
      "%s\n", s ? s : "{}");
  free(s);
  cJSON_Delete(root);
}

static void send_openai_completion_ex(struct mg_connection *c,
                                      const char *model,
                                      const char *content,
                                      cJSON *tool_calls /* owned, may be NULL */) {
  cJSON *root = cJSON_CreateObject();
  cJSON_AddStringToObject(root, "id", "chatcmpl-relay");
  cJSON_AddStringToObject(root, "object", "chat.completion");
  cJSON_AddNumberToObject(root, "created", (double) time(NULL));
  cJSON_AddStringToObject(root, "model", model ? model : "deepseek-web");

  cJSON *choices = cJSON_CreateArray();
  cJSON *choice  = cJSON_CreateObject();
  cJSON_AddNumberToObject(choice, "index", 0);

  cJSON *msg = cJSON_CreateObject();
  cJSON_AddStringToObject(msg, "role", "assistant");

  if (tool_calls && cJSON_GetArraySize(tool_calls) > 0) {
    /* OpenAI permits content=null when tool_calls are present */
    if (content && content[0]) {
      cJSON_AddStringToObject(msg, "content", content);
    } else {
      cJSON_AddNullToObject(msg, "content");
    }
    cJSON_AddItemToObject(msg, "tool_calls", tool_calls);
  } else {
    cJSON_AddStringToObject(msg, "content", content ? content : "");
  }

  cJSON_AddItemToObject(choice, "message", msg);
  cJSON_AddStringToObject(choice, "finish_reason",
                          (tool_calls && cJSON_GetArraySize(tool_calls) > 0)
                            ? "tool_calls" : "stop");
  cJSON_AddItemToArray(choices, choice);
  cJSON_AddItemToObject(root, "choices", choices);

  cJSON *usage = cJSON_CreateObject();
  cJSON_AddNumberToObject(usage, "prompt_tokens", 0);
  cJSON_AddNumberToObject(usage, "completion_tokens", 0);
  cJSON_AddNumberToObject(usage, "total_tokens", 0);
  cJSON_AddItemToObject(root, "usage", usage);

  char *s = cJSON_PrintUnformatted(root);
  mg_http_reply(c, 200,
      "Content-Type: application/json\r\n"
      "Access-Control-Allow-Origin: *\r\n",
      "%s\n", s ? s : "{}");
  free(s);
  cJSON_Delete(root);
}

/* Keep the old name as a thin wrapper so nothing else breaks. */
static void send_openai_completion(struct mg_connection *c,
                                   const char *model,
                                   const char *content) {
  send_openai_completion_ex(c, model, content, NULL);
}

/* ================================================================== */
/*  Build the browser prompt from the OpenAI messages[] array         */
/* ================================================================== */
/* ================================================================== */
/*  Render OpenAI tools[] as text for the browser model               */
/* ================================================================== */

static char *render_tools_as_text(cJSON *tools) {
  if (!cJSON_IsArray(tools) || cJSON_GetArraySize(tools) == 0) return NULL;

  size_t cap = 4096, len = 0;
  char *out = (char *)malloc(cap);
  if (!out) return NULL;
  out[0] = '\0';

#define APPEND_RAW(s) do {                                              \
    size_t _n = strlen(s);                                              \
    while (len + _n + 1 > cap) { cap *= 2;                              \
      char *_nb = (char *)realloc(out, cap);                            \
      if (!_nb) { free(out); return NULL; }                             \
      out = _nb; }                                                      \
    memcpy(out + len, s, _n); len += _n; out[len] = '\0';               \
  } while (0)

  APPEND_RAW(
    "\n\n--- TOOLS ---\n"
    "You have access to the following tools. To call a tool, you MUST\n"
    "emit a fenced block. Bare JSON is NOT accepted and will be ignored.\n"
    "The fence is required. Format, exactly, with the fence markers on\n"
    "their own lines and nothing else on those lines:\n\n"
    "```tool_call\n"
    "{\"name\":\"<tool>\",\"arguments\":{...}}\n"
    "```\n\n"
    "WRONG (will be ignored):\n"
    "{\"name\":\"get_objects\",\"arguments\":{}}\n\n"
    "CORRECT:\n"
    "```tool_call\n"
    "{\"name\":\"get_objects\",\"arguments\":{}}\n"
    "```\n\n"
    "Rules:\n"
    "- The opening line is exactly ```tool_call and nothing else.\n"
    "- The closing line is exactly ``` and nothing else.\n"
    "- Between them, one JSON object with keys \"name\" and \"arguments\".\n"
    "- You may emit multiple tool_call blocks in one reply.\n"
    "- After emitting tool calls, STOP. Do not narrate, explain, or wrap\n"
    "  them in prose. Do not add any text before or after the blocks.\n"
    "- If you have no tool to call, reply normally with plain text and\n"
    "  no fence at all.\n\n"
    "When you receive tool results, they appear as user messages that\n"
    "start with [tool_result]. Use them to continue.\n\n"
    "Available tools:\n");

  cJSON *t = NULL;
  cJSON_ArrayForEach(t, tools) {
    cJSON *fn = cJSON_GetObjectItem(t, "function");
    if (!cJSON_IsObject(fn)) continue;
    cJSON *name = cJSON_GetObjectItem(fn, "name");
    cJSON *desc = cJSON_GetObjectItem(fn, "description");
    cJSON *params = cJSON_GetObjectItem(fn, "parameters");
    if (!cJSON_IsString(name)) continue;

    char *ps = params ? cJSON_PrintUnformatted(params) : NULL;
    size_t need = strlen(name->valuestring) + 64 +
                  (cJSON_IsString(desc) ? strlen(desc->valuestring) : 0) +
                  (ps ? strlen(ps) : 2);
    if (len + need > cap) {
      while (len + need > cap) cap *= 2;
      char *nb = (char *)realloc(out, cap);
      if (!nb) { free(ps); free(out); return NULL; }
      out = nb;
    }
    len += (size_t)snprintf(out + len, cap - len,
      "- %s: %s\n  parameters: %s\n",
      name->valuestring,
      cJSON_IsString(desc) ? desc->valuestring : "",
      ps ? ps : "{}");
    free(ps);
  }

#undef APPEND_RAW
  return out;
}

/* ================================================================== */
/*  Extract ```tool_call ... ``` blocks from a model reply            */
/* ================================================================== */
static char *strip_tool_calls(const char *reply) {
  if (!reply) return NULL;

  size_t cap = strlen(reply) + 1, len = 0;
  char *out = malloc(cap);
  if (!out) return NULL;

  const char *p = reply;
  while (*p) {
    if (*p == '{') {
      const char *start = p;
      int depth = 0, in_str = 0, esc = 0;
      const char *end = NULL;
      for (const char *q = start; *q; q++) {
        char c = *q;
        if (in_str) {
          if (esc) { esc = 0; continue; }
          if (c == '\\') { esc = 1; continue; }
          if (c == '"')  { in_str = 0; continue; }
          continue;
        }
        if (c == '"') { in_str = 1; continue; }
        if (c == '{') depth++;
        else if (c == '}') { depth--; if (depth == 0) { end = q; break; } }
      }
      if (end) {
        size_t blen = (size_t)(end - start) + 1;
        char *body = malloc(blen + 1);
        if (body) {
          memcpy(body, start, blen); body[blen] = '\0';
          cJSON *obj = cJSON_Parse(body);
          free(body);
          if (cJSON_IsObject(obj)) {
            cJSON *nm = cJSON_GetObjectItem(obj, "name");
            cJSON *ar = cJSON_GetObjectItem(obj, "arguments");
            if (cJSON_IsString(nm) && ar) {
              /* it's a tool call: skip it in the output */
              cJSON_Delete(obj);
              p = end + 1;
              continue;
            }
          }
          cJSON_Delete(obj);
        }
      }
    }
    /* not a tool call: copy the char through */
    if (len + 2 > cap) { cap *= 2; char *nb = realloc(out, cap);
      if (!nb) { free(out); return NULL; } out = nb; }
    out[len++] = *p++;
  }
  out[len] = '\0';
  return out;
}

static cJSON *extract_tool_calls(const char *reply) {
  if (!reply) return NULL;

  cJSON *calls = NULL;
  int n = 0;
  const char *p = reply;

  while (*p) {
    /* find next opening brace */
    while (*p && *p != '{') p++;
    if (!*p) break;

    /* brace-match to find the end of this object, respecting strings */
    const char *start = p;
    int depth = 0;
    int in_str = 0;
    int esc = 0;
    const char *end = NULL;

    for (const char *q = start; *q; q++) {
      char c = *q;
      if (in_str) {
        if (esc) { esc = 0; continue; }
        if (c == '\\') { esc = 1; continue; }
        if (c == '"')  { in_str = 0; continue; }
        continue;
      }
      if (c == '"') { in_str = 1; continue; }
      if (c == '{') depth++;
      else if (c == '}') {
        depth--;
        if (depth == 0) { end = q; break; }
      }
    }
    if (!end) break;

    size_t blen = (size_t)(end - start) + 1;
    char *body = malloc(blen + 1);
    if (!body) break;
    memcpy(body, start, blen);
    body[blen] = '\0';

    cJSON *obj = cJSON_Parse(body);
    free(body);

    if (cJSON_IsObject(obj)) {
      cJSON *name = cJSON_GetObjectItem(obj, "name");
      cJSON *args = cJSON_GetObjectItem(obj, "arguments");
      if (cJSON_IsString(name)) {
        if (!calls) calls = cJSON_CreateArray();
        cJSON *call = cJSON_CreateObject();
        char cid[32];
        snprintf(cid, sizeof(cid), "call_%d", n++);
        cJSON_AddStringToObject(call, "id", cid);
        cJSON_AddStringToObject(call, "type", "function");
        cJSON *fn = cJSON_CreateObject();
        cJSON_AddStringToObject(fn, "name", name->valuestring);
        char *as = args ? cJSON_PrintUnformatted(args) : NULL;
        cJSON_AddStringToObject(fn, "arguments", as ? as : "{}");
        free(as);
        cJSON_AddItemToObject(call, "function", fn);
        cJSON_AddItemToArray(calls, call);
      }
    }
    cJSON_Delete(obj);
    p = end + 1;
  }
  return calls;
}

static char *build_browser_transcript(cJSON *messages) {
  if (!cJSON_IsArray(messages)) return NULL;

  /* Growable buffer. Deliberately not GString, so this function is
   * usable on Windows and macOS too. */
  size_t cap = 4096, len = 0;
  char *out = (char *)malloc(cap);
  if (!out) return NULL;
  out[0] = '\0';

  int n = cJSON_GetArraySize(messages);
  int wrote = 0;

  for (int i = 0; i < n; i++) {
    cJSON *m    = cJSON_GetArrayItem(messages, i);
    cJSON *role = cJSON_GetObjectItem(m, "role");
    cJSON *cont = cJSON_GetObjectItem(m, "content");
    if (!cJSON_IsString(role)) continue;

    char *body = NULL;
    size_t blen = 0, bcap = 0;
    int got = 0;

    if (cJSON_IsString(cont)) {
      blen = strlen(cont->valuestring);
      body = strdup(cont->valuestring);
      got  = (body != NULL);
    } else if (cJSON_IsArray(cont)) {
      cJSON *part = NULL;
      cJSON_ArrayForEach(part, cont) {
        cJSON *type = cJSON_GetObjectItem(part, "type");
        cJSON *text = cJSON_GetObjectItem(part, "text");
        if (cJSON_IsString(type) &&
            strcmp(type->valuestring, "text") == 0 &&
            cJSON_IsString(text)) {
          size_t tlen = strlen(text->valuestring);
          if (blen + tlen + 1 > bcap) {
            bcap = (blen + tlen + 1) * 2;
            char *nb = (char *)realloc(body, bcap);
            if (!nb) { got = 0; break; }
            body = nb;
          }
          memcpy(body + blen, text->valuestring, tlen);
          blen += tlen;
          body[blen] = '\0';
          got = 1;
        }
      }
    }


    /* Tool results: render as [tool_result] <content> so the browser
     * model sees them as user-visible context. */
    if (strcmp(role->valuestring, "tool") == 0) {
        free(body);
        cJSON *tcid = cJSON_GetObjectItem(m, "tool_call_id");
        const char *cid_s = cJSON_IsString(tcid) ? tcid->valuestring : "";
        const char *tc = cJSON_IsString(cont) ? cont->valuestring : "";
        size_t need = 16 + strlen(cid_s) + strlen(tc) + 4;
        char *line = (char *)malloc(need);
        if (!line) continue;
        snprintf(line, need, "[tool_result %s]\n%s", cid_s, tc);

        size_t nlen = strlen(line);
        if (len + nlen + 3 > cap) {
            while (len + nlen + 3 > cap) cap *= 2;
            char *nb = (char *)realloc(out, cap);
            if (!nb) { free(line); free(out); return NULL; }
            out = nb;
        }
        if (wrote) { out[len++] = '\n'; out[len++] = '\n'; }
        memcpy(out + len, line, nlen);
        len += nlen;
        out[len] = '\0';
        free(line);
        wrote = 1;
        continue;
    }

    /* Assistant messages that carry tool_calls: replay the calls as
     * text so the model sees what it asked for on the previous turn. */
    cJSON *tcs = cJSON_GetObjectItem(m, "tool_calls");
    if (cJSON_IsArray(tcs) && strcmp(role->valuestring, "assistant") == 0) {
        cJSON *call = NULL;
        cJSON_ArrayForEach(call, tcs) {
            cJSON *fn = cJSON_GetObjectItem(call, "function");
            if (!cJSON_IsObject(fn)) continue;
            cJSON *n = cJSON_GetObjectItem(fn, "name");
            cJSON *a = cJSON_GetObjectItem(fn, "arguments");
            const char *ns = cJSON_IsString(n) ? n->valuestring : "";
            const char *as = cJSON_IsString(a) ? a->valuestring : "{}";
            size_t need = 32 + strlen(ns) + strlen(as);
            char *line = (char *)malloc(need);
            if (!line) continue;
            snprintf(line, need,
                     "```tool_call\n{\"name\":\"%s\",\"arguments\":%s}\n```",
                     ns, as);
            size_t nlen = strlen(line);
            if (len + nlen + 3 > cap) {
                while (len + nlen + 3 > cap) cap *= 2;
                char *nb = (char *)realloc(out, cap);
                if (!nb) { free(line); free(out); return NULL; }
                out = nb;
            }
            if (wrote) { out[len++] = '\n'; out[len++] = '\n'; }
            memcpy(out + len, line, nlen);
            len += nlen;
            out[len] = '\0';
            free(line);
            wrote = 1;
        }
    }    


    if (!got) { free(body); continue; }

    size_t need = (wrote ? 2 : 0) + 1 + strlen(role->valuestring) + 2
                  + blen + 1;
    if (len + need > cap) {
      while (len + need > cap) cap *= 2;
      char *nb = (char *)realloc(out, cap);
      if (!nb) { free(body); free(out); return NULL; }
      out = nb;
    }
    if (wrote) { out[len++] = '\n'; out[len++] = '\n'; }
    out[len++] = '[';
    size_t rl = strlen(role->valuestring);
    memcpy(out + len, role->valuestring, rl); len += rl;
    out[len++] = ']';
    out[len++] = '\n';
    memcpy(out + len, body, blen); len += blen;
    out[len] = '\0';
    free(body);
    wrote = 1;
  }

  if (!wrote) { free(out); return NULL; }
  return out;
}

/* ================================================================== */
/*  HTTP handler                                                      */
/* ================================================================== */

static void http_handler(struct mg_connection *c, int ev, void *ev_data) {
  if (ev != MG_EV_HTTP_MSG) return;
  struct mg_http_message *hm = ev_data;

  if (mg_strcmp(hm->method, mg_str("OPTIONS")) == 0) {
    mg_http_reply(c, 204,
        "Access-Control-Allow-Origin: *\r\n"
        "Access-Control-Allow-Methods: POST,OPTIONS\r\n"
        "Access-Control-Allow-Headers: Content-Type,Authorization\r\n",
        "");
    return;
  }

  if (!mg_match(hm->uri, mg_str("/v1/chat/completions"), NULL)) {
    send_openai_error(c, 404, "not_found", "unknown endpoint");
    return;
  }
  if (mg_strcmp(hm->method, mg_str("POST")) != 0) {
    send_openai_error(c, 405, "method_not_allowed", "POST only");
    return;
  }

  pthread_mutex_lock(&g_gate_mutex);
  int stage = g_gate_stage;
  pthread_mutex_unlock(&g_gate_mutex);
  if (stage < 2) {
    send_openai_error(c, 503, "server_error",
        "provider selected but not ready yet — page still loading");
    return;
  }

  cJSON *body = cJSON_ParseWithLength(hm->body.buf, hm->body.len);
  if (!body) {
    send_openai_error(c, 400, "invalid_request_error", "invalid JSON body");
    return;
  }

  cJSON *model    = cJSON_GetObjectItem(body, "model");
  cJSON *messages = cJSON_GetObjectItem(body, "messages");
  cJSON *atts     = cJSON_GetObjectItem(body, "attachments");
  cJSON *tools    = cJSON_GetObjectItem(body, "tools");

  char model_name[128];
  if (cJSON_IsString(model) && model->valuestring[0]) {
    snprintf(model_name, sizeof(model_name), "%s", model->valuestring);
  } else {
    snprintf(model_name, sizeof(model_name), "model/default");
  }


  
  char *prompt = build_browser_transcript(messages);
  if (!prompt) {
    cJSON_Delete(body);
    send_openai_error(c, 400, "invalid_request_error",
                      "no usable content found in messages[]");
    return;
  }

  if (!g_tools_preamble_sent && cJSON_IsArray(tools)
      && cJSON_GetArraySize(tools) > 0) {
      char *tools_text = render_tools_as_text(tools);
      if (tools_text) {
          size_t old = strlen(prompt);
          size_t add = strlen(tools_text);
          char *merged = (char *)realloc(prompt, old + add + 1);
          if (merged) {
              memcpy(merged + old, tools_text, add);
              merged[old + add] = '\0';
              prompt = merged;
          }
          free(tools_text);
          g_tools_preamble_sent = 1;
          printf("[relay] tools preamble sent (first turn of session)\n");
      }
  }
  /* ---------------------------------------------------------------- */

  cJSON *atts_copy = NULL;
  int att_count = 0;
  if (cJSON_IsArray(atts)) {
    atts_copy = cJSON_Duplicate(atts, 1);
    att_count = cJSON_GetArraySize(atts_copy);
  }

  printf("[relay] HTTP request: model=%s prompt_len=%zu attachments=%d "
         "tools=%d\n",
         model_name, strlen(prompt), att_count,
         cJSON_IsArray(tools) ? cJSON_GetArraySize(tools) : 0);
  fflush(stdout);

  /* ---- busy check. No stale-reset. ---- */
  pthread_mutex_lock(&g_req_mutex);
  if (g_request_busy) {
    pthread_mutex_unlock(&g_req_mutex);
    free(prompt);
    if (atts_copy) cJSON_Delete(atts_copy);
    cJSON_Delete(body);
    send_openai_error(c, 503, "server_error",
                      "relay busy: another request is in flight");
    return;
  }
  g_request_busy = 1;
  clock_gettime(CLOCK_REALTIME, &g_request_started_at);
  free(g_reply);
  g_reply = NULL;
  pthread_mutex_unlock(&g_req_mutex);

  cJSON_Delete(body);

  /* Hand the prompt to the platform layer. Ownership of `prompt`
   * transfers; platform must free it when done. `atts_copy` too. */
  relay_platform_deliver_prompt(prompt, atts_copy);

  /* No wall-clock deadline. Only a shim error or the END marker
   * ends this wait. */
  pthread_mutex_lock(&g_req_mutex);
  while (g_request_busy) {
    pthread_cond_wait(&g_reply_cond, &g_req_mutex);
  }
  char *reply = g_reply;
  g_reply = NULL;
  g_request_busy = 0;
  g_request_started_at.tv_sec = 0;
  g_request_started_at.tv_nsec = 0;
  pthread_mutex_unlock(&g_req_mutex);

  if (!reply) {
    send_openai_error(c, 502, "server_error", "relay returned no reply");
    return;
  }
  if (reply[0] == '\0') {
    free(reply);
    send_openai_error(c, 502, "server_error",
                      "shim reported failure, see relay stderr");
    return;
  }

  /* ---- extract tool calls out of the browser reply ---- */
  cJSON *tool_calls = extract_tool_calls(reply);
  if (tool_calls && cJSON_GetArraySize(tool_calls) > 0) {
      char *visible = strip_tool_calls(reply);
      printf("[relay] reply contains %d tool_call(s), visible_len=%zu\n",
             cJSON_GetArraySize(tool_calls),
             visible ? strlen(visible) : 0);
      fflush(stdout);
      /* ownership of tool_calls transfers to the response builder */
      send_openai_completion_ex(c, model_name, visible, tool_calls);
      free(visible);
  } else {
      if (tool_calls) cJSON_Delete(tool_calls);
      send_openai_completion(c, model_name, reply);
  }
  free(reply);
}

/* ================================================================== */
/*  HTTP thread                                                       */
/* ================================================================== */
static void *http_thread(void *arg) {
  (void)arg;

  gate_wait_at_least(1);

  printf("[relay] provider selected — starting HTTP listener\n");
  fflush(stdout);

  struct mg_mgr mgr;
  mg_mgr_init(&mgr);

  int bound_port = 0;
  struct mg_connection *lst = NULL;
  char addr[96];

  for (int p = g_relay_port_base;
       p < g_relay_port_base + RELAY_HTTP_PORT_RANGE;
       p++) {
    snprintf(addr, sizeof(addr), "http://%s:%d", g_relay_host, p);
    lst = mg_http_listen(&mgr, addr, http_handler, NULL);
    if (lst) { bound_port = p; break; }
  }

  if (!lst) {
      fprintf(stderr,
              "[relay] no free port in range %s:%d-%d; is another relay running?\n",
              g_relay_host,
              g_relay_port_base,
              g_relay_port_base + RELAY_HTTP_PORT_RANGE - 1);
      relay_platform_set_title("relay - no free port");
      mg_mgr_free(&mgr);
      return NULL;
  }

  /* Loopback is always advertised as 127.0.0.1 even when we bind
   * 0.0.0.0, because that is the address a local client should use. */
  const char *advertise_host =
      (strcmp(g_relay_host, "0.0.0.0") == 0) ? "127.0.0.1" : g_relay_host;

  printf("[relay] HTTP API listening on %s:%d\n", g_relay_host, bound_port);
  printf("[relay]   POST /v1/chat/completions\n");
  printf("[relay]   api_url: "
        "http://%s:%d/v1/chat/completions\n", advertise_host, bound_port);
  if (strcmp(g_relay_host, "0.0.0.0") == 0) {
      printf("[relay]   NOTE: bound to all interfaces (no auth)\n");
  }

  /* Put the endpoint in the title bar so N relay windows are
  * distinguishable at a glance and the port is never a mystery. */
  {
      char title[200];
      snprintf(title, sizeof(title),
              "http://%s:%d/v1/chat/completions",
              advertise_host, bound_port);
      relay_platform_set_title(title);
  }

  fflush(stdout);

  for (;;) mg_mgr_poll(&mgr, 200);

  mg_mgr_free(&mgr);
  return NULL;
}

static char *build_chooser_data_url(void) {
  /* Build the HTML in a growable buffer, then percent-encode it. */
  size_t cap = 8192, len = 0;
  char *html = (char *)malloc(cap);
  if (!html) return NULL;
  html[0] = '\0';

#define APPEND(s) do {                                                  \
    size_t _n = strlen(s);                                              \
    while (len + _n + 1 > cap) { cap *= 2;                              \
      char *_nb = (char *)realloc(html, cap);                           \
      if (!_nb) { free(html); return NULL; }                            \
      html = _nb; }                                                     \
    memcpy(html + len, s, _n); len += _n; html[len] = '\0';             \
  } while (0)

  APPEND(CHOOSER_HTML_HEAD);
  for (const provider_t *p = g_providers; p->id; p++) {
    int active = (strcmp(p->id, "deepseek") == 0) ||
                 (strcmp(p->id, "chatgpt")  == 0) ||
                 (strcmp(p->id, "qwen")     == 0) ||
                 (strcmp(p->id, "gemini")   == 0);

      const char *fmt =
          "<div class=\"card%s\" data-id=\"%s\" data-name=\"%s\" "
          "data-active=\"%d\" onclick=\"pick(this)\">"
          "<div class=\"icon\" style=\"background:%s;color:%s\">"
          "<img src=\"%s\" alt=\"\" onerror=\"iconFallback(this)\">"
          "<span class=\"mono\">%c</span>"
          "</div>"
          "<div class=\"body\">"
          "<p class=\"name\">%s</p><p class=\"note\">%s</p>"
          "</div></div>";

      int need = snprintf(NULL, 0, fmt,
          active ? "" : " dimmed",
          p->id, p->name, active,
          p->bg, p->fg, p->icon,
          p->name[0], p->name, p->note);
      if (need < 0) continue;

      char *buf = (char *)malloc((size_t)need + 1);
      if (!buf) continue;
      snprintf(buf, (size_t)need + 1, fmt,
          active ? "" : " dimmed",
          p->id, p->name, active,
          p->bg, p->fg, p->icon,
          p->name[0], p->name, p->note);
      APPEND(buf);
      free(buf);
  }
  APPEND(CHOOSER_HTML_TAIL);
#undef APPEND

  /* Percent-encode into a data: URL. */
  size_t ucap = len * 3 + 32;
  char *url = (char *)malloc(ucap);
  if (!url) { free(html); return NULL; }
  const char *prefix = "data:text/html;charset=utf-8,";
  size_t ulen = strlen(prefix);
  memcpy(url, prefix, ulen);

  static const char hex[] = "0123456789ABCDEF";
  for (size_t i = 0; i < len; i++) {
    unsigned char u = (unsigned char)html[i];
    if ((u >= 'A' && u <= 'Z') || (u >= 'a' && u <= 'z') ||
        (u >= '0' && u <= '9') ||
        u == '-' || u == '_' || u == '.' || u == '~') {
      url[ulen++] = (char)u;
    } else {
      url[ulen++] = '%';
      url[ulen++] = hex[(u >> 4) & 0xf];
      url[ulen++] = hex[u & 0xf];
    }
  }
  url[ulen] = '\0';
  free(html);
  return url;
}

/* ================================================================== */
/*  Platform: GTK (Linux)                                             */
/* ================================================================== */
#if defined(WEBVIEW_GTK) && !defined(RELAY_TERMUX)

#include <gtk/gtk.h>
#include <gdk/gdkx.h>
#include <X11/Xatom.h>
#include <webkit2/webkit2.h>
#include <glib/gstdio.h>
#include <webkit2/webkit2.h>


static WebKitWebView  *g_view = NULL;
static struct webview *g_w    = NULL;

/* ---- prompt delivery ---- */

typedef struct {
  char  *text;
  cJSON *attachments;
} pending_prompt;


typedef struct { char title[160]; } title_msg;

static gboolean apply_title_idle(gpointer data) {
    title_msg *m = (title_msg *)data;
    if (g_w && g_w->priv.window && GTK_IS_WINDOW(g_w->priv.window)) {
        gtk_window_set_title(GTK_WINDOW(g_w->priv.window), m->title);
    }
    free(m);
    return G_SOURCE_REMOVE;
}

void relay_platform_set_title(const char *title) {
    /* http_thread runs on a plain pthread; GTK calls must run on the
     * main thread. g_idle_add() marshals the update and returns
     * immediately. The payload is heap-allocated and freed inside the
     * idle callback. */
    title_msg *m = calloc(1, sizeof(*m));
    if (!m) return;
    snprintf(m->title, sizeof(m->title), "%s", title ? title : "");
    g_idle_add(apply_title_idle, m);
}

static void relay_send_bundle(WebKitWebView *view,
                              const char *text,
                              cJSON *attachments) {
  if (!WEBKIT_IS_WEB_VIEW(view)) {
    fprintf(stderr, "[relay] relay_send_bundle: not a WebKitWebView\n");
    return;
  }

  char *quoted = json_quote(text ? text : "");
  if (!quoted) return;

  char *atts_json = NULL;
  if (attachments && cJSON_IsArray(attachments)) {
    atts_json = cJSON_PrintUnformatted(attachments);
  }
  if (!atts_json) atts_json = strdup("[]");

  size_t cap = strlen(quoted) + strlen(atts_json) + 64;
  char *script = (char *)malloc(cap);
  snprintf(script, cap,
           "window.relay && window.relay.sendBundle(%s,%s)",
           quoted, atts_json);

  webkit_web_view_evaluate_javascript(view, script, -1,
                                      NULL, NULL, NULL, NULL, NULL);

  free(quoted);
  free(atts_json);
  free(script);
}

static gboolean deliver_prompt_idle(gpointer data) {
  pending_prompt *p = (pending_prompt *)data;

  if (WEBKIT_IS_WEB_VIEW(g_view)) {
    printf("[relay] sending bundle: text_len=%zu attachments=%d\n",
           p->text ? strlen(p->text) : 0,
           p->attachments ? cJSON_GetArraySize(p->attachments) : 0);
    fflush(stdout);
    relay_send_bundle(g_view, p->text, p->attachments);
  } else {
    fprintf(stderr, "[relay] webview gone, dropping prompt\n");
    deliver_reply("");
  }

  free(p->text);
  if (p->attachments) cJSON_Delete(p->attachments);
  free(p);
  return G_SOURCE_REMOVE;
}

void relay_platform_deliver_prompt(const char *text, cJSON *attachments) {
  pending_prompt *p = calloc(1, sizeof(*p));
  p->text = strdup(text ? text : "");
  p->attachments = attachments;
  g_idle_add(deliver_prompt_idle, p);
}

void relay_platform_load_url(const char *url) {
  if (g_view && WEBKIT_IS_WEB_VIEW(g_view)) {
    webkit_web_view_load_uri(g_view, url);
  }
}

/* ---- external message from shim ---- */

static void on_external_message(struct webview *w, const char *arg) {
  if (!arg) return;

  if (strncmp(arg, "connect:", 8) == 0) {
    const char *id = arg + 8;
    g_tools_preamble_sent = 0;   /* browser history will be wiped */
    for (const provider_t *p = g_providers; p->id; p++) {
      if (strcmp(p->id, id) == 0) {
        printf("[relay] connecting to %s (%s)\n", p->name, p->url);
        fflush(stdout);
        gate_set(1);
        relay_platform_load_url(p->url);
        return;
      }
    }
    fprintf(stderr, "[relay] unknown provider id: %s\n", id);
    return;
  }

  if (strncmp(arg, "ready:", 6) == 0) {
    printf("[relay] provider ready: %s\n", arg + 6);
    fflush(stdout);
    gate_set(2);
    return;
  }

  if (strncmp(arg, "error:", 6) == 0) {
    fprintf(stderr, "[relay] shim error: %s\n", arg + 6);
    fflush(stderr);
    deliver_reply("");
    return;
  }

  printf("%s\n", arg);
  fflush(stdout);
  deliver_reply(arg);
}

/* ---- window close handling ---- */

static void hard_exit(void) { fflush(stdout); _exit(0); }

static gboolean on_delete_event(GtkWidget *widget, GdkEvent *event, gpointer data) {
  (void)widget; (void)event; (void)data; hard_exit(); return FALSE;
}
static gboolean on_destroy(GtkWidget *widget, gpointer data) {
  (void)widget; (void)data; hard_exit(); return FALSE;
}
static void accel_exit(GtkAccelGroup *g, GObject *o, guint key,
                       GdkModifierType mods, gpointer data) {
  (void)g; (void)o; (void)key; (void)mods; (void)data; hard_exit();
}

static void install_wm_delete(GtkWidget *window) {
  GdkWindow *gw = gtk_widget_get_window(window);
  if (!gw) return;
  GdkDisplay *dpy = gdk_window_get_display(gw);
  Display *xdpy = GDK_DISPLAY_XDISPLAY(dpy);
  Window xid = GDK_WINDOW_XID(gw);
  Atom wm_delete = XInternAtom(xdpy, "WM_DELETE_WINDOW", False);
  XSetWMProtocols(xdpy, xid, &wm_delete, 1);
}

static gboolean on_window_state(GtkWidget *widget,
                                GdkEventWindowState *event,
                                gpointer data) {
    (void)data;
    if (event->new_window_state & GDK_WINDOW_STATE_ICONIFIED) {
        gtk_window_deiconify(GTK_WINDOW(widget));
    }
    return FALSE;
}


static void install_close_handlers(struct webview *w) {
  GtkWidget *top = gtk_widget_get_toplevel(w->priv.window);
  if (!top) top = w->priv.window;

  g_signal_connect(G_OBJECT(top), "delete-event",
                   G_CALLBACK(on_delete_event), w);
  g_signal_connect(G_OBJECT(top), "destroy",
                   G_CALLBACK(on_destroy), w);
  g_signal_connect(G_OBJECT(top), "window-state-event",
                  G_CALLBACK(on_window_state), NULL);                   

  GtkAccelGroup *accel = gtk_accel_group_new();
  gtk_window_add_accel_group(GTK_WINDOW(top), accel);
  gtk_accel_group_connect(accel, GDK_KEY_q, GDK_CONTROL_MASK, 0,
      g_cclosure_new(G_CALLBACK(accel_exit), NULL, NULL));
  gtk_accel_group_connect(accel, GDK_KEY_Escape, 0, 0,
      g_cclosure_new(G_CALLBACK(accel_exit), NULL, NULL));

  gtk_window_present(GTK_WINDOW(top));
  gtk_window_set_focus_on_map(GTK_WINDOW(top), FALSE);
  gtk_window_set_skip_taskbar_hint(GTK_WINDOW(top), FALSE);
  gtk_window_set_skip_pager_hint(GTK_WINDOW(top), FALSE);
  gtk_window_set_urgency_hint(GTK_WINDOW(top), FALSE);
}

/* ---- X11 watchdog ---- */

typedef struct { Display *dpy; Window xid; } watchdog_ctx;

/*
static gpointer exit_watchdog(gpointer data) {
  watchdog_ctx *ctx = (watchdog_ctx *)data;
  for (;;) {
    struct timespec ts = { 0, 500 * 1000 * 1000 };
    nanosleep(&ts, NULL);
    if (!ctx->dpy || !ctx->xid) continue;
    XWindowAttributes attr;
    if (XGetWindowAttributes(ctx->dpy, ctx->xid, &attr) == 0) {
      const char *m = "[relay] watchdog: X window gone, exiting\n";
      ssize_t n = write(2, m, strlen(m)); (void)n; _exit(0);
    }
    if (attr.map_state == IsUnmapped) continue;
    if (attr.map_state == IsUnviewable) {
      const char *m = "[relay] watchdog: X window unviewable, exiting\n";
      ssize_t n = write(2, m, strlen(m)); (void)n; _exit(0);
    }
  }
  return NULL;
}
*/

/* ---- stdin reader ---- */

typedef struct { WebKitWebView *view; char *line; } stdin_msg;

static gboolean deliver_stdin_msg(gpointer data) {
  stdin_msg *m = (stdin_msg *)data;
  if (WEBKIT_IS_WEB_VIEW(m->view)) {
    printf("[relay] stdin sending: %s\n", m->line);
    fflush(stdout);
    relay_send_bundle(m->view, m->line, NULL);
  } else {
    fprintf(stderr, "[relay] webview gone, dropping: %s\n", m->line);
  }
  g_free(m->line);
  g_free(m);
  return G_SOURCE_REMOVE;
}

static gpointer stdin_thread(gpointer data) {
  WebKitWebView *view = WEBKIT_WEB_VIEW(data);
  char *line = NULL;
  size_t cap = 0;
  ssize_t n;

  printf("relay> "); fflush(stdout);

  while ((n = getline(&line, &cap, stdin)) != -1) {
    while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r')) {
      line[--n] = '\0';
    }
    if (n > 0) {
      stdin_msg *m = g_new0(stdin_msg, 1);
      m->view = view;
      m->line = g_strdup(line);
      g_idle_add(deliver_stdin_msg, m);
    }
    printf("relay> "); fflush(stdout);
  }
  free(line);
  printf("\n[relay] stdin closed, exiting reader thread\n");
  fflush(stdout);
  return NULL;
}

/* ---- main ---- */

int main(int argc, char **argv) {
   XInitThreads();  
  install_signal_traps();
  parse_args(argc, argv);

  char *chooser = build_chooser_data_url();

  struct webview w;
  memset(&w, 0, sizeof(w));
  w.title     = "Starting...";
  w.url       = chooser ? chooser : "";
  w.width     = 1024;
  w.height    = 768;
  w.resizable = 1;
  w.debug     = 1;
  w.external_invoke_cb = on_external_message;

  if (webview_init(&w) != 0) {
    fprintf(stderr, "webview_init failed\n");
    free(chooser);
    return 1;
  }
  free(chooser);

  WebKitWebView *view = WEBKIT_WEB_VIEW(w.priv.webview);
  g_view = view;
  g_w    = &w;

  gtk_widget_realize(w.priv.window);
  install_close_handlers(&w);
  install_wm_delete(w.priv.window);


  /* Persistent cookie jar only — no context surgery needed. */
  WebKitWebView *view_for_cookies = WEBKIT_WEB_VIEW(w.priv.webview);
  WebKitWebContext *ctx = webkit_web_view_get_context(view_for_cookies);
  WebKitCookieManager *cm = webkit_web_context_get_cookie_manager(ctx);

  char cookie_dir[700];
  char cookie_path[760];
  const char *home = getenv("HOME");
  if (!home || !home[0]) home = "/tmp";
  snprintf(cookie_dir, sizeof(cookie_dir),
           "%s/.local/share/wisp-relay", home);
  snprintf(cookie_path, sizeof(cookie_path),
           "%s/cookies.sqlite", cookie_dir);
  g_mkdir_with_parents(cookie_dir, 0700);

  webkit_cookie_manager_set_persistent_storage(
  cm, cookie_path, WEBKIT_COOKIE_PERSISTENT_STORAGE_SQLITE);

  /*
  static watchdog_ctx wd;
  wd.dpy = GDK_DISPLAY_XDISPLAY(gdk_display_get_default());
  wd.xid = GDK_WINDOW_XID(gtk_widget_get_window(w.priv.window));
  if (wd.dpy && wd.xid) {
    GThread *wdt = g_thread_new("exit-watchdog", exit_watchdog, &wd);
    (void)wdt;
  }
  */

  WebKitSettings *settings = webkit_web_view_get_settings(view);
  webkit_settings_set_user_agent(settings,
      "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 "
      "(KHTML, like Gecko) Chrome/131.0.0.0 Safari/537.36");
  webkit_settings_set_enable_developer_extras(settings, TRUE);
  webkit_settings_set_enable_write_console_messages_to_stdout(settings, TRUE);
  webkit_settings_set_javascript_can_access_clipboard(settings, TRUE);

  WebKitUserContentManager *ucm =
      webkit_web_view_get_user_content_manager(view);
  WebKitUserScript *script = webkit_user_script_new(
      INJECT_JS,
      WEBKIT_USER_CONTENT_INJECT_ALL_FRAMES,
      WEBKIT_USER_SCRIPT_INJECT_AT_DOCUMENT_START,
      NULL, NULL);
  webkit_user_content_manager_add_script(ucm, script);
  webkit_user_script_unref(script);

  webkit_web_view_reload(view);

  pthread_t http_tid;
  if (pthread_create(&http_tid, NULL, http_thread, NULL) != 0) {
    fprintf(stderr, "[relay] failed to start HTTP thread\n");
    return 1;
  }
  pthread_detach(http_tid);

  GThread *reader = g_thread_new("stdin-reader", stdin_thread, view);
  (void)reader;

  printf("[relay] ready.\n");
  printf("[relay] Pick a provider in the window, then click Connect.\n");
  printf("[relay] stdin: type a message here to send it manually\n");
  printf("[relay] Replies are printed as <reply>\n");
  printf("[relay] Ctrl-D to quit.\n\n");
  fflush(stdout);

  while (webview_loop(&w, 1) == 0) {
  }

  webview_exit(&w);
  return 0;
}

#endif /* WEBVIEW_GTK && !RELAY_TERMUX */

/* ================================================================== */
/*  Platform: Termux (Android, GTK, no X11)                           */
/* ================================================================== */
#if defined(WEBVIEW_GTK) && defined(RELAY_TERMUX)

#include <gtk/gtk.h>
#include <webkit2/webkit2.h>

static WebKitWebView  *g_view = NULL;
static struct webview *g_w    = NULL;

typedef struct { char *text; cJSON *attachments; } pending_prompt;

static void relay_send_bundle(WebKitWebView *view,
                              const char *text,
                              cJSON *attachments) {
  if (!WEBKIT_IS_WEB_VIEW(view)) return;

  char *quoted = json_quote(text ? text : "");
  if (!quoted) return;

  char *atts_json = NULL;
  if (attachments && cJSON_IsArray(attachments)) {
    atts_json = cJSON_PrintUnformatted(attachments);
  }
  if (!atts_json) atts_json = strdup("[]");

  size_t cap = strlen(quoted) + strlen(atts_json) + 64;
  char *script = (char *)malloc(cap);
  snprintf(script, cap,
           "window.relay && window.relay.sendBundle(%s,%s)",
           quoted, atts_json);

  webkit_web_view_evaluate_javascript(view, script, -1,
                                      NULL, NULL, NULL, NULL, NULL);

  free(quoted);
  free(atts_json);
  free(script);
}

static gboolean deliver_prompt_idle(gpointer data) {
  pending_prompt *p = (pending_prompt *)data;
  if (WEBKIT_IS_WEB_VIEW(g_view)) {
    relay_send_bundle(g_view, p->text, p->attachments);
  } else {
    deliver_reply("");
  }
  free(p->text);
  if (p->attachments) cJSON_Delete(p->attachments);
  free(p);
  return G_SOURCE_REMOVE;
}

void relay_platform_deliver_prompt(const char *text, cJSON *attachments) {
  pending_prompt *p = calloc(1, sizeof(*p));
  p->text = strdup(text ? text : "");
  p->attachments = attachments;
  g_idle_add(deliver_prompt_idle, p);
}

void relay_platform_load_url(const char *url) {
  if (g_view && WEBKIT_IS_WEB_VIEW(g_view)) {
    webkit_web_view_load_uri(g_view, url);
  }
}

static void on_external_message(struct webview *w, const char *arg) {
  if (!arg) return;

  if (strncmp(arg, "connect:", 8) == 0) {
    const char *id = arg + 8;
    g_tools_preamble_sent = 0;
    for (const provider_t *p = g_providers; p->id; p++) {
      if (strcmp(p->id, id) == 0) {
        printf("[relay] connecting to %s (%s)\n", p->name, p->url);
        fflush(stdout);
        gate_set(1);
        relay_platform_load_url(p->url);
        return;
      }
    }
    return;
  }
  if (strncmp(arg, "ready:", 6) == 0) {
    printf("[relay] provider ready: %s\n", arg + 6);
    fflush(stdout);
    gate_set(2);
    return;
  }
  if (strncmp(arg, "error:", 6) == 0) {
    fprintf(stderr, "[relay] shim error: %s\n", arg + 6);
    fflush(stderr);
    deliver_reply("");
    return;
  }
  printf("%s\n", arg);
  fflush(stdout);
  deliver_reply(arg);
}

static void hard_exit(void) { fflush(stdout); _exit(0); }

static gboolean on_delete_event(GtkWidget *widget, GdkEvent *event, gpointer data) {
  (void)widget; (void)event; (void)data; hard_exit(); return FALSE;
}
static gboolean on_destroy(GtkWidget *widget, gpointer data) {
  (void)widget; (void)data; hard_exit(); return FALSE;
}
static void accel_exit(GtkAccelGroup *g, GObject *o, guint key,
                       GdkModifierType mods, gpointer data) {
  (void)g; (void)o; (void)key; (void)mods; (void)data; hard_exit();
}

static void install_close_handlers(struct webview *w) {
  GtkWidget *top = gtk_widget_get_toplevel(w->priv.window);
  if (!top) top = w->priv.window;

  g_signal_connect(G_OBJECT(top), "delete-event",
                   G_CALLBACK(on_delete_event), w);
  g_signal_connect(G_OBJECT(top), "destroy",
                   G_CALLBACK(on_destroy), w);

  GtkAccelGroup *accel = gtk_accel_group_new();
  gtk_window_add_accel_group(GTK_WINDOW(top), accel);
  gtk_accel_group_connect(accel, GDK_KEY_q, GDK_CONTROL_MASK, 0,
      g_cclosure_new(G_CALLBACK(accel_exit), NULL, NULL));

  gtk_window_present(GTK_WINDOW(top));
}

/* Termux has no X11/XSetWMProtocols. The GTK "delete-event" handler
 * above is sufficient: closing the window calls hard_exit(). */

typedef struct { WebKitWebView *view; char *line; } stdin_msg;

static gboolean deliver_stdin_msg(gpointer data) {
  stdin_msg *m = (stdin_msg *)data;
  if (WEBKIT_IS_WEB_VIEW(m->view)) {
    relay_send_bundle(m->view, m->line, NULL);
  }
  g_free(m->line);
  g_free(m);
  return G_SOURCE_REMOVE;
}

static gpointer stdin_thread(gpointer data) {
  WebKitWebView *view = WEBKIT_WEB_VIEW(data);
  char *line = NULL;
  size_t cap = 0;
  ssize_t n;

  printf("relay> "); fflush(stdout);

  while ((n = getline(&line, &cap, stdin)) != -1) {
    while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r')) {
      line[--n] = '\0';
    }
    if (n > 0) {
      stdin_msg *m = g_new0(stdin_msg, 1);
      m->view = view;
      m->line = g_strdup(line);
      g_idle_add(deliver_stdin_msg, m);
    }
    printf("relay> "); fflush(stdout);
  }
  free(line);
  return NULL;
}

int main(int argc, char **argv) {
  install_signal_traps();
  parse_args(argc, argv);

  char *chooser = build_chooser_data_url();

  struct webview w;
  memset(&w, 0, sizeof(w));
  w.title     = "Starting...";
  w.url       = chooser ? chooser : "";
  w.width     = 1024;
  w.height    = 768;
  w.resizable = 1;
  w.debug     = 1;
  w.external_invoke_cb = on_external_message;

  if (webview_init(&w) != 0) {
    fprintf(stderr, "webview_init failed\n");
    free(chooser);
    return 1;
  }
  free(chooser);

  WebKitWebView *view = WEBKIT_WEB_VIEW(w.priv.webview);
  g_view = view;
  g_w    = &w;

  gtk_widget_realize(w.priv.window);
  install_close_handlers(&w);

  WebKitSettings *settings = webkit_web_view_get_settings(view);
  webkit_settings_set_user_agent(settings,
      "Mozilla/5.0 (Linux; Android 14) AppleWebKit/537.36 "
      "(KHTML, like Gecko) Chrome/131.0.0.0 Mobile Safari/537.36");
  webkit_settings_set_enable_developer_extras(settings, TRUE);
  webkit_settings_set_enable_write_console_messages_to_stdout(settings, TRUE);
  webkit_settings_set_javascript_can_access_clipboard(settings, TRUE);

  WebKitUserContentManager *ucm =
      webkit_web_view_get_user_content_manager(view);
  WebKitUserScript *script = webkit_user_script_new(
      INJECT_JS,
      WEBKIT_USER_CONTENT_INJECT_ALL_FRAMES,
      WEBKIT_USER_SCRIPT_INJECT_AT_DOCUMENT_START,
      NULL, NULL);
  webkit_user_content_manager_add_script(ucm, script);
  webkit_user_script_unref(script);

  webkit_web_view_reload(view);

  pthread_t http_tid;
  if (pthread_create(&http_tid, NULL, http_thread, NULL) != 0) {
    fprintf(stderr, "[relay] failed to start HTTP thread\n");
    return 1;
  }
  pthread_detach(http_tid);

  GThread *reader = g_thread_new("stdin-reader", stdin_thread, view);
  (void)reader;

  printf("[relay] ready (termux).\n");
  fflush(stdout);

  while (webview_loop(&w, 1) == 0) {
  }
  webview_exit(&w);
  return 0;
}

#endif /* WEBVIEW_GTK && RELAY_TERMUX */

/* ================================================================== */
/*  Platform: Windows (WebView2 / Chromium)                           */
/* ================================================================== */
#if defined(WEBVIEW_WEBVIEW2)

#include <windows.h>

static struct webview *g_w = NULL;

/* Prompt delivery crosses a thread boundary: the HTTP thread calls
 * relay_platform_deliver_prompt(), the UI thread (inside webview_loop)
 * drains the pending prompt and injects it via webview_eval. Only one
 * request is ever in flight (the HTTP handler holds g_request_busy),
 * so a single slot is enough. No lock is needed: the HTTP thread sets
 * g_pending before blocking on the reply condvar, and the UI thread
 * consumes it before the HTTP thread can be woken with a new prompt. */
typedef struct { char *text; cJSON *attachments; } pending_prompt;
static pending_prompt *g_pending = NULL;

void relay_platform_set_title(const char *title) {
    if (!g_w || !g_w->priv.hwnd) return;
    SetWindowTextA(g_w->priv.hwnd, title ? title : "");
}

static void relay_send_bundle_win(const char *text, cJSON *attachments) {
  /* Build "window.relay && window.relay.sendBundle(<q>,<a>)". */
  char *quoted = json_quote(text ? text : "");
  if (!quoted) return;

  char *atts_json = NULL;
  if (attachments && cJSON_IsArray(attachments)) {
    atts_json = cJSON_PrintUnformatted(attachments);
  }
  if (!atts_json) atts_json = strdup("[]");

  size_t cap = strlen(quoted) + strlen(atts_json) + 64;
  char *script = (char *)malloc(cap);
  snprintf(script, cap,
           "window.relay && window.relay.sendBundle(%s,%s)",
           quoted, atts_json);

  if (g_w) webview_eval(g_w, script);

  free(quoted);
  free(atts_json);
  free(script);
}

void relay_platform_deliver_prompt(const char *text, cJSON *attachments) {
  pending_prompt *p = calloc(1, sizeof(*p));
  p->text = strdup(text ? text : "");
  p->attachments = attachments;
  g_pending = p;
}

void relay_platform_load_url(const char *url) {
  if (!g_w) return;
  /* WebView2 has no public "navigate" entry point in webview.h, but
   * the webview has a live DOM. Setting window.location.href is the
   * portable way to navigate, and it works on every backend. */
  char script[2048];
  snprintf(script, sizeof(script), "window.location.href=%s;", json_quote(url));
  webview_eval(g_w, script);
}

static void on_external_message(struct webview *w, const char *arg) {
  (void)w;
  if (!arg) return;

  if (strncmp(arg, "connect:", 8) == 0) {
    const char *id = arg + 8;
    g_tools_preamble_sent = 0;
    for (const provider_t *p = g_providers; p->id; p++) {
      if (strcmp(p->id, id) == 0) {
        printf("[relay] connecting to %s (%s)\n", p->name, p->url);
        fflush(stdout);
        gate_set(1);
        relay_platform_load_url(p->url);
        return;
      }
    }
    fprintf(stderr, "[relay] unknown provider id: %s\n", id);
    return;
  }
  if (strncmp(arg, "ready:", 6) == 0) {
    printf("[relay] provider ready: %s\n", arg + 6);
    fflush(stdout);
    gate_set(2);
    return;
  }
  if (strncmp(arg, "error:", 6) == 0) {
    fprintf(stderr, "[relay] shim error: %s\n", arg + 6);
    fflush(stderr);
    deliver_reply("");
    return;
  }
  printf("%s\n", arg);
  fflush(stdout);
  deliver_reply(arg);
}

/* Drain any pending prompt. Called from the main loop, on the UI
 * thread, once per webview_loop iteration. */
static void pump_pending_prompt(void) {
  if (!g_pending) return;
  pending_prompt *p = g_pending;
  g_pending = NULL;

  if (g_w) {
    relay_send_bundle_win(p->text, p->attachments);
  } else {
    deliver_reply("");
  }

  free(p->text);
  if (p->attachments) cJSON_Delete(p->attachments);
  free(p);
}

/* Plain getline-based reader; no GLib on Windows. compat/posix.h
 * supplies getline() when building against the MSVC CRT. */
static DWORD WINAPI stdin_thread(LPVOID param) {
  (void)param;
  char *line = NULL;
  size_t cap = 0;
  ssize_t n;

  printf("relay> "); fflush(stdout);
  while ((n = getline(&line, &cap, stdin)) != -1) {
    while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r')) {
      line[--n] = '\0';
    }
    if (n > 0 && g_w) {
      relay_send_bundle_win(line, NULL);
    }
    printf("relay> "); fflush(stdout);
  }
  free(line);
  return 0;
}

int main(int argc, char **argv) {
  install_signal_traps();
  parse_args(argc, argv);

  char *chooser = build_chooser_data_url();

  struct webview w;
  memset(&w, 0, sizeof(w));
  w.title     = "Starting...";
  w.url       = chooser ? chooser : "";
  w.width     = 1024;
  w.height    = 768;
  w.resizable = 1;
  w.debug     = 1;
  w.external_invoke_cb = on_external_message;

  if (webview_init(&w) != 0) {
    fprintf(stderr, "webview_init failed\n");
    free(chooser);
    return 1;
  }
  free(chooser);
  g_w = &w;

  /* Install the shim at document start on every navigation. WebView2
   * keeps this active across page loads, which the single-eval
   * approach did not: the shim must already be running on the provider
   * page before the provider's own scripts execute. */
  webview_add_init_script(&w, INJECT_JS);

  pthread_t http_tid;
  if (pthread_create(&http_tid, NULL, http_thread, NULL) != 0) {
    fprintf(stderr, "[relay] failed to start HTTP thread\n");
    return 1;
  }
  pthread_detach(http_tid);

  HANDLE reader = CreateThread(NULL, 0, stdin_thread, NULL, 0, NULL);
  if (reader) CloseHandle(reader);

  printf("[relay] ready (win32 / WebView2).\n");
  printf("[relay] Pick a provider in the window, then click Connect.\n");
  printf("[relay] stdin: type a message here to send it manually\n");
  printf("[relay] Replies are printed as <reply>\n");
  printf("[relay] Ctrl-D to quit.\n\n");
  fflush(stdout);

  while (webview_loop(&w, 1) == 0) {
    pump_pending_prompt();
  }

  webview_exit(&w);
  return 0;
}

#endif /* WEBVIEW_WEBVIEW2 */

/* ================================================================== */
/*  Platform: macOS (WEBVIEW_COCOA)                                   */
/* ================================================================== */
#if defined(WEBVIEW_COCOA)

/* webview.h's Cocoa backend needs Objective-C. relay.c is compiled as
 * C on all platforms in the current CMakeLists. To use the Cocoa
 * backend, compile relay.c as Objective-C (add "-x objective-c" for
 * the file, or rename to relay.m) and define RELAY_MAC_OBJC.
 *
 * If RELAY_MAC_OBJC is not defined, we compile a stub that prints a
 * clear error so the failure mode is obvious instead of a link
 * error. */

#if !defined(RELAY_MAC_OBJC)

int main(void) {
  fprintf(stderr,
      "[relay] Cocoa backend requires Objective-C.\n"
      "[relay] Recompile relay.c with -x objective-c (or rename to\n"
      "[relay] relay.m) and define RELAY_MAC_OBJC.\n");
  return 1;
}

#else /* RELAY_MAC_OBJC */

#import <Cocoa/Cocoa.h>
#import <WebKit/WebKit.h>

static struct webview *g_w = NULL;

typedef struct { char *text; cJSON *attachments; } pending_prompt;
static pending_prompt *g_pending = NULL;

void relay_platform_set_title(const char *title) {
    NSString *ns = [NSString stringWithUTF8String:(title ? title : "")];
    dispatch_async(dispatch_get_main_queue(), ^{
        if (g_w && g_w->priv.window) {
            [g_w->priv.window setTitle:ns];
        }
    });
}

static void relay_send_bundle_mac(const char *text, cJSON *attachments) {
  char *quoted = json_quote(text ? text : "");
  if (!quoted) return;

  char *atts_json = NULL;
  if (attachments && cJSON_IsArray(attachments)) {
    atts_json = cJSON_PrintUnformatted(attachments);
  }
  if (!atts_json) atts_json = strdup("[]");

  size_t cap = strlen(quoted) + strlen(atts_json) + 64;
  char *script = (char *)malloc(cap);
  snprintf(script, cap,
           "window.relay && window.relay.sendBundle(%s,%s)",
           quoted, atts_json);

  if (g_w) webview_eval(g_w, script);

  free(quoted);
  free(atts_json);
  free(script);
}

void relay_platform_deliver_prompt(const char *text, cJSON *attachments) {
  pending_prompt *p = calloc(1, sizeof(*p));
  p->text = strdup(text ? text : "");
  p->attachments = attachments;
  g_pending = p;
}

void relay_platform_load_url(const char *url) {
  if (!g_w) return;
  NSString *nsURL = [NSString stringWithUTF8String:url];
  NSURL *u = [NSURL URLWithString:nsURL];
  [[g_w->priv.webview mainFrame] loadRequest:[NSURLRequest requestWithURL:u]];
}

static void on_external_message(struct webview *w, const char *arg) {
  (void)w;
  if (!arg) return;

  if (strncmp(arg, "connect:", 8) == 0) {
    const char *id = arg + 8;
    g_tools_preamble_sent = 0;
    for (const provider_t *p = g_providers; p->id; p++) {
      if (strcmp(p->id, id) == 0) {
        printf("[relay] connecting to %s (%s)\n", p->name, p->url);
        fflush(stdout);
        gate_set(1);
        relay_platform_load_url(p->url);
        return;
      }
    }
    return;
  }
  if (strncmp(arg, "ready:", 6) == 0) {
    printf("[relay] provider ready: %s\n", arg + 6);
    fflush(stdout);
    gate_set(2);
    return;
  }
  if (strncmp(arg, "error:", 6) == 0) {
    fprintf(stderr, "[relay] shim error: %s\n", arg + 6);
    fflush(stderr);
    deliver_reply("");
    return;
  }
  printf("%s\n", arg);
  fflush(stdout);
  deliver_reply(arg);
}

static void pump_pending_prompt(void) {
  if (!g_pending) return;
  pending_prompt *p = g_pending;
  g_pending = NULL;
  if (g_w) relay_send_bundle_mac(p->text, p->attachments);
  else     deliver_reply("");
  free(p->text);
  if (p->attachments) cJSON_Delete(p->attachments);
  free(p);
}

static gpointer stdin_thread_mac(gpointer data) {
  (void)data;
  char *line = NULL;
  size_t cap = 0;
  ssize_t n;
  printf("relay> "); fflush(stdout);
  while ((n = getline(&line, &cap, stdin)) != -1) {
    while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r')) {
      line[--n] = '\0';
    }
    if (n > 0 && g_w) relay_send_bundle_mac(line, NULL);
    printf("relay> "); fflush(stdout);
  }
  free(line);
  return NULL;
}

int main(int argc, char **argv) {
  install_signal_traps();
  parse_args(argc, argv);

  char *chooser = build_chooser_data_url();

  struct webview w;
  memset(&w, 0, sizeof(w));
  w.title     = "Starting...";
  w.url       = chooser ? chooser : "";
  w.width     = 1024;
  w.height    = 768;
  w.resizable = 1;
  w.debug     = 1;
  w.external_invoke_cb = on_external_message;

  if (webview_init(&w) != 0) {
    fprintf(stderr, "webview_init failed\n");
    free(chooser);
    return 1;
  }
  free(chooser);
  g_w = &w;

  webview_add_init_script(&w, INJECT_JS);

  pthread_t http_tid;
  if (pthread_create(&http_tid, NULL, http_thread, NULL) != 0) {
    fprintf(stderr, "[relay] failed to start HTTP thread\n");
    return 1;
  }
  pthread_detach(http_tid);

  GThread *reader = g_thread_new("stdin-reader", stdin_thread_mac, NULL);
  (void)reader;

  printf("[relay] ready (cocoa).\n");
  fflush(stdout);

  while (webview_loop(&w, 1) == 0) {
    pump_pending_prompt();
  }

  webview_exit(&w);
  return 0;
}

#endif /* RELAY_MAC_OBJC */
#endif /* WEBVIEW_COCOA */