#ifndef GAME_BOARDHISTORY_H_
#define GAME_BOARDHISTORY_H_

#include "../core/global.h"
#include "../core/hash.h"
#include "../game/board.h"
#include "../game/rules.h"

struct KoHashTable;

//A data structure enabling checking of move legality, including optionally superko,
//and implements scoring and support for various rulesets (see rules.h)
struct BoardHistory {
  Rules rules;

  //Chronological history of moves
  std::vector<Move> moveHistory;
  //Chronological history of hashes, including the latest board's hash.
  //Theses are the hashes that determine whether a board is the "same" or not given the rules
  //(e.g. they include the player if situational superko, and not if positional)
  //Cleared on a pass if passes clear ko bans
  std::vector<Hash128> koHashHistory;
  //The index of the first turn for which we have a koHashHistory (since depending on rules, passes may clear it).
  //Index 0 = starting state, index 1 = state after move 0, index 2 = state after move 1, etc...
  int firstTurnIdxWithKoHistory;

  //The board and player to move as of the very start, before moveHistory.
  Board initialBoard;
  Player initialPla;
  //The "turn number" as of the initial board. Does not affect any rules, but possibly uses may
  //care about this number, for cases where we set up a position from midgame.
  int initialTurnNumber;
  //How we count handicap at the start of the game. Set manually by some close-to-user-level apps or subcommands
  bool assumeMultipleStartingBlackMovesAreHandicap;
  bool whiteHasMoved;

  static const int NUM_RECENT_BOARDS = 6;
  Board recentBoards[NUM_RECENT_BOARDS];
  int currentRecentBoardIdx;
  Player presumedNextMovePla;

  //Did this board location ever have a stone there before, or was it ever played?
  //(Also includes locations of suicides)
  bool wasEverOccupiedOrPlayed[Board::MAX_ARR_SIZE];
  //Locations where the next player is not allowed to play due to superko
  bool superKoBanned[Board::MAX_ARR_SIZE];


  //Used to implement once-only rules for ko captures in encore
  STRUCT_NAMED_TRIPLE(Hash128,posHashBeforeMove,Loc,moveLoc,Player,movePla,EncoreKoCapture);
  std::vector<EncoreKoCapture> koCapturesInEncore;

  static float whiteAdjustmentForDraws(double drawEquivalentWinsForWhite) ;


  //Is the game been prolonged to stay in a given phase without proceeding to the next?
  bool isPastNormalPhaseEnd;

  //Is the game supposed to be ended now?
  bool isGameFinished;
  //Winner of the game if the game is supposed to have ended now, C_EMPTY if it is a draw or isNoResult.
  Player winner;
  //Score difference of the game if the game is supposed to have ended now, does NOT take into account whiteKomiAdjustmentForDrawUtility
  //Always an integer or half-integer.
  float finalWhiteMinusBlackScore;
  //True if this game is supposed to be ended with a score
  bool isScored;
  //True if this game is supposed to be ended but there is no result
  bool isNoResult;
  //True if this game is supposed to be ended but it was by resignation rather than an actual end position
  bool isResignation;

  BoardHistory();
  ~BoardHistory();

  BoardHistory(const Board& board, Player pla, const Rules& rules);

  BoardHistory(const BoardHistory& other);
  BoardHistory& operator=(const BoardHistory& other);

  BoardHistory(BoardHistory&& other) noexcept;
  BoardHistory& operator=(BoardHistory&& other) noexcept;

  //Clears all history and status and bonus points, sets encore phase and rules
  void clear(const Board& board, Player pla, const Rules& rules);

  //Set the initial turn number. Affects nothing else.
  void setInitialTurnNumber(int n);


  //Returns a reference a recent board state, where 0 is the current board, 1 is 1 move ago, etc.
  //Requires that numMovesAgo < NUM_RECENT_BOARDS
  const Board& getRecentBoard(int numMovesAgo) const;

  //Check if a move on the board is legal, taking into account the full game state and superko
  bool isLegal(const Board& board, Loc moveLoc, Player movePla) const;

  //Check if this is the final phase of the game, such that ending it moves to scoring.
  bool isFinalPhase() const;

  //For all of the below, rootKoHashTable is optional and if provided will slightly speedup superko searches
  //This function should behave gracefully so long as it is pseudolegal (board.isLegal, but also still ok if the move is on board.ko_loc)
  //even if the move violates superko or encore ko recapture prohibitions, or is past when the game is ended.
  //This allows for robustness when this code is being used for analysis or with external data sources.
  //preventEncore artifically prevents any move from entering or advancing the encore phase when using territory scoring.
  void makeBoardMoveAssumeLegal(Board& board, Loc moveLoc, Player movePla, const KoHashTable* rootKoHashTable);
  void makeBoardMoveAssumeLegal(Board& board, Loc moveLoc, Player movePla, const KoHashTable* rootKoHashTable, bool preventEncore);

  // 包括的情况：无子可下，等待数子=C_EMPTY，规则胜利=C_BLACK/C_WHITE，没有结束=C_WALL
  static Color thisMoveEndGame(Board& board, Loc moveLoc, Player movePla);

  //Score the board as-is. If the game is already finished, and is NOT a no-result, then this should be idempotent.
  void endAndScoreGameNow(const Board& board, Color mWinner);
  void getAreaNow(const Board& board) const;

  void setWinnerByResignation(Player pla);

  void printBasicInfo(std::ostream& out, const Board& board) const;
  void printDebugInfo(std::ostream& out, const Board& board) const;
  int numberOfKoHashOccurrencesInHistory(Hash128 koHash, const KoHashTable* rootKoHashTable) const;


  //Heuristically check if this history looks like an sgf variation where black passed to effectively
  //turn into white, or similar.
  bool hasBlackPassOrWhiteFirst() const;

  //Compute a hash that takes into account the full situation, the rules, discretized komi, and any immediate ko prohibitions.
  static Hash128 getSituationRulesAndKoHash(const Board& board, const BoardHistory& hist, Player nextPlayer, double drawEquivalentWinsForWhite);

private:
  bool koHashOccursInHistory(Hash128 koHash, const KoHashTable* rootKoHashTable) const;
  static int countAreaScoreWhiteMinusBlack(const Board& board) ;
  static float winScoreWhiteMinusBlack(const Board& board) ;
  void setFinalScoreAndWinner(float score);
};

struct KoHashTable {
  uint32_t* idxTable;
  std::vector<Hash128> koHashHistorySortedByLowBits;
  int firstTurnIdxWithKoHistory;

  static const int TABLE_SIZE = 1 << 10;
  static const uint64_t TABLE_MASK = TABLE_SIZE-1;

  KoHashTable();
  ~KoHashTable();

  KoHashTable(const KoHashTable& other) = delete;
  KoHashTable& operator=(const KoHashTable& other) = delete;

  size_t size() const;

  void recompute(const BoardHistory& history);
  bool containsHash(Hash128 hash) const;
  int numberOfOccurrencesOfHash(Hash128 hash) const;

};


#endif  // GAME_BOARDHISTORY_H_
