#include "application/application.hpp"
#include "application/camera_roam.hpp"
#include "application/imageio.hpp"
#include "application/scene_loader.hpp"
#include "application/opengl.hpp"
#include "scene/scene.hpp"
#include "scene/sphere.hpp"
#include "scene/triangle.hpp"
#include "p3/raytracer.hpp"
#include <typeinfo>
#include "scene/model.hpp"
#include "cudaScene.hpp"
#include <cuda.h>
#include <cuda_runtime.h>
#include <curand.h>
#include "raytracer_cuda.hpp"
#include "cycleTimer.h"

#include "master.hpp"
#include "slave.hpp"

#include <SDL.h>

#include <stdlib.h>
#include <iostream>
#include <cstring>
#include <string>

cudaScene cscene;
cudaScene cscene_host;
using namespace std;

unsigned char *cimg;

namespace _462 {

#define DEFAULT_WIDTH 800
#define DEFAULT_HEIGHT 600
#define dwidth 800
#define dheight 600

#define BUFFER_SIZE(w,h) ( (size_t) ( 4 * (w) * (h) ) )
#define KEY_RAYTRACE_GPU SDLK_g
static const size_t NUM_GL_LIGHTS = 8;
struct Options
{
    bool open_window;
    const char* input_filename;
    const char* output_filename;
    int width, height;
    int num_samples;
	bool master = false;
	bool slave = false;

	std::string host; // host to connect from slave
};

class RaytracerApplication : public Application
{
public:
    void render_scene(const Scene& scene);

    RaytracerApplication( const Options& opt )
        : options( opt ), buffer( 0 ), buf_width( 0 ),
		buf_height(0), gpu_raytracing(false) {}

    virtual ~RaytracerApplication() {
		if (buffer)
			free( buffer );
	}

    virtual bool initialize();
    virtual void destroy();
    virtual void update( real_t );
    virtual void render();
    virtual void handle_event( const SDL_Event& event );

    // flips raytracing, does any necessary initialization
	void do_gpu_raytracing();

