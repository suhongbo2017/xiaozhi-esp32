#ifndef MP3_PLAYER_H
#define MP3_PLAYER_H

#include <string>
#include <functional>

class Mp3Player {
public:
    static Mp3Player& GetInstance() {
        static Mp3Player instance;
        return instance;
    }

    bool PlayUrl(const std::string& url);
    void Stop();
    bool IsPlaying() const { return playing_; }

private:
    Mp3Player() = default;
    ~Mp3Player();

    static void PlayTask(void* arg);
    
    bool playing_ = false;
    std::string current_url_;
};

#endif // MP3_PLAYER_H
