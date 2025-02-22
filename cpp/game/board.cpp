#include "../game/board.h"

/*
 * board.cpp
 * Originally from an unreleased project back in 2010, modified since.
 * Authors: brettharrison (original), David Wu (original and later modificationss).
 */

#include <algorithm>
#include <cassert>
#include <cstring>
#include <iostream>
#include <vector>

#include "../core/rand.h"

using namespace std;

//STATIC VARS-----------------------------------------------------------------------------
bool Board::IS_ZOBRIST_INITALIZED = false;
Hash128 Board::ZOBRIST_SIZE_X_HASH[MAX_LEN+1];
Hash128 Board::ZOBRIST_SIZE_Y_HASH[MAX_LEN+1];
Hash128 Board::ZOBRIST_BOARD_HASH[MAX_ARR_SIZE][4];
Hash128 Board::ZOBRIST_PLAYER_HASH[4];
Hash128 Board::ZOBRIST_KO_LOC_HASH[MAX_ARR_SIZE];
Hash128 Board::ZOBRIST_KO_MARK_HASH[MAX_ARR_SIZE][4];
Hash128 Board::ZOBRIST_BOARD_HASH2[MAX_ARR_SIZE][4];
const Hash128 Board::ZOBRIST_GAME_IS_OVER = //Based on sha256 hash of Board::ZOBRIST_GAME_IS_OVER
  Hash128(0xb6f9e465597a77eeULL, 0xf1d583d960a4ce7fULL);

//LOCATION--------------------------------------------------------------------------------
Loc Location::getLoc(int x, int y, int x_size)
{
  return (x+1) + (y+1)*(x_size+1);
}
int Location::getX(Loc loc, int x_size)
{
  return (loc % (x_size+1)) - 1;
}
int Location::getY(Loc loc, int x_size)
{
  return (loc / (x_size+1)) - 1;
}
void Location::getAdjacentOffsets(short adj_offsets[8], int x_size)
{
  adj_offsets[0] = -(x_size+1);
  adj_offsets[1] = -1;
  adj_offsets[2] = 1;
  adj_offsets[3] = (x_size+1);
  adj_offsets[4] = -(x_size+1)-1;
  adj_offsets[5] = -(x_size+1)+1;
  adj_offsets[6] = (x_size+1)-1;
  adj_offsets[7] = (x_size+1)+1;
}

bool Location::isAdjacent(Loc loc0, Loc loc1, int x_size)
{
  return loc0 == loc1 - (x_size+1) || loc0 == loc1 - 1 || loc0 == loc1 + 1 || loc0 == loc1 + (x_size+1);
}

Loc Location::getMirrorLoc(Loc loc, int x_size, int y_size) {
  if(loc == Board::NULL_LOC || loc == Board::PASS_LOC)
    return loc;
  return getLoc(x_size-1-getX(loc,x_size),y_size-1-getY(loc,x_size),x_size);
}

Loc Location::getCenterLoc(int x_size, int y_size) {
  if(x_size % 2 == 0 || y_size % 2 == 0)
    return Board::NULL_LOC;
  return getLoc(x_size / 2, y_size / 2, x_size);
}

Loc Location::getCenterLoc(const Board& b) {
  return getCenterLoc(b.x_size,b.y_size);
}

bool Location::isCentral(Loc loc, int x_size, int y_size) {
  int x = getX(loc,x_size);
  int y = getY(loc,x_size);
  return x >= (x_size-1)/2 && x <= x_size/2 && y >= (y_size-1)/2 && y <= y_size/2;
}

bool Location::isNearCentral(Loc loc, int x_size, int y_size) {
  int x = getX(loc,x_size);
  int y = getY(loc,x_size);
  return x >= (x_size-1)/2-1 && x <= x_size/2+1 && y >= (y_size-1)/2-1 && y <= y_size/2+1;
}


#define FOREACHADJ(BLOCK) {int ADJOFFSET = -(x_size+1); {BLOCK}; ADJOFFSET = -1; {BLOCK}; ADJOFFSET = 1; {BLOCK}; ADJOFFSET = x_size+1; {BLOCK}};
#define ADJ0 (-(x_size+1))
#define ADJ1 (-1)
#define ADJ2 (1)
#define ADJ3 (x_size+1)

//CONSTRUCTORS AND INITIALIZATION----------------------------------------------------------

Board::Board()
{
  init(DEFAULT_LEN,DEFAULT_LEN);
}

Board::Board(int x, int y)
{
  init(x,y);
}


Board::Board(const Board& other)
{
  x_size = other.x_size;
  y_size = other.y_size;

  memcpy(colors, other.colors, sizeof(Color)*MAX_ARR_SIZE);

  ko_loc = other.ko_loc;
  // empty_list = other.empty_list;
  pos_hash = other.pos_hash;

  memcpy(adj_offsets, other.adj_offsets, sizeof(short)*8);
}

void Board::init(int xS, int yS)
{
  assert(IS_ZOBRIST_INITALIZED);
  if(xS < 0 || yS < 0 || xS > MAX_LEN || yS > MAX_LEN)
    throw StringError("Board::init - invalid board size");

  x_size = xS;
  y_size = yS;

  for(int i = 0; i < MAX_ARR_SIZE; i++)
    colors[i] = C_WALL;

  for(int y = 0; y < y_size; y++)
  {
    for(int x = 0; x < x_size; x++)
    {
      Loc loc = (x+1) + (y+1)*(x_size+1);
      colors[loc] = C_EMPTY;
      // empty_list.add(loc);
    }
  }

  ko_loc = NULL_LOC;
  pos_hash = ZOBRIST_SIZE_X_HASH[x_size] ^ ZOBRIST_SIZE_Y_HASH[y_size];

  Location::getAdjacentOffsets(adj_offsets,x_size);
}

void Board::initHash()
{
  if(IS_ZOBRIST_INITALIZED)
    return;
  Rand rand("Board::initHash()");

  auto nextHash = [&rand]() {
    uint64_t h0 = rand.nextUInt64();
    uint64_t h1 = rand.nextUInt64();
    return Hash128(h0,h1);
  };

  for(int i = 0; i<4; i++)
    ZOBRIST_PLAYER_HASH[i] = nextHash();
  //Do this second so that the player and encore hashes are not
  //afffected by the size of the board we compile with.
  for(int i = 0; i<MAX_ARR_SIZE; i++) {
    for(Color j = 0; j<4; j++) {
      if(j == C_EMPTY || j == C_WALL)
        ZOBRIST_BOARD_HASH[i][j] = Hash128();
      else
        ZOBRIST_BOARD_HASH[i][j] = nextHash();

      if(j == C_EMPTY || j == C_WALL)
        ZOBRIST_KO_MARK_HASH[i][j] = Hash128();
      else
        ZOBRIST_KO_MARK_HASH[i][j] = nextHash();
    }
    ZOBRIST_KO_LOC_HASH[i] = nextHash();
  }

  //Reseed the random number generator so that these hashes are also
  //not affected by the size of the board we compile with
  rand.init("Board::initHash() for ZOBRIST_SECOND_ENCORE_START hashes");
  for(int i = 0; i<MAX_ARR_SIZE; i++) {
    for(Color j = 0; j<4; j++) {
      if(j == C_EMPTY || j == C_WALL)
        ZOBRIST_SECOND_ENCORE_START_HASH[i][j] = Hash128();
      else
        ZOBRIST_SECOND_ENCORE_START_HASH[i][j] = nextHash();
    }
  }

  //Reseed the random number generator so that these size hashes are also
  //not affected by the size of the board we compile with
  rand.init("Board::initHash() for ZOBRIST_SIZE hashes");
  for(int i = 0; i<MAX_LEN+1; i++) {
    ZOBRIST_SIZE_X_HASH[i] = nextHash();
    ZOBRIST_SIZE_Y_HASH[i] = nextHash();
  }

  //Reseed and compute one more set of zobrist hashes, mixed a bit differently
  rand.init("Board::initHash() for second set of ZOBRIST hashes");
  for(int i = 0; i<MAX_ARR_SIZE; i++) {
    for(Color j = 0; j<4; j++) {
      ZOBRIST_BOARD_HASH2[i][j] = nextHash();
      ZOBRIST_BOARD_HASH2[i][j].hash0 = Hash::murmurMix(ZOBRIST_BOARD_HASH2[i][j].hash0);
      ZOBRIST_BOARD_HASH2[i][j].hash1 = Hash::splitMix64(ZOBRIST_BOARD_HASH2[i][j].hash1);
    }
  }

  IS_ZOBRIST_INITALIZED = true;
}

Hash128 Board::getSitHashWithSimpleKo(Player pla) const {
  Hash128 h = pos_hash;
  if(ko_loc != Board::NULL_LOC)
    h = h ^ Board::ZOBRIST_KO_LOC_HASH[ko_loc];
  h ^= Board::ZOBRIST_PLAYER_HASH[pla];
  return h;
}

void Board::clearSimpleKoLoc() {
  ko_loc = NULL_LOC;
}
void Board::setSimpleKoLoc(Loc loc) {
  ko_loc = loc;
}


//Gets the number of stones of the chain at loc. Precondition: location must be black or white.
int Board::getChainSize(Loc loc) const
{
  return chain_data[chain_head[loc]].num_locs;
}