    Scene scene;
    Options options;
    CameraRoamControl camera_control;
    // the image buffer for raytracing
    unsigned char* buffer = 0;
    // width and height of the buffer
    int buf_width, buf_height;
	bool gpu_raytracing;
};

bool RaytracerApplication::initialize()
{
    // copy camera into camera control so it can be moved via mouse
    camera_control.camera = scene.camera;
    bool load_gl = options.open_window;


    try {

        Material* const* materials = scene.get_materials();
        Mesh* const* meshes = scene.get_meshes();

        // load all textures
        for ( size_t i = 0; i < scene.num_materials(); ++i ) {
            if ( !materials[i]->load() || ( load_gl && !materials[i]->create_gl_data() )) {
                std::cout << "Error loading texture, aborting.\n";
                return false;
            }
        }

        // load all meshes
        for ( size_t i = 0; i < scene.num_meshes(); ++i ) {
            if ( !meshes[i]->load() || ( load_gl && !meshes[i]->create_gl_data() ) ) {
                std::cout << "Error loading mesh, aborting.\n";
                return false;
            }
        }

    }
    catch ( std::bad_alloc const& )
    {
        std::cout << "Out of memory error while initializing scene\n.";
        return false;
    }
	// Alloc cuda mem
	int N = scene.num_geometries();
	int N_light = scene.num_lights();
	int N_material = scene.num_materials();

	//gpuErrchk(cudaMalloc((void **)&cscene.position, sizeof(float) * 3 * N));
	gpuErrchk(cudaMalloc((void **)&cscene.scale, sizeof(float) * 3 * N));
	//gpuErrchk(cudaMalloc((void **)&cscene.rotation, sizeof(float) * 4 * N));
	gpuErrchk(cudaMalloc((void **)&cscene.type, sizeof(int) * 1 * N));
	gpuErrchk(cudaMalloc((void **)&cscene.radius, sizeof(float) * 1 * N));
	gpuErrchk(cudaMalloc((void **)&cscene.material, sizeof(float) * 1 * N));
	gpuErrchk(cudaMalloc((void **)&cscene.ambient, sizeof(float) * 3 * N_material));
	gpuErrchk(cudaMalloc((void **)&cscene.diffuse, sizeof(float) * 3 * N_material));
	gpuErrchk(cudaMalloc((void **)&cscene.specular, sizeof(float) * 3 * N_material));
	gpuErrchk(cudaMalloc((void **)&cscene.vertex0, sizeof(float) * 3 * N));
	gpuErrchk(cudaMalloc((void **)&cscene.vertex1, sizeof(float) * 3 * N));
	gpuErrchk(cudaMalloc((void **)&cscene.vertex2, sizeof(float) * 3 * N));
	gpuErrchk(cudaMalloc((void **)&cscene.curand, sizeof(curandState) * dwidth * dheight));
	gpuErrchk(cudaMalloc((void **)&cscene.data, sizeof(float) * 7 * N));

	// Mirrored host mem
	cscene_host.position = (float *)malloc(sizeof(float) * 3 * N);
	cscene_host.rotation = (float *)malloc(sizeof(float) * 4 * N);
	cscene_host.scale = (float *)malloc(sizeof(float) * 3 * N);
	cscene_host.type = (int *)malloc(sizeof(int) * 1 * N);
	cscene_host.radius = (float *)malloc(sizeof(float) * 1 * N);
	cscene_host.material = (int *)malloc(sizeof(int) * 1 * N);
	cscene_host.ambient = (float *)malloc(sizeof(float) * 3 * N_material);
	cscene_host.diffuse = (float *)malloc(sizeof(float) * 3 * N_material);
	cscene_host.specular = (float *)malloc(sizeof(float) * 3 * N_material);
	cscene_host.vertex0 = (float *)malloc(sizeof(float) * 3 * N);
	cscene_host.vertex1 = (float *)malloc(sizeof(float) * 3 * N);
	cscene_host.vertex2 = (float *)malloc(sizeof(float) * 3 * N);
	cscene_host.data = (float *)malloc(sizeof(float) * 7 * N);
	cscene_host.position = cscene_host.data + 4 * N;
	cscene_host.rotation = cscene_host.data;
	

	for (size_t i = 0; i < N; i++) {
		Geometry *g = scene.get_geometries()[i];
		g->post_initialize();
		//g->position.to_array(cscene_host.position + 3 * i);
		g->position.to_array(cscene_host.position + 3 * i);
		g->orientation.to_array(cscene_host.rotation + 4 * i);
		g->scale.to_array(cscene_host.scale + 3 * i);
		string type_string = typeid(*g).name();
			cout << type_string << endl;
		const Material *primary_material;
		if (type_string.find("Sphere") != string::npos) {
			Sphere *s = (Sphere *)g;
			primary_material = s->material;
			cscene_host.type[i] = 1;
			Vector3 scaled = g->scale * s->radius;
			scaled.to_array(cscene_host.scale + 3 * i);
		} else if (type_string.find("Triangle") != string::npos) {
			Triangle *t = (Triangle *)g;
			cscene_host.type[i] = 2;
			primary_material = t->vertices[0].material;
			t->vertices[0].position.to_array(cscene_host.vertex0 + 3 * i);
			t->vertices[1].position.to_array(cscene_host.vertex1 + 3 * i);
			t->vertices[2].position.to_array(cscene_host.vertex2 + 3 * i);
		} else if (type_string.find("Model") != string::npos) {
			Model *m = (Model *)m;
			primary_material = m->material;
			cscene_host.type[i] = 3;
		}
		for (int j = 0; j < N_material; j++) {
			if (scene.get_materials()[j] == primary_material)
				cscene_host.material[i] = j;
		}
	}

	scene.post_initialize();

	for (int i = 0; i < N_material; i++)
	{
		Material *m = scene.get_materials()[i];
		m->ambient.to_array(cscene_host.ambient + 3 * i);
		m->diffuse.to_array(cscene_host.diffuse + 3 * i);
		m->specular.to_array(cscene_host.specular + 3 * i);
	}

	//cudaMemcpy(cscene.position, cscene_host.position, sizeof(float) * 3 * N, cudaMemcpyHostToDevice);
	///cudaMemcpy(cscene.rotation, cscene_host.rotation, sizeof(float) * 4 * N, cudaMemcpyHostToDevice);
	//gpuErrchk(cudaMemcpy(cscene.data + 4 * N, cscene_host.position, sizeof(float) * 3 * N, cudaMemcpyHostToDevice));
	//gpuErrchk(cudaMemcpy(cscene.data, cscene_host.rotation, sizeof(float) * 4 * N, cudaMemcpyHostToDevice));
	gpuErrchk(cudaMemcpy(cscene.data, cscene_host.data, sizeof(float) * 7 * N, cudaMemcpyHostToDevice));
	gpuErrchk(cudaMemcpy(cscene.scale, cscene_host.scale, sizeof(float) * 3 * N, cudaMemcpyHostToDevice));
	cudaMemcpy(cscene.type, cscene_host.type, sizeof(int) * 1 * N, cudaMemcpyHostToDevice);
	cudaMemcpy(cscene.radius, cscene_host.radius, sizeof(float) * 1 * N, cudaMemcpyHostToDevice);
	cudaMemcpy(cscene.material, cscene_host.material, sizeof(int) * 1 * N, cudaMemcpyHostToDevice);
	cudaMemcpy(cscene.ambient, cscene_host.ambient, sizeof(float) * 3 * N_material, cudaMemcpyHostToDevice);
	cudaMemcpy(cscene.diffuse, cscene_host.diffuse, sizeof(float) * 3 * N_material, cudaMemcpyHostToDevice);
	cudaMemcpy(cscene.specular, cscene_host.specular, sizeof(float) * 3 * N_material, cudaMemcpyHostToDevice);
	cudaMemcpy(cscene.vertex0, cscene_host.vertex0, sizeof(float) * 3 * N, cudaMemcpyHostToDevice);
	cudaMemcpy(cscene.vertex1, cscene_host.vertex1, sizeof(float) * 3 * N, cudaMemcpyHostToDevice);
	cudaMemcpy(cscene.vertex2, cscene_host.vertex2, sizeof(float) * 3 * N, cudaMemcpyHostToDevice);
	//cudaMemcpy(cscene.data, cscene_host.vertex2, sizeof(float) * 7 * N, cudaMemcpyHostToDevice);
	


	
	scene.camera.position.to_array(cscene.cam_position);
	scene.camera.orientation.to_array(cscene.cam_orientation);
	scene.ambient_light.to_array(cscene.ambient_light_col);
	cscene.fov = scene.camera.fov;
	cscene.aspect = (dwidth + 0.0) / (dheight + 0.0);
	cscene.near_clip = scene.camera.near_clip;
	cscene.far_clip = scene.camera.far_clip;
	cscene.N = N;
	cscene.N_material = N_material;

	EnvMap &envmap = scene.envmap;
	if (envmap.enabled) {
		envmap.initialize();
	int num_faces  = 6;
	int width = envmap.posx.width;
	int height = envmap.posx.height;
	std::cout << "Width : " << width << std::endl;

	int size = num_faces * width * height * sizeof(uchar4) ;
	int face_size = width * height * sizeof (uchar4);

	cudaChannelFormatDesc channelDesc = cudaCreateChannelDesc(8, 8, 8, 8, cudaChannelFormatKindUnsigned );
	cudaArray *cu_3darray;
	cudaMalloc3DArray(&cu_3darray, &channelDesc, make_cudaExtent(width, height, num_faces), cudaArrayCubemap);
	unsigned char *envmap_array = new unsigned char [size];
	memcpy(envmap_array, envmap.posx.data, face_size);
	memcpy(envmap_array + face_size, envmap.negx.data, face_size);
	memcpy(envmap_array + face_size * 2, envmap.posy.data, face_size);
	memcpy(envmap_array + face_size * 3, envmap.negy.data, face_size);
	memcpy(envmap_array + face_size * 4, envmap.posz.data, face_size);
	memcpy(envmap_array + face_size * 5, envmap.negz.data, face_size);

	cudaMemcpyToArray(cu_3darray, 0, 0, envmap_array, size, cudaMemcpyHostToDevice);
	bindEnvmap(cu_3darray, channelDesc);
	}

	std::cout << "Cuda initialized" << std::endl;

	// CUDA part
	gpuErrchk(cudaMalloc((void **)&cimg, 4 * dheight * dwidth));
    return true;
}

void RaytracerApplication::destroy()
{
}

void RaytracerApplication::update( real_t delta_time )
{
}

void RaytracerApplication::render()
{
    int width, height;

    // query current window size, resize viewport
    get_dimension( &width, &height );
    glViewport( 0, 0, width, height );

    // fix camera aspect
    Camera& camera = scene.camera;
    camera.aspect = real_t( width ) / real_t( height );

    // clear buffer
    glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );

