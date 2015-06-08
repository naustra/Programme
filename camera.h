#pragma once

#include "mmalincludes.h"
#include "cameracontrol.h"

class CCamera;

class CSyncOutput
{
public:
	MMAL_BUFFER_HEADER_T*	LockedBuffer;
	MMAL_POOL_T*			BufferPool;
	MMAL_QUEUE_T*			OutputQueue;
	MMAL_PORT_T*			BufferPort;

	CSyncOutput();
	~CSyncOutput();
	bool Init(MMAL_COMPONENT_T* input_component);
	void Release();

};

class CEncoderOutput
{
public:
	int						Width;
	int						Height;
	MMAL_COMPONENT_T*		EncoderComponent;
	MMAL_CONNECTION_T*		Connection;
	MMAL_BUFFER_HEADER_T*	LockedBuffer;
	MMAL_POOL_T*			BufferPool;
	MMAL_QUEUE_T*			OutputQueue;
	MMAL_PORT_T*			BufferPort;

	CEncoderOutput();
	~CEncoderOutput();
	bool Init(int width, int height, MMAL_COMPONENT_T* input_component);
	void Release();
	void OnVideoBufferCallback(MMAL_PORT_T *port, MMAL_BUFFER_HEADER_T *buffer);
	static void VideoBufferCallback(MMAL_PORT_T *port, MMAL_BUFFER_HEADER_T *buffer);
	MMAL_POOL_T* EnablePortCallbackAndCreateBufferPool(MMAL_PORT_T* port, MMAL_PORT_BH_CB_T cb, int buffer_count);
	MMAL_COMPONENT_T* CreateEncoderComponentAndSetupPorts(MMAL_PORT_T* video_output_port);

};

class CCamera
{
public:

private:
	CCamera();
	~CCamera();

	bool Init(int width, int height, int framerate);
	void Release();
	MMAL_COMPONENT_T* CreateCameraComponentAndSetupPorts();
	MMAL_COMPONENT_T* CreateSplitterComponentAndSetupPorts(MMAL_PORT_T* video_ouput_port);

	void OnCameraControlCallback(MMAL_PORT_T *port, MMAL_BUFFER_HEADER_T *buffer);
	static void CameraControlCallback(MMAL_PORT_T *port, MMAL_BUFFER_HEADER_T *buffer);

	int							Width;
	int							Height;
	int							FrameRate;
	RASPICAM_CAMERA_PARAMETERS	CameraParameters;
	MMAL_COMPONENT_T*			CameraComponent;    
	MMAL_COMPONENT_T*			SplitterComponent;
	MMAL_CONNECTION_T*			VidToSplitConn;
	CEncoderOutput*				EncoderOutput;
	CSyncOutput*				SyncOutput;

	friend CCamera* StartCamera(int width, int height, int framerate);
	friend void StopCamera();
};

CCamera* StartCamera(int width, int height, int framerate);
void StopCamera();