//Gets the number of liberties of the chain at loc. Assertion: location must be black or white.
int Board::getNumLiberties(Loc loc) const
{
  return chain_data[chain_head[loc]].num_liberties;
}


//Returns a fast lower bound on the number of liberties a new stone placed here would have
void Board::getBoundNumLibertiesAfterPlay(Loc loc, Player pla, int& lowerBound, int& upperBound) const
{
  Player opp = getOpp(pla);

  int numImmediateLibs = 0; //empty spaces adjacent
  int numCaps = 0; //number of adjacent directions in which we will capture
  int potentialLibsFromCaps = 0; //Total number of stones we're capturing (possibly with multiplicity)
  int numConnectionLibs = 0; //Sum over friendly groups connected to of their libs-1
  int maxConnectionLibs = 0; //Max over friendly groups connected to of their libs-1

  for(int i = 0; i < 4; i++) {
    Loc adj = loc + adj_offsets[i];
    if(colors[adj] == C_EMPTY) {
      numImmediateLibs++;
    }
    else if(colors[adj] == opp) {
      int libs = chain_data[chain_head[adj]].num_liberties;
      if(libs == 1) {
        numCaps++;
        potentialLibsFromCaps += chain_data[chain_head[adj]].num_locs;
      }
    }
    else if(colors[adj] == pla) {
      int libs = chain_data[chain_head[adj]].num_liberties;
      int connLibs = libs-1;
      numConnectionLibs += connLibs;
      if(connLibs > maxConnectionLibs)
        maxConnectionLibs = connLibs;
    }
  }

  lowerBound = numCaps + (maxConnectionLibs > numImmediateLibs ? maxConnectionLibs : numImmediateLibs);
  upperBound = numImmediateLibs + potentialLibsFromCaps + numConnectionLibs;
}


//Returns the number of liberties a new stone placed here would have, or max if it would be >= max.
int Board::getNumLibertiesAfterPlay(Loc loc, Player pla, int max) const
{
  Player opp = getOpp(pla);

  int numLibs = 0;
  Loc libs[MAX_PLAY_SIZE];
  int numCapturedGroups = 0;
  Loc capturedGroupHeads[4];

  //First, count immediate liberties and groups that would be captured
  for(int i = 0; i < 4; i++) {
    Loc adj = loc + adj_offsets[i];
    if(colors[adj] == C_EMPTY) {
      libs[numLibs++] = adj;
      if(numLibs >= max)
        return max;
    }
    else if(colors[adj] == opp && getNumLiberties(adj) == 1) {
      libs[numLibs++] = adj;
      if(numLibs >= max)
        return max;

      Loc head = chain_head[adj];
      bool alreadyFound = false;
      for(int j = 0; j<numCapturedGroups; j++) {
        if(capturedGroupHeads[j] == head)
        {alreadyFound = true; break;}
      }
      if(!alreadyFound)
        capturedGroupHeads[numCapturedGroups++] = head;
    }
  }

  auto wouldBeEmpty = [numCapturedGroups,&capturedGroupHeads,this,opp](Loc lc) {
    if(this->colors[lc] == C_EMPTY)
      return true;
    if(this->colors[lc] == opp) {
      for(int i = 0; i<numCapturedGroups; i++)
        if(capturedGroupHeads[i] == this->chain_head[lc])
          return true;
    }
    return false;
  };

  //Next, walk through all stones of all surrounding groups we would connect with and count liberties, avoiding overlap.
  int numConnectingGroups = 0;
  Loc connectingGroupHeads[4];
  for(int i = 0; i<4; i++) {
    Loc adj = loc + adj_offsets[i];
    if(colors[adj] == pla) {
      Loc head = chain_head[adj];
      bool alreadyFound = false;
      for(int j = 0; j<numConnectingGroups; j++) {
        if(connectingGroupHeads[j] == head)
        {alreadyFound = true; break;}
      }
      if(!alreadyFound) {
        connectingGroupHeads[numConnectingGroups++] = head;

        Loc cur = adj;
        do
        {
          for(int k = 0; k < 4; k++) {
            Loc possibleLib = cur + adj_offsets[k];
            if(possibleLib != loc && wouldBeEmpty(possibleLib)) {
              bool alreadyCounted = false;
              for(int l = 0; l<numLibs; l++) {
                if(libs[l] == possibleLib)
                {alreadyCounted = true; break;}
              }
              if(!alreadyCounted) {
                libs[numLibs++] = possibleLib;
                if(numLibs >= max)
                  return max;
              }
            }
          }

          cur = next_in_chain[cur];
        } while (cur != adj);
      }
    }
  }
  return numLibs;
}

//Check if moving here is illegal due to simple ko
bool Board::isKoBanned(Loc loc) const
{
  return loc == ko_loc;
}

bool Board::isOnBoard(Loc loc) const {
  return loc >= 0 && loc < MAX_ARR_SIZE && colors[loc] != C_WALL;
}

//Check if moving here is illegal.
bool Board::isLegal(Loc loc, Player pla) const
{
  if(pla != P_BLACK && pla != P_WHITE)
    return false;
  return loc == PASS_LOC || (
    loc >= 0 &&
    loc < MAX_ARR_SIZE &&
    (colors[loc] == C_EMPTY) &&
    !isKoBanned(loc)
  );
}

//Check if moving here is illegal, ignoring simple ko
bool Board::isLegalIgnoringKo(Loc loc, Player pla) const
{
  if(pla != P_BLACK && pla != P_WHITE)
    return false;
  return loc == PASS_LOC || (
    loc >= 0 &&
    loc < MAX_ARR_SIZE &&
    (colors[loc] == C_EMPTY)
  );
}

//Check if this location contains a simple eye for the specified player.
bool Board::isSimpleEye(Loc loc, Player pla) const
{
  if(colors[loc] != C_EMPTY)
    return false;

  bool against_wall = false;

  //Check that surounding points are owned
  for(int i = 0; i < 4; i++)
  {
    Loc adj = loc + adj_offsets[i];
    if(colors[adj] == C_WALL)
      against_wall = true;
    else if(colors[adj] != pla)
      return false;
  }

  //Check that opponent does not own too many diagonal points
  Player opp = getOpp(pla);
  int num_opp_corners = 0;
  for(int i = 4; i < 8; i++)
  {
    Loc corner = loc + adj_offsets[i];
    if(colors[corner] == opp)
      num_opp_corners++;
  }

  if(num_opp_corners >= 2 || (against_wall && num_opp_corners >= 1))
    return false;

  return true;
}

bool Board::wouldBeCapture(Loc loc, Player pla) const {
  if(colors[loc] != C_EMPTY)
    return false;
  Player opp = getOpp(pla);
  FOREACHADJ(
    Loc adj = loc + ADJOFFSET;
    if(colors[adj] == opp)
    {
      if(getNumLiberties(adj) == 1)
        return true;
    }
  );

  return false;
}


bool Board::wouldBeKoCapture(Loc loc, Player pla) const {
  if(colors[loc] != C_EMPTY)
    return false;
  //Check that surounding points are are all opponent owned and exactly one of them is capturable
  Player opp = getOpp(pla);
  Loc oppCapturableLoc = NULL_LOC;
  for(int i = 0; i < 4; i++)
  {
    Loc adj = loc + adj_offsets[i];
    if(colors[adj] != C_WALL && colors[adj] != opp)
      return false;
    if(colors[adj] == opp && getNumLiberties(adj) == 1) {
      if(oppCapturableLoc != NULL_LOC)
        return false;
      oppCapturableLoc = adj;
    }
  }
  if(oppCapturableLoc == NULL_LOC)
    return false;

  //Check that the capturable loc has exactly one stone
  if(chain_data[chain_head[oppCapturableLoc]].num_locs != 1)
    return false;
  return true;
}

Loc Board::getKoCaptureLoc(Loc loc, Player pla) const {
  if(colors[loc] != C_EMPTY)
    return NULL_LOC;
  //Check that surounding points are are all opponent owned and exactly one of them is capturable
  Player opp = getOpp(pla);
  Loc oppCapturableLoc = NULL_LOC;
  for(int i = 0; i < 4; i++)
  {
    Loc adj = loc + adj_offsets[i];
    if(colors[adj] != C_WALL && colors[adj] != opp)
      return NULL_LOC;
    if(colors[adj] == opp && getNumLiberties(adj) == 1) {
      if(oppCapturableLoc != NULL_LOC)
        return NULL_LOC;
      oppCapturableLoc = adj;
    }
  }
  if(oppCapturableLoc == NULL_LOC)
    return NULL_LOC;

  //Check that the capturable loc has exactly one stone
  if(chain_data[chain_head[oppCapturableLoc]].num_locs != 1)
    return NULL_LOC;
  return oppCapturableLoc;
}

bool Board::isAdjacentToPla(Loc loc, Player pla) const {
  FOREACHADJ(
    Loc adj = loc + ADJOFFSET;
    if(colors[adj] == pla)
      return true;
  );
  return false;
}

bool Board::isAdjacentOrDiagonalToPla(Loc loc, Player pla) const {
  for(int i = 0; i<8; i++) {
    Loc adj = loc + adj_offsets[i];
    if(colors[adj] == pla)
      return true;
  }
  return false;
}

bool Board::isAdjacentToChain(Loc loc, Loc chain) const {
  if(colors[chain] == C_EMPTY)
    return false;
  FOREACHADJ(
    Loc adj = loc + ADJOFFSET;
    if(colors[adj] == colors[chain] && chain_head[adj] == chain_head[chain])
      return true;
  );
  return false;
}


