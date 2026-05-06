#include "runtime/text_corrector.h"

#include <QChar>
#include <QRegularExpression>
#include <QString>
#include <QStringList>
#include <QVector>

namespace vinput::windows_app {
namespace {

struct Segment {
  QString text;
  QString punct;
};

QStringList FillerWords() {
  return {
      QStringLiteral("呃"),       QStringLiteral("嗯"),
      QStringLiteral("额"),       QStringLiteral("啊"),
      QStringLiteral("那个"),     QStringLiteral("这个"),
      QStringLiteral("就是说"),   QStringLiteral("就是"),
      QStringLiteral("然后那个"), QStringLiteral("然后呢"),
      QStringLiteral("怎么说呢"), QStringLiteral("其实呢"),
      QStringLiteral("对了"),
  };
}

QStringList CorrectionMarkers() {
  return {
      QStringLiteral("我说错了"), QStringLiteral("更准确地说"),
      QStringLiteral("准确地说"), QStringLiteral("我的意思是"),
      QStringLiteral("我想起来了"), QStringLiteral("应该是"),
      QStringLiteral("不对"),     QStringLiteral("不是"),
      QStringLiteral("错了"),
  };
}

bool IsSeparator(const QChar ch) {
  static const QString separators = QStringLiteral("。！？!?；;，,\n\r");
  return separators.contains(ch);
}

QString NormalizeWhitespace(QString text) {
  text.replace(QRegularExpression(QStringLiteral("\\s+")), QStringLiteral(" "));
  text.replace(QRegularExpression(QStringLiteral("\\s*([，。！？；：])\\s*")),
               QStringLiteral("\\1"));
  return text.trimmed();
}

bool ContainsCjk(const QString &text) {
  for (const QChar ch : text) {
    const ushort unicode = ch.unicode();
    if (unicode >= 0x4E00 && unicode <= 0x9FFF) {
      return true;
    }
  }
  return false;
}

QVector<Segment> SplitSegments(const QString &text) {
  QVector<Segment> segments;
  QString current;
  for (const QChar ch : text) {
    if (IsSeparator(ch)) {
      segments.push_back({current.trimmed(), QString(ch)});
      current.clear();
    } else {
      current.append(ch);
    }
  }
  if (!current.trimmed().isEmpty()) {
    segments.push_back({current.trimmed(), QString()});
  }
  return segments;
}

QString TrimLeadingNoise(QString text) {
  text = text.trimmed();
  while (!text.isEmpty() &&
         (text.front().isSpace() || QStringLiteral("，,。.!！？?；;：:").contains(text.front()))) {
    text.remove(0, 1);
  }
  return text.trimmed();
}

bool IsFillerOnly(const QString &text) {
  const QString trimmed = TrimLeadingNoise(text);
  if (trimmed.isEmpty()) {
    return true;
  }
  for (const QString &word : FillerWords()) {
    if (trimmed == word) {
      return true;
    }
  }
  return false;
}

QString StripFillerPrefix(QString text) {
  text = TrimLeadingNoise(text);
  bool changed = true;
  while (changed) {
    changed = false;
    for (const QString &word : FillerWords()) {
      if (text == word) {
        return QString();
      }
      if (text.startsWith(word) && text.size() > word.size()) {
        text.remove(0, word.size());
        text = TrimLeadingNoise(text);
        changed = true;
        break;
      }
    }
  }
  return text;
}

QString StripCorrectionIntro(QString text) {
  text = TrimLeadingNoise(text);
  const QStringList intros = {
      QStringLiteral("应该是"), QStringLiteral("是"),
      QStringLiteral("改成"),   QStringLiteral("改为"),
      QStringLiteral("说"),     QStringLiteral("就是"),
  };
  bool changed = true;
  while (changed) {
    changed = false;
    for (const QString &intro : intros) {
      if (text.startsWith(intro) && text.size() > intro.size()) {
        text.remove(0, intro.size());
        text = TrimLeadingNoise(text);
        changed = true;
        break;
      }
    }
  }
  return text;
}

QString CollapseImmediateRepeats(QString text) {
  if (text.isEmpty()) {
    return text;
  }

  QString collapsed_chars;
  const QString repeatable_chars = QStringLiteral("我你他她它这那就不嗯呃额啊");
  for (int i = 0; i < text.size(); ++i) {
    collapsed_chars.append(text[i]);
    if (!repeatable_chars.contains(text[i])) {
      continue;
    }
    while (i + 1 < text.size() && text[i + 1] == text[i]) {
      ++i;
    }
  }
  text = collapsed_chars;

  for (int i = 0; i < text.size();) {
    bool collapsed = false;
    for (int len = 4; len >= 2; --len) {
      if (i + len * 2 > text.size()) {
        continue;
      }
      const QString phrase = text.mid(i, len);
      if (phrase.trimmed().isEmpty() || phrase.contains(QRegularExpression(QStringLiteral("\\s")))) {
        continue;
      }
      if (text.mid(i + len, len) == phrase) {
        text.remove(i + len, len);
        collapsed = true;
        break;
      }
    }
    if (!collapsed) {
      ++i;
    }
  }
  return text;
}

QString ApplyCorrectionPrefix(QString prefix, QString correction) {
  prefix = StripFillerPrefix(prefix);
  correction = StripCorrectionIntro(StripFillerPrefix(correction));
  if (correction.isEmpty()) {
    return prefix;
  }

  const QString connectors = QStringLiteral("在是为叫到用成：:");
  int best = -1;
  for (const QChar connector : connectors) {
    const int index = prefix.lastIndexOf(connector);
    if (index > best) {
      best = index;
    }
  }

  if (best >= 0 && best + 1 < prefix.size()) {
    return prefix.left(best + 1) + correction;
  }
  return correction;
}

QString CorrectSegmentText(QString text) {
  text = StripFillerPrefix(text);
  text.replace(QStringLiteral("呃"), QString());
  text.replace(QStringLiteral("嗯"), QString());
  text.replace(QStringLiteral("额"), QString());
  text = CollapseImmediateRepeats(text);
  return text.trimmed();
}

} // namespace

std::string correctRecognizedText(const std::string &text) {
  QString input = NormalizeWhitespace(QString::fromUtf8(text.data(), static_cast<int>(text.size())));
  if (input.isEmpty()) {
    return {};
  }
  if (!ContainsCjk(input)) {
    return input.toUtf8().toStdString();
  }

  const QVector<Segment> segments = SplitSegments(input);
  QVector<Segment> output;
  QString pending_correction_prefix;

  for (Segment segment : segments) {
    segment.text = CorrectSegmentText(segment.text);
    if (IsFillerOnly(segment.text)) {
      continue;
    }

    bool handled_marker = false;
    for (const QString &marker : CorrectionMarkers()) {
      const int index = segment.text.indexOf(marker);
      if (index < 0) {
        continue;
      }

      const QString before = segment.text.left(index).trimmed();
      QString after = segment.text.mid(index + marker.size()).trimmed();
      if (!before.isEmpty()) {
        pending_correction_prefix = before;
      } else if (!output.empty()) {
        pending_correction_prefix = output.back().text;
        output.pop_back();
      }

      after = CorrectSegmentText(StripCorrectionIntro(after));
      if (!after.isEmpty()) {
        output.push_back({ApplyCorrectionPrefix(pending_correction_prefix, after), segment.punct});
        pending_correction_prefix.clear();
      }
      handled_marker = true;
      break;
    }
    if (handled_marker) {
      continue;
    }

    if (!pending_correction_prefix.isEmpty()) {
      segment.text = ApplyCorrectionPrefix(pending_correction_prefix, segment.text);
      pending_correction_prefix.clear();
    }

    if (!segment.text.isEmpty()) {
      if (!output.empty() && output.back().text == segment.text) {
        output.back().punct = segment.punct;
      } else {
        output.push_back(segment);
      }
    }
  }

  QString corrected;
  for (const Segment &segment : output) {
    corrected += segment.text;
    corrected += segment.punct;
  }
  return NormalizeWhitespace(corrected).toUtf8().toStdString();
}

} // namespace vinput::windows_app
