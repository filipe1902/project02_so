/**
 *  \file semSharedMemSmoker.c (implementation file)
 *
 *  \brief Problem name: SoccerGame
 *
 *  Synchronization based on semaphores and shared memory.
 *  Implementation with SVIPC.
 *
 *  Definition of the operations carried out by the players:
 *     \li arrive
 *     \li playerConstituteTeam
 *     \li waitReferee
 *     \li playUntilEnd
 *
 *  \author Nuno Lau - December 2024
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <string.h>
#include <math.h>

#include "probConst.h"
#include "probDataStruct.h"
#include "logging.h"
#include "sharedDataSync.h"
#include "semaphore.h"
#include "sharedMemory.h"

/** \brief logging file name */
static char nFic[51];

/** \brief shared memory block access identifier */
static int shmid;

/** \brief semaphore set access identifier */
static int semgid;

/** \brief pointer to shared memory region */
static SHARED_DATA *sh;

/** \brief player takes some time to arrive */
static void arrive (int id);

/** \brief player constitutes team */
static int playerConstituteTeam (int id);

/** \brief player waits for referee to start match */
static void waitReferee(int id, int team);

/** \brief player waits for referee to end match */
static void playUntilEnd(int id, int team);

/**
 *  \brief Main program.
 *
 *  Its role is to generate the life cycle of one of intervening entities in the problem: the player.
 */
int main (int argc, char *argv[])
{
    int key;                                            /*access key to shared memory and semaphore set */
    char *tinp;                                                       /* numerical parameters test flag */
    int n, team;

    /* validation of command line parameters */
    if (argc != 4) { 
        freopen ("error_PL", "a", stderr);
        fprintf (stderr, "Number of parameters is incorrect!\n");
        return EXIT_FAILURE;
    }
    

    /* get goalie id - argv[1]*/
    n = (unsigned int) strtol (argv[1], &tinp, 0);
    if ((*tinp != '\0') || (n >= NUMPLAYERS )) { 
        fprintf (stderr, "Player process identification is wrong!\n");
        return EXIT_FAILURE;
    }

    /* get logfile name - argv[2]*/
    strcpy (nFic, argv[2]);

    /* redirect stderr to error file  - argv[3]*/
    freopen (argv[3], "w", stderr);
    setbuf(stderr,NULL);


    /* getting key value */
    if ((key = ftok (".", 'a')) == -1) {
        perror ("error on generating the key");
        exit (EXIT_FAILURE);
    }

    /* connection to the semaphore set and the shared memory region and mapping the shared region onto the
       process address space */
    if ((semgid = semConnect (key)) == -1) { 
        perror ("error on connecting to the semaphore set");
        return EXIT_FAILURE;
    }
    if ((shmid = shmemConnect (key)) == -1) { 
        perror ("error on connecting to the shared memory region");
        return EXIT_FAILURE;
    }
    if (shmemAttach (shmid, (void **) &sh) == -1) { 
        perror ("error on mapping the shared region on the process address space");
        return EXIT_FAILURE;
    }

    /* initialize random generator */
    srandom ((unsigned int) getpid ());                                                 


    /* simulation of the life cycle of the player */
    arrive(n);
    if((team = playerConstituteTeam(n))!=0) {
        waitReferee(n, team);
        playUntilEnd(n, team);
    }

    /* unmapping the shared region off the process address space */
    if (shmemDettach (sh) == -1) {
        perror ("error on unmapping the shared region off the process address space");
        return EXIT_FAILURE;;
    }

    return EXIT_SUCCESS;
}

/**
 *  \brief player takes some time to arrive
 *
 *  Player updates state and takes some time to arrive
 *  The internal state should be saved.
 *
 */
static void arrive(int id)
{    
    if (semDown (semgid, sh->mutex) == -1)  {                                                     /* enter critical region */
        perror ("error on the up operation for semaphore access (PL)");
        exit (EXIT_FAILURE);
    }

    /* TODO: insert your code here */
    sh->fSt.st.playerStat[id] = ARRIVING;   //atualizei o estado do jogador (arriving)
    saveState(nFic, &sh->fSt);              //salvar o estado
    
    if (semUp (semgid, sh->mutex) == -1) {                                                         /* exit critical region */
        perror ("error on the down operation for semaphore access (PL)");
        exit (EXIT_FAILURE);
    }

    usleep((200.0*random())/(RAND_MAX+1.0)+50.0);
}

/**
 *  \brief player constitutes team
 *
 *  If player is late, it updates state and leaves.
 *  If there are enough free players and free goalies to form a team, player forms team allowing 
 *  team members to proceed and waiting for them to acknowledge registration.
 *  Otherwise it updates state, waits for the forming teammate to "call" him, saves its team
 *  and acknowledges registration.
 *  The internal state should be saved.
 *
 *  \param id player id
 * 
 *  \return id of player team (0 for late goalies; 1 for team 1; 2 for team 2)
 *
 */
