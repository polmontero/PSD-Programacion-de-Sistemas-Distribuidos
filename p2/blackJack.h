//gsoap blackJackns service name: blackJack
//gsoap blackJackns service style: rpc
//gsoap blackJackns service location: http//localhost:10000
//gsoap blackJackns service encoding: encoded
//gsoap blackJackns service namespace: urn:blackJackns

/** A player is already registered with the same name */
#define ERROR_NAME_REPEATED -1

/** Server is full. No more games are allowed */
#define ERROR_SERVER_FULL -2

/** Player not found */
#define ERROR_PLAYER_NOT_FOUND -3

/** Action taken by the player to stand */
#define PLAYER_STAND 0

/** Action taken by the player to hit a card */
#define PLAYER_HIT_CARD 1

/** Play (player's turn) */
#define TURN_PLAY 2

/** Player must wait and see the rival's play */
#define TURN_WAIT 3

/** Player wins */
#define GAME_WIN 4

/** Player loses */
#define GAME_LOSE 5

//Definimos para poder devolver que los jugadores han terminado de jugar
#define GAME_FINISHED 6

/** Deck's size */
#define DECK_SIZE 52

/** Number of suits in the deck */
#define SUIT_SIZE 13

/** Maximum bet */
#define MAX_BET 5

/** True value */
#define TRUE 1

/** False value */
#define FALSE 0

/** Length for tString */
#define STRING_LENGTH 256


/** Dynamic array of chars */
typedef char *xsd__string;

/** Structure for sending the player's name and messages from the server */
typedef struct tMessage{
	int __size;
	xsd__string msg;
}blackJackns__tMessage;

/** Structure that represents a deck */
typedef struct tDeck{
	int __size;
	unsigned int *cards;	
}blackJackns__tDeck;

/** Response from the server */
typedef struct tBlock{
	int code;
	blackJackns__tMessage msgStruct;
	blackJackns__tDeck deck;
}blackJackns__tBlock;

// Funcion encargada de registrar a los jugadores.
int blackJackns__register(blackJackns__tMessage playerName, int* result);

// Funcion para informar de que stack tiene y que la apuesta es default.
int blackJackns__betInfo(blackJackns__tMessage playerName, int gameId, int *result);

// Funcion para el reparto inicial de cartas
int blackJackns__deckIn(blackJackns__tMessage playerName, int gameId, int *result);

// Funcion para mostrar el estado de la partida
int blackJackns__getStatus(blackJackns__tMessage playerName, int gameId, blackJackns__tBlock *status);

// Funcion que gestiona el movimiento del jugador
int blackJackns__playerMove(blackJackns__tMessage playerName, int gameId, unsigned int move, unsigned int *pass, unsigned int *code);