#include "save_load.h"
#include "platform_dialogs.h"
#include "undo.h"
#include "input.h"
#include <fstream>
#include <vector>
#include <iomanip>
#include <sstream>
#include <cstdio>

// Simple text-based format (not JSON to avoid dependency)
// Format: sections delimited by markers

static void writeVec3(std::ofstream& f, Vec3 v) {
    f << v.x << " " << v.y << " " << v.z << "\n";
}

static Vec3 readVec3(std::ifstream& f) {
    Vec3 v;
    f >> v.x >> v.y >> v.z;
    return v;
}

static void writeColor(std::ofstream& f, Color c) {
    f << c.r << " " << c.g << " " << c.b << " " << c.a << "\n";
}

static Color readColor(std::ifstream& f) {
    Color c;
    f >> c.r >> c.g >> c.b >> c.a;
    return c;
}


static void rememberProjectPath(const std::string& filepath) {
    std::snprintf(app.projectPathBuf, sizeof(app.projectPathBuf), "%s", filepath.c_str());
}

bool saveProject(const std::string& filepath) {
    std::ofstream f(filepath);
    if (!f.is_open()) {
        app.statusText = "Save failed: cannot write to file";
        return false;
    }

    f << "SIMPLECAD_V1\n";

    // Save solids
    f << "SOLIDS " << app.solids.size() << "\n";
    for (size_t si = 0; si < app.solids.size(); si++) {
        const auto& solid = app.solids[si];
        f << "SOLID_BEGIN\n";
        writeColor(f, si < app.solidColors.size() ? app.solidColors[si] : solid.color);
        f << (si < app.solidNames.size() ? app.solidNames[si] : "Body") << "\n";
        f << "FACES " << solid.faces.size() << "\n";
        for (const auto& face : solid.faces) {
            f << "FACE " << face.outerLoop.size() << "\n";
            for (const auto& p : face.outerLoop)
                writeVec3(f, p);
            writeColor(f, face.color);
            // Hole loops are written only when present, so files without bores
            // stay byte-identical to the previous format.
            if (!face.innerLoops.empty()) {
                f << "HOLES " << face.innerLoops.size() << "\n";
                for (const auto& hole : face.innerLoops) {
                    f << "LOOP " << hole.size() << "\n";
                    for (const auto& p : hole) writeVec3(f, p);
                }
            }
        }
        f << "SOLID_END\n";
    }

    // Save sketch state
    f << "SKETCH " << (app.inSketchMode ? 1 : 0) << "\n";
    if (app.inSketchMode) {
        writeVec3(f, app.sketch.sketchPlane.normal);
        f << app.sketch.sketchPlane.d << "\n";
        writeVec3(f, app.sketch.origin);
        f << "ENTITIES " << app.sketch.entities.size() << "\n";
        for (const auto& e : app.sketch.entities) {
            f << (int)e.type << " ";
            f << e.p0.x << " " << e.p0.y << " ";
            f << e.p1.x << " " << e.p1.y << " ";
            f << e.p2.x << " " << e.p2.y << " ";
            f << e.radius << " ";
            f << (e.construction ? 1 : 0) << "\n";
        }
    }

    f << "END\n";
    return true;
}

