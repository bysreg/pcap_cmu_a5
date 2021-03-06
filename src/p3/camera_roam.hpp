/**
 * @file camera_roam.cpp
 * @brief CameraRoamControl class
 *
 * @author Zeyang Li (zeyangl)
 */

#ifndef _462_APPLICATION_CAMERAROAM_HPP_
#define _462_APPLICATION_CAMERAROAM_HPP_

#include "application.hpp"
#include "math/camera.hpp"

class CameraRoamControl
{
public:

    CameraRoamControl();
    ~CameraRoamControl();

    void update( float dt );
    void handle_event( const SDL_Event& event );

    // the camera of this control
    Camera *camera;

private:

    enum Direction { DZERO=0, DPOS=1, DNEG=2 };
    enum Rotation { RNONE, RPITCHYAW, RROLL };

    void set_dir( bool pressed, int index, Direction newdir );

    // current directions in the local camera axes, (x, y, z)
    Direction direction[3];
    // the current rotation
    Rotation rotation;
};

#endif /* _462_APPLICATION_CAMERAROAM_HPP_ */
