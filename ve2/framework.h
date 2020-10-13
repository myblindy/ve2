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


extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

#define GLM_FORCE_SWIZZLE
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/transform.hpp>

constexpr const char* ApplicationName = "ve2";