    // reset matrices
    glMatrixMode( GL_PROJECTION );
    glLoadIdentity();
    glMatrixMode( GL_MODELVIEW );
    glLoadIdentity();

	if ( gpu_raytracing) {
        assert( buffer );
        glColor4d( 1.0, 1.0, 1.0, 1.0 );
        glRasterPos2f( -1.0f, -1.0f );
        glDrawPixels( buf_width, buf_height, GL_RGBA,
              GL_UNSIGNED_BYTE, &buffer[0] );
    }
}

void RaytracerApplication::handle_event( const SDL_Event& event )
{
    int width, height;

    if ( !gpu_raytracing ) {
        camera_control.handle_event( this, event );
    }
	scene.handle_event(event);

    switch ( event.type )
    {
    case SDL_KEYDOWN:
        switch ( event.key.keysym.sym )
        {
		case KEY_RAYTRACE_GPU:
			do_gpu_raytracing();
			break;
        default:
            break;
        }
    default:
        break;
    }
}

void RaytracerApplication::do_gpu_raytracing()
{
	int width = DEFAULT_WIDTH;
	int height = DEFAULT_HEIGHT;
	buf_width = width;
	buf_height = height;
	if (!buffer) {
		buffer = new unsigned char [width * height * 4];
	}

	cscene.width = width;
	cscene.height = height;
	gpu_raytracing = true;
	cudaRayTrace(&cscene, cimg);
	gpuErrchk(cudaMemcpy(buffer, cimg, 4 * dwidth * dheight, cudaMemcpyDeviceToHost));
}


