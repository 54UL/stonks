#ifndef STNKS_APP_H
#define STNKS_APP_H

#include <glm/glm.hpp>
#include <App/EnginePipeline.hpp>
#include <App/ExecutionPipeline.hpp>

#include <memory>

/*
API SPECS:
WINDOW ABSTRACTIONS
KEYBOARD ABSTRACTION (RETURN KEYCODES)
MOUSE ABSTRACTIONS
*/

namespace stnks
{
    enum class AppEventType
    {
        INPUT,
        WINDOW,
        QUIT
    };

    class AppEvent
    {
    private:
        /* data */
    public:
        AppEvent(/* args */) {}
        ~AppEvent() {}
    };

    // Window/display mode
    enum class WindowMode : int { Windowed = 0, Fullscreen = 1, Borderless = 2 };

    class App
    {
    public:
        virtual ~App() = default;

        virtual int Init(int argc, char **argv) = 0;
        virtual int Exec() = 0;
        virtual float GetDeltaTime() = 0;
        virtual float GetAppTime() {return -1.0f;};

        virtual glm::ivec2 GetMainWindowSize() = 0;
        virtual void AddExecutionPipeline(std::shared_ptr<ExecutionPipeline> executionItem) = 0;

        // Display settings
        virtual void SetWindowMode(WindowMode mode) { (void)mode; }
        virtual WindowMode GetWindowMode() const { return WindowMode::Windowed; }
        virtual void SetVSync(bool enabled) { (void)enabled; }
        virtual bool GetVSync() const { return false; }
        virtual void SetResolution(int w, int h) { (void)w; (void)h; }
        virtual void SetFPSTarget(int fps) { (void)fps; }
        virtual int  GetFPSTarget() const { return 0; }
    };
}

#endif