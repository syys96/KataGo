#include "../game/boardhistory.h"

#include <algorithm>

using namespace std;

static Hash128 getKoHash(const Rules& rules, const Board& board, Player pla) {
    return board.pos_hash;
}
static Hash128 getKoHashAfterMoveNonEncore(const Rules& rules, Hash128 posHashAfterMove, Player pla) {
    return posHashAfterMove;
}


BoardHistory::BoardHistory()
  :rules(),
   moveHistory(),koHashHistory(),
   firstTurnIdxWithKoHistory(0),
   initialBoard(),
   initialPla(P_BLACK),
   initialTurnNumber(0),
   assumeMultipleStartingBlackMovesAreHandicap(false),
   whiteHasMoved(false),
   recentBoards(),
   currentRecentBoardIdx(0),
   presumedNextMovePla(P_BLACK),
   koCapturesInEncore(),
   isPastNormalPhaseEnd(false),
   isGameFinished(false),winner(C_EMPTY),finalWhiteMinusBlackScore(0.0f),
   isScored(false),isNoResult(false),isResignation(false)
{
  std::fill(wasEverOccupiedOrPlayed, wasEverOccupiedOrPlayed+Board::MAX_ARR_SIZE, false);
  std::fill(superKoBanned, superKoBanned+Board::MAX_ARR_SIZE, false);
}

BoardHistory::~BoardHistory()
{}

BoardHistory::BoardHistory(const Board& board, Player pla, const Rules& r)
  :rules(r),
   moveHistory(),koHashHistory(),
   firstTurnIdxWithKoHistory(0),
   initialBoard(),
   initialPla(),
   initialTurnNumber(0),
   assumeMultipleStartingBlackMovesAreHandicap(false),
   whiteHasMoved(false),
   recentBoards(),
   currentRecentBoardIdx(0),
   presumedNextMovePla(pla),
   koCapturesInEncore(),
   isPastNormalPhaseEnd(false),
   isGameFinished(false),winner(C_EMPTY),finalWhiteMinusBlackScore(0.0f),
   isScored(false),isNoResult(false),isResignation(false)
{
  std::fill(wasEverOccupiedOrPlayed, wasEverOccupiedOrPlayed+Board::MAX_ARR_SIZE, false);
  std::fill(superKoBanned, superKoBanned+Board::MAX_ARR_SIZE, false);

  clear(board,pla,rules);
}

BoardHistory::BoardHistory(const BoardHistory& other)
  :rules(other.rules),
   moveHistory(other.moveHistory),koHashHistory(other.koHashHistory),
   firstTurnIdxWithKoHistory(other.firstTurnIdxWithKoHistory),
   initialBoard(other.initialBoard),
   initialPla(other.initialPla),
   initialTurnNumber(other.initialTurnNumber),
   assumeMultipleStartingBlackMovesAreHandicap(other.assumeMultipleStartingBlackMovesAreHandicap),
   whiteHasMoved(other.whiteHasMoved),
   recentBoards(),
   currentRecentBoardIdx(other.currentRecentBoardIdx),
   presumedNextMovePla(other.presumedNextMovePla),
   koCapturesInEncore(other.koCapturesInEncore),
   isPastNormalPhaseEnd(other.isPastNormalPhaseEnd),
   isGameFinished(other.isGameFinished),winner(other.winner),finalWhiteMinusBlackScore(other.finalWhiteMinusBlackScore),
   isScored(other.isScored),isNoResult(other.isNoResult),isResignation(other.isResignation)
{
  std::copy(other.recentBoards, other.recentBoards+NUM_RECENT_BOARDS, recentBoards);
  std::copy(other.wasEverOccupiedOrPlayed, other.wasEverOccupiedOrPlayed+Board::MAX_ARR_SIZE, wasEverOccupiedOrPlayed);
  std::copy(other.superKoBanned, other.superKoBanned+Board::MAX_ARR_SIZE, superKoBanned);
}


