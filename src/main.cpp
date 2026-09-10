#include "app_state.h"
#include "camera.h"
#include "scene.h"
#include "input.h"
#include "ui.h"
#include "gizmo.h"
#include "save_load.h"
#include "theme.h"
#include "tool_move.h"

#include <GLFW/glfw3.h>
#ifdef __APPLE__
#ifndef GL_SILENCE_DEPRECATION
#define GL_SILENCE_DEPRECATION
#endif
#include <OpenGL/gl3.h>
#else
#include <GL/gl.h>
#endif
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <cstdio>
#include <cstdlib>
#include <string>

using namespace cad;

// Global app state
AppState app;

int main(int argc, char** argv) {
    // Headless export: `simplecad part.scad --export-stl out.stl`.  Useful for
    // scripting a build, and it exercises the exporter without a window.
    if (argc >= 4 && std::string(argv[2]) == "--export-stl") {
        if (!loadProject(argv[1])) {
            std::fprintf(stderr, "could not open %s\n", argv[1]);
            return 1;
        }
        if (!exportStl(argv[3])) {
            std::fprintf(stderr, "export failed: %s\n", app.statusText.c_str());
            return 1;
        }
        std::printf("%s -> %s (%s)\n", argv[1], argv[3], app.statusText.c_str());
        return 0;
    }

    if (!glfwInit()) {
        std::fprintf(stderr, "Failed to init GLFW\n");
        return 1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif
    glfwWindowHint(GLFW_SAMPLES, 4);

    GLFWwindow* window = glfwCreateWindow(1280, 800, "SimpleCad", nullptr, nullptr);
    if (!window) {
        std::fprintf(stderr, "Failed to create window\n");
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    glfwSetScrollCallback(window, scrollCallback);
    glfwSetKeyCallback(window, keyCallback);

    // ImGui setup
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGui::StyleColorsDark();
    theme::apply(window);

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330");

    if (!app.renderer.init()) {
        std::fprintf(stderr, "Failed to init renderer\n");
        return 1;
    }

    app.camera.target = {0, 0, 0};
    app.camera.distance = 30.0f;
    app.camera.yaw = 45.0f;
    app.camera.pitch = 30.0f;

    // Open a project passed on the command line, so `simplecad part.scad`
    // works from a terminal or a file manager.
    if (argc > 1 && argv[1][0] != '\0') {
        std::snprintf(app.projectPathBuf, sizeof(app.projectPathBuf), "%s", argv[1]);
        if (loadProject(argv[1])) fitViewToModel();
    }

    float lastTime = (float)glfwGetTime();

    // ---- Main Loop ----
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        float now = (float)glfwGetTime();
        float dt = now - lastTime;
        lastTime = now;

        updateCameraAnimation(dt);

        // ---- ImGui frame start (must be before any GetForegroundDrawList calls) ----
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        processInput(window);
        renderScene();

        // ---- ImGui UI ----
        drawUI();
        drawViewGizmo();
        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);
    }

    // Cleanup
    app.renderer.shutdown();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
