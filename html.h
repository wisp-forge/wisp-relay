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

#ifndef HTML_H
#define HTML_H

/* ------------------------------------------------------------------ */
/*  Favicons (base64 PNG, data: URLs)                                 */
/*                                                                    */
/*  Each blob is the full data URL, ready for <img src="...">.        */
/*  Regenerate from *.png in the current directory with:              */
/*                                                                    */
/*    for f in *.png; do                                              */
/*      echo "=== $(basename "$f" .png) ==="                          */
/*      base64 -w0 "$f"; echo; echo;                                  */
/*    done                                                            */
/*                                                                    */
/*  and paste the results back here as new string literals.           */
/* ------------------------------------------------------------------ */

static const char FAVICON_chatgpt[] =
"data:image/png;base64,"
"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";

static const char FAVICON_claude[] =
"data:image/png;base64,"
"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";

static const char FAVICON_copilot[] =
"data:image/png;base64,"
"iVBORw0KGgoAAAANSUhEUgAAABAAAAAQCAYAAAAf8/9hAAAACXBIWXMAAAsSAAALEgHS3X78AAACiElEQVQ4EaVTzU8TURCf2tJuS7tQtlRb6UKBIkQwkRRSEzkQgyEc6lkOKgcOph78Y+CgjXjDs2i44FXY9AMTlQRUELZapVlouy3d7kKtb0Zr0MSLTvL2zb75eL838xtTvV6H/xELBptMJojeXLCXyobnyog4YhzXYvmCFi6qVSfaeRdXdrfaU1areV5KykmX06rcvzumjY/1ggkR3Jh+bNf1mr8v1D5bLuvR3qDgFbvbBJYIrE1mCIoCrKxsHuzK+Rzvsi29+6DEbTZz9unijEYI8ObBgXOzlcrx9OAlXyDYKUCzwwrDQx1wVDGg089Dt+gR3mxmhcUnaWeoxwMbm/vzDFzmDEKMMNhquRqduT1KwXiGt0vre6iSeAUHNDE0d26NBtAXY9BACQyjFusKuL2Ry+IPb/Y9ZglwuVscdHaknUChqLF/O4jn3V5dP4mhgRJgwSYm+gV0Oi3XrvYB30yvhGa7BS70eGFHPoTJyQHhMK+F0ZesRVVznvXw5Ixv7/C10moEo6OZXbWvlFAF9FVZDOqEABUMRIkMd8GnLwVWg9/RkJF9sA4oDfYQAuzzjqzwvnaRUFxn/X2ZlmGLXAE7AL52B4xHgqAUqrC1nSNuoJkQtLkdqReszz/9aRvq90NOKdOS1nch8TpL555WDp49f3uAMXhACRjD5j4ykuCtf5PP7Fm1b0DIsl/VHGezzP1KwOiZQobFF9YyjSRYQETRENSlVzI8iK9mWlzckpSSCQHVALmN9Az1euDho9Xo8vKGd2rqooA8yBcrwHgCqYR0kMkWci08t/R+W4ljDCanWTg9TJGwGNaNk3vYZ7VUdeKsYJGFNkfSzjXNrSX20s4/h6kB81/271ghG17l+rPTAAAAAElFTkSuQmCC";

static const char FAVICON_deepseek[] =
"data:image/png;base64,"
"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";

