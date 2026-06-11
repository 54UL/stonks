#ifndef SDL_TINY_APP_H
#define SDL_TINY_APP_H

#include <App/App.hpp>
#include <App/EnginePipeline.hpp>

#include <memory>
#include <vector>

#include <stdint.h>
#include <Dependency.hpp>
#include <GL/glew.h>

#include <GL/gl.h>
#include <SDL.h>
#include <SDL_opengl.h>

struct ImGuiViewport;

namespace stnks
{
    class SDL2App : public App
    {
    private:
        SDL_Window *window_;
        SDL_GLContext glContext_;
        
        bool runningStatus_;
        bool initialized_;
        SDL_Thread *eventThread_;
        SDL_mutex  *eventMutex_;

        float currentDeltaTime_;
        float currentAppTime_;

        glm::ivec2 mainWindowSize_;

        std::shared_ptr<EnginePipeline> currentEngine_;
        std::vector<std::shared_ptr<ExecutionPipeline>> executionPipelines_;

        const char * windowTitle_;
        WindowMode windowMode_ = WindowMode::Windowed;
        bool       vsync_      = false;
        int        fpsTarget_  = 0; // 0 = unlimited

    public:
         SDL2App(const char * windowTitle);
        ~SDL2App();

    public:
        int Init(int argc, char **argv) override;
        int Exec() override;

        float GetDeltaTime() override;
        float GetAppTime();

        glm::ivec2 GetMainWindowSize() override;
        void AddExecutionPipeline(std::shared_ptr<ExecutionPipeline> engine) override;

        // Display settings
        void SetWindowMode(WindowMode mode) override;
        WindowMode GetWindowMode() const override;
        void SetVSync(bool enabled) override;
        bool GetVSync() const override;
        void SetResolution(int w, int h) override;
        void SetFPSTarget(int fps) override;
        int  GetFPSTarget() const override;

        SDL_Window* GetMainWindow();
        SDL_GLContext* GetGLContext();
        void Dispose();

    private:
        void AppInput();
        void SetRunningStatus(bool running);
        bool IsRunning();

        // App execution pipeline, this is where the engine pipeline gets attach to
        void RenderingInit();
        void PrepareFrame();
        void PresentFrame();

        // Viewport hook: gives undocked panels OS window decorations
        static void ViewportCreateWindowHook(ImGuiViewport* vp);
        static void (*s_origCreateWindow_)(ImGuiViewport*);
    };
}

#endif