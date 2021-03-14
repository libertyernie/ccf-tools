#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "zlib.h"

const int ZERO = 0;

// A template for a CCF header (as 32-bit numbers in little endian).
const int HEADER[8] = {
	0x00464343, 0x00000000, 0x00000000, 0x00000000,
	0x00000020, 0x00000000, 0x00000000, 0x00000000};

// A file descriptor for a CCF archive.
typedef struct ccffile {
	// The filename (null-padded ASCII)
	char filename[20];
	// The offset of the file's data with respect to the beginning of the CCF archive, in multiples of the chunk size in the archive's main header.
	uint32_t offset;
	// The number of bytes the file's data takes up within the archive.
	uint32_t datasize;
	// The number of bytes the file's data will take up once decompressed.
	uint32_t filesize;
} ccffile;

int main(int argc, char **argv) {
	ccffile **fileentries;
	FILE *infile, *outfile;
	int retval;
	uint32_t files, size, curpos, i, j;
	char *indata, *outdata;

	// Ensure that there is an argument
	if(argc < 2) {
		fprintf(stderr, "USAGE: ccfex <infile1> [infile2] ...\n");
		return(EXIT_FAILURE);
	}

	// Open output file stream
	fprintf(stderr, "Beginning compression of out.ccf.\n");
	outfile = fopen("out.ccf", "wb");
	if(outfile == NULL) {
		fprintf(stderr, "File out.ccf couldn't be opened.\n");
		return(EXIT_FAILURE);
	}

	//prepare file entries as far as we can for now
	files = argc - 1; //files to be inserted in to the archive
	curpos = files + 1; //current position in 32 byte blocks, after header and file entries have been written (start of data region)

	// Create a list of file descriptors
	fileentries = (ccffile **)malloc(sizeof(ccffile *) * files);
	if(fileentries == NULL) {
		fprintf(stderr, "Couldn't allocate enough memory!\n");
		return(EXIT_FAILURE);
	}
	for(i = 0; i < files; i++) {
		fprintf(stderr, "%u %s ", i, argv[i + 1]);
		// Create a new file descriptor
		fileentries[i] = (ccffile *)malloc(sizeof(ccffile));
		if(fileentries[i] == NULL) {
			fprintf(stderr, "Couldn't allocate enough memory!\n");
			return(EXIT_FAILURE);
		}
		// Truncate the filename to twenty characters if necessary
		int t = strlen(argv[i + 1]);
		if(t > 20) {
			t = 20;
		}
		// Make sure any unused bytes are zero before writing the filename to the header
		memset(fileentries[i]->filename, '\0', 20);
		memcpy(fileentries[i]->filename, argv[i + 1], t);
		// Open the input file stream
		infile = fopen(argv[i + 1], "rb");
		// Determine the uncompressed filesize
		fseek(infile, 0, SEEK_END);
		size = ftell(infile);
		fileentries[i]->filesize = size;
		fileentries[i]->datasize = size * 2 + 12; //needed for zlib, will have the proper size later on
		fclose(infile);
		fprintf(stderr, "%u bytes\n", fileentries[i]->filesize);
	}

	//write header
	fwrite(&HEADER, 4, 8, outfile);
	fseek(outfile, 20, SEEK_SET);
	fwrite(&files, 4, 1, outfile);
	fseek(outfile, 32, SEEK_SET);

	//write placeholders for proper headers
	for(i = 0; i < files; i++) {
		fwrite(&HEADER, 4, 8, outfile);
	}

	//compress and store files, then update the file entries
	for(i = 0; i < files; i++) {
		fprintf(stderr, ".");
		// Copy the input data to memory
		indata = (char *)malloc(sizeof(char) * fileentries[i]->filesize);
		if(indata == NULL) {
			fprintf(stderr, "Couldn't allocate enough memory!\n");
			return(EXIT_FAILURE);
		}
		// Create a place for the output data to go
		outdata = (char *)malloc(sizeof(char) * fileentries[i]->datasize);
		if(outdata == NULL) {
			fprintf(stderr, "Couldn't allocate enough memory!\n");
			return(EXIT_FAILURE);
		}

		// Open the input file stream again
		infile = fopen(argv[i + 1], "rb");
		if(infile == NULL) {
			fprintf(stderr, "Couldn't open file %s\n", argv[i + 1]);
			return(EXIT_FAILURE);
		}
		// Copy to memory
		fread(indata, 1, fileentries[i]->filesize, infile);
		fclose(infile);

		// Retrieve the buffer size
		long datasize = (long)fileentries[i]->datasize;
		// Perform compression, telling zlib to put the correct compressed size into the "datasize" variable
		retval = compress(outdata, &datasize, indata, fileentries[i]->filesize);
		switch(retval) {
			case Z_OK:
				break;
			case Z_MEM_ERROR:
				fprintf(stderr, "Zlib out of memory error.\n");
				return(EXIT_FAILURE);
			case Z_BUF_ERROR:
				fprintf(stderr, "The output filesize is misreported.\n");
				return(EXIT_FAILURE);
			case Z_DATA_ERROR:
				fprintf(stderr, "The compressed data is corrupted.\n");
				return(EXIT_FAILURE);
		}

		// Copy the actual compressed data size to the CCF file descriptor
		if (datasize > 0xFFFFFFFF) {
			fprintf(stderr, "Compressed file too large!\n");
			return(EXIT_FAILURE);
		}
		fileentries[i]->datasize = datasize & 0xFFFFFFFF;
		// Check whether the file became smaller
		if(fileentries[i]->datasize > fileentries[i]->filesize) {
			// The file is smaller now that it has been compressed - write the compressed data
			fileentries[i]->datasize = fileentries[i]->filesize;
			fwrite(indata, 1, fileentries[i]->filesize, outfile);
		} else {
			// Just write the original uncompressed data
			fwrite(outdata, 1, fileentries[i]->datasize, outfile);
		}
		// Add the file data's correct position to its file descriptor, and advance the next file's position as appropriate
		fileentries[i]->offset = curpos;
		curpos += fileentries[i]->datasize / 32 + 1;
		// Jump to the position in the output file stream where the file descriptor should go
		fseek(outfile, 32 * i + 32, SEEK_SET);
		// Write the file descriptor
		fwrite(fileentries[i], sizeof(ccffile), 1, outfile);
		if (files - 1 > i) {
			// Jump to the end of the file so we can pad it to a multiple of the block size of 32 bytes
			fseek(outfile, 0, SEEK_END);
			int end = ftell(outfile);
			for(j = 0; j < 32 - (end % 32); j++) { //pad out to next block
				fwrite(&ZERO, 1, 1, outfile);
			}
		}
		free(indata);
		free(outdata);
	}

	fprintf(stderr, "\n");
	fclose(outfile);
	return(EXIT_SUCCESS);
}
