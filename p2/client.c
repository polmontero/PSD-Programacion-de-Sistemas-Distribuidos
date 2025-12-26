#include "client.h"

unsigned int readBet()
{

	int isValid, bet = 0;
	xsd__string enteredMove;

	// While player does not enter a correct bet...
	do
	{

		// Init...
		enteredMove = (xsd__string)malloc(STRING_LENGTH);
		bzero(enteredMove, STRING_LENGTH);
		isValid = TRUE;

		printf("Enter a value:");
		fgets(enteredMove, STRING_LENGTH - 1, stdin);
		enteredMove[strlen(enteredMove) - 1] = 0;

		// Check if each character is a digit
		for (int i = 0; i < strlen(enteredMove) && isValid; i++)
			if (!isdigit(enteredMove[i]))
				isValid = FALSE;

		// Entered move is not a number
		if (!isValid)
			printf("Entered value is not correct. It must be a number greater than 0\n");
		else
			bet = atoi(enteredMove);

	} while (!isValid);

	printf("\n");
	free(enteredMove);

	return ((unsigned int)bet);
}

unsigned int readOption()
{

	unsigned int bet;

	do
	{
		printf("What is your move? Press %d to hit a card and %d to stand\n", PLAYER_HIT_CARD, PLAYER_STAND);
		bet = readBet();
		if ((bet != PLAYER_HIT_CARD) && (bet != PLAYER_STAND))
			printf("Wrong option!\n");
	} while ((bet != PLAYER_HIT_CARD) && (bet != PLAYER_STAND));

	return bet;
}