//Does this connect two pla distinct groups that are not both pass-alive and not within opponent pass-alive area either?
bool Board::isNonPassAliveSelfConnection(Loc loc, Player pla, Color* passAliveArea) const {
  if(colors[loc] != C_EMPTY || passAliveArea[loc] == pla)
    return false;

  Loc nonPassAliveAdjHead = NULL_LOC;
  for(int i = 0; i < 4; i++)
  {
    Loc adj = loc + adj_offsets[i];
    if(colors[adj] == pla && passAliveArea[adj] == C_EMPTY) {
      nonPassAliveAdjHead = chain_head[adj];
      break;
    }
  }

  if(nonPassAliveAdjHead == NULL_LOC)
    return false;

  for(int i = 0; i < 4; i++)
  {
    Loc adj = loc + adj_offsets[i];
    if(colors[adj] == pla && chain_head[adj] != nonPassAliveAdjHead)
      return true;
  }

  return false;
}

bool Board::isEmpty() const {
  for(int y = 0; y < y_size; y++) {
    for(int x = 0; x < x_size; x++) {
      Loc loc = Location::getLoc(x,y,x_size);
      if(colors[loc] != C_EMPTY)
        return false;
    }
  }
  return true;
}

int Board::numStonesOnBoard() const {
  int num = 0;
  for(int y = 0; y < y_size; y++) {
    for(int x = 0; x < x_size; x++) {
      Loc loc = Location::getLoc(x,y,x_size);
      if(colors[loc] == C_BLACK || colors[loc] == C_WHITE)
        num += 1;
    }
  }
  return num;
}

int Board::numPlaStonesOnBoard(Player pla) const {
  int num = 0;
  for(int y = 0; y < y_size; y++) {
    for(int x = 0; x < x_size; x++) {
      Loc loc = Location::getLoc(x,y,x_size);
      if(colors[loc] == pla)
        num += 1;
    }
  }
  return num;
}

bool Board::setStone(Loc loc, Color color)
{
  if(loc < 0 || loc >= MAX_ARR_SIZE || colors[loc] == C_WALL)
    return false;
  if(color != C_BLACK && color != C_WHITE && color != C_EMPTY)
    return false;

  if(colors[loc] == color)
  {}
  else if(colors[loc] == C_EMPTY)
    playMoveAssumeLegal(loc,color);
  else if(color == C_EMPTY)
    removeSingleStone(loc);
  else {
    removeSingleStone(loc);
    playMoveAssumeLegal(loc,color);
  }

  ko_loc = NULL_LOC;
  return true;
}


//Attempts to play the specified move. Returns true if successful, returns false if the move was illegal.
bool Board::playMove(Loc loc, Player pla)
{
  if(isLegal(loc,pla))
  {
    playMoveAssumeLegal(loc,pla);
    return true;
  }
  return false;
}

//Plays the specified move, assuming it is legal, and returns a MoveRecord for the move
Board::MoveRecord Board::playMoveRecorded(Loc loc, Player pla)
{
  MoveRecord record;
  record.loc = loc;
  record.pla = pla;
  record.ko_loc = ko_loc;
  record.capDirs = 0;

  if(loc != PASS_LOC) {
    Player opp = getOpp(pla);

    { int adj = loc + ADJ0;
      if(colors[adj] == opp && getNumLiberties(adj) == 1)
        record.capDirs |= (((uint8_t)1) << 0); }
    { int adj = loc + ADJ1;
      if(colors[adj] == opp && getNumLiberties(adj) == 1)
        record.capDirs |= (((uint8_t)1) << 1); }
    { int adj = loc + ADJ2;
      if(colors[adj] == opp && getNumLiberties(adj) == 1)
        record.capDirs |= (((uint8_t)1) << 2); }
    { int adj = loc + ADJ3;
      if(colors[adj] == opp && getNumLiberties(adj) == 1)
        record.capDirs |= (((uint8_t)1) << 3); }

    if(record.capDirs == 0 && isSuicide(loc,pla))
      record.capDirs = 0x10;
  }

  playMoveAssumeLegal(loc, pla);
  return record;
}

//Undo the move given by record. Moves MUST be undone in the order they were made.
//Undos will NOT typically restore the precise representation in the board to the way it was. The heads of chains
//might change, the order of the circular lists might change, etc.
void Board::undo(Board::MoveRecord record)
{
  ko_loc = record.ko_loc;

  Loc loc = record.loc;
  if(loc == PASS_LOC)
    return;

  //Re-fill stones in all captured directions
  for(int i = 0; i<4; i++)
  {
    int adj = loc + adj_offsets[i];
    if(record.capDirs & (1 << i))
    {
      if(colors[adj] == C_EMPTY) {
        addChain(adj, getOpp(record.pla));

        int numUncaptured = chain_data[chain_head[adj]].num_locs;
        if(record.pla == P_BLACK)
          numWhiteCaptures -= numUncaptured;
        else
          numBlackCaptures -= numUncaptured;
      }
    }
  }
  //Re-fill suicided stones
  if(record.capDirs == 0x10) {
    assert(colors[loc] == C_EMPTY);
    addChain(loc,record.pla);
    int numUncaptured = chain_data[chain_head[loc]].num_locs;
    if(record.pla == P_BLACK)
      numBlackCaptures -= numUncaptured;
    else
      numWhiteCaptures -= numUncaptured;
  }

  //Delete the stone played here.
  pos_hash ^= ZOBRIST_BOARD_HASH[loc][colors[loc]];
  colors[loc] = C_EMPTY;
  // empty_list.add(loc);

  //Uneat opp liberties
  changeSurroundingLiberties(loc, getOpp(record.pla),+1);

  //If this was not a single stone, we may need to recompute the chain from scratch
  if(chain_data[chain_head[loc]].num_locs > 1)
  {
    int numNeighbors = 0;
    FOREACHADJ(
      int adj = loc + ADJOFFSET;
      if(colors[adj] == record.pla)
        numNeighbors++;
    );

    //If the move had exactly one neighbor, we know its undoing didn't disconnect the group,
    //so don't need to rebuild the whole chain.
    if(numNeighbors <= 1) {
      //If the undone move was the location of the head, we need to move the head.
      Loc head = chain_head[loc];
      if(head == loc) {
        Loc newHead = next_in_chain[loc];
        //Run through the whole chain and make their heads point to the new head
        Loc cur = loc;
        do
        {
          chain_head[cur] = newHead;
          cur = next_in_chain[cur];
        } while (cur != loc);

        //Move over the head data
        chain_data[newHead] = chain_data[head];
        head = newHead;
      }

      //Extract this move out of the circlar list of stones. Unfortunately we don't have a prev pointer, so we need to walk the loop.
      {
        //Starting at the head is likely to need to walk less since whenever we merge a single stone into an existing group
        //we put it right after the old head.
        Loc cur = head;
        while(next_in_chain[cur] != loc)
          cur = next_in_chain[cur];
        //Advance the pointer to put loc out of the loop
        next_in_chain[cur] = next_in_chain[loc];
      }

      //Lastly, fix up liberties. Removing this stone removed all liberties next to this stone
      //that weren't already liberties of the group.
      int libertyDelta = 0;
      FOREACHADJ(
        int adj = loc + ADJOFFSET;
        if(colors[adj] == C_EMPTY && !isLibertyOf(adj,head)) libertyDelta--;
      );
      //Removing this stone itself added a liberty to the group though.
      libertyDelta++;
      chain_data[head].num_liberties += libertyDelta;
      //And update the count of stones in the group
      chain_data[head].num_locs--;
    }
    //More than one neighbor. Removing this stone potentially disconnects the group into two, so we just do a complete rebuild
    //of the resulting group(s).
    else {
      //Run through the whole chain and make their heads point to nothing
      Loc cur = loc;
      do
      {
        chain_head[cur] = NULL_LOC;
        cur = next_in_chain[cur];
      } while (cur != loc);

      //Rebuild each chain adjacent now
      for(int i = 0; i<4; i++)
      {
        int adj = loc + adj_offsets[i];
        if(colors[adj] == record.pla && chain_head[adj] == NULL_LOC)
          rebuildChain(adj, record.pla);
      }
    }
  }
}

