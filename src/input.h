#pragma once
#include "app_state.h"
#include "tool_edge_modifier.h"

struct GLFWwindow;

// GLFW callbacks
void scrollCallback(GLFWwindow* window, double xoff, double yoff);
void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);

// Mouse click dispatch
void handleMouseClick();
void handleRightClick();
void updateOriginCubeHover();

// Sketch actions
void enterSketchOnPlane(cad::Plane plane, cad::Vec3 origin, int sourceSolidIdx = -1, int sourceFaceIdx = -1, int sourcePlaneIdx = -1);
void startSketchTool(Tool tool);
void handleSketchClick(cad::Vec2 sketchPos);

// Utility
cad::Vec2 computeEndpoint(cad::Vec2 start, float lengthMm, float angleDeg);
void fitViewToModel();
void refreshSketchReferences();

// Helper to ensure solid metadata arrays stay in sync
void addSolid(const cad::Solid& solid, const std::string& name = "Body");
void removeSolid(int index);

// Merge every body in the current selection into one (Join tool).
void performJoinSelection();