static const char FAVICON_gemini[] =
"data:image/png;base64,"
"iVBORw0KGgoAAAANSUhEUgAAAIAAAACACAMAAAD04JH5AAABiVBMVEVHcEyPl5iTkKTatS6Pv1/pZFjocVDxpyGS0Vyw0Uk/i/BLlOjmvSTZxS43iP3ubEf3bDzewi82iP83iP0xhv8vvmPjxSjyUE31SkoluXXTyzVIvnX1SksRuWYbtIA0v2b2mCJyy2PwfzSKfs03o8msc6tph+kxhv8yh/82h/89h/01if07ke44i/k4jfVHifk7leVViPE7mdo7nc5nh+X2vhgUuWc6ocPdW2zrUlrwTVF3hNlGqaz4RUQlsoUdtnR9yWfnWF5QspX0sxuFgs2SfMHqxyNdvIBEpbn0SUvTYHliwXUwqaisdKdWt4pHjfLKZoQ0u3Jwxm3Ba480pbWgeLQrrpeh0VOLy162b5w+tYdCrp5xwXJTraDXzTFLwHDC0D7rmSxdj+Spwkt2jtHaaW5NmduohKNRn8jkfUu/eo3rijbDwzrTd3HXpzl1p5t1m7p2uHezmXNkl9LGh3VfpLO9tUXQmE6UhrnXiVWjsGCtj4+Kkbhpsoq6pViSpIeMsHSUmZ5HcEz0t47pAAAAg3RSTlMACQP9/v7+/P7+Q/79+qZEH2YfbN4gpWykbzBJ2aHa4uatrbGwYN//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////ADoltewAAAh5SURBVHic7Zv5VxJrGMeHAbeUgFvulrdu97IobhiaFpiYS6lImSnuoqgEYm64ov7n93neZWZYVFRm5tx7+pKnc/rl83m/z/vOjECC8Du/85+OaK2rs+opYP0A0dFArEOBOv0EBOR/GNOPb6UC+s2glvDHanUTaCL8sSa9+AZawFifQSeBWsbv02sGdAJ9fX06zcDKC3j3Tp9zUCfx3+lzLZL5H3v14NeyDYD8Xj224ZiigN7P2vMtEr8X+J8tmgs0KdYPAlNa8y1jfAN8JPwprStoYnw6gKmpb3Pa8i3yAaD8b3N/ayrA188EgD83oSW/Vp4/G8DcxISGFYjy+j8z/tzE9++iZgJ10v7jGwD53//Sim/J6p/xP316opFAHv4E8oeGNMGLdWT8uXwQ0GQIlpv5/f0aDMFA6r+B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";

