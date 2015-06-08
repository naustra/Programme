#include <stdio.h>
#include <unistd.h>
#include "camera.h"

#define MAIN_TEXTURE_WIDTH 640
#define MAIN_TEXTURE_HEIGHT 420

char tmpbuff[MAIN_TEXTURE_WIDTH*MAIN_TEXTURE_HEIGHT*4];

//entry point
int main(int argc, const char **argv)
{
	//init graphics and the camera
	CCamera* cam = StartCamera(MAIN_TEXTURE_WIDTH, MAIN_TEXTURE_HEIGHT,30);

	printf("Running frame loop\n");
	for(int i = 0; i < 3000; i++)
	{
		//One frame on two, both inputs work. Otherwise, only output[1] work
		int texidx = i%2;

		//Display number of frame on output[0] and stock frame in directory for output[1]
	}

	StopCamera();
}