BoardHistory& BoardHistory::operator=(const BoardHistory& other)
{
  if(this == &other)
    return *this;
  rules = other.rules;
  moveHistory = other.moveHistory;
  koHashHistory = other.koHashHistory;
  firstTurnIdxWithKoHistory = other.firstTurnIdxWithKoHistory;
  initialBoard = other.initialBoard;
  initialPla = other.initialPla;
  initialTurnNumber = other.initialTurnNumber;
  assumeMultipleStartingBlackMovesAreHandicap = other.assumeMultipleStartingBlackMovesAreHandicap;
  whiteHasMoved = other.whiteHasMoved;
  std::copy(other.recentBoards, other.recentBoards+NUM_RECENT_BOARDS, recentBoards);
  currentRecentBoardIdx = other.currentRecentBoardIdx;
  presumedNextMovePla = other.presumedNextMovePla;
  std::copy(other.wasEverOccupiedOrPlayed, other.wasEverOccupiedOrPlayed+Board::MAX_ARR_SIZE, wasEverOccupiedOrPlayed);
  std::copy(other.superKoBanned, other.superKoBanned+Board::MAX_ARR_SIZE, superKoBanned);
  koCapturesInEncore = other.koCapturesInEncore;
  isPastNormalPhaseEnd = other.isPastNormalPhaseEnd;
  isGameFinished = other.isGameFinished;
  winner = other.winner;
  finalWhiteMinusBlackScore = other.finalWhiteMinusBlackScore;
  isScored = other.isScored;
  isNoResult = other.isNoResult;
  isResignation = other.isResignation;

  return *this;
}

BoardHistory::BoardHistory(BoardHistory&& other) noexcept
 :rules(other.rules),
  moveHistory(std::move(other.moveHistory)),koHashHistory(std::move(other.koHashHistory)),
  firstTurnIdxWithKoHistory(other.firstTurnIdxWithKoHistory),
  initialBoard(other.initialBoard),
  initialPla(other.initialPla),
  initialTurnNumber(other.initialTurnNumber),
  assumeMultipleStartingBlackMovesAreHandicap(other.assumeMultipleStartingBlackMovesAreHandicap),
  whiteHasMoved(other.whiteHasMoved),
  recentBoards(),
  currentRecentBoardIdx(other.currentRecentBoardIdx),
  presumedNextMovePla(other.presumedNextMovePla),
  koCapturesInEncore(std::move(other.koCapturesInEncore)),
  isPastNormalPhaseEnd(other.isPastNormalPhaseEnd),
  isGameFinished(other.isGameFinished),winner(other.winner),finalWhiteMinusBlackScore(other.finalWhiteMinusBlackScore),
  isScored(other.isScored),isNoResult(other.isNoResult),isResignation(other.isResignation)
{
  std::copy(other.recentBoards, other.recentBoards+NUM_RECENT_BOARDS, recentBoards);
  std::copy(other.wasEverOccupiedOrPlayed, other.wasEverOccupiedOrPlayed+Board::MAX_ARR_SIZE, wasEverOccupiedOrPlayed);
  std::copy(other.superKoBanned, other.superKoBanned+Board::MAX_ARR_SIZE, superKoBanned);
}

BoardHistory& BoardHistory::operator=(BoardHistory&& other) noexcept
{
  rules = other.rules;
  moveHistory = std::move(other.moveHistory);
  koHashHistory = std::move(other.koHashHistory);
  firstTurnIdxWithKoHistory = other.firstTurnIdxWithKoHistory;
  initialBoard = other.initialBoard;
  initialPla = other.initialPla;
  initialTurnNumber = other.initialTurnNumber;
  assumeMultipleStartingBlackMovesAreHandicap = other.assumeMultipleStartingBlackMovesAreHandicap;
  whiteHasMoved = other.whiteHasMoved;
  std::copy(other.recentBoards, other.recentBoards+NUM_RECENT_BOARDS, recentBoards);
  currentRecentBoardIdx = other.currentRecentBoardIdx;
  presumedNextMovePla = other.presumedNextMovePla;
  std::copy(other.wasEverOccupiedOrPlayed, other.wasEverOccupiedOrPlayed+Board::MAX_ARR_SIZE, wasEverOccupiedOrPlayed);
  std::copy(other.superKoBanned, other.superKoBanned+Board::MAX_ARR_SIZE, superKoBanned);
  koCapturesInEncore = std::move(other.koCapturesInEncore);
  isPastNormalPhaseEnd = other.isPastNormalPhaseEnd;
  isGameFinished = other.isGameFinished;
  winner = other.winner;
  finalWhiteMinusBlackScore = other.finalWhiteMinusBlackScore;
  isScored = other.isScored;
  isNoResult = other.isNoResult;
  isResignation = other.isResignation;

  return *this;
}