bool loadProject(const std::string& filepath) {
    std::ifstream f(filepath);
    if (!f.is_open()) return false;

    std::string token;
    f >> token;
    if (token != "SIMPLECAD_V1") return false;

    // Parse into temporary structures so we don't destroy current state on error
    std::vector<Solid> tempSolids;
    std::vector<std::string> tempNames;
    std::vector<bool> tempVisible;
    std::vector<Color> tempColors;
    std::vector<SketchEntity> tempEntities;
    bool tempInSketchMode = false;
    Plane tempPlane;
    Vec3 tempOrigin;

    while (f >> token) {
        if (token == "SOLIDS") {
            int count;
            f >> count;
            if (count < 0 || count > 10000) return false;  // sanity: avoid OOM
            for (int si = 0; si < count; si++) {
                f >> token; // SOLID_BEGIN
                if (token != "SOLID_BEGIN") return false;
                Color solidColor = readColor(f);
                std::string name;
                f.ignore();
                std::getline(f, name);

                f >> token; // FACES
                if (token != "FACES") return false;
                int faceCount;
                f >> faceCount;
                if (faceCount < 0 || faceCount > 100000) return false;  // sanity: avoid OOM

                Solid solid;
                for (int fi = 0; fi < faceCount; fi++) {
                    f >> token; // FACE
                    if (token != "FACE") return false;
                    int vertCount;
                    f >> vertCount;
                    if (vertCount < 3 || vertCount > 10000) return false;  // sanity check
                    if (!f.good()) return false;
                    BRepFace face;
                    for (int vi = 0; vi < vertCount; vi++) {
                        if (!f.good()) return false;
                        face.outerLoop.push_back(readVec3(f));
                    }
                    face.color = readColor(f);

                    // HOLES is optional — files written before faces could
                    // carry bores simply omit it, so peek and rewind.
                    std::streampos mark = f.tellg();
                    std::string maybe;
                    if (f >> maybe) {
                        if (maybe == "HOLES") {
                            int holeCount = 0;
                            f >> holeCount;
                            if (holeCount < 0 || holeCount > 10000) return false;
                            for (int hi = 0; hi < holeCount; hi++) {
                                f >> maybe;                       // LOOP
                                if (maybe != "LOOP") return false;
                                int hv = 0;
                                f >> hv;
                                if (hv < 3 || hv > 10000) return false;
                                std::vector<cad::Vec3> loop;
                                for (int vi = 0; vi < hv; vi++) {
                                    if (!f.good()) return false;
                                    loop.push_back(readVec3(f));
                                }
                                face.innerLoops.push_back(std::move(loop));
                            }
                        } else {
                            f.clear();
                            f.seekg(mark);
                        }
                    } else {
                        f.clear();
                        f.seekg(mark);
                    }

                    face.computeNormal();
                    solid.faces.push_back(face);
                }
                solid.rebuildEdges();
                solid.color = solidColor;
                tempSolids.push_back(solid);
                tempNames.push_back(name);
                tempVisible.push_back(true);
                tempColors.push_back(solidColor);

                f >> token; // SOLID_END
            }
        } else if (token == "SKETCH") {
            int active;
            f >> active;
            if (active) {
                Vec3 normal = readVec3(f);
                float d;
                f >> d;
                Vec3 origin = readVec3(f);
                tempPlane = Plane(normal, d);
                tempOrigin = origin;
                tempInSketchMode = true;

                f >> token; // ENTITIES
                int entCount;
                f >> entCount;
                if (entCount < 0 || entCount > 100000) return false;  // sanity: avoid OOM
                for (int i = 0; i < entCount; i++) {
                    SketchEntity e;
                    int type;
                    f >> type;
                    e.type = (SketchEntityType)type;
                    f >> e.p0.x >> e.p0.y;
                    f >> e.p1.x >> e.p1.y;
                    f >> e.p2.x >> e.p2.y;
                    f >> e.radius;
                    int constr;
                    f >> constr;
                    e.construction = (constr != 0);
                    tempEntities.push_back(e);
                }
            }
        } else if (token == "END") {
            break;
        }
    }

    // Successfully parsed — now apply to app state
    undoStack.clear();
    app.solids = std::move(tempSolids);
    app.solidNames = std::move(tempNames);
    app.solidVisible = std::move(tempVisible);
    app.solidColors = std::move(tempColors);
    app.sketch.entities = std::move(tempEntities);
    app.selectedSolidIdx = -1;
    app.selectedFaceIdx = -1;
    app.selectedEdgeIdx = -1;
    if (tempInSketchMode) {
        app.sketch.setup(tempPlane, tempOrigin);
        app.sketch.active = true;
        app.inSketchMode = true;
    } else {
        app.inSketchMode = false;
    }

    rememberProjectPath(filepath);

    return true;
}

