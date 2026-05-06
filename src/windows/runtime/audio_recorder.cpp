#include "runtime/audio_recorder.h"

#include <QAudioDevice>
#include <QAudioFormat>
#include <QAudioSource>
#include <QIODevice>
#include <QMediaDevices>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace vinput::windows_app {

namespace {

constexpr int kAsrSampleRate = 16000;

float ClampSample(float sample) {
  return std::clamp(sample, -1.0f, 1.0f);
}

float ReadSample(const char *data, QAudioFormat::SampleFormat format) {
  switch (format) {
  case QAudioFormat::UInt8:
    return (static_cast<int>(*reinterpret_cast<const uint8_t *>(data)) - 128) /
           128.0f;
  case QAudioFormat::Int16:
    return static_cast<float>(*reinterpret_cast<const int16_t *>(data)) /
           32768.0f;
  case QAudioFormat::Int32:
    return static_cast<float>(*reinterpret_cast<const int32_t *>(data)) /
           2147483648.0f;
  case QAudioFormat::Float:
    return *reinterpret_cast<const float *>(data);
  default:
    return 0.0f;
  }
}

int16_t FloatToInt16(float sample) {
  sample = ClampSample(sample);
  return static_cast<int16_t>(
      std::lround(sample * static_cast<float>(std::numeric_limits<int16_t>::max())));
}

} // namespace

AudioRecorder::AudioRecorder(QObject *parent) : QObject(parent) {}

AudioRecorder::~AudioRecorder() { reset(); }

bool AudioRecorder::start(const QString &device_key, QString *error) {
  reset();

  QList<QAudioDevice> devices = availableInputDevices();
  if (devices.isEmpty()) {
    if (error) {
      *error = tr("No input microphone is available.");
    }
    return false;
  }

  QAudioDevice selected = QMediaDevices::defaultAudioInput();
  for (const auto &device : devices) {
    if (deviceKey(device) == device_key) {
      selected = device;
      break;
    }
  }

  QAudioFormat format;
  format.setSampleRate(kAsrSampleRate);
  format.setChannelCount(1);
  format.setSampleFormat(QAudioFormat::Int16);

  if (!selected.isFormatSupported(format)) {
    format = selected.preferredFormat();
    if (format.sampleRate() <= 0 || format.channelCount() <= 0 ||
        format.sampleFormat() == QAudioFormat::Unknown) {
      if (error) {
        *error = tr("The selected microphone does not expose a usable capture format.");
      }
      return false;
    }
  }

  capture_format_ = format;
  source_ = new QAudioSource(selected, format, this);
  device_ = source_->start();
  if (!device_) {
    if (error) {
      *error = tr("Failed to start microphone capture.");
    }
    reset();
    return false;
  }

  connect(device_, &QIODevice::readyRead, this, [this]() {
    pcm_bytes_.append(device_->readAll());
  });

  if (error) {
    error->clear();
  }
  return true;
}

std::vector<int16_t> AudioRecorder::stop() {
  if (source_) {
    source_->stop();
  }

  std::vector<int16_t> pcm = convertToAsrPcm();

  reset();
  return pcm;
}

bool AudioRecorder::isRecording() const { return source_ != nullptr; }

QList<QAudioDevice> AudioRecorder::availableInputDevices() {
  return QMediaDevices::audioInputs();
}

QString AudioRecorder::deviceKey(const QAudioDevice &device) {
  return QString::fromLatin1(device.id().toHex());
}

void AudioRecorder::reset() {
  pcm_bytes_.clear();
  capture_format_ = {};
  if (source_) {
    source_->stop();
    source_->deleteLater();
    source_ = nullptr;
  }
  device_ = nullptr;
}

std::vector<int16_t> AudioRecorder::convertToAsrPcm() const {
  if (!capture_format_.isValid() || pcm_bytes_.isEmpty()) {
    return {};
  }

  const int bytes_per_sample = capture_format_.bytesPerSample();
  const int channels = capture_format_.channelCount();
  const int source_rate = capture_format_.sampleRate();
  if (bytes_per_sample <= 0 || channels <= 0 || source_rate <= 0) {
    return {};
  }

  const int frame_bytes = bytes_per_sample * channels;
  const int frame_count = pcm_bytes_.size() / frame_bytes;
  if (frame_count <= 0) {
    return {};
  }

  std::vector<float> mono(static_cast<std::size_t>(frame_count));
  const char *raw = pcm_bytes_.constData();
  for (int frame = 0; frame < frame_count; ++frame) {
    float sum = 0.0f;
    const char *frame_ptr = raw + frame * frame_bytes;
    for (int channel = 0; channel < channels; ++channel) {
      sum += ReadSample(frame_ptr + channel * bytes_per_sample,
                        capture_format_.sampleFormat());
    }
    mono[static_cast<std::size_t>(frame)] = sum / static_cast<float>(channels);
  }

  if (source_rate == kAsrSampleRate) {
    std::vector<int16_t> pcm(mono.size());
    std::transform(mono.begin(), mono.end(), pcm.begin(), FloatToInt16);
    return pcm;
  }

  const double ratio = static_cast<double>(source_rate) / kAsrSampleRate;
  const auto output_count =
      static_cast<std::size_t>(std::floor(frame_count / ratio));
  std::vector<int16_t> pcm(output_count);
  for (std::size_t i = 0; i < output_count; ++i) {
    const double source_index = static_cast<double>(i) * ratio;
    const auto left = static_cast<std::size_t>(source_index);
    const auto right = std::min(left + 1, mono.size() - 1);
    const float t = static_cast<float>(source_index - left);
    const float sample = mono[left] * (1.0f - t) + mono[right] * t;
    pcm[i] = FloatToInt16(sample);
  }
  return pcm;
}

} // namespace vinput::windows_app
