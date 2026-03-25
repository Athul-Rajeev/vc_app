#include "UI/WindowManager.hpp"

WindowManager::WindowManager()
{
    m_isWindowOpen = false;
}

WindowManager::~WindowManager()
{
}

bool WindowManager::initialize()
{
    m_isWindowOpen = true;
    return true;
}

void WindowManager::render()
{
    // ImGui rendering logic will go here
}

void WindowManager::cleanup()
{
    m_isWindowOpen = false;
}