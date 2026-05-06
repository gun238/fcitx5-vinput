#include "runtime/doubao_asr_client.h"

#include <QByteArray>
#include <QFile>
#include <QMap>
#include <QRegularExpression>
#include <QUuid>

#include <nlohmann/json.hpp>

#include <windows.h>
#include <winhttp.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>

namespace vinput::windows_app {

namespace {

constexpr const wchar_t *kWebSocketHost = L"openspeech.bytedance.com";
constexpr const wchar_t *kWebSocketPath =
    L"/api/v3/sauc/bigmodel_nostream";
constexpr const char *kResourceId = "volc.seedasr.sauc.duration";
constexpr int kChunkSamples = 16000 / 5; // 200 ms at 16 kHz.

struct Credentials {
  QString app_key;
  QString access_key;
};

QString CleanSecret(QString value) {
  value = value.trimmed();
  value.remove(QChar(0xfeff));
  QString cleaned;
  cleaned.reserve(value.size());
  for (const QChar ch : value) {
    if (ch.category() != QChar::Other_Control) {
      cleaned.append(ch);
    }
  }
  return cleaned.trimmed();
}

Credentials ReadCredentials(const QString &api_key_path, std::string *error) {
  QFile file(api_key_path);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    if (error) {
      *error = "failed to open Doubao API key file: " +
               api_key_path.toStdString();
    }
    return {};
  }

  const QString raw = QString::fromUtf8(file.readAll());
  QMap<QString, QString> fields;
  for (QString line : raw.split(QRegularExpression("[\\r\\n]+"),
                                Qt::SkipEmptyParts)) {
    line = CleanSecret(line);
    const int ascii_colon = line.indexOf(':');
    const int full_colon = line.indexOf(QChar(0xff1a));
    int colon = -1;
    if (ascii_colon >= 0 && full_colon >= 0) {
      colon = std::min(ascii_colon, full_colon);
    } else {
      colon = std::max(ascii_colon, full_colon);
    }
    if (colon < 0) {
      continue;
    }
    fields.insert(line.left(colon).trimmed().toLower(),
                  CleanSecret(line.mid(colon + 1)));
  }

  Credentials credentials;
  credentials.app_key =
      fields.value("appid", fields.value("app_key", fields.value("appkey")));
  credentials.access_key =
      fields.value("token",
                   fields.value("access_token",
                                fields.value("access_key", fields.value("ak"))));

