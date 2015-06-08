/*
Chris Cummings
This is wraps up the camera system in a simple StartCamera and StopCamera  api to read 
data from the feed. Based on parts of raspivid, and the work done by Pierre Raus at 
http://raufast.org/download/camcv_vid0.c to get the camera feeding into opencv. It 
*/

#include "camera.h"
#include <stdio.h>

// Standard port setting for the camera component
#define MMAL_CAMERA_PREVIEW_PORT 0
#define MMAL_CAMERA_VIDEO_PORT 1
#define MMAL_CAMERA_CAPTURE_PORT 2

// JPEG quality
#define QUALITY_JPEG 80

static CCamera* GCamera = NULL;

CCamera* StartCamera(int width, int height, int framerate)
{
	//can't create more than one camera
	if(GCamera != NULL)
	{
		printf("Can't create more than one camera\n");
		return NULL;
	}

	//create and attempt to initialize the camera
	GCamera = new CCamera();
	if(!GCamera->Init(width,height,framerate))
	{
		//failed so clean up
		printf("Camera init failed\n");
		delete GCamera;
		GCamera = NULL;
	}
	return GCamera;
}

void StopCamera()
{
	if(GCamera)
	{
		GCamera->Release();
		delete GCamera;
		GCamera = NULL;
	}
}

CCamera::CCamera()
{
	CameraComponent = NULL;    
	SplitterComponent = NULL;
	VidToSplitConn = NULL;
	EncoderOutput = NULL;
	SyncOutput = NULL;
}

CCamera::~CCamera()
{

}

void CCamera::CameraControlCallback(MMAL_PORT_T *port, MMAL_BUFFER_HEADER_T *buffer)
{
	GCamera->OnCameraControlCallback(port,buffer);
}

MMAL_COMPONENT_T* CCamera::CreateCameraComponentAndSetupPorts()
{
	MMAL_COMPONENT_T *camera = 0;
	MMAL_ES_FORMAT_T *format;
	MMAL_PORT_T *preview_port = NULL, *video_port = NULL, *still_port = NULL;
	MMAL_STATUS_T status;

	//create the camera component
	status = mmal_component_create(MMAL_COMPONENT_DEFAULT_CAMERA, &camera);
	if (status != MMAL_SUCCESS)
	{
		printf("Failed to create camera component\n");
		return NULL;
	}

	//check we have output ports
	if (!camera->output_num)
	{
		printf("Camera doesn't have output ports");
		mmal_component_destroy(camera);
		return NULL;
	}

	//get the 3 ports
	preview_port = camera->output[MMAL_CAMERA_PREVIEW_PORT];
	video_port = camera->output[MMAL_CAMERA_VIDEO_PORT];
	still_port = camera->output[MMAL_CAMERA_CAPTURE_PORT];

	// Enable the camera, and tell it its control callback function
	status = mmal_port_enable(camera->control, CameraControlCallback);
	if (status != MMAL_SUCCESS)
	{
		printf("Unable to enable control port : error %d", status);
		mmal_component_destroy(camera);
		return NULL;
	}

	//  set up the camera configuration
	{
		MMAL_PARAMETER_CAMERA_CONFIG_T cam_config;
		cam_config.hdr.id = MMAL_PARAMETER_CAMERA_CONFIG;
		cam_config.hdr.size = sizeof(cam_config);
		cam_config.max_stills_w = Width;
		cam_config.max_stills_h = Height;
		cam_config.stills_yuv422 = 0;
		cam_config.one_shot_stills = 0;
		cam_config.max_preview_video_w = Width;
		cam_config.max_preview_video_h = Height;
		cam_config.num_preview_video_frames = 3;
		cam_config.stills_capture_circular_buffer_height = 0;
		cam_config.fast_preview_resume = 0;
		cam_config.use_stc_timestamp = MMAL_PARAM_TIMESTAMP_MODE_RESET_STC;
		mmal_port_parameter_set(camera->control, &cam_config.hdr);
	}

	// setup preview port format - QUESTION: Needed if we aren't using preview?
	format = preview_port->format;
	format->encoding = MMAL_ENCODING_OPAQUE;
	format->encoding_variant = MMAL_ENCODING_I420;
	format->es->video.width = Width;
	format->es->video.height = Height;
	format->es->video.crop.x = 0;
	format->es->video.crop.y = 0;
	format->es->video.crop.width = Width;
	format->es->video.crop.height = Height;
	format->es->video.frame_rate.num = FrameRate;
	format->es->video.frame_rate.den = 1;
	status = mmal_port_format_commit(preview_port);
	if (status != MMAL_SUCCESS)
	{
		printf("Couldn't set preview port format : error %d", status);
		mmal_component_destroy(camera);
		return NULL;
	}

	//setup video port format
	format = video_port->format;
	format->encoding = MMAL_ENCODING_I420;
	format->encoding_variant = MMAL_ENCODING_I420;
	format->es->video.width = Width;
	format->es->video.height = Height;
	format->es->video.crop.x = 0;
	format->es->video.crop.y = 0;
	format->es->video.crop.width = Width;
	format->es->video.crop.height = Height;
	format->es->video.frame_rate.num = FrameRate;
	format->es->video.frame_rate.den = 1;
	status = mmal_port_format_commit(video_port);
	if (status != MMAL_SUCCESS)
	{
		printf("Couldn't set video port format : error %d", status);
		mmal_component_destroy(camera);
		return NULL;
	}

	//setup still port format
	format = still_port->format;
	format->encoding = MMAL_ENCODING_OPAQUE;
	format->encoding_variant = MMAL_ENCODING_I420;
	format->es->video.width = Width;
	format->es->video.height = Height;
	format->es->video.crop.x = 0;
	format->es->video.crop.y = 0;
	format->es->video.crop.width = Width;
	format->es->video.crop.height = Height;
	format->es->video.frame_rate.num = 1;
	format->es->video.frame_rate.den = 1;
	status = mmal_port_format_commit(still_port);
	if (status != MMAL_SUCCESS)
	{
		printf("Couldn't set still port format : error %d", status);
		mmal_component_destroy(camera);
		return NULL;
	}

	//apply all camera parameters
	raspicamcontrol_set_all_parameters(camera, &CameraParameters);

	//enable the camera
	status = mmal_component_enable(camera);
	if (status != MMAL_SUCCESS)
	{
		printf("Couldn't enable camera\n");
		mmal_component_destroy(camera);
		return NULL;	
	}

	return camera;
}