Hash128 Board::getPosHashAfterMove(Loc loc, Player pla) const {
  if(loc == PASS_LOC)
    return pos_hash;
  assert(loc != NULL_LOC);

  Hash128 hash = pos_hash;
  hash ^= ZOBRIST_BOARD_HASH[loc][pla];

  Player opp = getOpp(pla);

  //Count immediate liberties and groups that would be captured
  bool wouldBeSuicide = true;
  int numCapturedGroups = 0;
  Loc capturedGroupHeads[4];
  for(int i = 0; i < 4; i++) {
    Loc adj = loc + adj_offsets[i];
    if(colors[adj] == C_EMPTY)
      wouldBeSuicide = false;
    else if(colors[adj] == pla && getNumLiberties(adj) > 1)
      wouldBeSuicide = false;
    else if(colors[adj] == opp) {
      //Capture!
      if(getNumLiberties(adj) == 1) {
        //Make sure we haven't already counted it
        Loc head = chain_head[adj];
        bool alreadyFound = false;
        for(int j = 0; j<numCapturedGroups; j++) {
          if(capturedGroupHeads[j] == head)
          {alreadyFound = true; break;}
        }
        if(!alreadyFound) {
          capturedGroupHeads[numCapturedGroups++] = head;
          wouldBeSuicide = false;

          //Now iterate through the group to update the hash
          Loc cur = adj;
          do {
            hash ^= ZOBRIST_BOARD_HASH[cur][opp];
            cur = next_in_chain[cur];
          } while (cur != adj);
        }
      }
    }
  }

  //Update hash for suicidal moves
  if(wouldBeSuicide) {
    assert(numCapturedGroups == 0);

    for(int i = 0; i < 4; i++) {
      Loc adj = loc + adj_offsets[i];
      //Suicide capture!
      if(colors[adj] == pla && getNumLiberties(adj) == 1) {
        //Make sure we haven't already counted it
        Loc head = chain_head[adj];
        bool alreadyFound = false;
        for(int j = 0; j<numCapturedGroups; j++) {
          if(capturedGroupHeads[j] == head)
          {alreadyFound = true; break;}
        }
        if(!alreadyFound) {
          capturedGroupHeads[numCapturedGroups++] = head;

          //Now iterate through the group to update the hash
          Loc cur = adj;
          do {
            hash ^= ZOBRIST_BOARD_HASH[cur][pla];
            cur = next_in_chain[cur];
          } while (cur != adj);
        }
      }
    }

    //Don't forget the stone we'd place would also die
    hash ^= ZOBRIST_BOARD_HASH[loc][pla];
  }

  return hash;
}

//Plays the specified move, assuming it is legal.
void Board::playMoveAssumeLegal(Loc loc, Player pla)
{
  //Pass?
  if(loc == PASS_LOC)
  {
    ko_loc = NULL_LOC;
    return;
  }

  Player opp = getOpp(pla);

  //Add the new stone as an independent group
  colors[loc] = pla;
  pos_hash ^= ZOBRIST_BOARD_HASH[loc][pla];
  chain_data[loc].owner = pla;
  chain_data[loc].num_locs = 1;
  chain_data[loc].num_liberties = getNumImmediateLiberties(loc);
  chain_head[loc] = loc;
  next_in_chain[loc] = loc;
  // empty_list.remove(loc);

  //Merge with surrounding friendly chains and capture any necessary opp chains
  int num_captured = 0; //Number of stones captured
  Loc possible_ko_loc = NULL_LOC;  //What location a ko ban might become possible in
  int num_opps_seen = 0;  //How many opp chains we have seen so far
  Loc opp_heads_seen[4];   //Heads of the opp chains seen so far

  for(int i = 0; i < 4; i++)
  {
    int adj = loc + adj_offsets[i];

    //Friendly chain!
    if(colors[adj] == pla)
    {
      //Already merged?
      if(chain_head[adj] == chain_head[loc])
        continue;

      //Otherwise, eat one liberty and merge them
      chain_data[chain_head[adj]].num_liberties--;
      mergeChains(adj,loc);
    }

    //Opp chain!
    else if(colors[adj] == opp)
    {
      Loc opp_head = chain_head[adj];

      //Have we seen it already?
      bool seen = false;
      for(int j = 0; j<num_opps_seen; j++)
        if(opp_heads_seen[j] == opp_head)
        {seen = true; break;}

      if(seen)
        continue;

      //Not already seen! Eat one liberty from it and mark it as seen
      chain_data[opp_head].num_liberties--;
      opp_heads_seen[num_opps_seen++] = opp_head;

      //Kill it?
      if(getNumLiberties(adj) == 0)
      {
        num_captured += removeChain(adj);
        possible_ko_loc = adj;
      }
    }
  }

  //We have a ko if 1 stone was captured and the capturing move is one isolated stone
  //And the capturing move itself now only has one liberty
  if(num_captured == 1 && chain_data[chain_head[loc]].num_locs == 1 && chain_data[chain_head[loc]].num_liberties == 1)
    ko_loc = possible_ko_loc;
  else
    ko_loc = NULL_LOC;

  if(pla == P_BLACK)
    numWhiteCaptures += num_captured;
  else
    numBlackCaptures += num_captured;

  //Handle suicide
  if(getNumLiberties(loc) == 0) {
    int numSuicided = chain_data[chain_head[loc]].num_locs;
    removeChain(loc);

    if(pla == P_BLACK)
      numBlackCaptures += numSuicided;
    else
      numWhiteCaptures += numSuicided;
  }
}

int Board::getNumImmediateLiberties(Loc loc) const
{
  int num_libs = 0;
  if(colors[loc + ADJ0] == C_EMPTY) num_libs++;
  if(colors[loc + ADJ1] == C_EMPTY) num_libs++;
  if(colors[loc + ADJ2] == C_EMPTY) num_libs++;
  if(colors[loc + ADJ3] == C_EMPTY) num_libs++;

  return num_libs;
}

int Board::countHeuristicConnectionLibertiesX2(Loc loc, Player pla) const
{
  int num_libsX2 = 0;
  for(int i = 0; i < 4; i++) {
    Loc adj = loc + adj_offsets[i];
    if(colors[adj] == pla) {
      int libs = chain_data[chain_head[adj]].num_liberties;
      if(libs > 1)
        num_libsX2 += libs * 2 - 3;
    }
  }
  return num_libsX2;
}


//Remove a single stone, even a stone part of a larger group.
void Board::removeSingleStone(Loc loc)
{
  Player pla = colors[loc];

  //Save the entire chain's stone locations
  int num_locs = chain_data[chain_head[loc]].num_locs;
  int locs[MAX_PLAY_SIZE];
  int idx = 0;
  Loc cur = loc;
  do
  {
    locs[idx++] = cur;
    cur = next_in_chain[cur];
  } while (cur != loc);
  assert(idx == num_locs);

  //Delete the entire chain
  removeChain(loc);

  //Then add all the other stones back one by one.
  for(int i = 0; i<num_locs; i++) {
    if(locs[i] != loc)
      playMoveAssumeLegal(locs[i],pla);
  }
}


//Apply the specified delta to the liberties of all adjacent groups of the specified color
void Board::changeSurroundingLiberties(Loc loc, Player pla, int delta)
{
  Loc adj0 = loc + ADJ0;
  Loc adj1 = loc + ADJ1;
  Loc adj2 = loc + ADJ2;
  Loc adj3 = loc + ADJ3;

  if(colors[adj0] == pla)
    chain_data[chain_head[adj0]].num_liberties += delta;
  if(colors[adj1] == pla
     && !(colors[adj0] == pla && chain_head[adj0] == chain_head[adj1]))
    chain_data[chain_head[adj1]].num_liberties += delta;
  if(colors[adj2] == pla
     && !(colors[adj0] == pla && chain_head[adj0] == chain_head[adj2])
     && !(colors[adj1] == pla && chain_head[adj1] == chain_head[adj2]))
    chain_data[chain_head[adj2]].num_liberties += delta;
  if(colors[adj3] == pla
     && !(colors[adj0] == pla && chain_head[adj0] == chain_head[adj3])
     && !(colors[adj1] == pla && chain_head[adj1] == chain_head[adj3])
     && !(colors[adj2] == pla && chain_head[adj2] == chain_head[adj3]))
    chain_data[chain_head[adj3]].num_liberties += delta;
}


// Board::PointList::PointList()
// {
//   std::memset(list_, NULL_LOC, sizeof(list_));
//   std::memset(indices_, -1, sizeof(indices_));
//   size_ = 0;
// }

// Board::PointList::PointList(const Board::PointList& other)
// {
//   std::memcpy(list_, other.list_, sizeof(list_));
//   std::memcpy(indices_, other.indices_, sizeof(indices_));
//   size_ = other.size_;
// }

// void Board::PointList::operator=(const Board::PointList& other)
// {
//   if(this == &other)
//     return;
//   std::memcpy(list_, other.list_, sizeof(list_));
//   std::memcpy(indices_, other.indices_, sizeof(indices_));
//   size_ = other.size_;
// }

// void Board::PointList::add(Loc loc)
// {
//   //assert (size_ < MAX_PLAY_SIZE);
//   list_[size_] = loc;
//   indices_[loc] = size_;
//   size_++;
// }

// void Board::PointList::remove(Loc loc)
// {
//   //assert(size_ >= 0);
//   int index = indices_[loc];
//   //assert(index >= 0 && index < size_);
//   //assert(list_[index] == loc);
//   Loc end_loc = list_[size_-1];
//   list_[index] = end_loc;
//   list_[size_-1] = NULL_LOC;
//   indices_[end_loc] = index;
//   indices_[loc] = -1;
//   size_--;
// }

// int Board::PointList::size() const
// {
//   return size_;
// }

// Loc& Board::PointList::operator[](int n)
// {
//   assert (n < size_);
//   return list_[n];
// }

// bool Board::PointList::contains(Loc loc) const {
//   return indices_[loc] != -1;
// }

int Location::distance(Loc loc0, Loc loc1, int x_size) {
  int dx = getX(loc1,x_size) - getX(loc0,x_size);
  int dy = (loc1-loc0-dx) / (x_size+1);
  return (dx >= 0 ? dx : -dx) + (dy >= 0 ? dy : -dy);
}

