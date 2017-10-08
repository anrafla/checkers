#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/times.h>
#include <time.h>
#include "myprog.h"

#ifndef CLK_TCK
#define CLK_TCK CLOCKS_PER_SEC
#endif

float SecPerMove;
char board[8][8];
char bestmove[12];
int me,cutoff,endgame;
long NumNodes;
int MaxDepth;

/*** For timing ***/
clock_t start;
struct tms bff;

/*** For the jump list ***/
int jumpptr = 0;
int jumplist[48][12];

/*** For the move list ***/
int numLegalMoves = 0;
int movelist[48][12];

/* Print the amount of time passed since my turn began */
void PrintTime(void)
{
	clock_t current;
	float total;

	current = times(&bff);
	total = (float) ((float)current-(float)start)/CLK_TCK;
	fprintf(stderr, "Time = %f\n", total);
}

/* Determine if I'm low on time */
int LowOnTime(void) 
{
	clock_t current;
	float total;

	current = times(&bff);
	total = (float) ((float)current-(float)start)/CLK_TCK;
	if(total >= (SecPerMove-1.0)) return 1; else return 0;
}

/* Copy a square state */
void CopyState(char *dest, char src)
{
	char state;

	*dest &= Clear;
	state = src & 0xE0;
	*dest |= state;
}

/* Reset board to initial configuration */
void ResetBoard(void)
{
	int x,y;
	char pos;

	pos = 0;
	for(y=0; y<8; y++)
		for(x=0; x<8; x++)
		{
			if(x%2 != y%2) {
				board[y][x] = pos;
				if(y<3 || y>4) board[y][x] |= Piece; else board[y][x] |= Empty;
				if(y<3) board[y][x] |= Red; 
				if(y>4) board[y][x] |= White;
				pos++;
			} else board[y][x] = 0;
		}
	endgame = 0;
}

/* Add a move to the legal move list */
void AddMove(char move[12])
{
	int i;

	for(i=0; i<12; i++) movelist[numLegalMoves][i] = move[i];
	numLegalMoves++;
}

/* Finds legal non-jump moves for the King at position x,y */
void FindKingMoves(char board[8][8], int x, int y) 
{
	int i,j,x1,y1;
	char move[12];

	memset(move,0,12*sizeof(char));

	/* Check the four adjacent squares */
	for(j=-1; j<2; j+=2)
		for(i=-1; i<2; i+=2)
		{
			y1 = y+j; x1 = x+i;
			/* Make sure we're not off the edge of the board */
			if(y1<0 || y1>7 || x1<0 || x1>7) continue; 
			if(empty(board[y1][x1])) {  /* The square is empty, so we can move there */
				move[0] = number(board[y][x])+1;
				move[1] = number(board[y1][x1])+1;    
				AddMove(move);
			}
		}
}

/* Finds legal non-jump moves for the Piece at position x,y */
void FindMoves(int player, char board[8][8], int x, int y) 
{
	int i,j,x1,y1;
	char move[12];

	memset(move,0,12*sizeof(char));

	/* Check the two adjacent squares in the forward direction */
	if(player == 1) j = 1; else j = -1;
	for(i=-1; i<2; i+=2)
	{
		y1 = y+j; x1 = x+i;
		/* Make sure we're not off the edge of the board */
		if(y1<0 || y1>7 || x1<0 || x1>7) continue; 
		if(empty(board[y1][x1])) {  /* The square is empty, so we can move there */
			move[0] = number(board[y][x])+1;
			move[1] = number(board[y1][x1])+1;    
			AddMove(move);
		}
	}
}

/* Adds a jump sequence the the legal jump list */
void AddJump(char move[12])
{
	int i;

	for(i=0; i<12; i++) jumplist[jumpptr][i] = move[i];
	jumpptr++;
}

