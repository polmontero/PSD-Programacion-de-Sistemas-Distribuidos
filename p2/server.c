#include "server.h"

/** Shared array that contains all the games. */
tGame games[MAX_GAMES];

void initGame(tGame *game)
{

	// Init players' name
	memset(game->player1Name, 0, STRING_LENGTH);
	memset(game->player2Name, 0, STRING_LENGTH);

	// Alloc memory for the decks
	clearDeck(&(game->player1Deck));
	clearDeck(&(game->player2Deck));
	initDeck(&(game->gameDeck));

	// Bet and stack
	game->player1Bet = 0;
	game->player2Bet = 0;
	game->player1Stack = INITIAL_STACK;
	game->player2Stack = INITIAL_STACK;

	// Game status variables
	game->endOfGame = FALSE;
	game->status = gameEmpty;
	game->playersSeenResult = 0;

	game->player1Finished = 0;
	game->player2Finished = 0;

	game->endOfGame = 0;

	// Threads
	pthread_mutex_init(&game->mutex, NULL);
	pthread_cond_init(&game->cond, NULL);
}

void initServerStructures(struct soap *soap)
{

	if (DEBUG_SERVER)
		printf("Initializing structures...\n");

	// Init seed
	srand(time(NULL));

	// Init each game (alloc memory and init)
	for (int i = 0; i < MAX_GAMES; i++)
	{
		games[i].player1Name = (xsd__string)soap_malloc(soap, STRING_LENGTH);
		games[i].player2Name = (xsd__string)soap_malloc(soap, STRING_LENGTH);
		allocDeck(soap, &(games[i].player1Deck));
		allocDeck(soap, &(games[i].player2Deck));
		allocDeck(soap, &(games[i].gameDeck));
		initGame(&(games[i]));
	}
}

void initDeck(blackJackns__tDeck *deck)
{

	deck->__size = DECK_SIZE;

	for (int i = 0; i < DECK_SIZE; i++)
		deck->cards[i] = i;
}

void clearDeck(blackJackns__tDeck *deck)
{

	// Set number of cards
	deck->__size = 0;

	for (int i = 0; i < DECK_SIZE; i++)
		deck->cards[i] = UNSET_CARD;
}

tPlayer calculateNextPlayer(tPlayer currentPlayer)
{
	return ((currentPlayer == player1) ? player2 : player1);
}

unsigned int getRandomCard(blackJackns__tDeck *deck)
{

	unsigned int card, cardIndex, i;

	// Get a random card
	cardIndex = rand() % deck->__size;
	card = deck->cards[cardIndex];

	// Remove the gap
	for (i = cardIndex; i < deck->__size - 1; i++)
		deck->cards[i] = deck->cards[i + 1];

	// Update the number of cards in the deck
	deck->__size--;
	deck->cards[deck->__size] = UNSET_CARD;

	return card;
}

unsigned int calculatePoints(blackJackns__tDeck *deck)
{

	unsigned int points = 0;

	for (int i = 0; i < deck->__size; i++)
	{

		if (deck->cards[i] % SUIT_SIZE < 9)
			points += (deck->cards[i] % SUIT_SIZE) + 1;
		else
			points += FIGURE_VALUE;
	}

	return points;
}

void copyGameStatusStructure(blackJackns__tBlock *status, char *message, blackJackns__tDeck *newDeck, int newCode)
{

	// Copy the message
	memset((status->msgStruct).msg, 0, STRING_LENGTH);
	strcpy((status->msgStruct).msg, message);
	(status->msgStruct).__size = strlen((status->msgStruct).msg);

	// Copy the deck, only if it is not NULL
	if (newDeck->__size > 0)
		memcpy((status->deck).cards, newDeck->cards, DECK_SIZE * sizeof(unsigned int));
	else
		(status->deck).cards = NULL;

	(status->deck).__size = newDeck->__size;

	// Set the new code
	status->code = newCode;
}

