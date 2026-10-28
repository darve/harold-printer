// Asks Claude for a short printout on a theme, over HTTPS from the ESP32.
//
// There is no Anthropic SDK for Arduino, so this is a raw POST to
// /v1/messages, with a small JSON reader for the one field we need.

#pragma once

#include <HTTPClient.h>
#include <WiFiClientSecure.h>

#include "certs.h"

#define CLAUDE_MODEL       "claude-opus-5-5"
#define CLAUDE_API_URL     "https://api.anthropic.com/v1/messages"
#define CLAUDE_RECENT_MAX  6  // recent printouts passed back so it doesn't repeat itself

static String claude_recent[CLAUDE_RECENT_MAX];
static int claude_recent_count;

static String json_escape(const String &s) {
  String out;
  out.reserve(s.length() + 16);
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    switch (c) {
      case '"':  out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': break;
      case '\t': out += ' '; break;
      default:
        if ((uint8_t) c >= 0x20) out += c;
    }
  }
  return out;
}

// The printers only have ASCII fonts. Swap the common typographic characters
// for plain ones and drop everything else outside ASCII.
static void append_codepoint(String &out, uint32_t cp) {
  if (cp == '\n' || (cp >= 0x20 && cp < 0x7F)) { out += (char) cp; return; }
  switch (cp) {
    case 0x2018: case 0x2019: case 0x201B: case 0x2032: out += '\''; break;
    case 0x201C: case 0x201D: case 0x201F: case 0x2033: out += '"'; break;
    case 0x2013: case 0x2014: case 0x2212: out += '-'; break;
    case 0x2026: out += "..."; break;
    case 0x00A0: case 0x2009: case 0x200A: case 0x202F: out += ' '; break;
    case 0x00E9: out += 'e'; break;
    default: break;
  }
}

// Decodes the JSON string starting at body[i] (just after the opening quote).
static String json_read_string(const String &body, int i) {
  String out;
  while (i < (int) body.length()) {
    char c = body[i++];
    if (c == '"') break;
    if (c != '\\') {
      uint8_t b = (uint8_t) c;
      if (b < 0x80) { out += c; continue; }
      // UTF-8 sequence
      int extra = b >= 0xF0 ? 3 : b >= 0xE0 ? 2 : 1;
      uint32_t cp = b & (0x3F >> extra);
      for (int k = 0; k < extra && i < (int) body.length(); k++) cp = (cp << 6) | ((uint8_t) body[i++] & 0x3F);
      append_codepoint(out, cp);
      continue;
    }
    char e = body[i++];
    switch (e) {
      case 'n': out += '\n'; break;
      case 't': out += ' '; break;
      case 'u': {
        uint32_t cp = strtoul(body.substring(i, i + 4).c_str(), nullptr, 16);
        i += 4;
        append_codepoint(out, cp);
        break;
      }
      case 'r': case 'b': case 'f': break;
      default: out += e; break;  // \" \\ \/
    }
  }
  return out;
}

// Finds `"key"` followed by optional spaces, a colon and optional spaces,
// starting at `from`. Returns the index just past the colon, or -1.
static int json_find_key(const String &body, const char *key, int from) {
  String needle = String('"') + key + '"';
  for (int i = body.indexOf(needle, from); i >= 0; i = body.indexOf(needle, i + 1)) {
    int j = i + needle.length();
    while (j < (int) body.length() && body[j] == ' ') j++;
    if (j < (int) body.length() && body[j] == ':') {
      j++;
      while (j < (int) body.length() && body[j] == ' ') j++;
      return j;
    }
  }
  return -1;
}

// Joins the text of every {"type":"text","text":"..."} block in the reply.
// Thinking blocks have no "text" key, so they're skipped.
static String claude_extract_text(const String &body) {
  String out;
  int from = 0;
  for (;;) {
    int t = json_find_key(body, "type", from);
    if (t < 0) break;
    from = t;
    if (body.substring(t, t + 6) != "\"text\"") continue;
    int v = json_find_key(body, "text", t + 6);
    if (v < 0 || body[v] != '"') continue;
    out += json_read_string(body, v + 1);
    from = v + 1;
  }
  out.trim();
  return out;
}