void BoardHistory::clear(const Board& board, Player pla, const Rules& r) {
  rules = r;
  moveHistory.clear();
  koHashHistory.clear();
  firstTurnIdxWithKoHistory = 0;

  initialBoard = board;
  initialPla = pla;
  initialTurnNumber = 0;
  assumeMultipleStartingBlackMovesAreHandicap = false;
  whiteHasMoved = false;

  //This makes it so that if we ask for recent boards with a lookback beyond what we have a history for,
  //we simply return copies of the starting board.
  for(int i = 0; i<NUM_RECENT_BOARDS; i++)
    recentBoards[i] = board;
  currentRecentBoardIdx = 0;

  presumedNextMovePla = pla;

  for(int y = 0; y<board.y_size; y++) {
    for(int x = 0; x<board.x_size; x++) {
      Loc loc = Location::getLoc(x,y,board.x_size);
      wasEverOccupiedOrPlayed[loc] = (board.colors[loc] != C_EMPTY);
    }
  }

  std::fill(superKoBanned, superKoBanned+Board::MAX_ARR_SIZE, false);
  koCapturesInEncore.clear();
  isPastNormalPhaseEnd = false;
  isGameFinished = false;
  winner = C_EMPTY;
  finalWhiteMinusBlackScore = 0.0f;
  isScored = false;
  isNoResult = false;
  isResignation = false;


  //Push hash for the new board state
  koHashHistory.push_back(getKoHash(rules,board,pla));

}

void BoardHistory::setInitialTurnNumber(int n) {
  initialTurnNumber = n;
}


void BoardHistory::printBasicInfo(ostream& out, const Board& board) const {
  Board::printBoard(out, board, Board::NULL_LOC, &moveHistory);
  out << "Next player: " << PlayerIO::playerToString(presumedNextMovePla) << endl;
  out << "Rules: " << rules.toJsonString() << endl;
}

void BoardHistory::printDebugInfo(ostream& out, const Board& board) const {
  out << board << endl;
  out << "Initial pla " << PlayerIO::playerToString(initialPla) << endl;
  out << "Rules " << rules << endl;
  out << "Presumed next pla " << PlayerIO::playerToString(presumedNextMovePla) << endl;
  out << "Past normal phase end " << isPastNormalPhaseEnd << endl;
  out << "Game result " << isGameFinished << " " << PlayerIO::playerToString(winner) << " "
      << finalWhiteMinusBlackScore << " " << isScored << " " << isNoResult << " " << isResignation << endl;
  out << "Last moves ";
  for(int i = 0; i<moveHistory.size(); i++)
    out << Location::toString(moveHistory[i].loc,board) << " ";
  out << endl;
  assert(firstTurnIdxWithKoHistory + koHashHistory.size() == moveHistory.size() + 1);
}


const Board& BoardHistory::getRecentBoard(int numMovesAgo) const {
  assert(numMovesAgo >= 0 && numMovesAgo < NUM_RECENT_BOARDS);
  int idx = (currentRecentBoardIdx - numMovesAgo + NUM_RECENT_BOARDS) % NUM_RECENT_BOARDS;
  return recentBoards[idx];
}


//If rootKoHashTable is provided, will take advantage of rootKoHashTable rather than search within the first
//rootKoHashTable->size() moves of koHashHistory.
//ALSO counts the most recent ko hash!
bool BoardHistory::koHashOccursInHistory(Hash128 koHash, const KoHashTable* rootKoHashTable) const {
  size_t start = 0;
  size_t koHashHistorySize = koHashHistory.size();
  if(rootKoHashTable != NULL &&
     firstTurnIdxWithKoHistory == rootKoHashTable->firstTurnIdxWithKoHistory
  ) {
    size_t tableSize = rootKoHashTable->size();
    assert(firstTurnIdxWithKoHistory + koHashHistory.size() == moveHistory.size() + 1);
    assert(tableSize <= koHashHistorySize);
    if(rootKoHashTable->containsHash(koHash))
      return true;
    start = tableSize;
  }
  for(size_t i = start; i < koHashHistorySize; i++)
    if(koHashHistory[i] == koHash)
      return true;
  return false;
}

