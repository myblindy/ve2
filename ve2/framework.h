// header.h : include file for standard system include files,
// or project specific include files
//

#pragma once

#include <iostream>
#include <span>
#include <functional>
#include <thread>
#include <queue>
#include <vector>
#include <mutex>
#include <string>

extern "C"
{
#include <gl/glew.h>
#include <glfw/glfw3.h>
}
#pragma comment(lib, "opengl32")


#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/transform.hpp>

constexpr const char* ApplicationName = "ve2";