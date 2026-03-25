#pragma once

class WindowManager
{
public:
    WindowManager();
    ~WindowManager();

    bool initialize();
    void render();
    void cleanup();

private:
    bool m_isWindowOpen;
};