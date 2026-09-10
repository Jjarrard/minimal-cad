#pragma once

struct GLFWwindow;

// Process mouse input, camera orbit/pan, extrude drag, sketch cursor update
void processInput(GLFWwindow* window);

// Render 3D scene: solids, sketch, overlays, extrude preview
void renderScene();
