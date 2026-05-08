#ifndef WAKE_WORD_H
#define WAKE_WORD_H

#include <memory>

class AudioCodec;
class CustomWakeWord;
class EspWakeWord;
class AfeWakeWord;
class DSpotterWakeWord; // 添加DSpotter唤醒词声明
class MicroWakeWord;    // OHF Voice micro-wake-word (TFLite Micro)

class WakeWord {
public:
    virtual ~WakeWord() = default;
    
    virtual void Initialize(AudioCodec* codec) = 0;
    virtual void Feed(const std::vector<int16_t>& data) = 0;
    virtual void OnWakeWordDetected(std::function<void(const std::string& wake_word)> callback) = 0;
    virtual void Start() = 0;
    virtual void Stop() = 0;
    virtual size_t GetFeedSize() = 0;
    virtual void EncodeWakeWordData() = 0;
    virtual bool GetWakeWordOpus(std::vector<uint8_t>& opus) = 0;
    virtual const std::string& GetLastDetectedWakeWord() const = 0;

    static std::unique_ptr<WakeWord> Create();
};

#endif