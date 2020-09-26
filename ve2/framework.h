// header.h : include file for standard system include files,
// or project specific include files
//

#pragma once

#include <iostream>
#include <span>
#include <functional>

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

#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

constexpr const char* ApplicationName = "ve2";