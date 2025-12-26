#include "serverGame.h"
#include <pthread.h>

tPlayer getNextPlayer(tPlayer currentPlayer)
{

	tPlayer next;

	if (currentPlayer == player1)
		next = player2;
	else
		next = player1;

	return next;
}

void initDeck(tDeck *deck)
{

	deck->numCards = DECK_SIZE;

	for (int i = 0; i < DECK_SIZE; i++)
	{
		deck->cards[i] = i;
	}
}

void clearDeck(tDeck *deck)
{

	// Set number of cards
	deck->numCards = 0;

	for (int i = 0; i < DECK_SIZE; i++)
	{
		deck->cards[i] = UNSET_CARD;
	}
}

void printSession(tSession *session)
{

	printf("\n ------ Session state ------\n");

	// Player 1
	printf("%s [bet:%d; %d chips] Deck:", session->player1Name, session->player1Bet, session->player1Stack);
	printDeck(&(session->player1Deck));

	// Player 2
	printf("%s [bet:%d; %d chips] Deck:", session->player2Name, session->player2Bet, session->player2Stack);
	printDeck(&(session->player2Deck));

	// Current game deck
	if (DEBUG_PRINT_GAMEDECK)
	{
		printf("Game deck: ");
		printDeck(&(session->gameDeck));
	}
}

void initSession(tSession *session)
{

	clearDeck(&(session->player1Deck));
	session->player1Bet = 0;
	session->player1Stack = INITIAL_STACK;

	clearDeck(&(session->player2Deck));
	session->player2Bet = 0;
	session->player2Stack = INITIAL_STACK;

	initDeck(&(session->gameDeck));
	session->currentPlayer = player1;
}

unsigned int calculatePoints(tDeck *deck)
{

	unsigned int points;

	// Init...
	points = 0;

	for (int i = 0; i < deck->numCards; i++)
	{

		if (deck->cards[i] % SUIT_SIZE < 9)
			points += (deck->cards[i] % SUIT_SIZE) + 1;
		else
			points += FIGURE_VALUE;
	}

	return points;
}

unsigned int getRandomCard(tDeck *deck)
{

	unsigned int card, cardIndex, i;

	// Get a random card
	cardIndex = rand() % deck->numCards;
	card = deck->cards[cardIndex];

	// Remove the gap
	for (i = cardIndex; i < deck->numCards - 1; i++)
		deck->cards[i] = deck->cards[i + 1];

	// Update the number of cards in the deck
	deck->numCards--;
	deck->cards[deck->numCards] = UNSET_CARD;

	return card;
}

unsigned int askBet(int socket, unsigned int stack)
{
	unsigned int bet = 0;
	unsigned int code = TURN_BET;
	int bytes;
	int betValid = 0;
	while (!betValid)
	{
		// Send TURN_BET
		bytes = send(socket, &code, sizeof(unsigned int), 0);
		if (bytes < 0)
			printf("ERROR sending code.\n");

		bytes = send(socket, &stack, sizeof(unsigned int), 0);
		if (bytes < 0)
			printf("ERROR sending stack.\n");

		// Recived bet
		bytes = recv(socket, &bet, sizeof(unsigned int), 0);
		if (bytes < 0)
			printf("ERROR receiving bet.\n");

		// Validation
		if (bet > 0 && bet <= MAX_BET && bet <= stack)
		{
			printf("Bet is ok \n");
			code = TURN_BET_OK;
			betValid = 1;
		}

		bytes = send(socket, &code, sizeof(unsigned int), 0);
		if (bytes < 0)
		{
			printf("ERROR sending code.\n");
		}
	}

	return bet;
}

void getNewCard(tDeck *deck, tSession *session)
{
	deck->cards[deck->numCards] = getRandomCard(&session->gameDeck);
	deck->numCards++;
}