//If rootKoHashTable is provided, will take advantage of rootKoHashTable rather than search within the first
//rootKoHashTable->size() moves of koHashHistory.
//ALSO counts the most recent ko hash!
int BoardHistory::numberOfKoHashOccurrencesInHistory(Hash128 koHash, const KoHashTable* rootKoHashTable) const {
  int count = 0;
  size_t start = 0;
  size_t koHashHistorySize = koHashHistory.size();
  if(rootKoHashTable != NULL &&
     firstTurnIdxWithKoHistory == rootKoHashTable->firstTurnIdxWithKoHistory
  ) {
    size_t tableSize = rootKoHashTable->size();
    assert(firstTurnIdxWithKoHistory + koHashHistory.size() == moveHistory.size() + 1);
    assert(tableSize <= koHashHistorySize);
    count += rootKoHashTable->numberOfOccurrencesOfHash(koHash);
    start = tableSize;
  }
  for(size_t i = start; i < koHashHistorySize; i++)
    if(koHashHistory[i] == koHash)
      count++;
  return count;
}


int BoardHistory::countAreaScoreWhiteMinusBlack(const Board& board) {
  assert(rules.scoringRule == Rules::SCORING_ATTAX_STANDARD);
  int score = 0;
  for(int y = 0; y<board.y_size; y++) {
    for(int x = 0; x<board.x_size; x++) {
      Loc loc = Location::getLoc(x,y,board.x_size);
      if(board.colors[loc] == C_WHITE)
        score += 1;
      else if(board.colors[loc] == C_BLACK)
        score -= 1;
    }
  }
  return score;
}

void BoardHistory::setFinalScoreAndWinner(float score) {
  finalWhiteMinusBlackScore = score;
  if(finalWhiteMinusBlackScore > 0.0f)
    winner = C_WHITE;
  else if(finalWhiteMinusBlackScore < 0.0f)
    winner = C_BLACK;
  else
    winner = C_EMPTY;
}

void BoardHistory::getAreaNow(const Board& board) const {
    countAreaScoreWhiteMinusBlack(board);
}


void BoardHistory::endAndScoreGameNow(const Board& board, Color mWinner) {
    int boardScore;
    if (mWinner != C_EMPTY) {
        // 规则给出胜负
        boardScore = mWinner == C_WHITE ? 1.0f : (mWinner == C_BLACK ? -1.0f : 0.0f);
    } else {
        // 无子可下 数子给出胜负
        boardScore = countAreaScoreWhiteMinusBlack(board);
    }
    setFinalScoreAndWinner((float)boardScore);
    isScored = true;
    isNoResult = false;
    isResignation = false;
    isGameFinished = true;
    isPastNormalPhaseEnd = false;
}

void BoardHistory::setWinnerByResignation(Player pla) {
  isGameFinished = true;
  isPastNormalPhaseEnd = false;
  isScored = false;
  isNoResult = false;
  isResignation = true;
  winner = pla;
  finalWhiteMinusBlackScore = 0.0f;
}


bool BoardHistory::isLegal(const Board& board, Loc moveLoc, Player movePla) const {
  {
    //Only check ko bans during normal play.
    //Ko mechanics in the encore are totally different, we ignore simple ko loc.
    if(board.isKoBanned(moveLoc))
      return false;
  }
  if(!board.isLegalIgnoringKo(moveLoc,movePla))
    return false;
  if(superKoBanned[moveLoc])
    return false;

  return true;
}



bool BoardHistory::isFinalPhase() const {
  return
    rules.scoringRule == Rules::SCOREING_NUM;
}

bool BoardHistory::isLegalTolerant(const Board& board, Loc moveLoc, Player movePla) const {
  bool multiStoneSuicideLegal = true; //Tolerate suicide regardless of rules
  if(encorePhase <= 0 && board.isKoBanned(moveLoc))
    return false;
  if(!isPassForKo(board, moveLoc, movePla) && !board.isLegalIgnoringKo(moveLoc,movePla,multiStoneSuicideLegal))
    return false;
  return true;
}
bool BoardHistory::makeBoardMoveTolerant(Board& board, Loc moveLoc, Player movePla) {
  bool multiStoneSuicideLegal = true; //Tolerate suicide regardless of rules
  if(encorePhase <= 0 && board.isKoBanned(moveLoc))
    return false;
  if(!isPassForKo(board, moveLoc, movePla) && !board.isLegalIgnoringKo(moveLoc,movePla,multiStoneSuicideLegal))
    return false;
  makeBoardMoveAssumeLegal(board,moveLoc,movePla,NULL);
  return true;
}
bool BoardHistory::makeBoardMoveTolerant(Board& board, Loc moveLoc, Player movePla, bool preventEncore) {
  bool multiStoneSuicideLegal = true; //Tolerate suicide regardless of rules
  if(encorePhase <= 0 && board.isKoBanned(moveLoc))
    return false;
  if(!isPassForKo(board, moveLoc, movePla) && !board.isLegalIgnoringKo(moveLoc,movePla,multiStoneSuicideLegal))
    return false;
  makeBoardMoveAssumeLegal(board,moveLoc,movePla,NULL,preventEncore);
  return true;
}

