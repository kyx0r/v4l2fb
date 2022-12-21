/*
 ============================================================================
 Name        : live.c
 Author      : LincolnHard
 Version     :
 Copyright   : free and open
 Description : Hello World in C, Ansi-style
 ============================================================================
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <fcntl.h>              /* low-level i/o control (open)*/
#include <errno.h>
#include <string.h>             /* strerror show errno meaning */
#include <sys/stat.h>           /* getting information about files attributes */
#include <linux/videodev2.h>    /* v4l2 structure */
#include <sys/mman.h>           /* memory mapping */
#include <unistd.h>             /* read write close */
#include <sys/time.h>           /* for select time */
#include <limits.h>             /* for UCHAR_MAX */
#include <linux/fb.h>
#include "draw_framebuffer.c"
#include "video_capture.c"

unsigned char **malloc_char_image(int xres,int yres)
{
	unsigned char **image;
	int j;

	image = (unsigned char **) malloc( sizeof(unsigned char *) * yres);
	if (image == NULL)
		abort();

	for (j=0;j<yres;j++) {
		image[j] = (unsigned char *) malloc(sizeof(unsigned char) * xres);
		if(image[j] == NULL)
			abort();
	}
	return image;
}

void free_char_image(unsigned char **image,int yres)
{
	int j;
	for(j=0;j<yres;j++)
		free(image[j]);
	free((char*)image);
}


/* Given an image and a mask, it will return the convolution result.
Note that each mask needs to be sent separately.
Also note that this method deals with special edge cases by ignoring 
a 1 pixel border around the image */
int **convolve(unsigned char** image_in, int** mask, int mask_size, int width,
		int height)
{
	/* Copy of input image with extra height and width for working */
	static int** work_image;
	
	/* Results of convolution will be saved to this 2D array */
	static int** out_image;
	int temp = 0;
	
	/* Allocate memory for workable image */
	if (!work_image) {
		work_image = malloc(height * sizeof(int*));
		for (int i = 0; i < height; i++)
			work_image[i] = malloc(width * sizeof(int));
	}
	/* Copy contents of input image to the workable copy */
	for (int i = 0; i < height; i++)
		for (int j = 0; j < width; j++)
			work_image[i][j] = (int)image_in[i][j];
	if (!out_image) {
		/* Allocate memory for output int** */
		out_image = malloc(height * sizeof(int*));
		for (int i = 0; i < height; i++)
			out_image[i] = malloc(width * sizeof(int));
	}
	
	/* We will be ignoring the 1 pixel border around image for edge cases */
	for (int i = 1; i < height-1; i++) {
		for (int j = 1; j < width-1; j++) {
			/* Get pixel and the neighbours and then apply onto the mask matrix */
			if (mask_size == 3) { // Multiplies the 3x3 matrix with a pixel and its 8 neighbours
				temp = (work_image[i-1][j-1] * mask[0][0]) + (work_image[i-1][j] * mask[0][1]) + 
					(work_image[i-1][j+1] * mask[0][2]) + (work_image[i][j-1] * mask[1][0]) + 
					(work_image[i][j] * mask[1][1]) + (work_image[i][j+1] * mask[1][2]) + 
					(work_image[i+1][j-1] * mask[2][0]) + (work_image[i+1][j] * mask[2][1]) + 
					(work_image[i+1][j+1] * mask[2][2]);
					
			} else if (mask_size == 2) { // Multiplies the 2x2 matrix with pixel and its 3 neighbours
				temp = (work_image[i][j] * mask[0][0]) + (work_image[i+1][j+1] * mask[1][1]) + 
					(work_image[i][j+1] * mask[0][1]) + (work_image[i+1][j] * mask[1][0]);
			}
			out_image[i][j] = temp;
		}
	}
	return out_image;
}