static String claude_error_message(const String &body) {
  int m = json_find_key(body, "message", 0);
  if (m < 0 || body[m] != '"') return body.substring(0, 120);
  return json_read_string(body, m + 1);
}

// Returns the printout text, or "" and sets `err`.
static String claude_ask(const String &api_key, const String &theme, unsigned cols, String &err) {
  if (api_key.isEmpty()) { err = "no API key set"; return ""; }
  if (WiFi.status() != WL_CONNECTED) { err = "no Wi-Fi"; return ""; }

  String system =
    "You write short, funny printouts for a little thermal printer with a big "
    "button on it, at Dave's place. Someone just pressed the button. Reply with "
    "only the printout: no preamble, no title, no markdown, no emoji, plain "
    "ASCII characters only. Keep it under 60 words. Keep it friendly and fine "
    "for anyone to read. Lines are " + String(cols) + " characters wide, so "
    "short lines and line breaks work well.";

  String user = "Write " + theme + ".";
  if (claude_recent_count) {
    user += "\n\nIt must be different from these recent ones:";
    for (int i = 0; i < claude_recent_count; i++) user += "\n---\n" + claude_recent[i];
  }
  user += "\n\n(Random seed for variety: " + String(esp_random() % 100000) + ")";

  // Opus 5.5 always thinks; low effort keeps a joke quick and cheap.
  // "fallbacks": "default" lets the API retry on another model if this one
  // declines, instead of returning a refusal.
  String req = String("{\"model\":\"" CLAUDE_MODEL "\",\"max_tokens\":4000,"
                      "\"output_config\":{\"effort\":\"low\"},"
                      "\"fallbacks\":\"default\","
                      "\"system\":\"") + json_escape(system) + "\","
               "\"messages\":[{\"role\":\"user\",\"content\":\"" + json_escape(user) + "\"}]}";

  WiFiClientSecure tls;
  tls.setCACert(ANTHROPIC_ROOT_CAS);
  HTTPClient http;
  http.setTimeout(45000);
  http.setConnectTimeout(10000);
  if (!http.begin(tls, CLAUDE_API_URL)) { err = "couldn't connect"; return ""; }
  http.addHeader("content-type", "application/json");
  http.addHeader("x-api-key", api_key);
  http.addHeader("anthropic-version", "2026-10-21");
  http.addHeader("anthropic-beta", "server-side-fallback-2026-07-01");

  int code = http.POST(req);
  String body = code > 0 ? http.getString() : String();
  http.end();

  if (code <= 0) { err = "request failed (" + HTTPClient::errorToString(code) + ")"; return ""; }
  if (code != 200) { err = "API error " + String(code) + ": " + claude_error_message(body); return ""; }

  int sr = json_find_key(body, "stop_reason", 0);
  if (sr >= 0 && body.substring(sr, sr + 9) == "\"refusal\"") { err = "Claude declined that one"; return ""; }

  String text = claude_extract_text(body);
  if (text.isEmpty()) { err = "empty reply"; return ""; }

  // Remember it, newest last.
  if (claude_recent_count == CLAUDE_RECENT_MAX) {
    for (int i = 1; i < CLAUDE_RECENT_MAX; i++) claude_recent[i - 1] = claude_recent[i];
    claude_recent_count--;
  }
  claude_recent[claude_recent_count++] = text;
  return text;
}

// Word-wraps to `cols` and escapes for the bridge's TEXT command.
static String wrap_for_printer(const String &text, unsigned cols) {
  String out, line, word;
  auto flush_word = [&]() {
    while (word.length() > cols) {  // a single word wider than a line
      if (line.length()) { out += line + "\\n"; line = ""; }
      out += word.substring(0, cols) + "\\n";
      word = word.substring(cols);
    }
    if (word.isEmpty()) return;
    if (line.length() && line.length() + 1 + word.length() > cols) { out += line + "\\n"; line = ""; }
    if (line.length()) line += ' ';
    line += word;
    word = "";
  };
  for (size_t i = 0; i <= text.length(); i++) {
    char c = i < text.length() ? text[i] : '\n';
    if (c == '\\') c = '/';
    if (c == ' ') { flush_word(); }
    else if (c == '\n') { flush_word(); out += line + "\\n"; line = ""; }
    else word += c;
  }
  while (out.endsWith("\\n")) out.remove(out.length() - 2);
  return out;
}