void BoardHistory::makeBoardMoveAssumeLegal(Board& board, Loc moveLoc, Player movePla, const KoHashTable* rootKoHashTable) {
  makeBoardMoveAssumeLegal(board,moveLoc,movePla,rootKoHashTable,false);
}

void BoardHistory::makeBoardMoveAssumeLegal(Board& board, Loc moveLoc, Player movePla, const KoHashTable* rootKoHashTable, bool preventEncore) {
  Hash128 posHashBeforeMove = board.pos_hash;

  //If somehow we're making a move after the game was ended, just clear those values and continue
  isGameFinished = false;
  isPastNormalPhaseEnd = false;
  winner = C_EMPTY;
  finalWhiteMinusBlackScore = 0.0f;
  isScored = false;
  isNoResult = false;
  isResignation = false;

  //Update consecutiveEndingPasses and button
  bool isSpightlikeEndingPass = false;

  // handle regular moves
  {
    board.playMoveAssumeLegal(moveLoc,movePla);
  }

  //Update recent boards
  currentRecentBoardIdx = (currentRecentBoardIdx + 1) % NUM_RECENT_BOARDS;
  recentBoards[currentRecentBoardIdx] = board;

  Hash128 koHashAfterThisMove = getKoHash(rules,board,getOpp(movePla),encorePhase,koRecapBlockHash);
  koHashHistory.push_back(koHashAfterThisMove);
  moveHistory.push_back(Move(moveLoc,movePla));
  numTurnsThisPhase += 1;
  presumedNextMovePla = getOpp(movePla);

  if(moveLoc != Board::PASS_LOC)
    wasEverOccupiedOrPlayed[moveLoc] = true;

  //Mark all locations that are superko-illegal for the next player, by iterating and testing each point.
  Player nextPla = getOpp(movePla);
  if(encorePhase <= 0 && rules.koRule != Rules::KO_SIMPLE) {
    assert(koRecapBlockHash == Hash128());
    for(int y = 0; y<board.y_size; y++) {
      for(int x = 0; x<board.x_size; x++) {
        Loc loc = Location::getLoc(x,y,board.x_size);
        //Cannot be superko banned if it's not a pseudolegal move in the first place, or we would already ban the move under simple ko.
        if(board.colors[loc] != C_EMPTY || board.isIllegalSuicide(loc,nextPla,rules.multiStoneSuicideLegal) || loc == board.ko_loc)
          superKoBanned[loc] = false;
        //Also cannot be superko banned if a stone was never there or played there before AND the move is not suicide, because that means
        //the move results in a new stone there and if no stone was ever there in the past the it must be a new position.
        else if(!wasEverOccupiedOrPlayed[loc] && !board.isSuicide(loc,nextPla))
          superKoBanned[loc] = false;
        else {
          Hash128 posHashAfterMove = board.getPosHashAfterMove(loc,nextPla);
          Hash128 koHashAfterMove = getKoHashAfterMoveNonEncore(rules, posHashAfterMove, getOpp(nextPla));
          superKoBanned[loc] = koHashOccursInHistory(koHashAfterMove,rootKoHashTable);
        }
      }
    }
  }
  else if(encorePhase > 0) {
    //During the encore, only one capture of each ko in a given position by a given player
    std::fill(superKoBanned, superKoBanned+Board::MAX_ARR_SIZE, false);
    for(size_t i = 0; i<koCapturesInEncore.size(); i++) {
      const EncoreKoCapture& ekc = koCapturesInEncore[i];
      if(ekc.posHashBeforeMove == board.pos_hash && ekc.movePla == nextPla)
        superKoBanned[ekc.moveLoc] = true;
    }
  }

  //Handicap bonus score
  if(movePla == P_WHITE && moveLoc != Board::PASS_LOC)
    whiteHasMoved = true;
  if(assumeMultipleStartingBlackMovesAreHandicap && !whiteHasMoved && movePla == P_BLACK && rules.whiteHandicapBonusRule != Rules::WHB_ZERO) {
    whiteHandicapBonusScore = computeWhiteHandicapBonus();
  }

  // check game end
  Color mWinner = thisMoveEndGame(board, moveLoc, movePla);
  if(mWinner != C_WALL) {
      endAndScoreGameNow(board, mWinner);
  }

  //Break long cycles with no-result
  if(moveLoc != Board::PASS_LOC && (encorePhase > 0 || rules.koRule == Rules::KO_SIMPLE)) {
    if(numberOfKoHashOccurrencesInHistory(koHashHistory[koHashHistory.size()-1], rootKoHashTable) >= 3) {
      isNoResult = true;
      isGameFinished = true;
    }
  }

}


