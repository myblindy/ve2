// ve2.cpp : Defines the entry point for the application.
//

#include "framework.h"
#include "ve2.h"

using namespace std;

#define CHECK_TRUE(cmd, errormsg) if(!(cmd)) { cerr << errormsg; return -1; }

void init() noexcept
{
	glClearColor(0, 0, 0, 0);
}

// The main render function, return true to swap buffers.
bool render() noexcept
{
	glClear(GL_COLOR_BUFFER_BIT);

	return true;
}

int main(const char *args, int argc)
{
	CHECK_TRUE(glfwInit(), "Could not initialize GLFW.");

	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
	glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

	auto const window = glfwCreateWindow(800, 600, ApplicationName, nullptr, nullptr);
	CHECK_TRUE(window, "Could not create window.");

	glewExperimental = GL_TRUE;
	CHECK_TRUE(glewInit(), "Could not initialize GLEW.");

	init();

	while (!glfwWindowShouldClose(window))
	{
		if (render())
			glfwSwapBuffers(window);

		glfwPollEvents();
	}
}