int blackJackns__register(struct soap *soap, blackJackns__tMessage playerName, int *result)
{

	playerName.msg[playerName.__size] = 0;

	if (DEBUG_SERVER)
		printf("[Register] Registering new player -> [%s]\n", playerName.msg);

	// Busca una room en la que haya hueco
	for (int i = 0; i < MAX_GAMES; i++)
	{
		pthread_mutex_lock(&games[i].mutex);

		// Partida vacia
		if (games[i].status == gameEmpty)
		{
			strncpy(games[i].player1Name, playerName.msg, STRING_LENGTH - 1);
			games[i].status = gameWaitingPlayer;
			*result = i;

			if (DEBUG_SERVER)
			{
				printf("[Register] %s joined room %d as Player 1. Waiting for Player 2...\n",
					   playerName.msg, i);
			}

			// Espera a que se conecte el jugador 2
			while (games[i].status != gameReady)
			{
				pthread_cond_wait(&games[i].cond, &games[i].mutex);
			}

			pthread_mutex_unlock(&games[i].mutex);
			return SOAP_OK;
		}

		// Sala con un jugador
		if (games[i].status == gameWaitingPlayer)
		{
			// Si el jugador1 se llama ya como quiere el 2
			if (strcmp(games[i].player1Name, playerName.msg) == 0)
			{
				*result = ERROR_NAME_REPEATED;
				pthread_mutex_unlock(&games[i].mutex);
				return SOAP_OK;
			}
			// Si no se llama igual lo guardamos
			else
			{
				strncpy(games[i].player2Name, playerName.msg, STRING_LENGTH - 1);
				games[i].status = gameReady;
				*result = i;
				games[i].currentPlayer = (rand() % 2 == 0) ? player1 : player2;
				pthread_cond_signal(&games[i].cond);

				if (DEBUG_SERVER)
				{
					printf("[Register] Player 2 has joined room %d. Game is ready to start.\n", i);
				}

				pthread_mutex_unlock(&games[i].mutex);
				return SOAP_OK;
			}
		}

		pthread_mutex_unlock(&games[i].mutex);
	}

	*result = -1;
	return SOAP_OK;
}

int blackJackns__betInfo(struct soap *soap, blackJackns__tMessage playerName, int gameId, int *result)
{
	unsigned int stack;
	playerName.msg[playerName.__size] = 0;

	if (DEBUG_SERVER)
	{
		printf("[Bet] Entering betInfo() | Player: %s | Game ID: %d\n", playerName.msg, gameId);
	}

	// Mutex bloqueado
	pthread_mutex_lock(&games[gameId].mutex);

	// Conseguimos el stack del jugador que toque
	if (strcmp(playerName.msg, games[gameId].player1Name) == 0)
	{
		stack = games[gameId].player1Stack;
	}
	else if (strcmp(playerName.msg, games[gameId].player2Name) == 0)
	{
		stack = games[gameId].player2Stack;
	}
	// El jugador no concuerda con ninguno de los 2.
	else
	{
		printf("Error: Player name doesn't match: %s \n", playerName.msg);
		pthread_mutex_unlock(&games[gameId].mutex);
		*result = -1;
		return SOAP_OK;
	}

	// Cerramos y devolvemos
	pthread_mutex_unlock(&games[gameId].mutex);

	if (DEBUG_SERVER)
	{
		printf("[Bet] Exiting betInfo() | Stack: %u | Default bet: %u\n", stack, DEFAULT_BET);
	}

	*result = stack;
	return SOAP_OK;
}