bool BoardHistory::hasBlackPassOrWhiteFirst() const {
  //First move was made by white this game, on an empty board.
  if(initialBoard.isEmpty() && moveHistory.size() > 0 && moveHistory[0].pla == P_WHITE)
    return true;
  //Black passed exactly once or white doublemoved
  int numBlackPasses = 0;
  int numWhitePasses = 0;
  int numBlackDoubleMoves = 0;
  int numWhiteDoubleMoves = 0;
  for(int i = 0; i<moveHistory.size(); i++) {
    if(moveHistory[i].loc == Board::PASS_LOC && moveHistory[i].pla == P_BLACK)
      numBlackPasses++;
    if(moveHistory[i].loc == Board::PASS_LOC && moveHistory[i].pla == P_WHITE)
      numWhitePasses++;
    if(i > 0 && moveHistory[i].pla == P_BLACK && moveHistory[i-1].pla == P_BLACK)
      numBlackDoubleMoves++;
    if(i > 0 && moveHistory[i].pla == P_WHITE && moveHistory[i-1].pla == P_WHITE)
      numWhiteDoubleMoves++;
  }
  if(numBlackPasses == 1 && numWhitePasses == 0 && numBlackDoubleMoves == 0 && numWhiteDoubleMoves == 0)
    return true;
  if(numBlackPasses == 0 && numWhitePasses == 0 && numBlackDoubleMoves == 0 && numWhiteDoubleMoves == 1)
    return true;

  return false;
}

Hash128 BoardHistory::getSituationRulesAndKoHash(const Board& board, const BoardHistory& hist, Player nextPlayer, double drawEquivalentWinsForWhite) {
  int xSize = board.x_size;
  int ySize = board.y_size;

  //Note that board.pos_hash also incorporates the size of the board.
  Hash128 hash = board.pos_hash;
  hash ^= Board::ZOBRIST_PLAYER_HASH[nextPlayer];

  assert(hist.encorePhase >= 0 && hist.encorePhase <= 2);
  hash ^= Board::ZOBRIST_ENCORE_HASH[hist.encorePhase];

  if(hist.encorePhase == 0) {
    if(board.ko_loc != Board::NULL_LOC)
      hash ^= Board::ZOBRIST_KO_LOC_HASH[board.ko_loc];
    for(int y = 0; y<ySize; y++) {
      for(int x = 0; x<xSize; x++) {
        Loc loc = Location::getLoc(x,y,xSize);
        if(hist.superKoBanned[loc] && loc != board.ko_loc)
          hash ^= Board::ZOBRIST_KO_LOC_HASH[loc];
      }
    }
  }
  else {
    for(int y = 0; y<ySize; y++) {
      for(int x = 0; x<xSize; x++) {
        Loc loc = Location::getLoc(x,y,xSize);
        if(hist.superKoBanned[loc])
          hash ^= Board::ZOBRIST_KO_LOC_HASH[loc];
        if(hist.koRecapBlocked[loc])
          hash ^= Board::ZOBRIST_KO_MARK_HASH[loc][P_BLACK] ^ Board::ZOBRIST_KO_MARK_HASH[loc][P_WHITE];
      }
    }
    if(hist.encorePhase == 2) {
      for(int y = 0; y<ySize; y++) {
        for(int x = 0; x<xSize; x++) {
          Loc loc = Location::getLoc(x,y,xSize);
          Color c = hist.secondEncoreStartColors[loc];
          if(c != C_EMPTY)
            hash ^= Board::ZOBRIST_SECOND_ENCORE_START_HASH[loc][c];
        }
      }
    }
  }

  float selfKomi = hist.currentSelfKomi(nextPlayer,drawEquivalentWinsForWhite);

  //Discretize the komi for the purpose of matching hash
  int64_t komiDiscretized = (int64_t)(selfKomi*256.0f);
  uint64_t komiHash = Hash::murmurMix((uint64_t)komiDiscretized);
  hash.hash0 ^= komiHash;
  hash.hash1 ^= Hash::basicLCong(komiHash);

  //Fold in the ko, scoring, and suicide rules
  hash ^= Rules::ZOBRIST_KO_RULE_HASH[hist.rules.koRule];
  hash ^= Rules::ZOBRIST_SCORING_RULE_HASH[hist.rules.scoringRule];
  hash ^= Rules::ZOBRIST_TAX_RULE_HASH[hist.rules.taxRule];
  if(hist.rules.multiStoneSuicideLegal)
    hash ^= Rules::ZOBRIST_MULTI_STONE_SUICIDE_HASH;
  if(hist.hasButton)
    hash ^= Rules::ZOBRIST_BUTTON_HASH;

  return hash;
}