MMAL_COMPONENT_T* CCamera::CreateSplitterComponentAndSetupPorts(MMAL_PORT_T* video_output_port)
{
	MMAL_COMPONENT_T *splitter = 0;
	MMAL_ES_FORMAT_T *format;
	MMAL_PORT_T *input_port = NULL, *output_port = NULL;
	MMAL_STATUS_T status;

	//create the camera component
	status = mmal_component_create(MMAL_COMPONENT_DEFAULT_VIDEO_SPLITTER, &splitter);
	if (status != MMAL_SUCCESS)
	{
		printf("Failed to create splitter component\n");
		goto error;
	}

	//check we have output ports
	if (splitter->output_num != 4 || splitter->input_num != 1)
	{
		printf("Splitter doesn't have correct ports: %d, %d\n",splitter->input_num,splitter->output_num);
		goto error;
	}

	//get the ports
	input_port = splitter->input[0];
	mmal_format_copy(input_port->format,video_output_port->format);
	input_port->buffer_num = 3;
	status = mmal_port_format_commit(input_port);
	if (status != MMAL_SUCCESS)
	{
		printf("Couldn't set resizer input port format : error %d", status);
		goto error;
	}

	for(int i = 0; i < splitter->output_num; i++)
	{
		output_port = splitter->output[i];
		output_port->buffer_num = 3;
		mmal_format_copy(output_port->format,input_port->format);
		status = mmal_port_format_commit(output_port);
		if (status != MMAL_SUCCESS)
		{
			printf("Couldn't set resizer output port format : error %d", status);
			goto error;
		}
	}

	return splitter;

error:
	if(splitter)
		mmal_component_destroy(splitter);
	return NULL;
}