static const char FAVICON_grok[] =
"data:image/png;base64,"
"iVBORw0KGgoAAAANSUhEUgAAAIAAAACACAMAAAD04JH5AAAAP1BMVEVHcEwFBQUJCQn8/PwGBgYFBQUFBQUSEhIpKSkFBQUFBQUAAAD///8sLCzr6+vV1dVRUVFsbGy6urqjo6OFhYUN5RCmAAAACnRSTlMAnRf/NG3ECQTlEImoaQAACCtJREFUeJzFW4uWoyAM1fqoioqv///WTQAxKARsp27O7JzZVriXkIQQIcu80jTNGwR+v8qyrKoqz2uQVkkXFPU1PljnOTSCpq931qiOmsaPFIaHFiXiKjxhMQT5+yJC4D8hTBMBbIBJ2WTNHQ4IjyOvTtAfiFA/bZuXL+w0jQKOHsZefwN8ZVKDHpQWUuCbV8XM84cMurZ664mNav9V/TW64dBWr4ZXgtb+3yrfkbrMOCUo1/vR8Hep3mEGSv35b/G7Lg8yQPxfqt+IqMvGy0Dh/7nx+6Qtfab4HP7O4P/h+xg8iS8Ug5Mlov/93v4UvBiQQf0CBg7+7/3PyKwIdLnDACag+m7ZSxQh5tngVIcZYAAsH0BH/G0V+0BLqwJMex4ygFkeioZJMCpQE/CITONEJhonYfeARzxQTMXmGJpRwVMKENMohUOgylAFYAGPhCAYfzG4ngbhqFH56hMKgPH3q6uATlToCF+4gKASx5fD+SGIh032aQxA0GGa5nmb53kYWA4C8fvp+gDEArCD+0FYgW+LlONYgIzjKOU6hxUhBtn3i+c78MTsnd01QRz5CiMq4EcL/gkybtPgoSAU/niZAMyTkcC9GcCxr/LApgIfym24qEEMCzBcvcrBreMtH0D4sfChW9lOWsDxF14FdDgH2S0TQPiexweslYKJDsYfUkCXgxekEwBjll7dn2dCTlYJQiyqhV8BIn9n6VEA5zIOrynsQV90EpuAC/g1UL+y1Dishp8ED1O0zJ2pEejxF3MgSLRlKgHIZcbE4RfLboY7vicI7gIEEvG3q+31VOyny7QXUUS36o+9QUg/UmZJXgjjP+Nj4FkwDKNsy2h4LNT+NtOmD80ALskpBLArF78fMeR0ZkFSxaENAvNKSkhCrDZShtcJIJCQDZ/H3xfr3O3zvHcOVjoRIJw0yzaMkWcJYUAMFL9XkcY3JvoZNZqwCaQRULGUEJATW6rb8Y9JC4VBTSAah8hcqvFvKVsYx2n6LdyiTiAwU/wxbNAh/C8JiM4JQJ605trkZLSMFyIBvkdnAvoxBb87B80IgUhn09FZGr6gTaIE2lhCZlbTaEekxXBetDgbiBIYCP45r/fjn8f/HQFiAcySRhsM10WTiwMxAqQ7X15/xb+On4+EQIDr1q5nfC/H85f517rjCPA9EhNM8kCD39tf+r9MU5YAWYWCWZ3z+I6PWaldDOH/TPTmCdgonBIC9gS47zEvpxGMMx+egJ0Bbhr3h/cEEJYLnaokLYccASGsScdjkB2/3RSIwyAZ+iyBYbTTGIsBBh+SQvvk4UJcCGEJ7HYE3UaSgH38xXbYKhZl4hbEEjiySmYSNf5Kp998SIJCeAYZAoIsREw07+yaTdSv5bDhcPs0ArwT6gQUUuVzEWpJ0GCYgCAqDOzu9wdnLJcU0xlfz4shEOqA04Al0EvGBs34PYZOdgbhOMoZYRIBlQDq4Hf5av6OANVAsL3aAPbj5t2ofEng0EDBEJjdiojD7c8IhDUgpl7VA/xffmkDCUaoEqCghZMV+UMCiyUQqDFBxuYEv9PXKZE0KRCFYjngy3CNmASyzyIh3RP4YjlOkc/77Pffh+Ijknk6AP0z6nd2SNyWltXAsZ5el2N8A+P1vuOJ+ct8QNBNxrUHGfI+2/ywQSaS8imZDKVkMP/e4Oe0PhI6Zk/BJ6VkDKcK+MpNv5bpSEqZjJIncPThOKLYX0FzbYkPMWW6jtsbEiPo3WA6xOtEZJfI5PSC35w65REnFiXsUsi+5PPtuZhtYs4syd6W1oVZJ4xoYC/3R8dxbUh2JWxhI1ohIYpMq9AZfFpb5DLaaIXk8IPgiydfM1In47eVsSoZ2WGCcLmp24rUSfjKShutEzoVxzQzcOokkdJWvE5I8dckDVADjBYWbhGIRX/TwqkTxfb1daxcb6eg7+PRv+vOL9eilZ3Y+wLRkZwijo9vbmidLl5bjBIwSUVaELi+Wo21ErGXViag9G5PvkMT6kzBciqTx72GJ2A3yO5iLIZZym3qLAv1xzQvvTv8lOJyxb+41FsLx5UAapb6reE6D0YXw7Tim8OicPETvJZ/c2pevcsDH4aE8Notejy8oaXwHGlIKG0qAlxGMruWjCdHHCu7vLc9vkmLGm3Jvb5XIc3B32Tk9MLBLC1q1a8sfJJSbUxIJMHXV6kv8CNbBiv5O3yEQx/+oRMppjVJAbBhTl248QhHyA/Vmuo4EjpA5ASLgk8dfocnvbMm4AaY1JwdGT1u44+R9D3sWFKPh4q2xJNUfjfA2s81kAgVhbx2r9B7eT7Fw4o6yORfDXARCBky2IIKRWfwQuLxhTu5c44EvEepwACZ8iyqYbWHJrSMUgXGG+idOczmOc6Hq9rIWZJaBIZh2tZlkXJZt3kYunuDx17weLP3QCOsQSz+TuLOiUKfmAONlyOdgJ/2jsxSuY+Nok6Yq3PtrgogAqfvAL4Rc7gaD5Y7+N36DH5rjpefbrZAALq3Df1Y1H0Xc7ScOALgPzJ+csLfOdwu5kjt6c/kuOOgbneZSYAg94z+6fF+PQkqIKcUf/5G1AUHe8PhMIPbwfRDqV/uJRP0hOcu2egQ9HYuuTzL4IL/sA40/vWm00MMhBd/Z/DAXZPaj28Y/Py6kUD/C9/3Awa/vvAYxjeXnX9651CpP3bn8/2DO7dGzPC5i7c/pNAq+Ni9X8Pg/dcXn7u6MtqP3702SniV+V+pQV39fqfioy1qLYA92svvH2Ory++Y/Keon3IwLVATx/X/tuav/4vW3v/P8f4/Xv/XQw+P/h97Mz+/WEsMdAAAAABJRU5ErkJggg==";

static const char FAVICON_kimi[] =
"data:image/png;base64,"
"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";