Color BoardHistory::thisMoveEndGame(Board &board, Loc moveLoc, Player movePla) {
    return C_WALL;
}

float BoardHistory::winScoreWhiteMinusBlack(const Board &board) {
    assert(rules.scoringRule == Rules::SCORING_GOMOKU_STANDARD);
    Color final_winner = thisMoveEndGame(board)
    float_t boardScore;
    boardScore = final_winner == C_WHITE ? 1.0f : (final_winner == C_BLACK ? -1.0f : 0.0f);
}


KoHashTable::KoHashTable()
  :koHashHistorySortedByLowBits(),
   firstTurnIdxWithKoHistory(0)
{
  idxTable = new uint32_t[TABLE_SIZE];
  std::fill(idxTable,idxTable+TABLE_SIZE,(uint32_t)(0));
}
KoHashTable::~KoHashTable() {
  delete[] idxTable;
}

size_t KoHashTable::size() const {
  return koHashHistorySortedByLowBits.size();
}

void KoHashTable::recompute(const BoardHistory& history) {
  koHashHistorySortedByLowBits = history.koHashHistory;
  firstTurnIdxWithKoHistory = history.firstTurnIdxWithKoHistory;

  auto cmpFirstByLowBits = [](const Hash128& a, const Hash128& b) {
    if((a.hash0 & TABLE_MASK) < (b.hash0 & TABLE_MASK))
      return true;
    if((a.hash0 & TABLE_MASK) > (b.hash0 & TABLE_MASK))
      return false;
    return a < b;
  };

  std::sort(koHashHistorySortedByLowBits.begin(),koHashHistorySortedByLowBits.end(),cmpFirstByLowBits);

  //Just in case, since we're using 32 bits for indices.
  if(koHashHistorySortedByLowBits.size() > 1000000000)
    throw StringError("Board history length longer than 1000000000, not supported");
  uint32_t size = (uint32_t)koHashHistorySortedByLowBits.size();

  uint32_t idx = 0;
  for(uint32_t bits = 0; bits<TABLE_SIZE; bits++) {
    while(idx < size && ((koHashHistorySortedByLowBits[idx].hash0 & TABLE_MASK) < bits))
      idx++;
    idxTable[bits] = idx;
  }
}

bool KoHashTable::containsHash(Hash128 hash) const {
  uint32_t bits = hash.hash0 & TABLE_MASK;
  size_t idx = idxTable[bits];
  size_t size = koHashHistorySortedByLowBits.size();
  while(idx < size && ((koHashHistorySortedByLowBits[idx].hash0 & TABLE_MASK) == bits)) {
    if(hash == koHashHistorySortedByLowBits[idx])
      return true;
    idx++;
  }
  return false;
}

int KoHashTable::numberOfOccurrencesOfHash(Hash128 hash) const {
  uint32_t bits = hash.hash0 & TABLE_MASK;
  size_t idx = idxTable[bits];
  size_t size = koHashHistorySortedByLowBits.size();
  int count = 0;
  while(idx < size && ((koHashHistorySortedByLowBits[idx].hash0 & TABLE_MASK) == bits)) {
    if(hash == koHashHistorySortedByLowBits[idx])
      count++;
    idx++;
  }
  return count;
}