  if (credentials.app_key.isEmpty() || credentials.access_key.isEmpty()) {
    if (error) {
      *error =
          "Doubao credential file must contain appid and access token/ak fields";
    }
  }
  return credentials;
}

void AppendBe32(QByteArray *bytes, qint32 value) {
  const auto u = static_cast<quint32>(value);
  bytes->append(static_cast<char>((u >> 24) & 0xff));
  bytes->append(static_cast<char>((u >> 16) & 0xff));
  bytes->append(static_cast<char>((u >> 8) & 0xff));
  bytes->append(static_cast<char>(u & 0xff));
}

qint32 ReadBe32(const QByteArray &bytes, qsizetype offset) {
  if (offset + 4 > bytes.size()) {
    return 0;
  }
  const auto b0 = static_cast<quint8>(bytes[offset]);
  const auto b1 = static_cast<quint8>(bytes[offset + 1]);
  const auto b2 = static_cast<quint8>(bytes[offset + 2]);
  const auto b3 = static_cast<quint8>(bytes[offset + 3]);
  return static_cast<qint32>((b0 << 24) | (b1 << 16) | (b2 << 8) | b3);
}

QByteArray BuildFullClientRequest(int sample_rate) {
  const nlohmann::json request = {
      {"user", {{"uid", "vinput-windows"}, {"platform", "Windows"}}},
      {"audio",
       {{"format", "pcm"},
        {"codec", "raw"},
        {"rate", sample_rate},
        {"bits", 16},
        {"channel", 1}}},
      {"request",
       {{"model_name", "bigmodel"},
        {"enable_nonstream", true},
        {"enable_punc", true}}},
  };

  const std::string payload_text = request.dump();
  QByteArray frame;
  frame.append('\x11'); // version 1, 4-byte header.
  frame.append('\x10'); // full client request, no sequence flag.
  frame.append('\x10'); // JSON, no compression.
  frame.append('\x00');
  AppendBe32(&frame, static_cast<qint32>(payload_text.size()));
  frame.append(payload_text.data(), static_cast<qsizetype>(payload_text.size()));
  return frame;
}

QByteArray BuildAudioRequest(const int16_t *samples, int sample_count,
                             int sequence, bool last) {
  const int byte_count = sample_count * static_cast<int>(sizeof(int16_t));
  QByteArray frame;
  frame.append('\x11');
  frame.append(last ? '\x23' : '\x21'); // audio request with sequence.
  frame.append('\x00');                 // no serialization, no compression.
  frame.append('\x00');
  AppendBe32(&frame, last ? -sequence : sequence);
  AppendBe32(&frame, byte_count);
  frame.append(reinterpret_cast<const char *>(samples), byte_count);
  return frame;
}

std::string ExtractTextFromJson(const nlohmann::json &json) {
  if (json.contains("result") && json["result"].contains("text") &&
      json["result"]["text"].is_string()) {
    return json["result"]["text"].get<std::string>();
  }
  if (json.contains("payload_msg") && json["payload_msg"].is_object()) {
    return ExtractTextFromJson(json["payload_msg"]);
  }
  if (json.contains("text") && json["text"].is_string()) {
    return json["text"].get<std::string>();
  }
  return {};
}

struct ParsedFrame {
  bool final = false;
  bool error = false;
  std::string text;
  std::string error_text;
};

std::string HexPreview(const QByteArray &bytes) {
  static constexpr char kHex[] = "0123456789abcdef";
  std::string out;
  const qsizetype count = std::min<qsizetype>(bytes.size(), 16);
  for (qsizetype i = 0; i < count; ++i) {
    const auto value = static_cast<uint8_t>(bytes[i]);
    if (!out.empty()) {
      out.push_back(' ');
    }
    out.push_back(kHex[value >> 4]);
    out.push_back(kHex[value & 0x0f]);
  }
  return out;
}

ParsedFrame ParseServerFrame(const QByteArray &frame) {
  ParsedFrame parsed;
  if (frame.size() < 8) {
    parsed.error = true;
    parsed.error_text = "Doubao ASR returned a truncated frame";
    return parsed;
  }

  const int header_size = (static_cast<quint8>(frame[0]) & 0x0f) * 4;
  const int message_type = (static_cast<quint8>(frame[1]) >> 4) & 0x0f;
  const int flags = static_cast<quint8>(frame[1]) & 0x0f;
  const int serialization = (static_cast<quint8>(frame[2]) >> 4) & 0x0f;
  const int compression = static_cast<quint8>(frame[2]) & 0x0f;

  qsizetype offset = header_size;
  if (flags == 1 || flags == 3) {
    const qint32 sequence = ReadBe32(frame, offset);
    parsed.final = sequence < 0;
    offset += 4;
  } else if (flags == 2) {
    parsed.final = true;
  }

  qint32 payload_size = ReadBe32(frame, offset);
  if ((payload_size < 0 || offset + 4 + payload_size > frame.size()) &&
      (flags == 1 || flags == 3)) {
    offset = header_size;
    payload_size = ReadBe32(frame, offset);
  }
  offset += 4;
  if (payload_size < 0 || offset + payload_size > frame.size()) {
    parsed.error = true;
    parsed.error_text = "Doubao ASR returned an invalid payload size";
    return parsed;
  }

  QByteArray payload = frame.mid(offset, payload_size);
  const bool empty_payload =
      payload.isEmpty() ||
      std::all_of(payload.begin(), payload.end(),
                  [](char ch) { return ch == '\0'; });
  if (empty_payload) {
    return parsed;
  }
  if (compression != 0) {
    parsed.error = true;
    parsed.error_text = "Doubao ASR returned compressed payload unexpectedly";
    return parsed;
  }

  if (message_type == 15) {
    parsed.error = true;
    parsed.error_text = payload.toStdString();
    return parsed;
  }
  if ((message_type != 9 && message_type != 11) || serialization != 1) {
    return parsed;
  }

  try {
    const auto json = nlohmann::json::parse(payload.toStdString());
    parsed.text = ExtractTextFromJson(json);
  } catch (const std::exception &ex) {
    parsed.error = true;
    parsed.error_text =
        std::string("failed to parse Doubao ASR websocket response: ") +
        ex.what() + " (message_type=" + std::to_string(message_type) +
        ", flags=" + std::to_string(flags) +
        ", serialization=" + std::to_string(serialization) +
        ", compression=" + std::to_string(compression) +
        ", payload_size=" + std::to_string(payload_size) +
        ", payload_hex=" + HexPreview(payload) + ")";
  }
  return parsed;
}

std::wstring ToWide(const QString &text) {
  return reinterpret_cast<const wchar_t *>(text.utf16());
}

std::string WinHttpError(const std::string &prefix, DWORD code) {
  LPSTR buffer = nullptr;
  const DWORD length = FormatMessageA(
      FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
          FORMAT_MESSAGE_IGNORE_INSERTS,
      nullptr, code, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
      reinterpret_cast<LPSTR>(&buffer), 0, nullptr);
  std::string message =
      length > 0 && buffer ? std::string(buffer, length)
                           : ("Windows error " + std::to_string(code));
  if (buffer) {
    LocalFree(buffer);
  }
  while (!message.empty() &&
         (message.back() == '\r' || message.back() == '\n')) {
    message.pop_back();
  }
  return prefix + ": " + message;
}

struct InternetHandleDeleter {
  void operator()(HINTERNET handle) const {
    if (handle) {
      WinHttpCloseHandle(handle);
    }
  }
};

using InternetHandle =
    std::unique_ptr<std::remove_pointer_t<HINTERNET>, InternetHandleDeleter>;

bool SendWebSocketFrame(HINTERNET socket, const QByteArray &frame,
                        std::string *error) {
  const DWORD code = WinHttpWebSocketSend(
      socket, WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE,
      const_cast<char *>(frame.constData()), static_cast<DWORD>(frame.size()));
  if (code != ERROR_SUCCESS) {
    if (error) {
      *error = WinHttpError("failed to send Doubao ASR websocket frame", code);
    }
    return false;
  }
  return true;
}

bool ReceiveWebSocketMessage(HINTERNET socket, QByteArray *message,
                             std::string *error) {
  message->clear();
  QByteArray buffer(65536, Qt::Uninitialized);

  while (true) {
    DWORD bytes_read = 0;
    WINHTTP_WEB_SOCKET_BUFFER_TYPE buffer_type;
    const DWORD code = WinHttpWebSocketReceive(
        socket, buffer.data(), static_cast<DWORD>(buffer.size()), &bytes_read,
        &buffer_type);
    if (code != ERROR_SUCCESS) {
      if (error) {
        *error = WinHttpError("failed to receive Doubao ASR websocket frame",
                              code);
      }
      return false;
    }
    if (buffer_type == WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE) {
      if (error) {
        *error = "Doubao ASR websocket was closed by the server";
      }
      return false;
    }
    if (bytes_read > 0) {
      message->append(buffer.constData(), static_cast<qsizetype>(bytes_read));
    }
    if (buffer_type == WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE ||
        buffer_type == WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE) {
      return true;
    }
    if (buffer_type != WINHTTP_WEB_SOCKET_BINARY_FRAGMENT_BUFFER_TYPE &&
        buffer_type != WINHTTP_WEB_SOCKET_UTF8_FRAGMENT_BUFFER_TYPE) {
      if (error) {
        *error = "Doubao ASR websocket returned an unsupported frame type";
      }
      return false;
    }
  }
}

} // namespace

