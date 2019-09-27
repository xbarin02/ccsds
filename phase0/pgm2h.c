#include <stdio.h>
#include <stdlib.h>

#include "common.h"
#include "frame.h"

int main(int argc, char *argv[])
{
	struct frame frame;
	size_t height, width;
	size_t size;
	int *data;
	size_t i;

	if (argc < 2) {
		fprintf(stderr, "[ERROR] argument expected\n");
		return EXIT_FAILURE;
	}

	dprint (("[DEBUG] loading...\n"));

	if (frame_load_pgm(&frame, argv[1])) {
		fprintf(stderr, "[ERROR] unable to load image\n");
		return EXIT_FAILURE;
	}

	if (frame.width > (1<<20) || frame.width < 17) {
		fprintf(stderr, "[ERROR] unsupported image width\n");
		return EXIT_FAILURE;
	}

	if (frame.height < 17) {
		fprintf(stderr, "[ERROR] unsupported image height\n");
		return EXIT_FAILURE;
	}

	height = ceil_multiple8(frame.height);
	width = ceil_multiple8(frame.width);
	size = height * width;
	data = frame.data;

	if (argc < 3) {
		fprintf(stderr, "[ERROR] second argument expected\n");
		return EXIT_FAILURE;
	}

	if (freopen(argv[2], "w", stdout) == NULL) {
		fprintf(stderr, "[ERROR] unable to open output file\n");
		return EXIT_FAILURE;
	}

	puts("#include \"frame.h\"\n");

	printf("int input_data[%lu] = {", size);
	for (i = 0; i < size; ++i) {
		if (i % 16 == 0) {
			puts("");
		}
		printf("%3i", data[i]);
		if (i < size - 1) {
			printf(", ");
		}

	}
	puts("};\n");

	printf("struct frame input_frame = { %lu, %lu, %lu, input_data };\n",
		(unsigned long)frame.height,
		(unsigned long)frame.width,
		(unsigned long)frame.bpp
	);

	return 0;
}