int blackJackns__deckIn(struct soap *soap, blackJackns__tMessage playerName, int gameId, int *result)
{
	playerName.msg[playerName.__size] = '\0';

	if (DEBUG_SERVER)
	{
		printf("[DeckIn] Starting deckIn| Player: %s | Game ID: %d\n", playerName.msg, gameId);
	}

	pthread_mutex_lock(&games[gameId].mutex);

	// Inicializas el puntero al mazo
	blackJackns__tDeck *inDeck;

	// El mazo pertenece al jugador 1
	if (strcmp(games[gameId].player1Name, playerName.msg) == 0)
	{
		inDeck = &games[gameId].player1Deck;
	}
	// El mazo pertenece al jugador 2
	else if (strcmp(games[gameId].player2Name, playerName.msg) == 0)
	{
		inDeck = &games[gameId].player2Deck;
	}
	// No pertence a nadie
	else
	{
		printf("Error, player not found. \n");
		pthread_mutex_unlock(&games[gameId].mutex);
		*result = -1;
		return SOAP_OK;
	}

	// Metemos las dos cartas en el mazo
	unsigned int i = getRandomCard(&games[gameId].gameDeck);
	inDeck->cards[inDeck->__size] = i;
	inDeck->__size++;
	i = getRandomCard(&games[gameId].gameDeck);
	inDeck->cards[inDeck->__size] = i;
	inDeck->__size++;
	// pthread_cond_wait(&games[gameId].cond, &games[gameId].mutex);

	*result = 1;
	// pthread_cond_signal(&games[gameId].cond);
	pthread_mutex_unlock(&games[gameId].mutex);
	return SOAP_OK;
}

int blackJackns__getStatus(struct soap *soap, blackJackns__tMessage playerName, int gameId, blackJackns__tBlock *status)
{
	playerName.msg[playerName.__size] = '\0';
	pthread_mutex_lock(&games[gameId].mutex);

	if (DEBUG_SERVER)
		printf("[Status] getStatus() | Player: %s | Game ID: %d\n", playerName.msg, gameId);

	blackJackns__tBlock block;
	memset(&block, 0, sizeof(block));

	// Juego aún no listo
	if (games[gameId].status == gameWaitingPlayer)
	{
		block.code = gameWaitingPlayer;
		block.msgStruct.msg = soap_strdup(soap, "Waiting for another player to join...");
		block.msgStruct.__size = strlen(block.msgStruct.msg);
		pthread_mutex_unlock(&games[gameId].mutex);
		*status = block;
		return SOAP_OK;
	}

	// Verificar si el juego ha terminado
	if (games[gameId].player1Finished && games[gameId].player2Finished)
	{
		block.code = GAME_FINISHED; 

		// Calcular puntos
		int points1 = calculatePoints(&games[gameId].player1Deck);
		int points2 = calculatePoints(&games[gameId].player2Deck);

		char *message;

		// Determinar ganador
		if (points1 > 21 && points2 > 21)
		{
			message = "Both players busted! It's a draw.";
		}
		else if (points1 > 21)
		{
			if (strcmp(playerName.msg, games[gameId].player2Name) == 0)
				message = "You win! The other player busted!";
			else
				message = "You busted! The other player wins.";
		}
		else if (points2 > 21)
		{
			if (strcmp(playerName.msg, games[gameId].player2Name) == 0)
				message = "You busted! The other player wins.";
			else
				message = "You win! The other player busted.";
		}
		else if (points1 > points2)
		{
			if (strcmp(playerName.msg, games[gameId].player2Name) == 0)
				message = "You lose.";
			else
				message = "You win.";
		}
		else if (points2 > points1)
		{
			if (strcmp(playerName.msg, games[gameId].player2Name) == 0)
				message = "You win.";
			else
				message = "You lose.";
		}
		else
		{
			message = "It's a draw.";
		}

		block.msgStruct.msg = soap_strdup(soap, message);
		block.msgStruct.__size = strlen(block.msgStruct.msg);

		// Devolvemos el mazo del jugador que consulta
		if (strcmp(playerName.msg, games[gameId].player1Name) == 0)
			block.deck = games[gameId].player1Deck;
		else if (strcmp(playerName.msg, games[gameId].player2Name) == 0)
			block.deck = games[gameId].player2Deck;

		// Incrementa el contador y resetea si ambos vieron el resultado
		games[gameId].playersSeenResult++;
		if (games[gameId].playersSeenResult >= 2)
		{
			if (DEBUG_SERVER)
				printf("[getStatus] Both players saw the result. Resetting game %d\n", gameId);

			pthread_mutex_unlock(&games[gameId].mutex);
			*status = block;
			sleep(1);

			// Resetear el juego
			initGame(&games[gameId]);

			return SOAP_OK;
		}

		pthread_mutex_unlock(&games[gameId].mutex);
		*status = block;
		return SOAP_OK;
	}

	// Jugador 1
	if (strcmp(playerName.msg, games[gameId].player1Name) == 0)
	{
		block.deck = games[gameId].player1Deck;

		if (games[gameId].player1Finished)
		{
			block.code = TURN_WAIT;
			block.msgStruct.msg = soap_strdup(soap, "You finished. Waiting for the other player."); 
			block.msgStruct.__size = strlen(block.msgStruct.msg);
		}
		else if (games[gameId].currentPlayer == player1)
		{
			block.code = TURN_PLAY;
			block.msgStruct.msg = soap_strdup(soap, "It's your turn."); 
			block.msgStruct.__size = strlen(block.msgStruct.msg);
		}
		else
		{
			block.code = TURN_WAIT;
			block.msgStruct.msg = soap_strdup(soap, "Waiting for the other player."); 
			block.msgStruct.__size = strlen(block.msgStruct.msg);
		}
	}
	// Jugador 2
	else if (strcmp(playerName.msg, games[gameId].player2Name) == 0)
	{
		block.deck = games[gameId].player2Deck;

		if (games[gameId].player2Finished)
		{
			block.code = TURN_WAIT;
			block.msgStruct.msg = soap_strdup(soap, "You finished. Waiting for the other player."); 
			block.msgStruct.__size = strlen(block.msgStruct.msg);
		}
		else if (games[gameId].currentPlayer == player2)
		{
			block.code = TURN_PLAY;
			block.msgStruct.msg = soap_strdup(soap, "It's your turn."); 
			block.msgStruct.__size = strlen(block.msgStruct.msg);
		}
		else
		{
			block.code = TURN_WAIT;
			block.msgStruct.msg = soap_strdup(soap, "Waiting for the other player."); 
			block.msgStruct.__size = strlen(block.msgStruct.msg);
		}
	}
	// Jugador no encontrado
	else
	{
		block.code = ERROR_PLAYER_NOT_FOUND;
		block.msgStruct.msg = soap_strdup(soap, "Player not found in this game."); 
		block.msgStruct.__size = strlen(block.msgStruct.msg);
	}

	pthread_mutex_unlock(&games[gameId].mutex);
	*status = block;
	return SOAP_OK;
}