/* Finds legal jump sequences for the King at position x,y */
int FindKingJump(int player, char board[8][8], char move[12], int len, int x, int y) 
{
	int i,j,x1,y1,x2,y2,FoundJump = 0;
	char one,two,mymove[12],myboard[8][8];

	memcpy(mymove,move,12*sizeof(char));

	/* Check the four adjacent squares */
	for(j=-1; j<2; j+=2)
		for(i=-1; i<2; i+=2)
		{
			y1 = y+j; x1 = x+i;
			y2 = y+2*j; x2 = x+2*i;
			/* Make sure we're not off the edge of the board */
			if(y2<0 || y2>7 || x2<0 || x2>7) continue; 
			one = board[y1][x1];
			two = board[y2][x2];
			/* If there's an enemy piece adjacent, and an empty square after hum, we can jump */
			if(!empty(one) && color(one) != player && empty(two)) {
				/* Update the state of the board, and recurse */
				memcpy(myboard,board,64*sizeof(char));
				myboard[y][x] &= Clear;
				myboard[y1][x1] &= Clear;
				mymove[len] = number(board[y2][x2])+1;
				FoundJump = FindKingJump(player,myboard,mymove,len+1,x+2*i,y+2*j);
				if(!FoundJump) {
					FoundJump = 1;
					AddJump(mymove);
				}
			}
		}
	return FoundJump;
}

/* Finds legal jump sequences for the Piece at position x,y */
int FindJump(int player, char board[8][8], char move[12], int len, int x, int y) 
{
	int i,j,x1,y1,x2,y2,FoundJump = 0;
	char one,two,mymove[12],myboard[8][8];

	memcpy(mymove,move,12*sizeof(char));

	/* Check the two adjacent squares in the forward direction */
	if(player == 1) j = 1; else j = -1;
	for(i=-1; i<2; i+=2)
	{
		y1 = y+j; x1 = x+i;
		y2 = y+2*j; x2 = x+2*i;
		/* Make sure we're not off the edge of the board */
		if(y2<0 || y2>7 || x2<0 || x2>7) continue; 
		one = board[y1][x1];
		two = board[y2][x2];
		/* If there's an enemy piece adjacent, and an empty square after hum, we can jump */
		if(!empty(one) && color(one) != player && empty(two)) {
			/* Update the state of the board, and recurse */
			memcpy(myboard,board,64*sizeof(char));
			myboard[y][x] &= Clear;
			myboard[y1][x1] &= Clear;
			mymove[len] = number(board[y2][x2])+1;
			FoundJump = FindJump(player,myboard,mymove,len+1,x+2*i,y+2*j);
			if(!FoundJump) {
				FoundJump = 1;
				AddJump(mymove);
			}
		}
	}
	return FoundJump;
}

/* Determines all of the legal moves possible for a given state */
int FindLegalMoves(struct State *state)
{
	int x,y;
	char move[12], board[8][8];

	memset(move,0,12*sizeof(char));
	jumpptr = numLegalMoves = 0;
	memcpy(board,state->board,64*sizeof(char));

	/* Loop through the board array, determining legal moves/jumps for each piece */
	for(y=0; y<8; y++){
		for(x=0; x<8; x++)
		{
			if(x%2 != y%2 && color(board[y][x]) == state->player && !empty(board[y][x])) {
				if(king(board[y][x])) { /* King */
					move[0] = number(board[y][x])+1;
					FindKingJump(state->player,board,move,1,x,y);
					if(!jumpptr) FindKingMoves(board,x,y);
				} 
				else if(piece(board[y][x])) { /* Piece */
					move[0] = number(board[y][x])+1;
					FindJump(state->player,board,move,1,x,y);
					if(!jumpptr) FindMoves(state->player,board,x,y);    
				}
			}    
		}

	}
	if(jumpptr) {
		for(x=0; x<jumpptr; x++) 
			for(y=0; y<12; y++) 
				state->movelist[x][y] = jumplist[x][y];
		state->numLegalMoves = jumpptr;
	} 
	else {
		for(x=0; x<numLegalMoves; x++) 
			for(y=0; y<12; y++) 
				state->movelist[x][y] = movelist[x][y];
		state->numLegalMoves = numLegalMoves;
	}
	return (jumpptr+numLegalMoves);
}