int Location::euclideanDistanceSquared(Loc loc0, Loc loc1, int x_size) {
  int dx = getX(loc1,x_size) - getX(loc0,x_size);
  int dy = (loc1-loc0-dx) / (x_size+1);
  return dx*dx + dy*dy;
}

//TACTICAL STUFF--------------------------------------------------------------------

//Helper, find liberties of group at loc. Fills in buf, returns the number of liberties.
//bufStart is where to start checking to avoid duplicates. bufIdx is where to start actually writing.
int Board::findLiberties(Loc loc, vector<Loc>& buf, int bufStart, int bufIdx) const {
  int numFound = 0;
  Loc cur = loc;
  do
  {
    for(int i = 0; i < 4; i++) {
      Loc lib = cur + adj_offsets[i];
      if(colors[lib] == C_EMPTY) {
        //Check for dups
        bool foundDup = false;
        for(int j = bufStart; j < bufIdx+numFound; j++) {
          if(buf[j] == lib) {
            foundDup = true;
            break;
          }
        }
        if(!foundDup) {
          if(bufIdx+numFound >= buf.size())
            buf.resize(buf.size() * 3/2 + 64);
          buf[bufIdx+numFound] = lib;
          numFound++;
        }
      }
    }

    cur = next_in_chain[cur];
  } while (cur != loc);

  return numFound;
}

//Helper, find captures that gain liberties for the group at loc. Fills in result, returns the number of captures.
//bufStart is where to start checking to avoid duplicates. bufIdx is where to start actually writing.
int Board::findLibertyGainingCaptures(Loc loc, vector<Loc>& buf, int bufStart, int bufIdx) const {
  Player opp = getOpp(colors[loc]);

  //For performance, avoid checking for captures on any chain twice
  //int arrSize = x_size*y_size;
  Loc chainHeadsChecked[MAX_PLAY_SIZE];
  int numChainHeadsChecked = 0;

  int numFound = 0;
  Loc cur = loc;
  do
  {
    for(int i = 0; i < 4; i++) {
      Loc adj = cur + adj_offsets[i];
      if(colors[adj] == opp) {
        Loc head = chain_head[adj];
        if(chain_data[head].num_liberties == 1) {
          bool alreadyChecked = false;
          for(int j = 0; j<numChainHeadsChecked; j++) {
            if(chainHeadsChecked[j] == head) {
              alreadyChecked = true;
              break;
            }
          }
          if(!alreadyChecked) {
            //Capturing moves are precisely the liberties of the groups around us with 1 liberty.
            numFound += findLiberties(adj, buf, bufStart, bufIdx+numFound);
            chainHeadsChecked[numChainHeadsChecked++] = head;
          }
        }
      }
    }

    cur = next_in_chain[cur];
  } while (cur != loc);

  return numFound;
}

//Helper, does the group at loc have at least one opponent group adjacent to it in atari?
bool Board::hasLibertyGainingCaptures(Loc loc) const {
  Player opp = getOpp(colors[loc]);
  Loc cur = loc;
  do
  {
    FOREACHADJ(
      Loc adj = cur + ADJOFFSET;
      if(colors[adj] == opp) {
        Loc head = chain_head[adj];
        if(chain_data[head].num_liberties == 1)
          return true;
      }
    );
    cur = next_in_chain[cur];
  } while (cur != loc);

  return false;
}

bool Board::searchIsLadderCapturedAttackerFirst2Libs(Loc loc, vector<Loc>& buf, vector<Loc>& workingMoves) {
  if(loc < 0 || loc >= MAX_ARR_SIZE)
    return false;
  if(colors[loc] != C_BLACK && colors[loc] != C_WHITE)
    return false;
  if(chain_data[chain_head[loc]].num_liberties != 2)
    return false;

  //Make it so that pla is always the defender
  Player pla = colors[loc];
  Player opp = getOpp(pla);

  int numLibs = findLiberties(loc,buf,0,0);
  assert(numLibs == 2);
  (void)numLibs; //Avoid warning when asserts are off

  Loc move0 = buf[0];
  Loc move1 = buf[1];
  bool move0Works = false;
  bool move1Works = false;

  //Suicide never relevant for ladders
  //Attacker: A suicide move cannot reduce the defender's liberties
  //Defender: A suicide move cannot gain liberties
  bool isMultiStoneSuicideLegal = false;
  if(isLegal(move0,opp,isMultiStoneSuicideLegal)) {
    MoveRecord record = playMoveRecorded(move0,opp);
    move0Works = searchIsLadderCaptured(loc,true,buf);
    undo(record);
  }
  if(isLegal(move1,opp,isMultiStoneSuicideLegal)) {
    MoveRecord record = playMoveRecorded(move1,opp);
    move1Works = searchIsLadderCaptured(loc,true,buf);
    undo(record);
  }

  if(move0Works || move1Works) {
    workingMoves.clear();
    if(move0Works)
      workingMoves.push_back(move0);
    if(move1Works)
      workingMoves.push_back(move1);
    return true;
  }
  return false;
}