static const char FAVICON_mistral[] =
"data:image/png;base64,"
"iVBORw0KGgoAAAANSUhEUgAAAIAAAACACAMAAAD04JH5AAAAVFBMVEX6UA/////6SQD9xLb6TwD9zL76TAD6cEH6QgD+49r/+/n7kHD908b9x7r+59/6bj38m4P8oYj+29H7hWH6Zi77imL8q5L+6+X9wKz7jWv8po78sZxZjIWGAAABK0lEQVR4nO3ayw6CMBRFUSlQUCn4fv7/f+rMezQ2YCgas/e0FtbsQnE2IyIiIiIiol/OO5ufYOPTZVYhfxTWRd+NxVo2rj4VuJDZyv6AUjYG9ykgHweQAwAAAACAsQCxEafzLw1gs7RtReC3srhJAsgWpmxe25/Wc11NA5BeAO8DAAAAAACpAN70BUC7q0y7dnLAgAAAAAAAAIC/ATSdmb/dXm+yl8UmCaC0TyCuU0Dn7OoEBxS+UkBl39anOCEBAAAAgL8FFLYXgOkOkJPJytvVZ4BcNnr/Q2k7ynWOsnY627PZ86n3xkNMUOgcjdXW9nS6jr0da81IgAEHFAAAAAAAIA2gVcBYs+Bi/38TK1xlyLtr742X6DzWL7Cxop9uB2wkIiIiIiKir3cD+u4lbQggGqAAAAAASUVORK5CYII=";

static const char FAVICON_perplexity[] =
"data:image/png;base64,"
"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";

static const char FAVICON_qwen[] =
"data:image/png;base64,"
"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";

static const char FAVICON_lumo[] =
"data:image/png;base64,"
"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";

/* ------------------------------------------------------------------ */
/*  Provider list                                                     */
/*                                                                    */
/*  Add a provider by adding one entry here and a matching            */
/*  FAVICON_<id> constant above. Nothing in relay.c needs to change.  */
/* ------------------------------------------------------------------ */

typedef struct {
    const char *id;
    const char *name;
    const char *url;
    const char *host;
    const char *note;
    const char *bg;
    const char *fg;
    const char *icon;
} provider_t;

static const provider_t g_providers[] = {
    { "deepseek",  "DeepSeek",   "https://chat.deepseek.com",
      "chat.deepseek.com",     "Free, long context", "#4D6BFE", "#FFFFFF",
      FAVICON_deepseek },
    { "chatgpt",   "ChatGPT",    "https://chat.openai.com",
      "chat.openai.com",       "OpenAI",             "#10A37F", "#FFFFFF",
      FAVICON_chatgpt },
    { "claude",    "Claude",     "https://claude.ai",
      "claude.ai",             "Anthropic",          "#D97757", "#FFFFFF",
      FAVICON_claude },
    { "gemini",    "Gemini",     "https://gemini.google.com",
      "gemini.google.com",     "Google",             "#4285F4", "#FFFFFF",
      FAVICON_gemini },
    { "grok",      "Grok",       "https://grok.com",
      "grok.com",              "xAI",                "#000000", "#FFFFFF",
      FAVICON_grok },
    { "mistral",   "Le Chat",    "https://chat.mistral.ai",
      "chat.mistral.ai",       "Mistral",            "#FF7000", "#FFFFFF",
      FAVICON_mistral },
    { "copilot",   "Copilot",    "https://copilot.microsoft.com",
      "copilot.microsoft.com", "Microsoft",          "#0078D4", "#FFFFFF",
      FAVICON_copilot },
    { "perplexity","Perplexity", "https://www.perplexity.ai",
      "www.perplexity.ai",     "Search + chat",      "#20808D", "#FFFFFF",
      FAVICON_perplexity },
    { "kimi",      "Kimi",       "https://kimi.moonshot.cn",
      "kimi.moonshot.cn",      "Moonshot",           "#1F1F1F", "#FFFFFF",
      FAVICON_kimi },
    { "qwen",      "Qwen Chat",  "https://chat.qwen.ai",
      "chat.qwen.ai",          "Alibaba",            "#615CED", "#FFFFFF",
      FAVICON_qwen },
    { "lumo",      "Lumo",       "https://lumo.proton.me",
      "lumo.proton.me",        "Proton",             "#6D4AFF", "#FFFFFF",
      FAVICON_lumo },
    { NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL }
};

/* ------------------------------------------------------------------ */
/*  Chooser page HTML                                                 */
/*                                                                    */
/*  Stored as a single string constant. build_chooser_data_url() in   */
/*  relay.c wraps it in a data: URL and percent-encodes it. The       */
/*  provider cards are generated from g_providers[] at runtime, so    */
/*  this string only holds the shell — header, styles, script, and    */
/*  the grid container. Cards are injected before </div>.             */
/* ------------------------------------------------------------------ */