/* Employ your favorite search to find the best move here.  */
/* This example code shows you how to call the FindLegalMoves function */
/* and the PerformMove function */
void FindBestMove(int player)
{
	int i, x, currBestMove, currBestVal; 
   
    struct State state; 
	state.player = player;
	/* Set up the current state */
	memcpy(state.board, board, 64*sizeof(char));
	memset(bestmove,0,12*sizeof(char));

	/* Find the legal moves for the current state */
	FindLegalMoves(&state);
	//int bestMoves[48][12];
	//int dups = 0;
	//int uniqueBest = 0;
	currBestMove=rand()%state.numLegalMoves;
    currBestVal=-10000000;
	// For now, until you write your search routine, we will just set the best move
	for(x = 0; x<state.numLegalMoves; x++){
		double rval;
		char nextBoard[8][8];
		//prep data
		memcpy(nextBoard, state.board, 64*sizeof(char));
		PerformMove(nextBoard, state.movelist[x], MoveLength(state.movelist[x]));
		rval = minVal(nextBoard, -1000000, 1000000, MaxDepth);

		if(currBestVal<=rval){//play more randomly, maybe store in an array, for duplicates of same score
	/*		if(currBestVal==rval){
				bestMoves[uniqueBest][dups]=x;
				dups++;
			}
			else{
				dups=0;
				uniqueBest++;
				bestMoves[uniqueBest][dups]=x;
			}
	*/
			currBestVal=rval;
			currBestMove=x;
		}
		i=currBestMove;
		//int r = rand() % dups;
		//i=bestMoves[uniqueBest][r];
		memcpy(bestmove, state.movelist[i], MoveLength(state.movelist[i]));

	}
	//fprintf(stderr, "We found a best move!, currBestVal: %i", currBestVal);
}

/* Converts a square label to it's x,y position */
void NumberToXY(char num, int *x, int *y)
{
	int i=0,newy,newx;

	for(newy=0; newy<8; newy++)
		for(newx=0; newx<8; newx++)
		{
			if(newx%2 != newy%2) {
				i++;
				if(i==(int) num) {
					*x = newx;
					*y = newy;
					return;
				}
			}
		}
	*x = 0; 
	*y = 0;
}

/* Returns the length of a move */
int MoveLength(char move[12])
{
	int i;

	i = 0;
	while(i<12 && move[i]) i++;
	return i;
}    

/* Converts the text version of a move to its integer array version */
int TextToMove(char *mtext, char move[12])
{
	int i=0,len=0,last;
	char val,num[64];

	while(mtext[i] != '\0') {
		last = i;
		while(mtext[i] != '\0' && mtext[i] != '-') i++;
		strncpy(num,&mtext[last],i-last);
		num[i-last] = '\0';
		val = (char) atoi(num);
		if(val <= 0 || val > 32) return 0;
		move[len] = val;
		len++;
		if(mtext[i] != '\0') i++;
	}
	if(len<2 || len>12) return 0; else return len;
}

/* Converts the integer array version of a move to its text version */
void MoveToText(char move[12], char *mtext)
{
	int i;
	char temp[8];

	mtext[0] = '\0';
	for(i=0; i<12; i++) {
		if(move[i]) {
			sprintf(temp,"%d",(int)move[i]);
			strcat(mtext,temp);
			strcat(mtext,"-");
		}
	}
	mtext[strlen(mtext)-1] = '\0';
}

/* Performs a move on the board, updating the state of the board */
void PerformMove(char board[8][8], char move[12], int mlen)
{
	int i,j,x,y,x1,y1,x2,y2;

	NumberToXY(move[0],&x,&y);
	NumberToXY(move[mlen-1],&x1,&y1);
	CopyState(&board[y1][x1],board[y][x]);
	if(y1 == 0 || y1 == 7) board[y1][x1] |= King;
	board[y][x] &= Clear;
	NumberToXY(move[1],&x2,&y2);
	if(abs(x2-x) == 2) {
		for(i=0,j=1; j<mlen; i++,j++) {
			if(move[i] > move[j]) {
				y1 = -1; 
				if((move[i]-move[j]) == 9) x1 = -1; else x1 = 1;
			}
			else {
				y1 = 1;
				if((move[j]-move[i]) == 7) x1 = -1; else x1 = 1;
			}
			NumberToXY(move[i],&x,&y);
			board[y+y1][x+x1] &= Clear;
		}
	}
}