/* open image file from file_in and write it out to file_out */
void process_image(unsigned char **image_in, unsigned char **image_out,
				int width, int height, int set_sobel)
{
	/* Robert's cross masks */
	static int** mask_one;
	static int** mask_two;
	
	/* Sobel masks */
	static int** smask_one;
	static int** smask_two;
	
	int** first;
	int** second;
	
	int temp; // Holds current convoluted value
	int threshold = 127; // Darker images will require lower thresholds
	
	if (!mask_one) {
		/* Allocate memory for the 2 robert's cross masks */ 
		mask_one = malloc(2 * sizeof(int*));
		mask_two = malloc(2 * sizeof(int*));
		for (int i = 0; i < 2; i++) {
			mask_one[i] = malloc(2 * sizeof(int));
			mask_two[i] = malloc(2 * sizeof(int));
		}
		
		/* Allocate memory for the 2 sobel masks */
		smask_one = malloc(3 * sizeof(int*));
		smask_two = malloc(3 * sizeof(int*));
		for (int i = 0; i < 3; i++) {
			smask_one[i] = malloc(3 * sizeof(int));
			smask_two[i] = malloc(3 * sizeof(int));
		}
	}
	
	/* Assign values of Robert's cross mask one */
	mask_one[0][0] = 1;
	mask_one[0][1] = 0;
	mask_one[1][0] = 0;
	mask_one[1][1] = -1;
	
	/* Assign values of Robert's cross mask two */
	mask_two[0][0] = 0;
	mask_two[0][1] = 1;
	mask_two[1][0] = -1;
	mask_two[1][1] = 0;
	
	/* Assign values of Sobel vertical mask */
	smask_one[0][0] = -1;
	smask_one[0][1] = 0;
	smask_one[0][2] = 1;
	smask_one[1][0] = -2;
	smask_one[1][1] = 0;
	smask_one[1][2] = 2;
	smask_one[2][0] = -1;
	smask_one[2][1] = 0;
	smask_one[2][2] = 1;
	
	/* Assign values of Sobel horizontal mask */
	smask_two[0][0] = 1;
	smask_two[0][1] = 2;
	smask_two[0][2] = 1;
	smask_two[1][0] = 0;
	smask_two[1][1] = 0;
	smask_two[1][2] = 0;
	smask_two[2][0] = -1;
	smask_two[2][1] = -2;
	smask_two[2][2] = -1;
	
	if (set_sobel == 1) { /* Did user select sobel or not? */
		first = convolve(image_in, smask_one, 3, width, height);
		second = convolve(image_in, smask_two, 3, width, height);
	} else {
		first = convolve(image_in, mask_one, 2, width, height);
		second = convolve(image_in, mask_two, 2, width, height);
	}
  
	/* Get convolution result and add them to get the final pixel */
	for (int i = 0; i < height; i++) {
		for (int j = 0; j < width; j++) {
			temp = abs(first[i][j]) + abs(second[i][j]);
			if (temp > threshold)
				temp = threshold;
			image_out[i][j] = (unsigned char)temp;
		}
	}
}
/* check the supported webcam resolutions using $v4l2-ctl --list-formats-ext */
int main(int argc, char** argv) {
	int width = 0, height = 0;

	if (argc >= 1) width = atoi(argv[0]);
	if (argc >= 2) height = atoi(argv[1]);

	if (width == 0) width = 640;
	if (height == 0) height = 480;

	printf("Using size: %dx%d.\n", width, height);
	sleep(1); // sleep one second

	unsigned char src_image[width * height * 3];
	unsigned char *p;
	unsigned char **_src_image = malloc_char_image(width, height);
	unsigned char **image_out = malloc_char_image(width, height);
	init_framebuffer();
	init_video_capture(width, height);
	char key = 0;

	while (1) {
		key = video_capture(src_image, width, height);
		p = src_image;
		for(int i=0;i<height;i++) {
			for(int j=0;j<width;j++) {
				//conversion formula of rgb to gray
				_src_image[i][j] = p[0]*0.3 + p[1]*0.59 + p[2]*0.11;
				p+=3;
			}
		}
		process_image(_src_image, image_out, width, height, 1);
		p = src_image;
		for(int i=0;i<height;i++) {
			for(int j=0;j<width;j++)
			{
				p[0] = image_out[i][j];
				p[1] = image_out[i][j];
				p[2] = image_out[i][j];
				p+=3;
			}
		}
		draw_framebuffer(src_image, width, height);
		if (key == 'q')
			break;
	}
	free_char_image(image_out, height);
	free_char_image(_src_image, height);
	free_video_capture();
	free_framebuffer();
	return EXIT_SUCCESS;
}
