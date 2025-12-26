
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
	unsigned int threshold;				   /** Threshold */
	unsigned int currentRow;			   /** Current row being processed */
	unsigned int currentPixel;			   /** Current pixel being processed */
	unsigned int outputPixel;			   /** Output pixel */
	unsigned int readBytes;				   /** Number of bytes read from input file */
	unsigned int writeBytes;			   /** Number of bytes written to output file */
	unsigned int totalBytes;			   /** Total number of bytes to send/receive a message */
	unsigned int numPixels;				   /** Number of neighbour pixels (including current pixel) */
	unsigned int currentWorker;			   /** Current worker process */
	tPixelVector vector;				   /** Vector of neighbour pixels */
	int imageDimensions[2];				   /** Dimensions of input image */
	double timeStart, timeEnd;			   /** Time stamps to calculate the filtering time */
	int size, rank, tag;				   /** Number of process, rank and tag */
	MPI_Status status;					   /** Status information for received messages */

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
	if (argc != 4)
	{

		if (rank == 0)
			printf("Usage: ./bmpFilterStatic sourceFile destinationFile threshold\n");

		MPI_Finalize();
		exit(0);
	}

	// Get input arguments...
	sourceFileName = argv[1];
	destinationFileName = argv[2];
	threshold = atoi(argv[3]);

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

		// Reparto de filas para pasarla a los worker
		rowsPerProcess = totalRows / (size - 1);
		int extraRows = totalRows % (size - 1);

		// Enviar datos a los Workers, cada uno se cacho
		for (currentWorker = 1; currentWorker < size; currentWorker++)
		{
			// Calculamos filas
			rowsSentToWorker = rowsPerProcess;
			if (currentWorker == size - 1)
			{
				rowsSentToWorker += extraRows;
			}

			// Enviamos filas y el tamaño
			MPI_Send(&rowsSentToWorker, 1, MPI_INT, currentWorker, tag, MPI_COMM_WORLD);
			MPI_Send(&rowSize, 1, MPI_INT, currentWorker, tag, MPI_COMM_WORLD);

			// Enviamos la porción de imagen que le toca al worker
			unsigned char *startPtr = inputBuffer + ((currentWorker - 1) * rowsPerProcess * rowSize);
			totalBytes = rowsSentToWorker * rowSize;
			MPI_Send(startPtr, totalBytes, MPI_UNSIGNED_CHAR, currentWorker, tag, MPI_COMM_WORLD);
		}

		// Debug
		printf("Master: Envío finalizado. Esperando respuestas de los workers...\n");
        fflush(stdout);

		// Bucle para la recepción de los worker (desordenada pero identificada)
        for (int i = 1; i < size; i++)
        {
            // Recibimos que worker nos habla
            int rowsRecv = 0;
            MPI_Recv(&rowsRecv, 1, MPI_INT, MPI_ANY_SOURCE, tag, MPI_COMM_WORLD, &status);

            // Vemos y almacenamos quien ha sido
            int sourceWorker = status.MPI_SOURCE;

            // Calculamos el paquete del worker
            rowsSentToWorker = rowsPerProcess;
            if (sourceWorker == size - 1)
            {
                rowsSentToWorker += extraRows;
            }

            //Calculamos la posicion del puntero exacto en el buffer final
            auxPtr = outputBuffer + ((sourceWorker - 1) * rowsPerProcess * rowSize);
            totalBytes = rowsSentToWorker * rowSize;

            // Recibimos la imagen en su sitio
            MPI_Recv(auxPtr, totalBytes, MPI_UNSIGNED_CHAR, sourceWorker, tag, MPI_COMM_WORLD, &status);
        }

		// Escribimos la imagen
		write(outputFile, outputBuffer, imageSize);

		// Liberamos memoria
		free(inputBuffer);
		free(outputBuffer);

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
		// Recibimos informacion del master
		MPI_Recv(&rowsSentToWorker, 1, MPI_INT, 0, tag, MPI_COMM_WORLD, &status);
		MPI_Recv(&rowSize, 1, MPI_INT, 0, tag, MPI_COMM_WORLD, &status);

		totalBytes = rowSize * rowsSentToWorker;

		// Reservamos la memoria de los buffer
		inputBuffer = (unsigned char *)malloc(totalBytes);
		outputBuffer = (unsigned char *)malloc(totalBytes);

		// Recibimos los pixeles que nos tocan
		MPI_Recv(inputBuffer, totalBytes, MPI_UNSIGNED_CHAR, 0, tag, MPI_COMM_WORLD, &status);

		// Recorremos cada fila recibida
		for (currentRow = 0; currentRow < rowsSentToWorker; currentRow++)
		{
			// Debug
			if (currentRow % 50 == 0)
			{
				printf("Worker %d: Procesando fila %d de %d\n", rank, currentRow, rowsSentToWorker);
				fflush(stdout);
			}

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

		// Devolvemos al master el resultado
		MPI_Send(&rowsSentToWorker, 1, MPI_INT, 0, tag, MPI_COMM_WORLD);
		MPI_Send(outputBuffer, totalBytes, MPI_UNSIGNED_CHAR, 0, tag, MPI_COMM_WORLD);

		// Liberamos espacio en memoria
		free(inputBuffer);
		free(outputBuffer);
	}

	// Finish MPI environment
	MPI_Finalize();
}