static int playerConstituteTeam (int id)
{
    int ret = 0;

    if (semDown (semgid, sh->mutex) == -1)  {                                                     /* enter critical region */
        perror ("error on the up operation for semaphore access (PL)");
        exit (EXIT_FAILURE);
    }

    sh->fSt.playersArrived++;   // Increment the number of players that have arrived
    sh->fSt.playersFree++;      // Increment the number of players without a team

    // If there are less players than the necessary number to form 2 teams:
    if (sh->fSt.playersArrived <= 2 * NUMTEAMPLAYERS) {
        
        // If there are enough players and a goalie to form a team:
        if (sh->fSt.playersFree >= NUMTEAMPLAYERS && sh->fSt.goaliesFree >= NUMTEAMGOALIES) {
            
            // In this case: a player is the captain
            sh->fSt.st.playerStat[id] = FORMING_TEAM;

            // For each player, except the captain
            for (int i = 0; i < NUMTEAMPLAYERS - 1; i++) {     

                // Signal the waiting player to proceed
                if (semUp(semgid, sh->playersWaitTeam) == -1) {
                    perror("error on the up operation for semaphore access (PL)");
                    exit(EXIT_FAILURE);
                }

                // The captain waits for the player to confirm is registration
                if (semDown(semgid, sh->playerRegistered) == -1) {
                    perror("error on the down operation for semaphore access (PL)");
                    exit(EXIT_FAILURE);
                }
            }

            sh->fSt.playersFree -= NUMTEAMPLAYERS;      // Decrement the number of free players

            // Signal the waiting goalie to proceed
            if (semUp(semgid, sh->goaliesWaitTeam) == -1) { 
                perror("error on the up operation for semaphore access (GL)");
                exit(EXIT_FAILURE);
            }

            // The captain waits for the goalie to confirm is registration
            if (semDown(semgid, sh->playerRegistered) == -1) {
                perror("error on the down operation for semaphore access (PL)");
                exit(EXIT_FAILURE);
            }
            
            sh->fSt.goaliesFree -= NUMTEAMGOALIES;      // Decrement the number of free goalies
            ret = sh->fSt.teamId++;                     // Change to the next team
            saveState(nFic, &sh->fSt); 

        // If there are not enough players to form a team:
        } else {
            sh->fSt.st.playerStat[id] = WAITING_TEAM; 
            saveState(nFic, &sh->fSt);
        }

    // If there are more than the necessary number of players for 2 teams:
    } else {
        ret = 0;
        sh->fSt.st.playerStat[id] = LATE;  
        sh->fSt.playersFree--;              // Decrement the number of free players
        saveState(nFic, &sh->fSt);
    }

    if (semUp (semgid, sh->mutex) == -1) {                                                         /* exit critical region */
        perror ("error on the down operation for semaphore access (PL)");
        exit (EXIT_FAILURE);
    }

    // If the player is waiting for a team to be formed:
    if (sh->fSt.st.playerStat[id] == WAITING_TEAM) {

        // Confirm that one player is waiting for a team to be formed
        if (semDown(semgid, sh->playersWaitTeam) == -1) { 
            perror ("error on the down operation for semaphore access (PL)");                                    
            exit(EXIT_FAILURE);
        }

        ret = sh->fSt.teamId;               // Return the current team's ID

        // Confirm one player as been registered
        if (semUp(semgid, sh->playerRegistered) == -1) {
            perror ("error on the up operation for semaphore access (PL)");
            exit(EXIT_FAILURE);
        }

    // If the player is forming a team:
    } else if (sh->fSt.st.playerStat[id] == FORMING_TEAM) {
        
        // Signals the referee to proceed 
        if (semUp(semgid, sh->refereeWaitTeams) == -1) {
            perror ("error on the up operation for semaphore access (RF)");
            exit(EXIT_FAILURE);
        }
    }

    return ret;
}

/**
 *  \brief player waits for referee to start match
 *
 *  The player updates its state and waits for referee to end match.  
 *  The internal state should be saved.
 *
 *  \param id   player id
 *  \param team player team
 */
static void waitReferee (int id, int team)
{
    if (semDown (semgid, sh->mutex) == -1)  {                                                     /* enter critical region */
        perror ("error on the up operation for semaphore access (PL)");
        exit (EXIT_FAILURE);
    }

    /* TODO: insert your code here */

    if (team == 1) {
        sh->fSt.st.playerStat[id] = WAITING_START_1;
    } else if (team == 2) {
        sh->fSt.st.playerStat[id] = WAITING_START_2;
    }   
    
    saveState(nFic, &sh->fSt);

    if (semUp (semgid, sh->mutex) == -1) {                                                         /* exit critical region */
        perror ("error on the down operation for semaphore access (PL)");
        exit (EXIT_FAILURE);
    }

    /* TODO: insert your code here */
    if (semDown(semgid, sh->playersWaitReferee) == -1) {                                      
        perror("error on the down operation for semaphore access (PL)");
        exit(EXIT_FAILURE);
    }
}

/**
 *  \brief player waits for referee to end match
 *
 *  The player updates its state and waits for referee to end match.  
 *  The internal state should be saved.
 *
 *  \param id   player id
 *  \param team player team
 */
static void playUntilEnd (int id, int team)
{
    if (semDown (semgid, sh->mutex) == -1)  {                                                     /* enter critical region */
        perror ("error on the up operation for semaphore access (PL)");
        exit (EXIT_FAILURE);
    }

    if (team == 1) {
        sh->fSt.st.playerStat[id] = PLAYING_1;
    } else if (team == 2) {
        sh->fSt.st.playerStat[id] = PLAYING_2;
    }  

    if (semUp(semgid, sh->playing) == -1) {                                      
        perror("error on the up operation for semaphore access (PL)");
        exit(EXIT_FAILURE);
    }

    saveState(nFic, &sh->fSt);

    if (semUp (semgid, sh->mutex) == -1) {                                                         /* exit critical region */
        perror ("error on the down operation for semaphore access (PL)");
        exit (EXIT_FAILURE);
    }

    if (semDown(semgid, sh->playersWaitEnd) == -1) {                                         
        perror("error on the down operation for semaphore access (PL)");
        exit(EXIT_FAILURE);
    }
}