bool Board::searchIsLadderCaptured(Loc loc, bool defenderFirst, vector<Loc>& buf) {
  if(loc < 0 || loc >= MAX_ARR_SIZE)
    return false;
  if(colors[loc] != C_BLACK && colors[loc] != C_WHITE)
    return false;

  if(chain_data[chain_head[loc]].num_liberties > 2 || (defenderFirst && chain_data[chain_head[loc]].num_liberties > 1))
    return false;

  //Make it so that pla is always the defender
  Player pla = colors[loc];
  Player opp = getOpp(pla);

  //Clear the ko loc for the defender at the root node - assume all kos work for the defender
  Loc ko_loc_saved = ko_loc;
  if(defenderFirst)
    ko_loc = NULL_LOC;

  //Stack for the search. These point to lists of possible moves to search at each level of the stack, indices refer to indices in [buf].
  int stackSize = x_size*y_size*3/2+1; //A bit bigger due to paranoia about recaptures making the sequence longer.
  static constexpr int arrSize = MAX_PLAY_SIZE * 3 / 2 + 1;
  int moveListStarts[arrSize]; //Buf idx of start of list
  int moveListLens[arrSize]; //Len of list
  int moveListCur[arrSize]; //Current move list idx searched, equal to -1 if list has not been generated.
  MoveRecord records[arrSize]; //Records so that we can undo moves as we search back up.
  int stackIdx = 0;
  int searchNodeCount = 0;
  static const int MAX_LADDER_SEARCH_NODE_BUDGET = 25000;

  moveListCur[0] = -1;
  moveListStarts[0] = 0;
  moveListLens[0] = 0;
  bool returnValue = false;
  bool returnedFromDeeper = false;
  // bool print = true;

  while(true) {
    // if(print) cout << ": " << stackIdx << " " << moveListCur[stackIdx] << " " << moveListStarts[stackIdx] << " " << moveListLens[stackIdx] << " " << returnValue << " " << returnedFromDeeper << endl;

    //Returned from the root - so that's the answer
    if(stackIdx <= -1) {
      assert(stackIdx == -1);
      ko_loc = ko_loc_saved;
      return returnValue;
    }

    //If we hit the stack limit, just consider it a failed ladder.
    if(stackIdx >= stackSize-1) {
      returnValue = true; returnedFromDeeper = true; stackIdx--; continue;
    }
    //If we hit a total node count limit, then just assume it doesn't work.
    if(searchNodeCount >= MAX_LADDER_SEARCH_NODE_BUDGET) {
      stackIdx -= 1;
      while(stackIdx >= 0) {
        undo(records[stackIdx]);
        stackIdx -= 1;
      }
      return false;
    }

    bool isDefender = (defenderFirst && (stackIdx % 2) == 0) || (!defenderFirst && (stackIdx % 2) == 1);

    //We just entered this level?
    if(moveListCur[stackIdx] == -1) {
      int libs = chain_data[chain_head[loc]].num_liberties;

      //Base cases.
      //If we are the attacker and the group has only 1 liberty, we already win.
      if(!isDefender && libs <= 1) { returnValue = true; returnedFromDeeper = true; stackIdx--; continue; }
      //If we are the attacker and the group has 3 liberties, we already lose.
      if(!isDefender && libs >= 3) { returnValue = false; returnedFromDeeper = true; stackIdx--; continue; }
      //If we are the defender and the group has 2 liberties, we already win.
      if(isDefender && libs >= 2) { returnValue = false; returnedFromDeeper = true; stackIdx--; continue; }
      //If we are the defender and the attacker left a simple ko point, assume we already win
      //because we don't want to say yes on ladders that depend on kos
      //This should also hopefully prevent any possible infinite loops - I don't know of any infinite loop
      //that would come up in a continuous atari sequence that doesn't ever leave a simple ko point.
      if(isDefender && ko_loc != NULL_LOC) { returnValue = false; returnedFromDeeper = true; stackIdx--; continue; }

      //Otherwise we need to keep searching.
      //Generate the move list. Attacker and defender generate moves on the group's liberties, but only the defender
      //generates moves on surrounding capturable opposing groups.
      int start = moveListStarts[stackIdx];
      int moveListLen = 0;
      if(isDefender) {
        moveListLen = findLibertyGainingCaptures(loc,buf,start,start);
        moveListLen += findLiberties(loc,buf,start,start+moveListLen);

        int lowerBoundLibs;
        int upperBoundLibs;
        //List is always nonempty, and the last element always is the lone liberty of the defender group
        getBoundNumLibertiesAfterPlay(buf[start+moveListLen-1], pla, lowerBoundLibs, upperBoundLibs);
        //Defender immediately wins if there are provably enough libs
        if(lowerBoundLibs >= 3)
        { returnValue = false; returnedFromDeeper = true; stackIdx--; continue; }
        //Attacker immediately wins if defender has not enough libs and there are no alternatives
        if(moveListLen == 1 && upperBoundLibs <= 1)
        { returnValue = true; returnedFromDeeper = true; stackIdx--; continue; }
      }
      else {
        moveListLen += findLiberties(loc,buf,start,start);
        // if(moveListLen != 2) {
        //   cout << *this << endl;
        //   cout << stackIdx << endl;
        //   for(int i = 0; i<stackIdx; i++) {
        //     cout << moveListCur[stackIdx] << " " << moveListStarts[stackIdx] << " " << moveListLens[stackIdx] << " "
        //          << Location::toString(buf[moveListStarts[stackIdx] + moveListCur[stackIdx]],*this) << endl;
        //   }
        //   cout << "===" << endl;
        //   checkConsistency();
        // }
        assert(moveListLen == 2);

        int libs0 = getNumImmediateLiberties(buf[start]);
        int libs1 = getNumImmediateLiberties(buf[start+1]);

        //If we are the attacker and we're in a double-ko death situation, then assume we win.
        //Both defender liberties must be ko mouths, connecting either ko mouth must not increase the defender's
        //liberties, and none of the attacker's surrounding stones can currently be in atari.
        //This is not complete - there are situations where the defender's connections increase liberties, or where
        //the attacker has stones in atari, but where the defender is still in inescapable atari even if they have
        //a large finite number of ko threats. But it's better than nothing.
        if(libs0 == 0 && libs1 == 0 && wouldBeKoCapture(buf[start],opp) && wouldBeKoCapture(buf[start+1],opp)) {
          if(getNumLibertiesAfterPlay(buf[start],pla,3) <= 2 && getNumLibertiesAfterPlay(buf[start+1],pla,3) <= 2) {
            if(!hasLibertyGainingCaptures(loc))
            { returnValue = true; returnedFromDeeper = true; stackIdx--; continue; }
          }
        }

        //Early quitouts if the liberties are not adjacent
        //(so that filling one doesn't fill an immediate liberty of the other)
        if(!Location::isAdjacent(buf[start],buf[start+1],x_size)) {
          //We lose automatically if both escapes get the defender too many libs
          if(libs0 >= 3 && libs1 >= 3)
          { returnValue = false; returnedFromDeeper = true; stackIdx--; continue; }
          //Move 1 is not possible, so shrink the list
          else if(libs0 >= 3)
          { moveListLen = 1; }
          //Move 0 is not possible, so swap and shrink the list
          else if(libs1 >= 3)
          { buf[start] = buf[start+1]; moveListLen = 1; }
        }
        //Order the two moves based on a simple heuristic - for each neighboring group with any liberties
        //count that the opponent could connect to, count liberties - 1.5.
        if(moveListLen > 1) {
          libs0 = libs0 * 2 + countHeuristicConnectionLibertiesX2(buf[start],pla);
          libs1 = libs1 * 2 + countHeuristicConnectionLibertiesX2(buf[start+1],pla);
          if(libs1 > libs0) {
            int tmp = buf[start];
            buf[start] = buf[start+1];
            buf[start+1] = tmp;
          }
        }
      }
      moveListLens[stackIdx] = moveListLen;

      //And indicate to begin search on the first move generated.
      moveListCur[stackIdx] = 0;
    }
    //Else, we returned from a deeper level (or the same level, via illegal move)
    else {
      assert(moveListCur[stackIdx] >= 0);
      assert(moveListCur[stackIdx] < moveListLens[stackIdx]);
      //If we returned from deeper we need to undo the move we made
      if(returnedFromDeeper)
        undo(records[stackIdx]);

      //Defender has a move that is not ladder captured?
      if(isDefender && !returnValue) {
        //Return! (returnValue is still false, as desired)
        returnedFromDeeper = true;
        stackIdx--;
        continue;
      }
      //Attacker has a move that does ladder capture?
      if(!isDefender && returnValue) {
        //Return! (returnValue is still true, as desired)
        returnedFromDeeper = true;
        stackIdx--;
        continue;
      }

      //Move on to the next move to search
      moveListCur[stackIdx]++;
    }

    //If there is no next move to search, then we lose.
    if(moveListCur[stackIdx] >= moveListLens[stackIdx]) {
      //For a defender, that means a ladder capture.
      //For an attacker, that means no ladder capture found.
      returnValue = isDefender;
      returnedFromDeeper = true;
      stackIdx--;
      continue;
    }

    //Otherwise we do have an next move to search. Grab it.
    Loc move = buf[moveListStarts[stackIdx] + moveListCur[stackIdx]];
    Player p = (isDefender ? pla : opp);

    // if(print) cout << "play " << Location::getX(move,x_size) << " " << Location::getY(move,x_size) << " " << p << endl;

    //Illegal move - treat it the same as a failed move, but don't return up a level so that we
    //loop again and just try the next move.
    bool isMultiStoneSuicideLegal = false;
    if(!isLegal(move,p,isMultiStoneSuicideLegal)) {
      returnValue = isDefender;
      returnedFromDeeper = false;
      // if(print) cout << "illegal " << endl;
      continue;
    }

    //Play and record the move!
    records[stackIdx] = playMoveRecorded(move,p);
    searchNodeCount++;

    //And recurse to the next level
    stackIdx++;
    moveListCur[stackIdx] = -1;
    moveListStarts[stackIdx] = moveListStarts[stackIdx-1] + moveListLens[stackIdx-1];
    moveListLens[stackIdx] = 0;
  }

}




void Board::calculateIndependentLifeAreaHelper(
  const Color* basicArea,
  Color* result,
  int& whiteMinusBlackIndependentLifeRegionCount
) const {
  Loc queue[MAX_ARR_SIZE];
  whiteMinusBlackIndependentLifeRegionCount = 0;

  //Iterate through all the regions that players own via area scoring and mark
  //all the ones that are touching dame OR that contain an atari stone
  bool isSeki[MAX_ARR_SIZE];
  for(int i = 0; i<MAX_ARR_SIZE; i++)
    isSeki[i] = false;

  int queueHead = 0;
  int queueTail = 0;

  for(int y = 0; y < y_size; y++) {
    for(int x = 0; x < x_size; x++) {
      Loc loc = Location::getLoc(x,y,x_size);
      if(basicArea[loc] != C_EMPTY && !isSeki[loc]) {
        if(
          //Stone of player owning the area is in atari? Treat as seki.
          (colors[loc] == basicArea[loc] && getNumLiberties(loc) == 1) ||
          //Touches dame? Treat as seki
          ((colors[loc+ADJ0] == C_EMPTY && basicArea[loc+ADJ0] == C_EMPTY) ||
           (colors[loc+ADJ1] == C_EMPTY && basicArea[loc+ADJ1] == C_EMPTY) ||
           (colors[loc+ADJ2] == C_EMPTY && basicArea[loc+ADJ2] == C_EMPTY) ||
           (colors[loc+ADJ3] == C_EMPTY && basicArea[loc+ADJ3] == C_EMPTY))
        ) {
          Player pla = basicArea[loc];
          isSeki[loc] = true;
          queue[queueTail++] = loc;
          while(queueHead != queueTail) {
            //Pop next location off queue
            Loc nextLoc = queue[queueHead++];

            //Look all around it, floodfill
            FOREACHADJ(
              Loc adj = nextLoc + ADJOFFSET;
              if(basicArea[adj] == pla && !isSeki[adj]) {
                isSeki[adj] = true;
                queue[queueTail++] = adj;
              }
            );
          }
        }
      }
    }
  }

  queueHead = 0;
  queueTail = 0;

  //Now, walk through and copy all non-seki-touching basic areas into the result counting
  //how many there are.
  for(int y = 0; y < y_size; y++) {
    for(int x = 0; x < x_size; x++) {
      Loc loc = Location::getLoc(x,y,x_size);
      if(basicArea[loc] != C_EMPTY && !isSeki[loc] && result[loc] != basicArea[loc]) {
        Player pla = basicArea[loc];
        whiteMinusBlackIndependentLifeRegionCount += (pla == P_WHITE ? 1 : -1);
        result[loc] = basicArea[loc];
        queue[queueTail++] = loc;
        while(queueHead != queueTail) {
          //Pop next location off queue
          Loc nextLoc = queue[queueHead++];

          //Look all around it, floodfill
          FOREACHADJ(
            Loc adj = nextLoc + ADJOFFSET;
            if(basicArea[adj] == pla && result[adj] != basicArea[adj]) {
              result[adj] = basicArea[adj];
              queue[queueTail++] = adj;
            }
          );
        }
      }
    }
  }
}



