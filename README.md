<p align="center">
  <img src="wisp-relay-p.png" alt="wisp-relay" width="50%">
</p>

<h1 align="center">wisp-relay</h1>

<p align="center">
  <strong>Expose any frontier LLM as a local endpoint</strong>
</p>

**Stop being locked into a single API.**
wisp-relay is a lightweight, local HTTP server that translates the OpenAI API standard into anything you want. Point your favorite coding tools at `http://localhost:35673/v1/chat/completions` and let wisp-relay route the traffic to your actual provider of choice.

**Bring your own key. No middleman.**
wisp-relay does not bundle API keys, does not add a billing layer, and does not collect telemetry. For API-based providers you pay them directly for what you use. For browser-driven providers you use your existing account (which may be free or paid); wisp-relay adds no billing layer of its own. The relay just handles the routing.

> **Note:** wisp-relay's browser-driven providers automate a provider's
> web chat, which some providers prohibit in their terms of service.
> Read [Terms of Service](#terms-of-service) at the end of this README
> before using them. For a fully ToS-compliant setup, see
> [Using OpenRouter](#using-openrouter-testing--legal-alternative).

---

## Supported Providers

wisp-relay fronts a real browser session against the provider's own web chat. You log in once, in the embedded WebView, and the relay drives the UI on your behalf.

<p align="center">
  <img src="docs/select.png" alt="wisp-relay provider chooser">
</p>

| Provider | Status | Notes |
|---|---|---|
| **DeepSeek** | ✅ Operational | `chat.deepseek.com`. Fast, long context, free tier. |
| **ChatGPT** | ✅ Operational | `chatgpt.com` / `chat.openai.com`. |
| **Qwen Chat** | ✅ Operational | `chat.qwen.ai`. Typed input for short prompts, file upload for very long ones. |
| **Gemini** | ✅ Operational | `gemini.google.com`. |
| Claude | 🔜 Planned | `claude.ai`. |
| Grok | 🔜 Planned | `grok.com`. |
| Le Chat | 🔜 Planned | `chat.mistral.ai`. |
| Copilot | 🔜 Planned | `copilot.microsoft.com`. |
| Perplexity | 🔜 Planned | `www.perplexity.ai`. |
| Kimi | 🔜 Planned | `kimi.moonshot.cn`. |
| Lumo | 🔜 Planned | `lumo.proton.me`. |

If you need an OpenAI-compatible HTTP provider instead of a browser front-end, see [Using OpenRouter](#using-openrouter-testing--legal-alternative) below.

---

## Using OpenRouter (testing / legal alternative)

If you want to use wisp-relay **without touching any provider's web UI**
— and therefore without the ToS concerns described at the end of this
README — point it at [OpenRouter](https://openrouter.ai) instead.
OpenRouter is an OpenAI-compatible aggregator with an official API, and
it explicitly permits programmatic access. It is the recommended path
for **testing** wisp-relay end-to-end without risking a provider account.

```sh
# 1. Get a key at https://openrouter.ai/keys
# 2. Start wisp-relay pointed at OpenRouter as an upstream
wisp-relay --upstream https://openrouter.ai/api/v1 \
           --upstream-key "$OPENROUTER_API_KEY"

# 3. Point your client at the local relay as usual
curl http://localhost:35673/v1/chat/completions \
  -H "Content-Type: application/json" \
  -d '{"model":"openai/gpt-4o-mini","messages":[{"role":"user","content":"hi"}]}'
```

> **Note:** the `--upstream` / `--upstream-key` flags shown above are the
> intended interface for an OpenAI-compatible upstream. If your build of
> wisp-relay does not yet expose them, use OpenRouter directly as your
> client's `api_url` (i.e. skip wisp-relay entirely) — the point is that
> OpenRouter is the safe, ToS-compliant way to test the client
> (`wisp-blend`) without browser automation.

This is **for testing purposes** and for users who want a fully
ToS-compliant setup. wisp-relay's browser-driven providers remain
available for personal experimentation, but OpenRouter is the path of
least resistance if you want to avoid the grey area entirely.

---

## Tool Calling

wisp-relay supports the OpenAI `tools[]` array end to end:

1. On the first turn of a session, the tool catalogue is rendered into a
   `--- TOOLS ---` preamble and prepended to the prompt.
2. The browser model is instructed to emit tool calls as a fenced
   ` ```tool_call ` block containing `{"name":...,"arguments":{...}}`.
3. The relay parses those blocks out of the reply, converts them into
   OpenAI-shaped `tool_calls[]`, sets `finish_reason: "tool_calls"`, and
   strips the fence from the visible content.
4. On the next turn, `tool` role messages and prior assistant
   `tool_calls` are replayed in the transcript as `[tool_result <id>]`
   and ` ```tool_call ` blocks so the model keeps context.

The tool preamble is re-sent whenever the provider page navigates or
reloads, because loading a URL wipes the browser conversation.

---

## How it Works

1. **Pick a provider.** Launch `wisp-relay`, choose a provider in the
   embedded window, and click Connect. Log in if the provider asks.
2. **Point your client.** Configure your tool (editor, script, chat UI)
   to use `http://127.0.0.1:35673/v1/chat/completions`.
3. **Go.** The client sends an OpenAI-format request. The relay injects
   the transcript into the chat UI, waits for the reply to finish
   (detected by a per-session terminator line the model is instructed to
   emit), and returns an OpenAI-shaped response.

No code changes on your client side. Multiple relay instances can run
side by side; the listener walks `35673..35688` and binds the first free
port, which is printed in the window title bar.

<p align="center">
  <img src="docs/deepseek.png" alt="wisp-relay talking to DeepSeek">
</p>

---

## Command-line options

```sh
wisp-relay [PORT] [--host ADDR] [--port PORT] [--listen-all]
```

---

## Terms of Service

wisp-relay automates the provider's **web chat interface** by driving a
browser session on your behalf. This behaviour may violate the terms of
service of some providers, which commonly prohibit "automated access",
"bots", or "programmatic extraction of outputs".

- You are solely responsible for reviewing and complying with each
  provider's terms before using wisp-relay with that provider.
- If a provider's terms prohibit automated access, **do not use
  wisp-relay with that provider** — use their official API instead, or
  use [OpenRouter](#using-openrouter-testing--legal-alternative) as
  described above.
- wisp-relay is provided **for personal experimentation and educational
  purposes only**. Use it at your own risk. The authors accept no
  liability for account restrictions, suspensions, or bans resulting
  from its use.
- **Recommended legal alternative:** use a provider that explicitly
  allows API access, such as [OpenRouter](https://openrouter.ai). It is
  a drop-in OpenAI-compatible endpoint with no browser automation
  involved.