static const char CHOOSER_HTML_HEAD[] =
"<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
"<title>relay — choose a provider</title><style>"
":root{"
"--bg-0:#0F1115;--bg-1:#15181E;--bg-2:#1B1F27;"
"--line:rgba(255,255,255,0.08);"
"--text:#E6E8EC;--text-bright:#F5F6F8;--text-dim:#9AA1AD;--text-mute:#5A616E;"
"--accent:#7C8CFF;--accent-bright:#A5B1FF;--accent-soft:#5A6BD8;"
"--accent-tint:rgba(124,140,255,0.10);"
"}"
"*{box-sizing:border-box}"
"html,body{margin:0;height:100%;background:var(--bg-0);"
"color:var(--text);font-family:-apple-system,BlinkMacSystemFont,"
"'Segoe UI',Roboto,sans-serif}"
"body{display:flex;align-items:center;justify-content:center;"
"padding:40px 20px}"
"body::before{content:'';position:fixed;inset:0;pointer-events:none;"
"background:radial-gradient(ellipse at 50% -10%,"
"rgba(124,140,255,0.10),transparent 60%)}"
".wrap{position:relative;max-width:960px;width:100%}"
"h1{font-size:22px;font-weight:600;color:var(--text-bright);"
"margin:0 0 6px}"
".sub{color:var(--text-dim);font-size:14px;margin:0 0 28px}"
".grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(240px,1fr));"
"gap:12px}"
".card{position:relative;display:flex;align-items:flex-start;gap:14px;"
"padding:16px 18px;background:var(--bg-1);border:1px solid var(--line);"
"border-radius:10px;cursor:pointer;"
"transition:border-color .15s,background .15s,transform .15s}"
".card:hover{border-color:var(--accent-soft);background:var(--bg-2);"
"transform:translateY(-1px)}"
".card.selected{border-color:var(--accent);"
"background:var(--accent-tint)}"
".card.dimmed{opacity:.55}"
".icon{position:relative;flex:0 0 40px;width:40px;height:40px;"
"border-radius:10px;display:flex;align-items:center;"
"justify-content:center;overflow:hidden}"
".icon img{width:24px;height:24px;display:block;position:absolute;"
"object-fit:contain;background:transparent}"
".icon .mono{font-size:18px;font-weight:700;letter-spacing:-.02em;"
"user-select:none}"
".body{flex:1;min-width:0}"
".card .name{font-size:15px;font-weight:600;color:var(--text-bright);"
"margin:0 0 4px}"
".card .note{font-size:12px;color:var(--text-mute);margin:0}"
".bar{margin-top:28px;display:flex;align-items:center;gap:16px}"
"button{padding:10px 22px;background:var(--accent);color:#fff;"
"border:1px solid var(--accent);border-radius:8px;font-size:14px;"
"font-weight:600;cursor:pointer;transition:background .15s}"
"button:hover{background:var(--accent-bright);"
"border-color:var(--accent-bright)}"
"button:disabled{opacity:.4;cursor:not-allowed}"
".status{font-size:13px;color:var(--text-dim)}"
"</style></head><body><div class=\"wrap\">"
"<h1>Choose a provider</h1>"
"<p class=\"sub\">The relay will open the selected chat in this window.</p>"
"<div class=\"grid\" id=\"grid\">";

static const char CHOOSER_HTML_TAIL[] =
"</div>"
"<div class=\"bar\">"
"<button id=\"go\" disabled onclick=\"connect()\">Connect</button>"
"<span class=\"status\" id=\"status\"></span>"
"</div>"
"<script>"
"var chosen=null;"
"function iconFallback(img){img.style.display='none';}"
"function pick(el){"
"  var all=document.querySelectorAll('.card');"
"  for(var i=0;i<all.length;i++) all[i].classList.remove('selected');"
"  el.classList.add('selected');"
"  chosen=el.getAttribute('data-id');"
"  var active=el.getAttribute('data-active')==='1';"
"  var s=document.getElementById('status');"
"  s.textContent=(active?'Selected: ':'Not wired up yet: ')"
"                +el.getAttribute('data-name');"
"  document.getElementById('go').disabled=!active;"
"}"
"function connect(){"
"  if(!chosen) return;"
"  document.getElementById('go').disabled=true;"
"  document.getElementById('status').textContent='Connecting…';"
"  try{"
"    if(window.webkit&&window.webkit.messageHandlers&&"
"        window.webkit.messageHandlers.external)"
"      window.webkit.messageHandlers.external.postMessage('connect:'+chosen);"
"    else"
"      document.getElementById('status').textContent='No host bridge.';"
"  }catch(e){"
"    document.getElementById('status').textContent='Error: '+e;"
"    document.getElementById('go').disabled=false;"
"  }"
"}"
"</script>"
"</body></html>";

#endif