void Board::checkConsistency() const {
  const string errLabel = string("Board::checkConsistency(): ");

  bool chainLocChecked[MAX_ARR_SIZE];
  for(int i = 0; i<MAX_ARR_SIZE; i++)
    chainLocChecked[i] = false;

  vector<Loc> buf;
  auto checkChainConsistency = [&buf,errLabel,&chainLocChecked,this](Loc loc) {
    Player pla = colors[loc];
    Loc head = chain_head[loc];
    Loc cur = loc;
    int stoneCount = 0;
    int pseudoLibs = 0;
    bool foundChainHead = false;
    do {
      chainLocChecked[cur] = true;

      if(colors[cur] != pla)
        throw StringError(errLabel + "Chain is not all the same color");
      if(chain_head[cur] != head)
        throw StringError(errLabel + "Chain does not all have the same head");

      stoneCount++;
      pseudoLibs += getNumImmediateLiberties(cur);
      if(cur == head)
        foundChainHead = true;

      if(stoneCount > MAX_PLAY_SIZE)
        throw StringError(errLabel + "Chain exceeds size of board - broken circular list?");
      cur = next_in_chain[cur];

      if(cur < 0 || cur >= MAX_ARR_SIZE)
        throw StringError(errLabel + "Chain location is outside of board bounds, data corruption?");

    } while (cur != loc);

    if(!foundChainHead)
      throw StringError(errLabel + "Chain loop does not contain head");

    const ChainData& data = chain_data[head];
    if(data.owner != pla)
      throw StringError(errLabel + "Chain data owner does not match stones");
    if(data.num_locs != stoneCount)
      throw StringError(errLabel + "Chain data num_locs does not match actual stone count");
    if(data.num_liberties > pseudoLibs)
      throw StringError(errLabel + "Chain data liberties exceeds pseudoliberties");
    if(data.num_liberties <= 0)
      throw StringError(errLabel + "Chain data liberties is nonpositive");

    int numFoundLibs = findLiberties(loc,buf,0,0);
    if(numFoundLibs != data.num_liberties)
      throw StringError(errLabel + "FindLiberties found a different number of libs");
  };

  Hash128 tmp_pos_hash = ZOBRIST_SIZE_X_HASH[x_size] ^ ZOBRIST_SIZE_Y_HASH[y_size];
  int emptyCount = 0;
  for(Loc loc = 0; loc < MAX_ARR_SIZE; loc++) {
    int x = Location::getX(loc,x_size);
    int y = Location::getY(loc,x_size);
    if(x < 0 || x >= x_size || y < 0 || y >= y_size) {
      if(colors[loc] != C_WALL)
        throw StringError(errLabel + "Non-WALL value outside of board legal area");
    }
    else {
      if(colors[loc] == C_BLACK || colors[loc] == C_WHITE) {
        if(!chainLocChecked[loc])
          checkChainConsistency(loc);
        // if(empty_list.contains(loc))
        //   throw StringError(errLabel + "Empty list contains filled location");

        tmp_pos_hash ^= ZOBRIST_BOARD_HASH[loc][colors[loc]];
        tmp_pos_hash ^= ZOBRIST_BOARD_HASH[loc][C_EMPTY];
      }
      else if(colors[loc] == C_EMPTY) {
        // if(!empty_list.contains(loc))
        //   throw StringError(errLabel + "Empty list doesn't contain empty location");
        emptyCount += 1;
      }
      else
        throw StringError(errLabel + "Non-(black,white,empty) value within board legal area");
    }
  }

  if(pos_hash != tmp_pos_hash)
    throw StringError(errLabel + "Pos hash does not match expected");

  // if(empty_list.size_ != emptyCount)
  //   throw StringError(errLabel + "Empty list size is not the number of empty points");
  // for(int i = 0; i<emptyCount; i++) {
  //   Loc loc = empty_list.list_[i];
  //   int x = Location::getX(loc,x_size);
  //   int y = Location::getY(loc,x_size);
  //   if(x < 0 || x >= x_size || y < 0 || y >= y_size)
  //     throw StringError(errLabel + "Invalid empty list loc");
  //   if(empty_list.indices_[loc] != i)
  //     throw StringError(errLabel + "Empty list index for loc in index i is not i");
  // }

  if(ko_loc != NULL_LOC) {
    int x = Location::getX(ko_loc,x_size);
    int y = Location::getY(ko_loc,x_size);
    if(x < 0 || x >= x_size || y < 0 || y >= y_size)
      throw StringError(errLabel + "Invalid simple ko loc");
    if(getNumImmediateLiberties(ko_loc) != 0)
      throw StringError(errLabel + "Simple ko loc has immediate liberties");
  }

  short tmpAdjOffsets[8];
  Location::getAdjacentOffsets(tmpAdjOffsets,x_size);
  for(int i = 0; i<8; i++)
    if(tmpAdjOffsets[i] != adj_offsets[i])
      throw StringError(errLabel + "Corrupted adj_offsets array");
}

bool Board::isEqualForTesting(const Board& other, bool checkNumCaptures, bool checkSimpleKo) const {
  checkConsistency();
  other.checkConsistency();
  if(x_size != other.x_size)
    return false;
  if(y_size != other.y_size)
    return false;
  if(checkSimpleKo && ko_loc != other.ko_loc)
    return false;
  if(checkNumCaptures && numBlackCaptures != other.numBlackCaptures)
    return false;
  if(checkNumCaptures && numWhiteCaptures != other.numWhiteCaptures)
    return false;
  if(pos_hash != other.pos_hash)
    return false;
  for(int i = 0; i<MAX_ARR_SIZE; i++) {
    if(colors[i] != other.colors[i])
      return false;
  }
  //We don't require that the chain linked lists are in the same order.
  //Consistency check ensures that all the linked lists are consistent with colors array, which we checked.
  return true;
}



//IO FUNCS------------------------------------------------------------------------------------------

char PlayerIO::colorToChar(Color c)
{
  switch(c) {
  case C_BLACK: return 'X';
  case C_WHITE: return 'O';
  case C_EMPTY: return '.';
  default:  return '#';
  }
}

string PlayerIO::playerToString(Color c)
{
  switch(c) {
  case C_BLACK: return "Black";
  case C_WHITE: return "White";
  case C_EMPTY: return "Empty";
  default:  return "Wall";
  }
}

string PlayerIO::playerToStringShort(Color c)
{
  switch(c) {
  case C_BLACK: return "B";
  case C_WHITE: return "W";
  case C_EMPTY: return "E";
  default:  return "";
  }
}

bool PlayerIO::tryParsePlayer(const string& s, Player& pla) {
  string str = Global::toLower(s);
  if(str == "black" || str == "b") {
    pla = P_BLACK;
    return true;
  }
  else if(str == "white" || str == "w") {
    pla = P_WHITE;
    return true;
  }
  return false;
}

Player PlayerIO::parsePlayer(const string& s) {
  Player pla = C_EMPTY;
  bool suc = tryParsePlayer(s,pla);
  if(!suc)
    throw StringError("Could not parse player: " + s);
  return pla;
}

string Location::toStringMach(Loc loc, int x_size)
{
  if(loc == Board::PASS_LOC)
    return string("pass");
  if(loc == Board::NULL_LOC)
    return string("null");
  char buf[128];
  sprintf(buf,"(%d,%d)",getX(loc,x_size),getY(loc,x_size));
  return string(buf);
}

string Location::toString(Loc loc, int x_size, int y_size)
{
  if(x_size > 25*25)
    return toStringMach(loc,x_size);
  if(loc == Board::PASS_LOC)
    return string("pass");
  if(loc == Board::NULL_LOC)
    return string("null");
  const char* xChar = "ABCDEFGHJKLMNOPQRSTUVWXYZ";
  int x = getX(loc,x_size);
  int y = getY(loc,x_size);
  if(x >= x_size || x < 0 || y < 0 || y >= y_size)
    return toStringMach(loc,x_size);

  char buf[128];
  if(x <= 24)
    sprintf(buf,"%c%d",xChar[x],y_size-y);
  else
    sprintf(buf,"%c%c%d",xChar[x/25-1],xChar[x%25],y_size-y);
  return string(buf);
}

string Location::toString(Loc loc, const Board& b) {
  return toString(loc,b.x_size,b.y_size);
}

string Location::toStringMach(Loc loc, const Board& b) {
  return toStringMach(loc,b.x_size);
}

static bool tryParseLetterCoordinate(char c, int& x) {
  if(c >= 'A' && c <= 'H')
    x = c-'A';
  else if(c >= 'a' && c <= 'h')
    x = c-'a';
  else if(c >= 'J' && c <= 'Z')
    x = c-'A'-1;
  else if(c >= 'j' && c <= 'z')
    x = c-'a'-1;
  else
    return false;
  return true;
}

bool Location::tryOfString(const string& str, int x_size, int y_size, Loc& result) {
  string s = Global::trim(str);
  if(s.length() < 2)
    return false;
  if(Global::isEqualCaseInsensitive(s,string("pass")) || Global::isEqualCaseInsensitive(s,string("pss"))) {
    result = Board::PASS_LOC;
    return true;
  }
  if(s[0] == '(') {
    if(s[s.length()-1] != ')')
      return false;
    s = s.substr(1,s.length()-2);
    vector<string> pieces = Global::split(s,',');
    if(pieces.size() != 2)
      return false;
    int x;
    int y;
    bool sucX = Global::tryStringToInt(pieces[0],x);
    bool sucY = Global::tryStringToInt(pieces[1],y);
    if(!sucX || !sucY)
      return false;
    result = Location::getLoc(x,y,x_size);
    return true;
  }
  else {
    int x;
    if(!tryParseLetterCoordinate(s[0],x))
      return false;

    //Extended format
    if((s[1] >= 'A' && s[1] <= 'Z') || (s[1] >= 'a' && s[1] <= 'z')) {
      int x1;
      if(!tryParseLetterCoordinate(s[1],x1))
        return false;
      x = (x+1) * 25 + x1;
      s = s.substr(2,s.length()-2);
    }
    else {
      s = s.substr(1,s.length()-1);
    }

    int y;
    bool sucY = Global::tryStringToInt(s,y);
    if(!sucY)
      return false;
    y = y_size - y;
    if(x < 0 || y < 0 || x >= x_size || y >= y_size)
      return false;
    result = Location::getLoc(x,y,x_size);
    return true;
  }
}