int main(int argc, char *argv[])
{
	char buf[1028],move[12];
	int len,mlen,player1;

	/* Convert command line parameters */
	SecPerMove = (float) atof(argv[1]); /* Time allotted for each move */
	//MaxDepth = (argc == 4) ? atoi(argv[3]) : -1;
	MaxDepth = 10;

	fprintf(stderr, "%s SecPerMove == %lg\n", argv[0], SecPerMove);

	/* Determine if I am player 1 (red) or player 2 (white) */
	//fgets(buf, sizeof(buf), stdin);
	len=read(STDIN_FILENO,buf,1028);
	buf[len]='\0';
	if(!strncmp(buf,"Player1", strlen("Player1"))) 
	{
		fprintf(stderr, "I'm Player 1\n");
		player1 = 1; 
	}
	else 
	{
		fprintf(stderr, "I'm Player 2\n");
		player1 = 0;
	}
	if(player1) me = 1; else me = 2;

	/* Set up the board */ 
	ResetBoard();
	srand((unsigned int)time(0));

	if (player1) {

		start = times(&bff);
		goto determine_next_move;
	}

	for(;;) {
		/* Read the other player's move from the pipe */
		//fgets(buf, sizeof(buf), stdin);
		len=read(STDIN_FILENO,buf,1028);
		buf[len]='\0';
		start = times(&bff);
		memset(move,0,12*sizeof(char));

		/* Update the board to reflect opponents move */
		mlen = TextToMove(buf,move);
		PerformMove(board,move,mlen);

determine_next_move:
		/* Find my move, update board, and write move to pipe */
		if(player1) FindBestMove(1); else FindBestMove(2);
		if(bestmove[0] != 0) { /* There is a legal move */
			mlen = MoveLength(bestmove);
			PerformMove(board,bestmove,mlen);
			MoveToText(bestmove,buf);
		}
		else exit(1); /* No legal moves available, so I have lost */

		/* Write the move to the pipe */
		//printf("%s", buf);
		write(STDOUT_FILENO,buf,strlen(buf));
		fflush(stdout);
	}

	return 0;
}

double materialAdvantage(struct State *currBoard){

	double red_total=0;
	double white_total=0;
	int x=0;
	int y=0;

	while(y<8){
		if(y%2) x=0;
		else x=1;
		while(x<8){
			if(king(currBoard->board[y][x])){
				if(color(currBoard->board[y][x]) == 1){
					red_total+=1.7;

				} else{ 
					white_total+=1.7;
				}
			} else if(piece(currBoard->board[y][x])) {//pawn
				if(color(currBoard->board[y][x]) == 1) {
					red_total+=1;
				} else {
					white_total+=1;
				}
			}
			x+=2;
		}

		y++;
	}

	if(me==1) return red_total-white_total;
	else return white_total-red_total;

}

