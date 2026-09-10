#include "undo.h"
#include "app_state.h"

const std::string UndoStack::emptyStr_;
UndoStack undoStack;

Snapshot UndoStack::captureState() const {
    Snapshot snap;
    snap.solids = app.solids;
    snap.solidNames = app.solidNames;
    snap.solidVisible = app.solidVisible;
    snap.solidColors = app.solidColors;
    snap.sketchEntities = app.sketch.entities;
    snap.inSketchMode = app.inSketchMode;
    snap.sketchPlane = app.sketch.sketchPlane;
    snap.sketchOrigin = app.sketch.origin;
    snap.sketchAxisU = app.sketch.axisU;
    snap.sketchAxisV = app.sketch.axisV;
    snap.sketchActive = app.sketch.active;
    snap.sketchVisible = app.sketchVisible;
    snap.selectedSolidIdx = app.selectedSolidIdx;
    snap.selectedFaceIdx = app.selectedFaceIdx;
    snap.selectedEdgeIdx = app.selectedEdgeIdx;
    return snap;
}

void UndoStack::restoreState(const Snapshot& snap) {
    app.solids = snap.solids;
    app.solidNames = snap.solidNames;
    app.solidVisible = snap.solidVisible;
    app.solidColors = snap.solidColors;
    app.sketch.entities = snap.sketchEntities;
    app.inSketchMode = snap.inSketchMode;
    app.sketch.sketchPlane = snap.sketchPlane;
    app.sketch.origin = snap.sketchOrigin;
    app.sketch.axisU = snap.sketchAxisU;
    app.sketch.axisV = snap.sketchAxisV;
    app.sketch.active = snap.sketchActive;
    app.sketchVisible = snap.sketchVisible;
    // Clamp indices to valid range after state restore
    if (snap.selectedSolidIdx >= (int)app.solids.size()) app.selectedSolidIdx = -1;
    else app.selectedSolidIdx = snap.selectedSolidIdx;
    if (app.selectedSolidIdx >= 0 && snap.selectedFaceIdx >= (int)app.solids[app.selectedSolidIdx].faces.size())
        app.selectedFaceIdx = -1;
    else app.selectedFaceIdx = snap.selectedFaceIdx;
    app.selectedEdgeIdx = snap.selectedEdgeIdx;
    // Ensure metadata arrays match solids
    app.solidNames.resize(app.solids.size(), "Body");
    app.solidVisible.resize(app.solids.size(), true);
    app.solidColors.resize(app.solids.size(), {0.6f, 0.65f, 0.75f, 1.0f});
}

void UndoStack::pushState(const std::string& description) {
    Snapshot snap = captureState();
    snap.description = description;
    undoStack_.push_back(snap);
    if ((int)undoStack_.size() > MAX_UNDO)
        undoStack_.erase(undoStack_.begin());
    redoStack_.clear();
}

bool UndoStack::undo() {
    if (undoStack_.empty()) return false;
    Snapshot current = captureState();
    current.description = "redo";
    redoStack_.push_back(current);
    restoreState(undoStack_.back());
    undoStack_.pop_back();
    return true;
}

bool UndoStack::redo() {
    if (redoStack_.empty()) return false;
    Snapshot current = captureState();
    current.description = "undo";
    undoStack_.push_back(current);
    restoreState(redoStack_.back());
    redoStack_.pop_back();
    return true;
}

bool UndoStack::canUndo() const { return !undoStack_.empty(); }
bool UndoStack::canRedo() const { return !redoStack_.empty(); }

const std::string& UndoStack::undoDescription() const {
    return undoStack_.empty() ? emptyStr_ : undoStack_.back().description;
}

const std::string& UndoStack::redoDescription() const {
    return redoStack_.empty() ? emptyStr_ : redoStack_.back().description;
}

void UndoStack::clear() {
    undoStack_.clear();
    redoStack_.clear();
}
