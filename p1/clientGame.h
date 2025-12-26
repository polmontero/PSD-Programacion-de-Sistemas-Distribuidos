#include "game.h"

/** Debug mode? */
#define DEBUG_CLIENT 0

 
/**
 * Reads a bet entered by the player.
 *
 * @return A number that represents the bet for the current play.
 */
unsigned int readBet ();

/**
 * Reads the action taken by the player (stand or hit).
 *
 * @return A number that represents the action taken by the player.
 */
unsigned int readOption ();

// The client sends the bet until is correct
unsigned int clientAskBet(int socket);

// The client makes hits and stand
void playerMakePlay(int socket);