void RaytracerApplication::render_scene(const Scene& scene)
{
    glPushAttrib( GL_ALL_ATTRIB_BITS );
    glPushClientAttrib( GL_CLIENT_ALL_ATTRIB_BITS );

    glClearColor(
        scene.background_color.r,
        scene.background_color.g,
        scene.background_color.b,
        1.0f );
    glPopClientAttrib();
    glPopAttrib();
}

}

using namespace _462;

static bool parse_args( Options* opt, int argc, char* argv[] )
{
    if ( argc < 2 ) {
		std::cout << "More arguments" << std::endl;
        return false;
    }

    opt->input_filename = argv[1];
    opt->output_filename = NULL;
    opt->open_window = true;
    opt->width = DEFAULT_WIDTH;
    opt->height = DEFAULT_HEIGHT;
    opt->num_samples = 1;
    for (int i = 2; i < argc; i++)
    {

    	if(strcmp(argv[i] + 1, "master") == 0) {    		
    		opt->master = true;
    		continue;
    	}

    	if(strcmp(argv[i] + 1, "slave") == 0) {
    		opt->slave = true;
    		opt->host = argv[i + 1]; // we assume the next parameter is the master's host for the slave to connect to
    		i++;
    		continue;
    	}

        switch (argv[i][1])
        {
        case 'd':
            if (i >= argc - 2) return false;
            opt->width = atoi(argv[++i]);
            opt->height = atoi(argv[++i]);
            // check for valid width/height
            if ( opt->width < 1 || opt->height < 1 )
            {
                std::cout << "Invalid window dimensions\n";
                return false;
            }
            break;
        case 'n':
            if (i < argc - 1)
                opt->num_samples = atoi(argv[++i]);
            break;
        }
    }

    return true;
}

using namespace std;
int main( int argc, char* argv[] )
{
    Options opt;
	int ret = 0;

    if ( !parse_args( &opt, argc, argv ) ) {
        return 1;
    }

    RaytracerApplication app( opt );

    // load the given scene
    if ( !load_scene( &app.scene, opt.input_filename ) ) {
        std::cout << "Error loading scene "
          << opt.input_filename << ". Aborting.\n";
        return 1;
    }

	Scene *scene = &app.scene;
	cout << "Geometries: " << scene->num_geometries() << endl;
	cout << "Meshes: " << scene->num_meshes() << endl;
	cout << "Materials: " << scene->num_materials() << endl;

	cout << "master:slave => " << opt.master << ":" << opt.slave << endl;

	if(opt.master) {
		Master::start();
	}else if(opt.slave) {
		Slave::start(opt.host);
	}	

	real_t fps = 60.0;
	const char* title = "15462 Project 3 - Raytracer";
	// start a new application
	ret = Application::start_application(&app,
					  opt.width,
					  opt.height,
					  fps, title);

	return ret;
}