int main(int argc, char **argv)
{

	struct soap soap;				  /** Soap struct */
	char *serverURL;				  /** Server URL */
	blackJackns__tMessage playerName; /** Player name */
	blackJackns__tBlock gameStatus;	  /** Game status */
	unsigned int playerMove;		  /** Player's move */
	int resCode, gameId;			  /** Result and gameId */
	int betInfo;					  // Variable para ver como llega la informacion de la apuesta, da el stack.
	unsigned int regist = 0;		  // Variable para ver si el registro ha sido correcto
	unsigned int pass = 0;			  // Variable para ver si el jugador se ha pasado de 21 puntos

	if (argc != 2)
	{
		printf("Usage: %s http://server:port\n", argv[0]);
		return 1;
	}

	serverURL = argv[1];
	soap_init(&soap);

	// Bucle para registrar
	while (!regist)
	{
		// Reservamos memoria
		allocClearMessage(&soap, &playerName);
		allocClearBlock(&soap, &gameStatus);

		// Nombre
		printf("Introduce your name: ");
		if (fgets(playerName.msg, STRING_LENGTH, stdin) == NULL)
		{
			fprintf(stderr, "Error reading input.\n");
			soap_end(&soap);
			soap_done(&soap);
			return 1;
		}

		// Eliminamos salto de línea y actualizamos tamaño
		playerName.msg[strcspn(playerName.msg, "\n")] = '\0';
		playerName.__size = strlen(playerName.msg);

		if (soap_call_blackJackns__register(&soap, serverURL, NULL, playerName, &resCode) == SOAP_OK)
		{
			// Si el nombre se repite
			if (resCode == ERROR_NAME_REPEATED)
			{
				printf("Name is already used, try again.\n");
			}
			// Falla
			else if (resCode == -1)
			{
				printf("No available rooms, please try again later.\n");
			}
			// Registro completado
			else
			{
				gameId = resCode;
				printf("Registered successfully. Welcome %s, you are in room %d.\n",
					   playerName.msg, gameId);
				printf("---\n");
				regist = 1;
				sleep(1);
			}
		}
		else
		{
			soap_print_fault(&soap, stderr);
			soap_end(&soap);
			soap_done(&soap);
			return 1;
		}
	}

	// Damos info de la apuesta y el stack
	if (soap_call_blackJackns__betInfo(&soap, serverURL, NULL, playerName, gameId, &resCode) == SOAP_OK)
	{
		betInfo = resCode;
		// Comprobamos si la info se mostro bien o no y si es asi le decimos al cliente que la apuesta se hizo.
		if (betInfo != -1)
		{
			// Mensajes que se muestran de informacion al jugador
			printf("There is a default bet: %d \n", 1);
			printf("Your stack is: %d \n", betInfo);
			printf("Your bet is automatically done. \n");
		}
		else
		{
			printf("There is a problem doing your bet. \n");
		}
	}
	else
	{
		soap_print_fault(&soap, stderr);
		soap_end(&soap);
		soap_done(&soap);
		return 1;
	}

	// Cargamos las cartas iniciales
	if (soap_call_blackJackns__deckIn(&soap, serverURL, NULL, playerName, gameId, &resCode) == SOAP_OK)
	{
		if (resCode == 1)
		{
			printf("Dealing the cards ... \n");
			sleep(2);
		}
		else
		{
			printf("Error dealing the cards ... \n");
		}
	}
	else
	{
		soap_print_fault(&soap, stderr);
		soap_end(&soap);
		soap_done(&soap);
		return 1;
	}

	// Bucle de juego
	int done = 0;
	while (!done)
	{
		// Consultamos el estado del juego
		if (soap_call_blackJackns__getStatus(&soap, serverURL, NULL, playerName, gameId, &gameStatus) != SOAP_OK)
		{
			soap_print_fault(&soap, stderr);
			soap_end(&soap);
			soap_done(&soap);
			return 1;
		}

		// Verifica si el juego ha terminado
		if (gameStatus.code == GAME_FINISHED) 
		{
			printf("\n GAME OVER \n");
			printf("%s\n", gameStatus.msgStruct.msg);
			printFancyDeck(&gameStatus.deck);
			done = 1;
			break;
		}

		if (gameStatus.code == TURN_PLAY)
		{
			printf("%s \n", gameStatus.msgStruct.msg);
			printFancyDeck(&gameStatus.deck);

			// Jugamos
			playerMove = readOption();
			unsigned int moveCode = playerMove; 
			unsigned int serverCode = 0;		

			if (soap_call_blackJackns__playerMove(&soap, serverURL, NULL, playerName, gameId, moveCode, &pass, &serverCode) != SOAP_OK)
			{
				soap_print_fault(&soap, stderr);
				soap_end(&soap);
				soap_done(&soap);
				return 1;
			}

			// Si GAME FINISHED
			if (serverCode == GAME_FINISHED)
			{
				// Refrescar para obtener el resultado final
				allocClearBlock(&soap, &gameStatus);
				if (soap_call_blackJackns__getStatus(&soap, serverURL, NULL, playerName, gameId, &gameStatus) != SOAP_OK)
				{
					soap_print_fault(&soap, stderr);
					soap_end(&soap);
					soap_done(&soap);
					return 1;
				}
				printf("\n GAME OVER \n");
				printf("%s \n", gameStatus.msgStruct.msg);
				printFancyDeck(&gameStatus.deck);
				done = 1;
				break;
			}

			// Refrescamos y mostramos el estado tras la jugada para ver la carta del HIT
			allocClearBlock(&soap, &gameStatus);
			if (soap_call_blackJackns__getStatus(&soap, serverURL, NULL, playerName, gameId, &gameStatus) != SOAP_OK)
			{
				soap_print_fault(&soap, stderr);
				soap_end(&soap);
				soap_done(&soap);
				return 1;
			}
			printf("%s\n", gameStatus.msgStruct.msg);
			printFancyDeck(&gameStatus.deck);

			// Se planta
			if (moveCode == PLAYER_STAND)
			{
				printf("Waiting for the other player to finish. \n");
	
			}
			// Si se pasa
			if (pass == 1)
			{
				printf("You busted. Waiting for the other player to finish. \n");
			}
		}
		else if (gameStatus.code == TURN_WAIT)
		{
			// Simplemente esperamos
			sleep(1);
			continue;
		}
		else
		{
			sleep(1);
		}
	}

	soap_end(&soap);
	soap_done(&soap);
	return 0;
}