bool CCamera::Init(int width, int height, int framerate)
{
	//init broadcom host - QUESTION: can this be called more than once??
	bcm_host_init();

	//store basic parameters
	Width = width;       
	Height = height;
	FrameRate = framerate;

	// Set up the camera_parameters to default
	raspicamcontrol_set_defaults(&CameraParameters);

	MMAL_COMPONENT_T *camera = 0;
	MMAL_COMPONENT_T *splitter = 0;
	MMAL_CONNECTION_T* vid_to_split_connection = 0;
	MMAL_PORT_T *video_port = NULL;
	MMAL_STATUS_T status;
	CEncoderOutput* encoder_output = 0;
	CSyncOutput* sync_output = 0;

	//create the camera component
	camera = CreateCameraComponentAndSetupPorts();
	if (!camera)
		goto error;

	//get the video port
	video_port = camera->output[MMAL_CAMERA_VIDEO_PORT];
	video_port->buffer_num = 3;

	//create the splitter component
	splitter = CreateSplitterComponentAndSetupPorts(video_port);
	if(!splitter)
		goto error;

	//create and enable a connection between the video output and the resizer input
	status = mmal_connection_create(&vid_to_split_connection, video_port, splitter->input[0], MMAL_CONNECTION_FLAG_TUNNELLING | MMAL_CONNECTION_FLAG_ALLOCATION_ON_INPUT);
	if (status != MMAL_SUCCESS)
	{
		printf("Failed to create connection\n");
		goto error;
	}
	status = mmal_connection_enable(vid_to_split_connection);
	if (status != MMAL_SUCCESS)
	{
		printf("Failed to enable connection\n");
		goto error;
	}

	//setup all the outputs
	encoder_output = new CEncoderOutput();
	if(!encoder_output->Init(Width,Height,splitter))
	{
		printf("Failed to initialize encoder output\n");
		goto error;
	}
	
	sync_output = new CSyncOutput();
	if(!sync_output->Init(splitter))
	{
		printf("Failed to initialize sync output\n");
		goto error;
	}

	//begin capture
	if (mmal_port_parameter_set_boolean(video_port, MMAL_PARAMETER_CAPTURE, 1) != MMAL_SUCCESS)
	{
		printf("Failed to start capture\n");
		goto error;
	}

	//store created info
	CameraComponent = camera;
	SplitterComponent = splitter;
	VidToSplitConn = vid_to_split_connection;
	SyncOutput = sync_output;
	EncoderOutput = encoder_output;

	//return success
	printf("Camera successfully created\n");
	return true;

error:
	if(vid_to_split_connection)
		mmal_connection_destroy(vid_to_split_connection);
	if(camera)
		mmal_component_destroy(camera);
	if(splitter)
		mmal_component_destroy(splitter);
	if(encoder_output)
	{
		encoder_output->Release();
		delete encoder_output;
	}
	if(sync_output)
	{
		sync_output->Release();
		delete sync_output;
	}
	return false;
}

void CCamera::Release()
{
	if(EncoderOutput)
	{
		EncoderOutput->Release();
		delete EncoderOutput;
		EncoderOutput = NULL;
	}
	if(SyncOutput)
	{
		SyncOutput->Release();
		delete SyncOutput;
		SyncOutput = NULL;
	}
	if(VidToSplitConn)
		mmal_connection_destroy(VidToSplitConn);
	if(CameraComponent)
		mmal_component_destroy(CameraComponent);
	if(SplitterComponent)
		mmal_component_destroy(SplitterComponent);
	VidToSplitConn = NULL;
	CameraComponent = NULL;
	SplitterComponent = NULL;
}

void CCamera::OnCameraControlCallback(MMAL_PORT_T *port, MMAL_BUFFER_HEADER_T *buffer)
{
	printf("Camera control callback\n");
}

CSyncOutput::CSyncOutput()
{
	memset(this,0,sizeof(CSyncOutput));
}

CSyncOutput::~CSyncOutput()
{
}

bool CSyncOutput::Init(MMAL_COMPONENT_T* input_component)
{
}

void CSyncOutput::Release()
{
	if(OutputQueue)
		mmal_queue_destroy(OutputQueue);
	if(BufferPool)
		mmal_port_pool_destroy(BufferPort,BufferPool);
	memset(this,0,sizeof(CSyncOutput));
}

CEncoderOutput::CEncoderOutput()
{
	memset(this,0,sizeof(CEncoderOutput));
}