DoubaoAsrResult DoubaoAsrClient::recognize(const std::vector<int16_t> &pcm,
                                           int sample_rate) {
  return recognize(pcm, apiKeyPath(), sample_rate);
}

DoubaoAsrResult DoubaoAsrClient::recognize(const std::vector<int16_t> &pcm,
                                           const QString &api_key_path,
                                           int sample_rate) {
  DoubaoAsrResult result;
  if (pcm.empty()) {
    result.error = "empty microphone capture";
    return result;
  }

  std::string key_error;
  const Credentials credentials = ReadCredentials(api_key_path, &key_error);
  if (credentials.app_key.isEmpty() || credentials.access_key.isEmpty()) {
    result.error = key_error;
    return result;
  }

  const std::string request_id =
      QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
  std::string best_text;
  std::string error_text;

  InternetHandle session(WinHttpOpen(
      L"fcitx5-vinput-windows/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
      WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
  if (!session) {
    result.error =
        WinHttpError("failed to initialize WinHTTP", GetLastError());
    return result;
  }

  InternetHandle connection(
      WinHttpConnect(session.get(), kWebSocketHost, INTERNET_DEFAULT_HTTPS_PORT,
                     0));
  if (!connection) {
    result.error =
        WinHttpError("failed to connect to Doubao ASR host", GetLastError());
    return result;
  }

  InternetHandle request(WinHttpOpenRequest(
      connection.get(), L"GET", kWebSocketPath, nullptr,
      WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE));
  if (!request) {
    result.error =
        WinHttpError("failed to create Doubao ASR request", GetLastError());
    return result;
  }

  DWORD timeout_ms = 70000;
  WinHttpSetOption(request.get(), WINHTTP_OPTION_CONNECT_TIMEOUT, &timeout_ms,
                   sizeof(timeout_ms));
  WinHttpSetOption(request.get(), WINHTTP_OPTION_RECEIVE_TIMEOUT, &timeout_ms,
                   sizeof(timeout_ms));
  WinHttpSetOption(request.get(), WINHTTP_OPTION_SEND_TIMEOUT, &timeout_ms,
                   sizeof(timeout_ms));

  if (!WinHttpSetOption(request.get(), WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET,
                        nullptr, 0)) {
    result.error = WinHttpError("failed to enable WebSocket upgrade",
                                GetLastError());
    return result;
  }

  const std::wstring headers =
      L"X-Api-App-Key: " + ToWide(credentials.app_key) + L"\r\n" +
      L"X-Api-Access-Key: " + ToWide(credentials.access_key) + L"\r\n" +
      L"X-Api-Resource-Id: " +
      std::wstring(kResourceId, kResourceId + std::strlen(kResourceId)) +
      L"\r\n" + L"X-Api-Connect-Id: " +
      std::wstring(request_id.begin(), request_id.end()) + L"\r\n";

  if (!WinHttpAddRequestHeaders(
          request.get(), headers.c_str(), static_cast<DWORD>(headers.size()),
          WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE)) {
    result.error =
        WinHttpError("failed to set Doubao ASR request headers", GetLastError());
    return result;
  }

  if (!WinHttpSendRequest(request.get(), WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                          WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
      !WinHttpReceiveResponse(request.get(), nullptr)) {
    result.error =
        WinHttpError("Doubao ASR websocket connection failed", GetLastError());
    return result;
  }

  DWORD status_code = 0;
  DWORD status_size = sizeof(status_code);
  WinHttpQueryHeaders(request.get(),
                      WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                      WINHTTP_HEADER_NAME_BY_INDEX, &status_code, &status_size,
                      WINHTTP_NO_HEADER_INDEX);
  if (status_code != 101 && (status_code < 200 || status_code >= 300)) {
    result.error =
        "Doubao ASR websocket returned HTTP " + std::to_string(status_code);
    return result;
  }

  InternetHandle socket(WinHttpWebSocketCompleteUpgrade(request.get(), 0));
  if (!socket) {
    result.error =
        WinHttpError("failed to complete Doubao ASR websocket upgrade",
                     GetLastError());
    return result;
  }
  request.reset();

  if (!SendWebSocketFrame(socket.get(), BuildFullClientRequest(sample_rate),
                          &error_text)) {
    result.error = error_text;
    return result;
  }

  int sequence = 2;
  for (std::size_t offset = 0; offset < pcm.size();
       offset += kChunkSamples, ++sequence) {
    const int count = static_cast<int>(
        std::min<std::size_t>(kChunkSamples, pcm.size() - offset));
    const bool last = offset + count >= pcm.size();
    if (!SendWebSocketFrame(
            socket.get(),
            BuildAudioRequest(pcm.data() + offset, count, sequence, last),
            &error_text)) {
      result.error = error_text;
      return result;
    }
  }

  bool finished = false;
  const bool debug = !qgetenv("VINPUT_DOUBAO_DEBUG").isEmpty();
  while (!finished) {
    QByteArray message;
    if (!ReceiveWebSocketMessage(socket.get(), &message, &error_text)) {
      if (!best_text.empty() &&
          error_text == "Doubao ASR websocket was closed by the server") {
        error_text.clear();
      }
      break;
    }
    const ParsedFrame frame = ParseServerFrame(message);
    if (debug) {
      std::cerr << "frame final=" << frame.final
                << " error=" << frame.error << " text=[" << frame.text
                << "] error_text=[" << frame.error_text << "]\n";
    }
    if (frame.error) {
      error_text = frame.error_text;
      break;
    }
    if (!frame.text.empty()) {
      best_text = frame.text;
    }
    finished = frame.final;
  }

  if (!error_text.empty()) {
    result.error = error_text;
    return result;
  }
  result.ok = true;
  result.text = best_text;
  return result;
}

QString DoubaoAsrClient::apiKeyPath() {
  return QStringLiteral("C:/Users/Administrator/Documents/apikeys/doubao.txt");
}

} // namespace vinput::windows_app
