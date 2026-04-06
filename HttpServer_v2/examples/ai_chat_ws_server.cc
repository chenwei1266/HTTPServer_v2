#include "http/HttpServer.h"
#include "ws/WsConnection.h"
#include "AnthropicClient.h"

static const char* API_URL = "http://localhost:8002/v1/chat/completions";

static const char* HTML = R"HTML(<!DOCTYPE html>
<html>
<head><meta charset="utf-8"><title>WebSocket Chat</title>
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

const ws = new WebSocket('ws://' + location.host + '/chat');
let botDiv = null;

ws.onmessage = ev => {
  if (ev.data === '[DONE]') {
    botDiv = null;
    btn.disabled = false;
    return;
  }
  if (!botDiv) {
    botDiv = document.createElement('div');
    botDiv.className = 'msg bot';
    messages.appendChild(botDiv);
  }
  botDiv.textContent += ev.data;
  messages.scrollTop = messages.scrollHeight;
};

ws.onerror = () => { btn.disabled = false; };

document.getElementById('form').addEventListener('submit', e => {
  e.preventDefault();
  const q = input.value.trim();
  if (!q || ws.readyState !== WebSocket.OPEN) return;
  input.value = '';
  btn.disabled = true;

  const d = document.createElement('div');
  d.className = 'msg user';
  d.textContent = q;
  messages.appendChild(d);
  messages.scrollTop = messages.scrollHeight;

  ws.send(q);
});
</script>
</body>
</html>)HTML";

int main()
{
    http::HttpServer server(8081, "ws-chat");
    server.setWorkerThreads(4);

    server.Get("/", [](const http::HttpRequest&, http::HttpResponse* resp) {
        resp->setStatusCode(http::HttpResponse::k200Ok);
        resp->setContentType("text/html");
        resp->setBody(HTML);
    });

    server.Ws("/chat",
        /*onOpen*/    nullptr,
        /*onMessage*/ [](const ws::WsConnectionPtr& ws, const std::string& msg, ws::Opcode) {
            streamChat(API_URL, msg, [&ws](const std::string& token) {
                ws->sendText(token);
            });
            ws->sendText("[DONE]");
        },
        /*onClose*/   nullptr
    );

    server.start();
}