CEncoderOutput::~CEncoderOutput()
{

}

bool CEncoderOutput::Init(int width, int height, MMAL_COMPONENT_T* input_component)
{
	printf("Init camera output with %d/%d\n",width,height);
	Width = width;
	Height = height;

	MMAL_COMPONENT_T *encoder = 0;
	MMAL_CONNECTION_T* connection = 0;
	MMAL_STATUS_T status;
	MMAL_POOL_T* video_buffer_pool = 0;
	MMAL_QUEUE_T* output_queue = 0;

	//using the output 0 of the splitter
	MMAL_PORT_T* input_port = input_component->output[0];

	//create the encoder component, reading from the splitter output
	encoder = CreateEncoderComponentAndSetupPorts(input_port);
	if(!encoder)
		goto error;

	//create and enable a connection between the video output and the encoder input
	status = mmal_connection_create(&connection, input_port, encoder->input[0], MMAL_CONNECTION_FLAG_TUNNELLING | MMAL_CONNECTION_FLAG_ALLOCATION_ON_INPUT);
	if (status != MMAL_SUCCESS)
	{
		printf("Failed to create connection\n");
		goto error;
	}
	status = mmal_connection_enable(connection);
	if (status != MMAL_SUCCESS)
	{
		printf("Failed to enable connection\n");
		goto error;
	}

	//set the buffer pool port to be the resizer output
	BufferPort = encoder->output[0];

	//setup the video buffer callback
	video_buffer_pool = EnablePortCallbackAndCreateBufferPool(BufferPort,VideoBufferCallback,3);
	if(!video_buffer_pool)
		goto error;

	//create the output queue
	output_queue = mmal_queue_create();
	if(!output_queue)
	{
		printf("Failed to create output queue\n");
		goto error;
	}

	EncoderComponent = encoder;
	BufferPool = video_buffer_pool;
	OutputQueue = output_queue;
	Connection = connection;

	return true;

error:

	if(output_queue)
		mmal_queue_destroy(output_queue);
	if(video_buffer_pool)
		mmal_port_pool_destroy(encoder->output[0],video_buffer_pool);
	if(connection)
		mmal_connection_destroy(connection);
	if(encoder)
		mmal_component_destroy(encoder);

	return false;
}

void CEncoderOutput::Release()
{
	if(OutputQueue)
		mmal_queue_destroy(OutputQueue);
	if(BufferPool)
		mmal_port_pool_destroy(BufferPort,BufferPool);
	if(Connection)
		mmal_connection_destroy(Connection);
	if(EncoderComponent)
		mmal_component_destroy(EncoderComponent);
	memset(this,0,sizeof(CEncoderOutput));
}

void CEncoderOutput::OnVideoBufferCallback(MMAL_PORT_T *port, MMAL_BUFFER_HEADER_T *buffer)
{
	//to handle the user not reading frames, remove and return any pre-existing ones
	if(mmal_queue_length(OutputQueue)>=2)
	{
		if(MMAL_BUFFER_HEADER_T* existing_buffer = mmal_queue_get(OutputQueue))
		{
			mmal_buffer_header_release(existing_buffer);
			if (port->is_enabled)
			{
				MMAL_STATUS_T status;
				MMAL_BUFFER_HEADER_T *new_buffer;
				new_buffer = mmal_queue_get(BufferPool->queue);
				if (new_buffer)
					status = mmal_port_send_buffer(port, new_buffer);
				if (!new_buffer || status != MMAL_SUCCESS)
					printf("Unable to return a buffer to the video port\n");
			}	
		}
	}

	//add the buffer to the output queue
	mmal_queue_put(OutputQueue,buffer);

	//printf("Video buffer callback, output queue len=%d\n", mmal_queue_length(OutputQueue));
}

void CEncoderOutput::VideoBufferCallback(MMAL_PORT_T *port, MMAL_BUFFER_HEADER_T *buffer)
{
	((CEncoderOutput*)port->userdata)->OnVideoBufferCallback(port,buffer);
}

