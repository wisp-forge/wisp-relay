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
wisp-relay does not bundle API keys, does not add a billing layer, and does not collect telemetry. You pay your upstream provider directly for what you use. The relay just handles the routing.

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

If you need an OpenAI-compatible HTTP provider instead of a browser front-end, check the [Related](#related) section below.

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