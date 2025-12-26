#include "clientGame.h"

unsigned int readBet()
{

	int isValid, bet = 0;
	tString enteredMove;

	// While player does not enter a correct bet...
	do
	{

		// Init...
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

	return ((unsigned int)bet);
}

unsigned int readOption()
{

	unsigned int bet;

	do
	{
		printf("What is your move? Press %d to hit a card and %d to stand\n", TURN_PLAY_HIT, TURN_PLAY_STAND);
		bet = readBet();
		if ((bet != TURN_PLAY_HIT) && (bet != TURN_PLAY_STAND))
			printf("Wrong option!\n");
	} while ((bet != TURN_PLAY_HIT) && (bet != TURN_PLAY_STAND));

	return bet;
}

unsigned int clientAskBet(int socket)
{
	unsigned int code = 0;
	unsigned int stack = 0;
	unsigned int bet = 0;
	int bytes = 0;
	int betValid = 0;
	int error = 0;

	while (!betValid && !error)
	{
		// TURN_BET
		bytes = recv(socket, &code, sizeof(unsigned int), 0);
		if (bytes <= 0)
		{
			error = 1;
		}

		// Stack
		if (!error)
		{
			bytes = recv(socket, &stack, sizeof(unsigned int), 0);
			if (bytes <= 0)
				error = 1;
		}

		if (!error && code != TURN_BET)
		{
			error = 1;
		}

		// Making the bet
		if (!error)
		{
			printf("Your stack is %u. Enter your bet:\n", stack);
			bet = readBet();

			// Enviar apuesta al servidor
			bytes = send(socket, &bet, sizeof(unsigned int), 0);
			if (bytes <= 0)
				error = 1;
		}

		// Confirmation
		if (!error)
		{
			bytes = recv(socket, &code, sizeof(unsigned int), 0);
			if (bytes <= 0)
				error = 1;
		}

		// Validate the bet
		if (!error && code == TURN_BET_OK)
		{
			betValid = 1;
			printf("Bet accepted! You bet %u\n", bet);
		}
		else if (!error && code != TURN_BET_OK)
		{
			printf("Bet not valid. Try again.\n");
		}
	}

	// If error returns 0
	if (error)
	{
		printf("Error communicating with server. Exiting bet.\n");
		bet = 0;
	}

	return bet;
}

void playerMakePlay(int socket)
{
	unsigned int code = 0, points = 0;
	tDeck deck;
	int activeDone = 0;	 // Did the active player finish?
	int passiveDone = 0; // Did the rival finish?

	while (!activeDone || !passiveDone)
	{
		// Receive code
		if (recv(socket, &code, sizeof(unsigned int), 0) <= 0)
		{
			printf("ERROR receiving code.\n");
			return;
		}

		// Receive points
		if (recv(socket, &points, sizeof(unsigned int), 0) <= 0)
		{
			printf("ERROR receiving points.\n");
			return;
		}

		// Receive current deck (active player’s deck)
		if (recv(socket, &deck, sizeof(tDeck), 0) <= 0)
		{
			printf("ERROR receiving deck.\n");
			return;
		}

		switch (code)
		{
		case TURN_PLAY: // Your turn
			printf("\nYour turn. Points: %u\n", points);
			printFancyDeck(&deck);

			unsigned int option = readOption();
			code = (option == TURN_PLAY_HIT) ? TURN_PLAY_HIT : TURN_PLAY_STAND;

			if (send(socket, &code, sizeof(unsigned int), 0) <= 0)
			{
				printf("ERROR sending action.\n");
				return;
			}

			if (code == TURN_PLAY_STAND)
				activeDone = 1; // Active player finished
			break;

		case TURN_PLAY_WAIT: // Waiting for opponent
			printf("\nWaiting your opponent... (Opponent points: %u)\n", points);
			printFancyDeck(&deck);
			break;

		case TURN_PLAY_OUT: // You busted
			printf("\nYou have more than 21!\n");
			printFancyDeck(&deck);
			activeDone = 1;
			break;

		case TURN_PLAY_RIVAL_DONE: // Opponent finished turn
			passiveDone = 1;
			break;

		default:
			printf("\nUnknown code received: %u\n", code);
			activeDone = 1;
			passiveDone = 1;
			break;
		}
	}
}

int main(int argc, char *argv[])
{

	int socketfd;					   /** Socket descriptor */
	unsigned int port;				   /** Port number (server) */
	struct sockaddr_in server_address; /** Server address structure */
	char *serverIP;					   /** Server IP */
	tString message;
	int bytes;
	tDeck deck;
	unsigned int final;
	unsigned int stack;
	unsigned int myBet; // User's bet
	unsigned int winner;
	unsigned int play = 1;

	// Check arguments
	if (argc != 3)
	{
		fprintf(stderr, "ERROR wrong number of arguments\n");
		fprintf(stderr, "Usage:\n$>%s serverIP port\n", argv[0]);
		exit(0);
	}

	// Get the server address
	serverIP = argv[1];

	// Get the port
	port = atoi(argv[2]);

	// Create socket
	socketfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

	// Check if the socket has been successfully created
	if (socketfd < 0)
		showError("ERROR while creating the socket");

	// Fill server address structure
	memset(&server_address, 0, sizeof(server_address));
	server_address.sin_family = AF_INET;
	server_address.sin_addr.s_addr = inet_addr(serverIP);
	server_address.sin_port = htons(port);

	// Connect with server
	if (connect(socketfd, (struct sockaddr *)&server_address, sizeof(server_address)) < 0)
		showError("ERROR while establishing connection");

	// Read the message
	printf("Introduce your name: ");
	memset(message, 0, STRING_LENGTH);
	fgets(message, STRING_LENGTH - 1, stdin);

	// Send to server
	bytes = send(socketfd, message, strlen(message), 0);
	if (bytes < 0)
	{
		showError("ERROR whriting socket!");
	}

	// Recived the message
	memset(message, 0, STRING_LENGTH);
	bytes = recv(socketfd, message, STRING_LENGTH - 1, 0);
	if (bytes < 0)
	{
		showError("ERROR reading socket!");
	}
	printf("%s\n", message);

	// Play
	while (play)
	{
		// Recive the starting deck and showing it
		memset(&deck, 0, sizeof(tDeck));
		bytes = recv(socketfd, &deck, sizeof(tDeck), 0);
		if (bytes < 0)
		{
			printf("ERROR reciving cards");
		}

		// Making the bet
		myBet = clientAskBet(socketfd);
		printf("Your bet: %u\n", myBet);

		printFancyDeck(&deck);

		// Playing
		playerMakePlay(socketfd);

		// Recive stack and who wins
		memset(&stack, 0, sizeof(unsigned int));
		memset(&final, 0, sizeof(unsigned int));
		recv(socketfd, &final, sizeof(unsigned int), 0);
		recv(socketfd, &stack, sizeof(unsigned int), 0);

		// Shows final message
		if(final == 1){
			printf("You win, final stack: %d\n", stack);
		}
		else if(final == 2){
			printf("You lose, final stack: %d\n", stack);
		}
		else{
			printf("Draw, final stack: %d\n", stack);
		}

		// Shows if there is a final winner
		memset(&winner, 0, sizeof(unsigned int));
		recv(socketfd, &winner, sizeof(unsigned int), 0);
		// Lose
		if(winner == TURN_GAME_LOSE){
			printf("You lose\n");
			play = 0;
		}
		// Win
		if(winner == TURN_GAME_WIN){
			printf("You win\n");
			play = 0;
		}
	}

	// Close the socket
	close(socketfd);

	return 0;
}