void makePlay(int usedSocket, int otherSocket, tDeck *deck, tSession *session)
{
	unsigned int action = 0;
	unsigned int points = 0;
	unsigned int codeUsed = TURN_PLAY;
	unsigned int codeOther = TURN_PLAY_WAIT;
	int playing = 1;

	while (playing)
	{
		points = calculatePoints(deck);

		// Send info to active player
		send(usedSocket, &codeUsed, sizeof(unsigned int), 0);
		send(usedSocket, &points, sizeof(unsigned int), 0);
		send(usedSocket, deck, sizeof(tDeck), 0);

		// Send info to passive player
		send(otherSocket, &codeOther, sizeof(unsigned int), 0);
		send(otherSocket, &points, sizeof(unsigned int), 0);
		send(otherSocket, deck, sizeof(tDeck), 0);

		// Wait for active player action
		if (recv(usedSocket, &action, sizeof(unsigned int), 0) <= 0)
		{
			printf("Error receiving player action.\n");
			return;
		}

		if (action == TURN_PLAY_HIT)
		{
			getNewCard(deck, session);
			points = calculatePoints(deck);

			if (points > 21)
			{
				// Player bust
				codeUsed = TURN_PLAY_OUT;
				codeOther = TURN_PLAY_RIVAL_DONE;

				// Send final state
				send(usedSocket, &codeUsed, sizeof(unsigned int), 0);
				send(usedSocket, &points, sizeof(unsigned int), 0);
				send(usedSocket, deck, sizeof(tDeck), 0);

				send(otherSocket, &codeOther, sizeof(unsigned int), 0);
				send(otherSocket, &points, sizeof(unsigned int), 0);
				send(otherSocket, deck, sizeof(tDeck), 0);

				playing = 0;
			}
			else
			{
				// Continue turn
				codeUsed = TURN_PLAY;
				codeOther = TURN_PLAY_WAIT;
			}
		}
		else if (action == TURN_PLAY_STAND)
		{
			// Player stands
			codeUsed = TURN_PLAY_WAIT;
			codeOther = TURN_PLAY_RIVAL_DONE;

			send(usedSocket, &codeUsed, sizeof(unsigned int), 0);
			send(usedSocket, &points, sizeof(unsigned int), 0);
			send(usedSocket, deck, sizeof(tDeck), 0);

			send(otherSocket, &codeOther, sizeof(unsigned int), 0);
			send(otherSocket, &points, sizeof(unsigned int), 0);
			send(otherSocket, deck, sizeof(tDeck), 0);

			playing = 0;
		}
		else
		{
			printf("Unknown action received: %u\n", action);
			playing = 0;
		}
	}
}

unsigned int seeWinner(tSession session)
{
	unsigned int point1, point2;

	point1 = calculatePoints(&session.player1Deck);
	point2 = calculatePoints(&session.player2Deck);

	// Player 1 wins
	if (point1 > point2 && point1 <= 21)
	{
		return 1;
	}
	else if (point1 <= 21 && point2 > 21)
	{
		return 1;
	}
	// Player 2 wins
	else if (point1 < point2 && point2 <= 21)
	{
		return 2;
	}
	else if (point2 <= 21 && point1 > 21)
	{
		return 2;
	}
	// Nobody wins
	else
	{
		return 0;
	}
}

int main(int argc, char *argv[])
{
	int socketfd;
	struct sockaddr_in serverAddress;
	unsigned int port;
	unsigned int clientLength = sizeof(struct sockaddr_in);

	srand(time(0));

	if (argc != 2)
	{
		fprintf(stderr, "ERROR wrong number of arguments\n");
		fprintf(stderr, "Usage:\n$>%s port\n", argv[0]);
		exit(1);
	}

	// Conf
	socketfd = socket(AF_INET, SOCK_STREAM, 0);
	if (socketfd < 0)
	{
		perror("ERROR opening socket");
	}
	memset(&serverAddress, 0, sizeof(serverAddress));
	port = atoi(argv[1]);
	serverAddress.sin_family = AF_INET;
	serverAddress.sin_addr.s_addr = htonl(INADDR_ANY);
	serverAddress.sin_port = htons(port);

	// If binds goes wrong
	if (bind(socketfd, (struct sockaddr *)&serverAddress, sizeof(serverAddress)) < 0)
	{
		perror("ERROR binding");
	}

	listen(socketfd, 10);

	printf("Server ready, waiting for players...\n");

	while (1)
	{
		// Mem
		tThreadArgs *args = malloc(sizeof(tThreadArgs));
		if (!args)
		{
			perror("malloc goes wrong");
			continue;
		}
		// Accept both players
		struct sockaddr_in tempAddr;
		args->socketPlayer1 = accept(socketfd, (struct sockaddr *)&tempAddr, &clientLength);
		args->socketPlayer2 = accept(socketfd, (struct sockaddr *)&tempAddr, &clientLength);

		if (args->socketPlayer1 < 0 || args->socketPlayer2 < 0)
		{
			perror("accept goes wrong");
			free(args);
			continue;
		}

		// Make the thread
		pthread_t threadID;
		pthread_create(&threadID, NULL, playGame, args);
		pthread_detach(threadID);
	}

	close(socketfd);
	return 0;
}

