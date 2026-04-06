#include "http/HttpServer.h"
#include "AnthropicClient.h"

static const char* API_URL = "http://localhost:8002/v1/chat/completions";

static const char* HTML = R"HTML(<!DOCTYPE html>
<html>
<head><meta charset="utf-8"><title>SSE Chat</title>
<style>
* { box-sizing: border-box; margin: 0; padding: 0; }
body { font-family: sans-serif; display: flex; flex-direction: column; height: 100vh; background: #f5f5f5; }
#messages { flex: 1; overflow-y: auto; padding: 16px; display: flex; flex-direction: column; gap: 8px; }
.msg { max-width: 70%; padding: 10px 14px; border-radius: 12px; line-height: 1.5; white-space: pre-wrap; word-break: break-word; }
.user { align-self: flex-end; background: #0084ff; color: #fff; }
.bot  { align-self: flex-start; background: #fff; border: 1px solid #ddd; }
#form { display: flex; padding: 12px; gap: 8px; background: #fff; border-top: 1px solid #ddd; }
#input { flex: 1; padding: 10px; border: 1px solid #ccc; border-radius: 8px; font-size: 14px; }
button { padding: 10px 20px; background: #0084ff; color: #fff; border: none; border-radius: 8px; cursor: pointer; font-size: 14px; }
button:disabled { background: #aaa; }
</style>
</head>
<body>
<div id="messages"></div>
<form id="form">
  <input id="input" placeholder="输入消息..." autocomplete="off">
  <button id="btn">发送</button>
</form>
<script>
const messages = document.getElementById('messages');
const input = document.getElementById('input');
const btn = document.getElementById('btn');

function addMsg(text, cls) {
  const d = document.createElement('div');
  d.className = 'msg ' + cls;
  d.textContent = text;
  messages.appendChild(d);
  messages.scrollTop = messages.scrollHeight;
  return d;
}

document.getElementById('form').addEventListener('submit', e => {
  e.preventDefault();
  const q = input.value.trim();
  if (!q) return;
  input.value = '';
  btn.disabled = true;

  addMsg(q, 'user');
  const botDiv = addMsg('', 'bot');

  const es = new EventSource('/chat?q=' + encodeURIComponent(q));
  es.onmessage = ev => {
    botDiv.textContent += ev.data;
    messages.scrollTop = messages.scrollHeight;
  };
  es.addEventListener('done', () => { es.close(); btn.disabled = false; });
  es.addEventListener('error', ev => {
    botDiv.textContent = ev.data || '请求失败';
    es.close();
    btn.disabled = false;
  });
  es.onerror = () => { es.close(); btn.disabled = false; };
});
</script>
</body>
</html>)HTML";

static std::string urlDecode(const std::string& s)
{
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size()) {
            int v = std::stoi(s.substr(i+1, 2), nullptr, 16);
            out += static_cast<char>(v);
            i += 2;
        } else if (s[i] == '+') {
            out += ' ';
        } else {
            out += s[i];
        }
    }
    return out;
}

int main()
{
    http::HttpServer server(8080, "sse-chat");
    server.setWorkerThreads(4);

    server.Get("/", [](const http::HttpRequest&, http::HttpResponse* resp) {
        resp->setStatusCode(http::HttpResponse::k200Ok);
        resp->setContentType("text/html");
        resp->setBody(HTML);
    });

    // GET /chat?q=<message>
    server.Sse("/chat", [](const http::HttpRequest& req, const sse::SseConnectionPtr& sse) {
        std::string msg = urlDecode(req.getQueryParameters("q"));
        if (msg.empty()) {
            sse->send("missing ?q=", "error");
            sse->close();
            return;
        }
        streamChat(API_URL, msg, [&sse](const std::string& token) {
            sse->send(token);
        });
        sse->send("[DONE]", "done");
        sse->close();
    });

    server.start();
}
