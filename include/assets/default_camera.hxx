#ifndef YORK_DEFAULT_CAMERA_HXX
#define YORK_DEFAULT_CAMERA_HXX

#include "types/scene_data.hxx"

namespace york::assets {
    // Appends a fitted default camera and selects it for scene formats that
    // carry no cameras (OBJ and camera-less glTF scenes): the view frames the
    // objects' world-space extents from a fixed 45-degree vertical FOV.
    void addDefaultCamera(types::SceneData &sceneData, float aspectRatio);
}

#endif // YORK_DEFAULT_CAMERA_HXX
