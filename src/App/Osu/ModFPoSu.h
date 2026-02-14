#pragma once
// Copyright (c) 2019, Colin Brook & PG, All rights reserved.
#include "noinclude.h"
#include "Vectors.h"
#include "Matrices.h"

#include <list>
#include <memory>

class Camera;
class ConVar;
class Image;
class Shader;
class VertexArrayObject;
class KeyboardEvent;
class UString;

class ModFPoSu3DModel;

class ModFPoSu {
    NOCOPY_NOMOVE(ModFPoSu);

   private:
    static constexpr const int SUBDIVISIONS = 4;

   public:
    ModFPoSu();
    ~ModFPoSu();

    void draw();
    void update();

    void onResolutionChange(vec2 newResolution);

    void onKeyDown(KeyboardEvent &key);
    void onKeyUp(KeyboardEvent &key);

    //[[nodiscard]] inline const Camera *getCamera() const { return this->camera; }

    [[nodiscard]] inline bool isCrosshairIntersectingScreen() const { return this->bCrosshairIntersectsScreen; }

    void resetCamera();

   private:
    void onResolutionChange0Args();

    void handleZoomedChange();
    void noclipMove();

    void handleInputOverrides(bool required);
    void setMousePosCompensated(vec2 newMousePos);
    vec2 intersectRayMesh(vec3 pos, vec3 dir);
    vec3 calculateUnProjectedVector(vec2 pos);

    void makePlayfield();
    void makeBackgroundCube();

    void onCurvedChange();
    void onDistanceChange();
    void onNoclipChange();

   private:
    struct VertexPair {
        vec3 a{0.f};
        vec3 b{0.f};
        float textureCoordinate;
        // vec3 normal{0.f};

        VertexPair(vec3 a, vec3 b, float tc) : a(a), b(b), textureCoordinate(tc) { ; }
    };

   private:
    static float subdivide(std::list<VertexPair> &meshList, const std::list<VertexPair>::iterator &begin,
                           const std::list<VertexPair>::iterator &end, int n, float edgeDistance);
    static vec3 normalFromTriangle(vec3 p1, vec3 p2, vec3 p3);

   private:
    Matrix4 modelMatrix;
    Matrix4 projectionMatrix;

    VertexArrayObject *vao;
    VertexArrayObject *vaoCube;

    std::unique_ptr<Camera> camera{nullptr};
    std::unique_ptr<ModFPoSu3DModel> skyboxModel{nullptr};  // lazy loaded

    std::list<VertexPair> meshList;

    vec3 vPrevNoclipCameraPos{0.f};
    vec3 vVelocity{0.f};

    float fCircumLength{0.f};

    float fZoomFOVAnimPercent{0.f};
    float fZoomFOVAnimPercentPrevious{0.f};

    float fEdgeDistance{0.f};

    bool bZoomKeyDown{false};
    bool bZoomed{false};
    bool bKeyLeftDown{false};
    bool bKeyUpDown{false};
    bool bKeyRightDown{false};
    bool bKeyDownDown{false};
    bool bKeySpaceDown{false};
    bool bKeySpaceUpDown{false};

    bool bCrosshairIntersectsScreen{false};
    bool bAlreadyWarnedAboutRawInputOverride{false};
};
