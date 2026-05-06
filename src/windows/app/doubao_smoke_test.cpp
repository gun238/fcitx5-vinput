#include "runtime/doubao_asr_client.h"
#include "runtime/text_corrector.h"

#include <QCoreApplication>
#include <QFile>
#include <QString>

#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>

namespace {

uint16_t ReadLe16(const QByteArray &bytes, qsizetype offset) {
  return static_cast<uint16_t>(static_cast<uint8_t>(bytes[offset]) |
                               (static_cast<uint8_t>(bytes[offset + 1]) << 8));
}

uint32_t ReadLe32(const QByteArray &bytes, qsizetype offset) {
  return static_cast<uint32_t>(static_cast<uint8_t>(bytes[offset]) |
                               (static_cast<uint8_t>(bytes[offset + 1]) << 8) |
                               (static_cast<uint8_t>(bytes[offset + 2]) << 16) |
                               (static_cast<uint8_t>(bytes[offset + 3]) << 24));
}

bool LoadPcm16MonoWav(const QString &path, std::vector<int16_t> *pcm,
                      std::string *error) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    *error = "failed to open WAV file";
    return false;
  }
  const QByteArray bytes = file.readAll();
  if (bytes.size() < 44 || bytes.mid(0, 4) != "RIFF" ||
      bytes.mid(8, 4) != "WAVE") {
    *error = "input is not a RIFF/WAVE file";
    return false;
  }

  uint16_t audio_format = 0;
  uint16_t channels = 0;
  uint32_t sample_rate = 0;
  uint16_t bits_per_sample = 0;
  qsizetype data_offset = -1;
  uint32_t data_size = 0;

  qsizetype offset = 12;
  while (offset + 8 <= bytes.size()) {
    const QByteArray chunk_id = bytes.mid(offset, 4);
    const uint32_t chunk_size = ReadLe32(bytes, offset + 4);
    offset += 8;
    if (offset + chunk_size > bytes.size()) {
      break;
    }
    if (chunk_id == "fmt " && chunk_size >= 16) {
      audio_format = ReadLe16(bytes, offset);
      channels = ReadLe16(bytes, offset + 2);
      sample_rate = ReadLe32(bytes, offset + 4);
      bits_per_sample = ReadLe16(bytes, offset + 14);
    } else if (chunk_id == "data") {
      data_offset = offset;
      data_size = chunk_size;
    }
    offset += chunk_size + (chunk_size % 2);
  }

  if (audio_format != 1 || channels != 1 || sample_rate != 16000 ||
      bits_per_sample != 16 || data_offset < 0) {
    *error = "WAV must be PCM 16 kHz mono 16-bit";
    return false;
  }

  const uint32_t usable = data_size - (data_size % 2);
  pcm->resize(usable / 2);
  std::memcpy(pcm->data(), bytes.constData() + data_offset, usable);
  return true;
}

bool RunCorrectionSelfTest() {
  struct Case {
    std::string input;
    std::string expected;
  };
  const Case cases[] = {
      {"会议定在周三，不对，是周四。", "会议定在周四。"},
      {"我我我觉得这个这个方案呃不错", "我觉得这个方案不错"},
      {"呃，那个，今天下午开会。", "今天下午开会。"},
  };

  bool ok = true;
  for (const Case &test : cases) {
    const std::string actual = vinput::windows_app::correctRecognizedText(test.input);
    if (actual != test.expected) {
      std::cerr << "correction self-test failed: [" << test.input << "] -> ["
                << actual << "], expected [" << test.expected << "]\n";
      ok = false;
    }
  }
  return ok;
}

} // namespace

int main(int argc, char **argv) {
  QCoreApplication app(argc, argv);
  if (argc < 2) {
    std::cerr << "usage: vinput-doubao-smoke <pcm16-mono-16k.wav>\n";
    return 2;
  }

  std::vector<int16_t> pcm;
  std::string error;
  if (!LoadPcm16MonoWav(QString::fromLocal8Bit(argv[1]), &pcm, &error)) {
    std::cerr << error << "\n";
    return 2;
  }

  const auto result = vinput::windows_app::DoubaoAsrClient::recognize(pcm);
  if (!result.ok) {
    std::cerr << result.error << "\n";
    return 1;
  }
  const std::string corrected =
      vinput::windows_app::correctRecognizedText(result.text);
  std::cout << "ASR: " << result.text << "\n";
  std::cout << "Corrected: " << (corrected.empty() ? result.text : corrected)
            << "\n";
  if (!RunCorrectionSelfTest()) {
    return 4;
  }
  return result.text.empty() ? 3 : 0;
}