void *playGame(void *args)
{
	tThreadArgs *threadArgs = (tThreadArgs *)args;
	tSession gameSession;
	unsigned int gameOver = 0;
	unsigned win = -1;
	int bytes;
	tString message;

	initSession(&gameSession);

	int socketPlayer1 = threadArgs->socketPlayer1;
	int socketPlayer2 = threadArgs->socketPlayer2;

	// Init and read message
	memset(message, 0, STRING_LENGTH);
	bytes = recv(socketPlayer1, message, STRING_LENGTH - 1, 0);

	// Check read bytes
	if (bytes < 0)
	{
		perror("ERROR while reading from socket");
	}

	// Save in gameSession Player1's name
	strcpy(gameSession.player1Name, message);

	printf("Player 1: %s\n", message);

	// Get the message length
	memset(message, 0, STRING_LENGTH);
	strcpy(message, "Welcome to BlackJack online");
	bytes = send(socketPlayer1, message, strlen(message), 0);

	// Check bytes sent
	if (bytes < 0)
		perror("ERROR while writing to socket");

	// Init and read message
	memset(message, 0, STRING_LENGTH);
	bytes = recv(socketPlayer2, message, STRING_LENGTH - 1, 0);

	// Check read bytes
	if (bytes < 0)
	{
		perror("ERROR while reading from socket");
	}

	// Save in gameSession Player2's name
	strcpy(gameSession.player2Name, message);

	// Show message
	printf("Player 2: %s\n", message);

	// Get the message length
	memset(message, 0, STRING_LENGTH);
	strcpy(message, "Welcome to BlackJack online");
	bytes = send(socketPlayer2, message, strlen(message), 0);

	// Check bytes sent
	if (bytes < 0)
		perror("ERROR while writing to socket");

	// Starts the game
	while (!gameOver)
	{
		int currentSocket, passiveSocket;
		unsigned int currentBet, currentStack;
		tDeck currentDeck;

		memset(&gameSession.player1Deck, 0, sizeof(tDeck));
		memset(&gameSession.player2Deck, 0, sizeof(tDeck));
		initDeck(&gameSession.gameDeck);
		gameSession.player1Bet = 0;
		gameSession.player2Bet = 0;

		// Deal the cards and send it to the players
		getNewCard(&gameSession.player1Deck, &gameSession);
		getNewCard(&gameSession.player1Deck, &gameSession);
		getNewCard(&gameSession.player2Deck, &gameSession);
		getNewCard(&gameSession.player2Deck, &gameSession);

		// Select socket and stack
		if (gameSession.currentPlayer == player1)
		{
			currentSocket = socketPlayer1;
			currentStack = gameSession.player1Stack;
			passiveSocket = socketPlayer2;
			currentDeck = gameSession.player1Deck;
		}
		else
		{
			currentSocket = socketPlayer2;
			currentStack = gameSession.player2Stack;
			passiveSocket = socketPlayer1;
			currentDeck = gameSession.player2Deck;
		}

		bytes = send(socketPlayer1, &gameSession.player1Deck, sizeof(tDeck), 0);
		if (bytes < 0)
		{
			printf("ERROR while sending deck");
		}
		bytes = send(socketPlayer2, &gameSession.player2Deck, sizeof(tDeck), 0);
		if (bytes < 0)
		{
			printf("ERROR while sending deck");
		}

		// Does 2 times the bet (fuera con los hilos creo)
		for (int i = 0; i < 2; i++)
		{
			// Bet
			currentBet = askBet(currentSocket, currentStack);
			printf("Player make a bet of %d \n", currentBet);
			// Save the bet
			if (gameSession.currentPlayer == player1)
			{
				gameSession.player1Bet = currentBet;
			}

			else
			{
				gameSession.player2Bet = currentBet;
			}
			// Player change
			gameSession.currentPlayer = getNextPlayer(gameSession.currentPlayer);
			// Select socket and stack
			if (gameSession.currentPlayer == player1)
			{
				currentSocket = socketPlayer1;
				currentStack = gameSession.player1Stack;
				passiveSocket = socketPlayer2;
				currentDeck = gameSession.player1Deck;
			}
			else
			{
				currentSocket = socketPlayer2;
				currentStack = gameSession.player2Stack;
				passiveSocket = socketPlayer1;
				currentDeck = gameSession.player2Deck;
			}
		}

		// Game phase
		for (int i = 0; i < 2; i++)
		{
			if (gameSession.currentPlayer == player1)
			{
				currentSocket = socketPlayer1;
				passiveSocket = socketPlayer2;
				currentDeck = gameSession.player1Deck;
			}
			else
			{
				currentSocket = socketPlayer2;
				passiveSocket = socketPlayer1;
				currentDeck = gameSession.player2Deck;
			}

			makePlay(currentSocket, passiveSocket, &currentDeck, &gameSession);

			// Save
			if (gameSession.currentPlayer == player1)
				gameSession.player1Deck = currentDeck;
			else
				gameSession.player2Deck = currentDeck;

			// Next player
			gameSession.currentPlayer = getNextPlayer(gameSession.currentPlayer);
		}
		// See who wins and update stacks
		win = seeWinner(gameSession);
		unsigned int final1 = 0, final2 = 0;

		if (win == 1)
		{
			// Player1 wins
			final1 = 1;
			final2 = 2;
			gameSession.player1Stack += gameSession.player1Bet;
			gameSession.player2Stack -= gameSession.player2Bet;
		}
		else if (win == 2)
		{
			// Player2 wins
			final1 = 2;
			final2 = 1;
			gameSession.player2Stack += gameSession.player2Bet;
			gameSession.player1Stack -= gameSession.player1Bet;
		}
		else
		{
			// Draw
			final1 = 0;
			final2 = 0;
		}

		//Aqui hacer print para ver que coño manda, Codigo socialista

		// Send results to each client,: final code and stack
		if (send(socketPlayer1, &final1, sizeof(unsigned int), 0) < 0)
			perror("send final1");
		if (send(socketPlayer1, &gameSession.player1Stack, sizeof(unsigned int), 0) < 0)
			perror("send stack1");

		if (send(socketPlayer2, &final2, sizeof(unsigned int), 0) < 0)
			perror("send final2");
		if (send(socketPlayer2, &gameSession.player2Stack, sizeof(unsigned int), 0) < 0)
			perror("send stack2");

		unsigned int sendWin = TURN_GAME_WIN, sendLose = TURN_GAME_LOSE, none = 0;

		if (gameSession.player1Stack <= 0)
		{
			// Player2 has won
			send(socketPlayer1, &sendLose, sizeof(unsigned int), 0);
			send(socketPlayer2, &sendWin, sizeof(unsigned int), 0);
			gameOver = 1;
			printf("Game over, %s wins\n", gameSession.player2Name);
		}
		else if (gameSession.player2Stack <= 0)
		{
			// PLayer 1 has won
			send(socketPlayer2, &sendLose, sizeof(unsigned int), 0);
			send(socketPlayer1, &sendWin, sizeof(unsigned int), 0);
			gameOver = 1;
			printf("Game over, %s wins\n", gameSession.player1Name);
		}
		else
		{
			send(socketPlayer1, &none, sizeof(unsigned int), 0);
			send(socketPlayer2, &none, sizeof(unsigned int), 0);
		}

		// Starts the one who finish
		gameSession.currentPlayer = getNextPlayer(gameSession.currentPlayer);
	}

	// Exit
	free(threadArgs);
	pthread_exit(NULL);
}