bool Location::tryOfStringAllowNull(const string& str, int x_size, int y_size, Loc& result) {
  if(str == "null") {
    result = Board::NULL_LOC;
    return true;
  }
  return tryOfString(str, x_size, y_size, result);
}

bool Location::tryOfString(const string& str, const Board& b, Loc& result) {
  return tryOfString(str,b.x_size,b.y_size,result);
}

bool Location::tryOfStringAllowNull(const string& str, const Board& b, Loc& result) {
  return tryOfStringAllowNull(str,b.x_size,b.y_size,result);
}

Loc Location::ofString(const string& str, int x_size, int y_size) {
  Loc result;
  if(tryOfString(str,x_size,y_size,result))
    return result;
  throw StringError("Could not parse board location: " + str);
}

Loc Location::ofStringAllowNull(const string& str, int x_size, int y_size) {
  Loc result;
  if(tryOfStringAllowNull(str,x_size,y_size,result))
    return result;
  throw StringError("Could not parse board location: " + str);
}

Loc Location::ofString(const string& str, const Board& b) {
  return ofString(str,b.x_size,b.y_size);
}


Loc Location::ofStringAllowNull(const string& str, const Board& b) {
  return ofStringAllowNull(str,b.x_size,b.y_size);
}

vector<Loc> Location::parseSequence(const string& str, const Board& board) {
  vector<string> pieces = Global::split(Global::trim(str),' ');
  vector<Loc> locs;
  for(size_t i = 0; i<pieces.size(); i++) {
    string piece = Global::trim(pieces[i]);
    if(piece.length() <= 0)
      continue;
    locs.push_back(Location::ofString(piece,board));
  }
  return locs;
}

void Board::printBoard(ostream& out, const Board& board, Loc markLoc, const vector<Move>* hist) {
  if(hist != NULL)
    out << "MoveNum: " << hist->size() << " ";
  out << "HASH: " << board.pos_hash << "\n";
  bool showCoords = board.x_size <= 50 && board.y_size <= 50;
  if(showCoords) {
    const char* xChar = "ABCDEFGHJKLMNOPQRSTUVWXYZ";
    out << "  ";
    for(int x = 0; x < board.x_size; x++) {
      if(x <= 24) {
        out << " ";
        out << xChar[x];
      }
      else {
        out << "A" << xChar[x-25];
      }
    }
    out << "\n";
  }

  for(int y = 0; y < board.y_size; y++)
  {
    if(showCoords) {
      char buf[16];
      sprintf(buf,"%2d",board.y_size-y);
      out << buf << ' ';
    }
    for(int x = 0; x < board.x_size; x++)
    {
      Loc loc = Location::getLoc(x,y,board.x_size);
      char s = PlayerIO::colorToChar(board.colors[loc]);
      if(board.colors[loc] == C_EMPTY && markLoc == loc)
        out << '@';
      else
        out << s;

      bool histMarked = false;
      if(hist != NULL) {
        size_t start = hist->size() >= 3 ? hist->size()-3 : 0;
        for(size_t i = 0; start+i < hist->size(); i++) {
          if((*hist)[start+i].loc == loc) {
            out << (1+i);
            histMarked = true;
            break;
          }
        }
      }

      if(x < board.x_size-1 && !histMarked)
        out << ' ';
    }
    out << "\n";
  }
  out << "\n";
}

ostream& operator<<(ostream& out, const Board& board) {
  Board::printBoard(out,board,Board::NULL_LOC,NULL);
  return out;
}


string Board::toStringSimple(const Board& board, char lineDelimiter) {
  string s;
  for(int y = 0; y < board.y_size; y++) {
    for(int x = 0; x < board.x_size; x++) {
      Loc loc = Location::getLoc(x,y,board.x_size);
      s += PlayerIO::colorToChar(board.colors[loc]);
    }
    s += lineDelimiter;
  }
  return s;
}

Board Board::parseBoard(int xSize, int ySize, const string& s) {
  return parseBoard(xSize,ySize,s,'\n');
}

Board Board::parseBoard(int xSize, int ySize, const string& s, char lineDelimiter) {
  Board board(xSize,ySize);
  vector<string> lines = Global::split(Global::trim(s),lineDelimiter);

  //Throw away coordinate labels line if it exists
  if(lines.size() == ySize+1 && Global::isPrefix(lines[0],"A"))
    lines.erase(lines.begin());

  if(lines.size() != ySize)
    throw StringError("Board::parseBoard - string has different number of board rows than ySize");

  for(int y = 0; y<ySize; y++) {
    string line = Global::trim(lines[y]);
    //Throw away coordinates if they exist
    size_t firstNonDigitIdx = 0;
    while(firstNonDigitIdx < line.length() && Global::isDigit(line[firstNonDigitIdx]))
      firstNonDigitIdx++;
    line.erase(0,firstNonDigitIdx);
    line = Global::trim(line);

    if(line.length() != xSize && line.length() != 2*xSize-1)
      throw StringError("Board::parseBoard - line length not compatible with xSize");

    for(int x = 0; x<xSize; x++) {
      char c;
      if(line.length() == xSize)
        c = line[x];
      else
        c = line[x*2];

      Loc loc = Location::getLoc(x,y,board.x_size);
      if(c == '.' || c == ' ' || c == '*' || c == ',' || c == '`')
        continue;
      else if(c == 'o' || c == 'O')
        board.setStone(loc,P_WHITE);
      else if(c == 'x' || c == 'X')
        board.setStone(loc,P_BLACK);
      else
        throw StringError(string("Board::parseBoard - could not parse board character: ") + c);
    }
  }
  return board;
}

nlohmann::json Board::toJson(const Board& board) {
  nlohmann::json data;
  data["xSize"] = board.x_size;
  data["ySize"] = board.y_size;
  data["stones"] = Board::toStringSimple(board,'|');
  data["koLoc"] = Location::toString(board.ko_loc,board);
  data["numBlackCaptures"] = board.numBlackCaptures;
  data["numWhiteCaptures"] = board.numWhiteCaptures;
  return data;
}

Board Board::ofJson(const nlohmann::json& data) {
  int xSize = data["xSize"].get<int>();
  int ySize = data["ySize"].get<int>();
  Board board = Board::parseBoard(xSize,ySize,data["stones"].get<string>(),'|');
  board.setSimpleKoLoc(Location::ofStringAllowNull(data["koLoc"].get<string>(),board));
  board.numBlackCaptures = data["numBlackCaptures"].get<int>();
  board.numWhiteCaptures = data["numWhiteCaptures"].get<int>();
  return board;
}


//Count empty spaces in the connected empty region containing initialLoc (which must be empty)
//not counting where emptyCounted == true, incrementing count each time and marking emptyCounted.
//Return early and true if count > bound. Return false otherwise.
bool Board::countEmptyHelper(bool* emptyCounted, Loc initialLoc, int& count, int bound) const {
  if(emptyCounted[initialLoc])
    return false;
  count += 1;
  emptyCounted[initialLoc] = true;
  if(count > bound)
    return true;

  int numLeft = 0;
  int numExpanded = 0;
  int toExpand[MAX_ARR_SIZE];
  toExpand[numLeft++] = initialLoc;
  while(numExpanded < numLeft) {
    Loc loc = toExpand[numExpanded++];
    FOREACHADJ(
      Loc adj = loc + ADJOFFSET;
      if(colors[adj] == C_EMPTY && !emptyCounted[adj]) {
        count += 1;
        emptyCounted[adj] = true;
        if(count > bound)
          return true;
        toExpand[numLeft++] = adj;
      }
    );
  }
  return false;
}

bool Board::simpleRepetitionBoundGt(Loc loc, int bound) const {
  if(loc == NULL_LOC || loc == PASS_LOC)
    return false;

  int count = 0;

  if(colors[loc] != C_EMPTY) {
    count += chain_data[chain_head[loc]].num_locs;
    //Fast quitout
    if(count + chain_data[chain_head[loc]].num_liberties > bound)
      return true;
  }

  bool emptyCounted[MAX_ARR_SIZE];
  memset(emptyCounted, false, sizeof(emptyCounted[0])*MAX_ARR_SIZE);

  //Suicide - return just the count of the empty spaces in the region
  if(colors[loc] == C_EMPTY) {
    return countEmptyHelper(emptyCounted, loc, count, bound);
  }
  else {
    Loc cur = loc;
    do {
      for(int i = 0; i < 4; i++) {
        Loc lib = cur + adj_offsets[i];
        if(colors[lib] == C_EMPTY) {
          if(countEmptyHelper(emptyCounted, lib, count, bound))
            return true;
        }
      }
      cur = next_in_chain[cur];
    } while (cur != loc);
  }

  return false;
}