double numProtected(struct State *currBoard){
    
	double red_protected=0;
	double white_protected=0;
	int x,y;
    
    for(y=0;y<8; y++){
        for(x=0;x<8;x++){
		    if(king(currBoard->board[y][x])) {//king , right now this is only checking for pieces on one side
                                                //TODO: implement both side protection
			/*	if(color(currBoard->board[y][x]) == 1) {
                    if(y!=0){//nothing protected on the first row
                        if(x>0 && x<7){//middle pieces, have potential protection from both sides behind it
                            if(color(currBoard->board[y-1][x-1]) == 1)
                                red_protected+=1.7;
                            if(color(currBoard->board[y-1][x+1]) == 1)
                                red_protected+=1.7;
                        } else { //if its against the wall, its protected on 1 side (i guess technically)
                            if(color(currBoard->board[y][x]) == 1)
                                red_protected+=0.85;//maybe .5 for this?
                        }
                    }
				} else if(color(currBoard->board[y][x]) == 2) {
                    if(y!=7){//nothing protected on the first row
                        if(x>0 && x<7){//middle pieces, have potential protection from both sides behind it
                            if(color(currBoard->board[y+1][x-1]) == 2)
                                white_protected+=1.7;
                            if(color(currBoard->board[y+1][x+1]) == 2)
                                white_protected+=1.7;
                        } else { //if its against the wall, its protected on 1 side (i guess technically)
                            if(color(currBoard->board[y][x]) == 2)
                                white_protected+=0.85;//maybe .5 for this?
                        }
                    }
			  *///  }
			} else if(piece(currBoard->board[y][x])) {//pawn
				if(color(currBoard->board[y][x]) == 1) {
                    if(y!=0){//nothing protected on the first row
                        if(x>0 && x<7){//middle pieces, have potential protection from both sides behind it
                            if(color(currBoard->board[y-1][x-1]) == 1)
                                red_protected+=1;
                            if(color(currBoard->board[y-1][x+1]) == 1)
                                red_protected+=1;
                        } else { //if its against the wall, its protected on 1 side (i guess technically)
                            if(color(currBoard->board[y][x]) == 1)
                                red_protected+=0.5;//maybe .5 for this?
                        }
                   }
				} else if(color(currBoard->board[y][x]) == 2) {
                    if(y!=7){//nothing protected on the first row
                        if(x>0 && x<7){//middle pieces, have potential protection from both sides behind it
                            if(color(currBoard->board[y+1][x-1]) == 2)
                                white_protected+=1;
                            if(color(currBoard->board[y+1][x+1]) == 2)
                                white_protected+=1;
                        } else { //if its against the wall, its protected on 1 side (i guess technically)
                            if(color(currBoard->board[y][x]) == 2)
                                white_protected+=0.5;//maybe .5 for this?
                        }
                    }
			    }
            }
        }
    }
    fprintf(stderr, "Red Protected: %f\n", red_protected);
    fprintf(stderr, "White Protected: %f\n", white_protected);

	if(me==1) return red_protected-white_protected;
	else return white_protected-red_protected;
//	while(y<8){
//		if(y%2) x=0;
//		else x=1;
//		while(x<8){
//			if(king(currBoard->board[y][x])){
//				if(color(currBoard->board[y][x]) == 1){
//					red_total+=1.7;
//
//				} else{ 
//					white_total+=1.7;
//				}
//			} else if(piece(currBoard->board[y][x])) {//pawn
//				if(color(currBoard->board[y][x]) == 1) {
//					red_total+=1;
//				} else {
//					white_total+=1;
//				}
//			}
//			x+=2;
//		}
//
//		y++;
//	}
}

double evalBoard(struct State *currBoard)
{
   // return 0.5*materialAdvantage(currBoard) + 0.5*numProtected(currBoard);
    return materialAdvantage(currBoard);
}

double minVal(char currBoard[8][8], double alpha, double beta, int depth)
{
	int i;
	struct State state;

	/* Set up the current state */
	memcpy(state.board,currBoard,64*sizeof(char));

	// Deal with depth limit
	depth--;
	if(depth<=0)
	{
		state.player=me;
		return evalBoard(&state);
	}

	// You've gotta setup the board with the correct player
	state.player = ((me==1)?2:1);
	/* Find the legal moves for the current state */
	FindLegalMoves(&state);

	// for every legal move
	for(i=0;i<state.numLegalMoves;i++)
	{
		char nextBoard[8][8];
		double max;
		memcpy(nextBoard,currBoard,sizeof(nextBoard));
		PerformMove(nextBoard, state.movelist[i], MoveLength(state.movelist[i]));

		max = maxVal(nextBoard, alpha, beta, depth);
		if(max<beta) beta=max;
		if(alpha>=beta) return alpha;
	}
	return beta;
}

double maxVal(char currBoard[8][8], double alpha, double beta, int depth)
{
	int i;
	struct State state;

	/* Set up the next state's board */
	memcpy(state.board,currBoard,64*sizeof(char));

	// Deal with depth limit
	depth--;
	if(depth<=0)
	{
		state.player = me;
		return evalBoard(&state);
	}

	// You've gotta setup the board with the correct player, 
	state.player = ((me==1)?1:2);

	/* Find the legal moves for the current state */
	FindLegalMoves(&state);

	// for every legal move
	for(i=0;i<state.numLegalMoves;i++)
	{
		char nextBoard[8][8];
		double min;
		memcpy(nextBoard,currBoard,sizeof(nextBoard));
		PerformMove(nextBoard, state.movelist[i], MoveLength(state.movelist[i]));

		min = minVal(nextBoard, alpha, beta, depth);
		if(min>alpha) alpha = min;
		if(alpha>=beta) return beta;
	}

	return alpha;
}
