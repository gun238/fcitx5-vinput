#pragma once

#include <QString>

#include <cstdint>
#include <string>
#include <vector>

namespace vinput::windows_app {

struct DoubaoAsrResult {
  bool ok = false;
  std::string text;
  std::string error;
};

class DoubaoAsrClient {
public:
  static DoubaoAsrResult recognize(const std::vector<int16_t> &pcm,
                                   int sample_rate = 16000);
  static DoubaoAsrResult recognize(const std::vector<int16_t> &pcm,
                                   const QString &api_key_path,
                                   int sample_rate = 16000);
  static QString apiKeyPath();
};

} // namespace vinput::windows_app
