#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifdef __cplusplus_cli
using namespace System;
using namespace System::IO;
using namespace System::IO::Compression;
using namespace System::Runtime::InteropServices;
#else
#include "zlib.h"
#endif

#define MAGIC (0x00464343)
#define ZLIB_CHUNK

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
	// Ensure that there is an argument
	if(argc < 2) {
		fprintf(stderr, "ccfex 20210711\n");
		fprintf(stderr, "USAGE: ccfex <ccffile>\n");
		return(EXIT_FAILURE);
	}

	// Open the file stream
#ifdef __cplusplus_cli
	fprintf(stderr, "Beginning extraction of %s using .NET.\n", argv[1]);
#else
	fprintf(stderr, "Beginning extraction of %s using zlib.\n", argv[1]);
#endif
	FILE *infile = fopen(argv[1], "rb");
	if(infile == NULL) {
		fprintf(stderr, "File %s couldn't be opened.\n", argv[1]);
		return(EXIT_FAILURE);
	}

	// Local variables
	uint32_t magic, chunksize, filecount, i;
	// A list of file descriptors
	ccffile **fileentries;
	// A buffer on the stack for reading filenames from the file descriptor
	char temp[23] = "                    __";
	// Verify the header (C, C, F, 0x00)
	fread(&magic, 4, 1, infile);
	if(magic != MAGIC) {
		fprintf(stderr, "File %s isn't a valid CCF archive. %X\n", argv[1], magic);
		return(EXIT_FAILURE);
	}
	// Jump to 0x10 and read the chunk size and number of files
	fseek(infile, 16, SEEK_SET); fread(&chunksize, 4, 1, infile);
	fseek(infile, 20, SEEK_SET); fread(&filecount, 4, 1, infile);
	if(filecount == 0) {
		fprintf(stderr, "File %s contains no files.\n", argv[1]);
		return(EXIT_FAILURE);
	}
	// Allocate a list to store pointers to file descriptors
	fileentries = (ccffile **)malloc(sizeof(ccffile *) * filecount);
	if(fileentries == NULL) {
		fprintf(stderr, "Couldn't allocate memory for file list.\n");
		return(EXIT_FAILURE);
	}
	// Jump to end of header
	fseek(infile, 0x20, SEEK_SET);
	for(i = 0; i < filecount; i++) {
		// Copy the file descriptor into the "fileentries" list
		fileentries[i] = (ccffile *)malloc(sizeof(ccffile) * filecount);
		if(fileentries[i] == NULL) {
			fprintf(stderr, "Couldn't allocate memory for file list.\n");
			return(EXIT_FAILURE);
		}
		fread(fileentries[i], 32, 1, infile);
		// Copy the name into the temporary buffer
		memcpy(temp, fileentries[i]->filename, 20);
		fprintf(stderr, "%u  %s  %u %u %u\n", i, temp, fileentries[i]->offset, fileentries[i]->datasize, fileentries[i]->filesize);
	}

	unsigned char *databuffer, *filebuffer;
	int retval;
	FILE *outfile;
	for(i = 0; i < filecount; i++) {
		// Copy the name into the temporary buffer
		memcpy(temp, fileentries[i]->filename, 20);
		// Open the output file stream
		outfile = fopen(temp, "wb");
		if(outfile == NULL) {
			fprintf(stderr, "Couldn't open %s for writing.\n", temp);
			return(EXIT_FAILURE);
		}
		// Jump to the beginning of the data for this file in the input stream
		fseek(infile, fileentries[i]->offset * chunksize, SEEK_SET);
		// Copy compressed data to a memory buffer capable of holding it
		databuffer = (unsigned char*)malloc(fileentries[i]->datasize);
		fread(databuffer, 1, fileentries[i]->datasize, infile);
		if(fileentries[i]->filesize == fileentries[i]->datasize) {
			// Uncompressed data - copy to output stream
			fwrite(databuffer, 1, fileentries[i]->datasize, outfile);
		} else {
			// Uncompress the data
			filebuffer = (unsigned char*)malloc(fileentries[i]->filesize);
			long size = fileentries[i]->filesize;

#ifdef __cplusplus_cli
			if (databuffer[0] != 0x78 || databuffer[1] != 0x9C) {
				fprintf(stderr, "zlib header (789C) not found at beginning of compressed data.\n");
				return(EXIT_FAILURE);
			}

			{
				// Create input and output streams pointing to unmanaged memory
				// Streams will be flushed and disposed once out of scope
				UnmanagedMemoryStream inputStream(databuffer + 2, fileentries[i]->datasize - 6, fileentries[i]->datasize - 6, FileAccess::Read);
				UnmanagedMemoryStream outputStream(filebuffer, fileentries[i]->filesize, fileentries[i]->filesize, FileAccess::Write);

				DeflateStream decompressor(%inputStream, CompressionMode::Decompress);
				decompressor.CopyTo(%outputStream);
			}

			// Verify adler32 checksum
			uint32_t a = 1, b = 0;
			for (uint8_t* ptr = filebuffer; ptr < filebuffer + fileentries[i]->filesize; ptr++) {
				a = (a + *ptr) % 65521;
				b = (b + a) % 65521;
			}
			uint32_t checksum = b << 16 | a;
			uint8_t* checksum_location = databuffer + fileentries[i]->datasize - 4;
			bool checksum_match = (checksum & 0xFF000000) >> 24 == checksum_location[0]
				&& (checksum & 0x00FF0000) >> 16 == checksum_location[1]
				&& (checksum & 0x0000FF00) >> 8 == checksum_location[2]
				&& (checksum & 0x000000FF) >> 0 == checksum_location[3];
			if (!checksum_match) {
				fprintf(stderr, "Alder-32 checksum for compressed data does not match.\n");
				return(EXIT_FAILURE);
			}
#else
			retval = uncompress(filebuffer, &size, databuffer, fileentries[i]->datasize);
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
#endif

			// Write the uncompressed data to the output stream
			fwrite(filebuffer, 1, size, outfile);
			free(filebuffer);
		}
		free(databuffer);
		fclose(outfile);
		fprintf(stderr, ".");
	}

	fprintf(stderr, "\n");
	fclose(infile);
	return(EXIT_SUCCESS);
}