bool saveProjectInteractive() {
    std::string filepath = app.projectPathBuf[0] != '\0' ? app.projectPathBuf : "project.scad";
#ifdef __APPLE__
    char selectedPath[sizeof(app.projectPathBuf)] = "";
    if (!showNativeProjectSaveDialog(filepath.c_str(), selectedPath, sizeof(selectedPath))) {
        app.statusText = "Save cancelled";
        return false;
    }
    filepath = selectedPath;
#endif

    if (!saveProject(filepath)) {
        return false;
    }

    rememberProjectPath(filepath);
    app.statusText = std::string("Saved ") + filepath;
    return true;
}

bool loadProjectInteractive() {
    std::string filepath = app.projectPathBuf[0] != '\0' ? app.projectPathBuf : "project.scad";
#ifdef __APPLE__
    char selectedPath[sizeof(app.projectPathBuf)] = "";
    if (!showNativeProjectOpenDialog(filepath.c_str(), selectedPath, sizeof(selectedPath))) {
        app.statusText = "Open cancelled";
        return false;
    }
    filepath = selectedPath;
#endif

    if (!loadProject(filepath)) {
        app.statusText = std::string("Load failed: could not open ") + filepath;
        return false;
    }

    // Frame what was just opened, rather than leaving the camera wherever the
    // previous model left it.
    fitViewToModel();

    app.statusText = std::string("Loaded ") + filepath;
    return true;
}

bool exportStl(const std::string& filepath) {
    // Binary STL: about a fifth the size of ASCII and far quicker for a slicer
    // to read.  Layout is an 80-byte header, a uint32 triangle count, then 50
    // bytes per triangle (normal, three vertices, a two-byte attribute).
    std::ofstream f(filepath, std::ios::binary);
    if (!f.is_open()) {
        app.statusText = "STL export failed: cannot write to file";
        return false;
    }

    struct Tri { float n[3]; float v[3][3]; };
    std::vector<Tri> tris;

    for (size_t solidIndex = 0; solidIndex < app.solids.size(); solidIndex++) {
        if (solidIndex < app.solidVisible.size() && !app.solidVisible[solidIndex]) continue;

        const Mesh mesh = app.solids[solidIndex].tessellate();
        for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
            uint32_t ia = mesh.indices[i], ib = mesh.indices[i + 1], ic = mesh.indices[i + 2];
            if (ia >= mesh.positions.size() || ib >= mesh.positions.size() ||
                ic >= mesh.positions.size()) continue;

            cad::Vec3 a = mesh.positions[ia], b = mesh.positions[ib], c = mesh.positions[ic];
            cad::Vec3 n = (b - a).cross(c - a).normalized();

            Tri t;
            t.n[0] = n.x; t.n[1] = n.y; t.n[2] = n.z;
            const cad::Vec3 src[3] = {a, b, c};
            for (int k = 0; k < 3; k++) {
                t.v[k][0] = src[k].x; t.v[k][1] = src[k].y; t.v[k][2] = src[k].z;
            }
            tris.push_back(t);
        }
    }

    if (tris.empty()) {
        app.statusText = "STL export skipped: no visible solid triangles";
        return false;
    }

    char header[80] = {};
    std::snprintf(header, sizeof(header), "SimpleCad binary STL");
    f.write(header, sizeof(header));

    uint32_t count = (uint32_t)tris.size();
    f.write(reinterpret_cast<const char*>(&count), 4);

    for (const Tri& t : tris) {
        f.write(reinterpret_cast<const char*>(t.n), 12);
        f.write(reinterpret_cast<const char*>(t.v), 36);
        uint16_t attr = 0;
        f.write(reinterpret_cast<const char*>(&attr), 2);
    }

    if (!f.good()) {
        app.statusText = "STL export failed while writing";
        return false;
    }
    app.statusText = "Exported " + std::to_string(count) + " triangles";
    return true;
}
