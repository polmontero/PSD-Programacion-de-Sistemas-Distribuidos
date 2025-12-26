
#include "bmpBlackWhite.h"
#include "mpi.h"
#include <time.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>

/** Show log messages */
#define SHOW_LOG_MESSAGES 1

/** Enable output for filtering information */
#define DEBUG_FILTERING 0

/** Show information of input and output bitmap headers */
#define SHOW_BMP_HEADERS 0

int main(int argc, char **argv)
{

	tBitmapFileHeader imgFileHeaderInput;  /** BMP file header for input image */
	tBitmapInfoHeader imgInfoHeaderInput;  /** BMP info header for input image */
	tBitmapFileHeader imgFileHeaderOutput; /** BMP file header for output image */
	tBitmapInfoHeader imgInfoHeaderOutput; /** BMP info header for output image */
	char *sourceFileName;				   /** Name of input image file */
	char *destinationFileName;			   /** Name of output image file */
	int inputFile, outputFile;			   /** File descriptors */
	unsigned char *outputBuffer;		   /** Output buffer for filtered pixels */
	unsigned char *inputBuffer;			   /** Input buffer to allocate original pixels */
	unsigned char *auxPtr;				   /** Auxiliary pointer */
	unsigned int rowSize;				   /** Number of pixels per row */
	unsigned int rowsPerProcess;		   /** Number of rows to be processed (at most) by each worker */
	unsigned int rowsSentToWorker;		   /** Number of rows to be sent to a worker process */
	unsigned int receivedRows;			   /** Total number of received rows */
	unsigned int threshold;				   /** Threshold */
	unsigned int currentRow;			   /** Current row being processed */
	unsigned int currentPixel;			   /** Current pixel being processed */
	unsigned int outputPixel;			   /** Output pixel */
	unsigned int readBytes;				   /** Number of bytes read from input file */
	unsigned int writeBytes;			   /** Number of bytes written to output file */
	unsigned int totalBytes;			   /** Total number of bytes to send/receive a message */
	unsigned int numPixels;				   /** Number of neighbour pixels (including current pixel) */
	unsigned int currentWorker;			   /** Current worker process */
	unsigned int *processIDs;
	tPixelVector vector;	   /** Vector of neighbour pixels */
	int imageDimensions[2];	   /** Dimensions of input image */
	double timeStart, timeEnd; /** Time stamps to calculate the filtering time */
	int size, rank, tag;	   /** Number of process, rank and tag */
	MPI_Status status;		   /** Status information for received messages */

	// Init
	MPI_Init(&argc, &argv);
	MPI_Comm_size(MPI_COMM_WORLD, &size);
	MPI_Comm_rank(MPI_COMM_WORLD, &rank);
	tag = 1;
	srand(time(NULL));

	// Check the number of processes
	if (size <= 2)
	{
		if (rank == 0)
			printf("This program must be launched with (at least) 3 processes\n");

		MPI_Finalize();
		exit(0);
	}

	// Check arguments
	if (argc != 5)
	{
		if (rank == 0)
			printf("Usage: ./bmpFilterDynamic sourceFile destinationFile threshold numRows\n");

		MPI_Finalize();
		exit(0);
	}

	// Get input arguments...
	sourceFileName = argv[1];
	destinationFileName = argv[2];
	threshold = atoi(argv[3]);
	rowsPerProcess = atoi(argv[4]);

	// Allocate memory for process IDs vector
	processIDs = (unsigned int *)malloc(size * sizeof(unsigned int));

	// Master process
	if (rank == 0)
	{
		// Process starts
		timeStart = MPI_Wtime();

		// Read headers from input file
		readHeaders(sourceFileName, &imgFileHeaderInput, &imgInfoHeaderInput);
		readHeaders(sourceFileName, &imgFileHeaderOutput, &imgInfoHeaderOutput);

		// Write header to the output file
		writeHeaders(destinationFileName, &imgFileHeaderOutput, &imgInfoHeaderOutput);

		// Calculate row size for input and output images
		rowSize = (((imgInfoHeaderInput.biBitCount * imgInfoHeaderInput.biWidth) + 31) / 32) * 4;

		// Show info before processing
		if (SHOW_LOG_MESSAGES)
		{
			printf("[MASTER] Applying filter to image %s (%dx%d) with threshold %d. Generating image %s\n", sourceFileName,
				   rowSize, imgInfoHeaderInput.biHeight, threshold, destinationFileName);
			printf("Number of workers:%d -> Each worker calculates (at most) %d rows\n", size - 1, rowsPerProcess);
		}

		// Show headers...
		if (SHOW_BMP_HEADERS)
		{
			printf("Source BMP headers:\n");
			printBitmapHeaders(&imgFileHeaderInput, &imgInfoHeaderInput);
			printf("Destination BMP headers:\n");
			printBitmapHeaders(&imgFileHeaderOutput, &imgInfoHeaderOutput);
		}

		// Open source image
		if ((inputFile = open(sourceFileName, O_RDONLY)) < 0)
		{
			printf("ERROR: Source file cannot be opened: %s\n", sourceFileName);
			exit(1);
		}

		// Open target image
		if ((outputFile = open(destinationFileName, O_WRONLY | O_APPEND, 0777)) < 0)
		{
			printf("ERROR: Target file cannot be open to append data: %s\n", destinationFileName);
			exit(1);
		}

		// Allocate memory to copy the bytes between the header and the image data
		outputBuffer = (unsigned char *)malloc((imgFileHeaderInput.bfOffBits - BIMAP_HEADERS_SIZE) * sizeof(unsigned char));

		// Copy bytes between headers and pixels
		lseek(inputFile, BIMAP_HEADERS_SIZE, SEEK_SET);
		read(inputFile, outputBuffer, imgFileHeaderInput.bfOffBits - BIMAP_HEADERS_SIZE);
		write(outputFile, outputBuffer, imgFileHeaderInput.bfOffBits - BIMAP_HEADERS_SIZE);

		// Liberamos el buffer para poder usarlo para la imagen
		free(outputBuffer);

		// Calculamos tamaño de imagen
		unsigned int totalRows = abs(imgInfoHeaderInput.biHeight);
		unsigned int imageSize = rowSize * totalRows;

		// Reservamos memoria
		inputBuffer = (unsigned char *)malloc(imageSize);
		outputBuffer = (unsigned char *)malloc(imageSize);

		// Leemos la imagen
		lseek(inputFile, imgFileHeaderInput.bfOffBits, SEEK_SET);
		read(inputFile, inputBuffer, imageSize);

		// Variables para controlarlo de forma dinamica
		unsigned int currentRow = 0;
		receivedRows = 0;
		int workersActive = 0;

		// Primero mandamos a todos los worker algo para hacer
		for (currentWorker = 1; currentWorker < size; currentWorker++)
		{
			// Si por algun casual la imagen es pequeña y no hay mas que mandar al worker de inicio se para el bucle
			// Puede pasar si hay muchos, muchos, workers
			if (currentRow >= totalRows)
			{
				int d = 0;
				MPI_Send(&d, 1, MPI_INT, currentWorker, 0, MPI_COMM_WORLD);
				continue;
			}

			unsigned int rowsToSend = rowsPerProcess;
			// Si solo queda un trozo pequeño y restante se guarda lo que quede
			if (currentRow + rowsToSend > totalRows)
			{
				rowsToSend = totalRows - currentRow;
			}

			processIDs[currentWorker] = currentRow;

			// Lo enviamos
			MPI_Send(&rowsToSend, 1, MPI_INT, currentWorker, 1, MPI_COMM_WORLD);
			MPI_Send(&rowSize, 1, MPI_INT, currentWorker, 1, MPI_COMM_WORLD);

			// Enviamos la imagen que toque
			unsigned char *ptr = inputBuffer + (currentRow * rowSize);
			MPI_Send(ptr, rowsToSend * rowSize, MPI_UNSIGNED_CHAR, currentWorker, 1, MPI_COMM_WORLD);

			currentRow += rowsToSend;
			workersActive++;
		}

		// Bucle dinamico, mientras no se haya mandado toda la imagen repartimos trabajo
		while (receivedRows < totalRows)
		{
			// Esperamos y recibimos la respuesta de un wolker que ha terminado su parte
			unsigned int rowsReturned;
			MPI_Recv(&rowsReturned, 1, MPI_INT, MPI_ANY_SOURCE, 1, MPI_COMM_WORLD, &status);

			int sourceWorker = status.MPI_SOURCE;
			unsigned int startRow = processIDs[sourceWorker];

			// Procesamos los datos que nos llegan
			unsigned char *destPtr = outputBuffer + (startRow * rowSize);
			MPI_Recv(destPtr, rowsReturned * rowSize, MPI_UNSIGNED_CHAR, sourceWorker, 1, MPI_COMM_WORLD, &status);

			// Actualizamos el proceso para el bucle
			receivedRows += rowsReturned;

			// Vemos si todavia queda trabajo para asignarle mas al worker que ha acabado
			if (currentRow < totalRows)
			{
				// Calculamos el siguiente trozo
				unsigned int rowsToSend = rowsPerProcess;
				if (currentRow + rowsToSend > totalRows)
				{
					rowsToSend = totalRows - currentRow;
				}

				// Registramos y enviamos
				processIDs[sourceWorker] = currentRow;
				MPI_Send(&rowsToSend, 1, MPI_INT, sourceWorker, 1, MPI_COMM_WORLD);
				MPI_Send(&rowSize, 1, MPI_INT, sourceWorker, 1, MPI_COMM_WORLD);

				unsigned char *ptr = inputBuffer + (currentRow * rowSize);
				MPI_Send(ptr, rowsToSend * rowSize, MPI_UNSIGNED_CHAR, sourceWorker, 1, MPI_COMM_WORLD);

				currentRow += rowsToSend;
			}
			else
			{
				// Si no queda trabajo enviamos que pare
				int d = 0;
				MPI_Send(&d, 1, MPI_INT, sourceWorker, 0, MPI_COMM_WORLD);
			}
		}

		// Escribir resultado final
		write(outputFile, outputBuffer, imageSize);

		// Liberamos la memoria
		free(inputBuffer);
		free(outputBuffer);
		free(processIDs);

		// Close files
		close(inputFile);
		close(outputFile);

		// Process ends
		timeEnd = MPI_Wtime();

		// Show processing time
		printf("Filtering time: %f\n", timeEnd - timeStart);
	}

	// Worker process
	else
	{
		// Bucle infinito pidiendo trabajo
		while (1)
		{
			// Recibimos la orden del master
			MPI_Recv(&receivedRows, 1, MPI_INT, 0, MPI_ANY_TAG, MPI_COMM_WORLD, &status);

			// Si el master manda tag = 0 entonces debemos romper el bucle
			if (status.MPI_TAG == 0)
			{
				break;
			}

			// Si el tag es 1 vemos lo que nos han mandado para procesar
			MPI_Recv(&rowSize, 1, MPI_INT, 0, 1, MPI_COMM_WORLD, &status);

			totalBytes = receivedRows * rowSize;

			// Debug
            if (SHOW_LOG_MESSAGES) {
                printf("   -> [Worker %d] Recibido paquete de %d filas. Procesando...\n", rank, receivedRows);
                fflush(stdout);
            }

			// Reservamos memoria
			inputBuffer = (unsigned char *)malloc(totalBytes);
			outputBuffer = (unsigned char *)malloc(totalBytes);

			// Recibimos los píxeles
			MPI_Recv(inputBuffer, totalBytes, MPI_UNSIGNED_CHAR, 0, 1, MPI_COMM_WORLD, &status);

			// Se procesan los datos como en el estatico
			for (currentRow = 0; currentRow < receivedRows; currentRow++)
			{
				unsigned char *rowIn = inputBuffer + (currentRow * rowSize);
				unsigned char *rowOut = outputBuffer + (currentRow * rowSize);

				// Iteramos por cada píxel de la fila
				for (currentPixel = 0; currentPixel < rowSize; currentPixel++)
				{
					numPixels = 0;

					// Si no es el primero y hay vecino a la izquierda
					if (currentPixel > 0)
					{
						vector[numPixels] = rowIn[currentPixel - 1];
						numPixels++;
					}

					// Para el pixel central siempre se hace
					vector[numPixels] = rowIn[currentPixel];
					numPixels++;

					// Si hay vecino derecho
					if (currentPixel < rowSize - 1)
					{
						vector[numPixels] = rowIn[currentPixel + 1];
						numPixels++;
					}

					// Calculamos color y lo guardamos
					outputPixel = calculatePixelValue(vector, numPixels, threshold, DEBUG_FILTERING);
					rowOut[currentPixel] = (unsigned char)outputPixel;
				}
			}

			// Enviamos proceso a master
			MPI_Send(&receivedRows, 1, MPI_INT, 0, 1, MPI_COMM_WORLD);
			MPI_Send(outputBuffer, totalBytes, MPI_UNSIGNED_CHAR, 0, 1, MPI_COMM_WORLD);

			// Liberamos la memoria
			free(inputBuffer);
			free(outputBuffer);
		}
	}

	// Finish MPI environment
	MPI_Finalize();
}