int blackJackns__playerMove(struct soap *soap, blackJackns__tMessage playerName, int gameId, unsigned int move, unsigned int *pass, unsigned int *code)
{
	playerName.msg[playerName.__size] = 0;
	pthread_mutex_lock(&games[gameId].mutex);

	int currentIndex = (strcmp(playerName.msg, games[gameId].player1Name) == 0) ? 0 : 1;
	int *myFinished = (currentIndex == 0) ? &games[gameId].player1Finished : &games[gameId].player2Finished;

	// Si ya terminé, salgo directamente
	if (*myFinished)
	{
		// Si ambos terminaron, el juego acabó
		if (games[gameId].player1Finished && games[gameId].player2Finished)
		{
			*code = GAME_FINISHED;
		}
		else
		{
			*code = TURN_WAIT;
		}
		pthread_mutex_unlock(&games[gameId].mutex);
		return SOAP_OK;
	}

	// Espera activa hasta que sea su turno
	while ((games[gameId].currentPlayer == player1 && strcmp(games[gameId].player1Name, playerName.msg) != 0) ||
		   (games[gameId].currentPlayer == player2 && strcmp(games[gameId].player2Name, playerName.msg) != 0))
	{
		// Si ambos ya terminaron mientras esperaba sale
		if (games[gameId].player1Finished && games[gameId].player2Finished)
		{
			*code = GAME_FINISHED;
			pthread_mutex_unlock(&games[gameId].mutex);
			return SOAP_OK;
		}

		if (DEBUG_SERVER)
			printf("[playerMove] %s espera su turno...\n", playerName.msg);
		pthread_cond_wait(&games[gameId].cond, &games[gameId].mutex);
	}

	// Si pide carta
	if (move == PLAYER_HIT_CARD)
	{
		if (DEBUG_SERVER){
			printf("[playerMove] %s pide carta.\n", playerName.msg);
		}
			
		unsigned int i = getRandomCard(&games[gameId].gameDeck);

		if (currentIndex == 0)
		{
			games[gameId].player1Deck.cards[games[gameId].player1Deck.__size++] = i;
			if (calculatePoints(&games[gameId].player1Deck) > 21)
			{
				games[gameId].player1Finished = 1;
				games[gameId].endOfGame++;
				*pass = 1;

				// Verificar si ambos terminaron
				if (games[gameId].player1Finished && games[gameId].player2Finished)
				{
					*code = GAME_FINISHED;
					pthread_cond_broadcast(&games[gameId].cond); // Despertar a todos
				}
				else
				{
					// Solo cambio de jugador si el otro NO ha terminado
					games[gameId].currentPlayer = player2;
					*code = TURN_WAIT;
					pthread_cond_signal(&games[gameId].cond);
				}
			}
			else
				*code = TURN_PLAY;
		}
		else
		{
			games[gameId].player2Deck.cards[games[gameId].player2Deck.__size++] = i;
			if (calculatePoints(&games[gameId].player2Deck) > 21)
			{
				games[gameId].player2Finished = 1;
				games[gameId].endOfGame++;
				*pass = 1;

				// Verificar si ambos terminaron
				if (games[gameId].player1Finished && games[gameId].player2Finished)
				{
					*code = GAME_FINISHED;
					// Despertar a ambos
					pthread_cond_broadcast(&games[gameId].cond); 
				}
				else
				{
					// Solo cambio de jugador si el otro no ha terminado
					games[gameId].currentPlayer = player1;
					*code = TURN_WAIT;
					pthread_cond_signal(&games[gameId].cond);
				}
			}
			else
				*code = TURN_PLAY;
		}
	}
	// Si se planta
	else if (move == PLAYER_STAND)
	{
		if (DEBUG_SERVER)
			printf("[playerMove] %s se planta.\n", playerName.msg);

		*myFinished = 1;
		games[gameId].endOfGame++;

		// Verificar si ambos terminaron
		if (games[gameId].player1Finished && games[gameId].player2Finished)
		{
			*code = GAME_FINISHED;
			// Despertar a todos
			pthread_cond_broadcast(&games[gameId].cond); 
		}
		else
		{
			// Solo cambio de jugador si el otro no ha terminado
			if (games[gameId].currentPlayer == player1)
			{
				games[gameId].currentPlayer = player2;
			}
			else
			{
				games[gameId].currentPlayer = player1;
			}
			*code = TURN_WAIT;
			pthread_cond_signal(&games[gameId].cond);
		}
	}
	else
	{
		if (DEBUG_SERVER)
			printf("[playerMove] Turno finalizado por fallo.\n");
		*code = -1;
		pthread_mutex_unlock(&games[gameId].mutex);
		return SOAP_OK;
	}

	if (DEBUG_SERVER)
		printf("[playerMove] Turno finalizado por %s\n", playerName.msg);
	pthread_mutex_unlock(&games[gameId].mutex);
	return SOAP_OK;
}

int main(int argc, char **argv)
{

	struct soap soap;
	struct soap *tsoap;
	pthread_t tid;
	int port;
	SOAP_SOCKET m, s;

	if (argc != 2)
	{
		printf("Usage: %s port\n", argv[0]);
		exit(0);
	}

	port = atoi(argv[1]);

	soap_init(&soap);
	initServerStructures(&soap);

	m = soap_bind(&soap, NULL, port, MAX_GAMES);
	if (!soap_valid_socket(m))
	{
		printf("Error doing bind \n");
		exit(1);
	}
	printf("Server is ON \n");

	while (1)
	{
		s = soap_accept(&soap);
		if (!soap_valid_socket(s))
		{
			if (soap.errnum)
			{
				soap_print_fault(&soap, stderr);
				exit(1);
			}
			printf("Connection interrupted. \n");
			break;
		}

		printf("Connection accepted. \n");
		tsoap = soap_copy(&soap);
		pthread_create(&tid, NULL, (void *(*)(void *))soap_serve, (void *)tsoap);
		pthread_detach(tid);
	}

	soap_done(&soap);
	return 0;
}