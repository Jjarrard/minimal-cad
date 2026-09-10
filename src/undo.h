#pragma once
#include "geometry.h"
#include "sketch.h"
#include <vector>
#include <string>

// Snapshot of application state for undo/redo
struct Snapshot {
    std::vector<cad::Solid> solids;
    std::vector<std::string> solidNames;
    std::vector<bool> solidVisible;
    std::vector<cad::Color> solidColors;
    std::vector<cad::SketchEntity> sketchEntities;
    bool inSketchMode = false;
    cad::Plane sketchPlane;
    cad::Vec3 sketchOrigin;
    cad::Vec3 sketchAxisU, sketchAxisV;
    bool sketchActive = false;
    bool sketchVisible = true;
    int selectedSolidIdx = -1;
    int selectedFaceIdx = -1;
    int selectedEdgeIdx = -1;
    std::string description;
};

class UndoStack {
public:
    void pushState(const std::string& description);
    bool undo();
    bool redo();
    bool canUndo() const;
    bool canRedo() const;
    const std::string& undoDescription() const;
    const std::string& redoDescription() const;
    void clear();
    int undoCount() const { return (int)undoStack_.size(); }
    int redoCount() const { return (int)redoStack_.size(); }
private:
    Snapshot captureState() const;
    void restoreState(const Snapshot& snap);
    std::vector<Snapshot> undoStack_;
    std::vector<Snapshot> redoStack_;
    static const int MAX_UNDO = 100;
    static const std::string emptyStr_;
};

extern UndoStack undoStack;