MMAL_COMPONENT_T* CEncoderOutput::CreateEncoderComponentAndSetupPorts(MMAL_PORT_T* video_output_port)
{
	MMAL_COMPONENT_T *encoder = 0;
	MMAL_ES_FORMAT_T *format;
    MMAL_PORT_T *encoder_input = NULL, *encoder_output = NULL;
    MMAL_STATUS_T status;
    MMAL_POOL_T *pool;

	//create the encoder component
	status = mmal_component_create(MMAL_COMPONENT_DEFAULT_IMAGE_ENCODER, &encoder);

    if (status != MMAL_SUCCESS)
    {
       vcos_log_error("Unable to create video encoder component");
       goto error;
    }
	
	//check we have output ports
	if (!encoder->input_num || !encoder->output_num)
    {
       status = MMAL_ENOSYS;
       vcos_log_error("Video encoder doesn't have input/output ports");
       goto error;
    }
	
	//get the ports
	encoder_input = encoder->input[0];
    encoder_output = encoder->output[0];    
 
	//same format on output video port and input encoder port
	mmal_format_copy(encoder_input->format, video_output_port->format); 
 
    // We want same format on input and output
    mmal_format_copy(encoder_output->format, encoder_input->format);
 
    // Only supporting H264 at the moment
    encoder_output->format->encoding = MMAL_ENCODING_JPEG;
 
    encoder_output->buffer_size = encoder_output->buffer_size_recommended;
 
    if (encoder_output->buffer_size < encoder_output->buffer_size_min)
       encoder_output->buffer_size = encoder_output->buffer_size_min;
 
    encoder_output->buffer_num = encoder_output->buffer_num_recommended;
 
    if (encoder_output->buffer_num < encoder_output->buffer_num_min)
       encoder_output->buffer_num = encoder_output->buffer_num_min;
 
    //commit the port changes to the output port
    status = mmal_port_format_commit(encoder_output);
 
    if (status != MMAL_SUCCESS)
    {
       vcos_log_error("Unable to set format on video encoder output port");
       goto error;
    }
 
	//set the JPEG quality level
   status = mmal_port_parameter_set_uint32(encoder_output, MMAL_PARAMETER_JPEG_Q_FACTOR, QUALITY_JPEG);

   if (status != MMAL_SUCCESS)
   {
      vcos_log_error("Unable to set JPEG quality");
      goto error;
   }
   
    //enable component
    status = mmal_component_enable(encoder);
    if (status != MMAL_SUCCESS)
    {
       vcos_log_error("Unable to enable video encoder component");
       goto error;
    }
 
    return encoder;
 
    error:
    if (encoder)
       mmal_component_destroy(encoder);
 
    return NULL;
}

MMAL_POOL_T* CEncoderOutput::EnablePortCallbackAndCreateBufferPool(MMAL_PORT_T* port, MMAL_PORT_BH_CB_T cb, int buffer_count)
{
	MMAL_STATUS_T status;
	MMAL_POOL_T* buffer_pool = 0;

	//setup video port buffer and a pool to hold them
	port->buffer_num = buffer_count;
	port->buffer_size = port->buffer_size_recommended;
	printf("Creating pool with %d buffers of size %d\n", port->buffer_num, port->buffer_size);
	buffer_pool = mmal_port_pool_create(port, port->buffer_num, port->buffer_size);
	if (!buffer_pool)
	{
		printf("Couldn't create video buffer pool\n");
		goto error;
	}

	//enable the port and hand it the callback
    port->userdata = (struct MMAL_PORT_USERDATA_T *)this;
	status = mmal_port_enable(port, cb);
	if (status != MMAL_SUCCESS)
	{
		printf("Failed to set video buffer callback\n");
		goto error;
	}

	//send all the buffers in our pool to the video port ready for use
	{
		int num = mmal_queue_length(buffer_pool->queue);
		int q;
		for (q=0;q<num;q++)
		{
			MMAL_BUFFER_HEADER_T *buffer = mmal_queue_get(buffer_pool->queue);
			if (!buffer)
			{
				printf("Unable to get a required buffer %d from pool queue\n", q);
				goto error;
			}
			else if (mmal_port_send_buffer(port, buffer)!= MMAL_SUCCESS)
			{
				printf("Unable to send a buffer to port (%d)\n", q);
				goto error;
			}
		}
	}

	return buffer_pool;

error:
	if(buffer_pool)
		mmal_port_pool_destroy(port,buffer_pool);
	return NULL;
}
