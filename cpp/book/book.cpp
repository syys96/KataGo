
#include "../book/book.h"

#include <fstream>
#include <thread>
#include "../core/makedir.h"
#include "../core/fileutils.h"
#include "../neuralnet/nninputs.h"
#include "../external/nlohmann_json/json.hpp"

using std::vector;
using std::map;
using std::cout;
using std::endl;
using std::string;
using nlohmann::json;

BookHash::BookHash()
:historyHash(),stateHash()
{}

BookHash::BookHash(Hash128 hhash, Hash128 shash)
:historyHash(hhash),stateHash(shash)
{}

bool BookHash::operator==(const BookHash other) const
{return historyHash == other.historyHash && stateHash == other.stateHash;}

bool BookHash::operator!=(const BookHash other) const
{return historyHash != other.historyHash || stateHash != other.stateHash;}

bool BookHash::operator>(const BookHash other) const
{
  if(stateHash > other.stateHash) return true;
  if(stateHash < other.stateHash) return false;
  return historyHash > other.historyHash;
}
bool BookHash::operator>=(const BookHash other) const
{
  if(stateHash > other.stateHash) return true;
  if(stateHash < other.stateHash) return false;
  return historyHash >= other.historyHash;
}
bool BookHash::operator<(const BookHash other) const
{
  if(stateHash < other.stateHash) return true;
  if(stateHash > other.stateHash) return false;
  return historyHash < other.historyHash;
}
bool BookHash::operator<=(const BookHash other) const
{
  if(stateHash < other.stateHash) return true;
  if(stateHash > other.stateHash) return false;
  return historyHash <= other.historyHash;
}

BookHash BookHash::operator^(const BookHash other) const {
  return BookHash(historyHash ^ other.historyHash, stateHash ^ other.stateHash);
}
BookHash BookHash::operator|(const BookHash other) const {
  return BookHash(historyHash | other.historyHash, stateHash | other.stateHash);
}
BookHash BookHash::operator&(const BookHash other) const {
  return BookHash(historyHash & other.historyHash, stateHash & other.stateHash);
}
BookHash& BookHash::operator^=(const BookHash other) {
  historyHash ^= other.historyHash;
  stateHash ^= other.stateHash;
  return *this;
}
BookHash& BookHash::operator|=(const BookHash other) {
  historyHash |= other.historyHash;
  stateHash |= other.stateHash;
  return *this;
}
BookHash& BookHash::operator&=(const BookHash other) {
  historyHash &= other.historyHash;
  stateHash &= other.stateHash;
  return *this;
}

std::ostream& operator<<(std::ostream& out, const BookHash other)
{
  out << other.stateHash << other.historyHash;
  return out;
}

std::string BookHash::toString() const {
  return stateHash.toString() + historyHash.toString();
}

BookHash BookHash::ofString(const string& s) {
  if(s.size() != 64)
    throw IOError("Could not parse as BookHash: " + s);
  Hash128 stateHash = Hash128::ofString(s.substr(0,32));
  Hash128 historyHash = Hash128::ofString(s.substr(32,32));
  return BookHash(historyHash,stateHash);
}

// Just to fill out the extra 128 bits we have with another independent zobrist
static Hash128 getExtraPosHash(const Board& board) {
  Hash128 hash;
  for(int y = 0; y<board.y_size; y++) {
    for(int x = 0; x<board.x_size; x++) {
      Loc loc = Location::getLoc(x,y,board.x_size);
      hash ^= Board::ZOBRIST_BOARD_HASH2[loc][board.colors[loc]];
    }
  }
  return hash;
}

static Hash128 getStateHash(const BoardHistory& hist) {
  const Board& board = hist.getRecentBoard(0);
  Player nextPlayer = hist.presumedNextMovePla;
  double drawEquivalentWinsForWhite = 0.5;
  Hash128 hash = BoardHistory::getSituationRulesAndKoHash(board, hist, nextPlayer, drawEquivalentWinsForWhite);

  // Fold in whether the game is over or not
  if(hist.isGameFinished)
    hash ^= Board::ZOBRIST_GAME_IS_OVER;

  return hash;
}

void BookHash::getHashAndSymmetry(const BoardHistory& hist, int repBound, BookHash& hashRet, int& symmetryToAlignRet, vector<int>& symmetriesRet) {
  Board boardsBySym[SymmetryHelpers::NUM_SYMMETRIES];
  BoardHistory histsBySym[SymmetryHelpers::NUM_SYMMETRIES];
  Hash128 accums[SymmetryHelpers::NUM_SYMMETRIES];

  // Make sure the book all matches orientation for rectangular boards.
  // Don't consider symmetries that change the lengths of x and y.
  // This also lets us be fairly lazy in the rest of the book implementation and not have to carefully consider every case whether
  // we need to swap xSize and ySize when passing into getSymLoc.
  int numSymmetries = (hist.getRecentBoard(0).x_size != hist.getRecentBoard(0).y_size) ?
    SymmetryHelpers::NUM_SYMMETRIES_WITHOUT_TRANSPOSE : SymmetryHelpers::NUM_SYMMETRIES;

  for(int symmetry = 0; symmetry < numSymmetries; symmetry++) {
    boardsBySym[symmetry] = SymmetryHelpers::getSymBoard(hist.initialBoard,symmetry);
    histsBySym[symmetry] = BoardHistory(boardsBySym[symmetry], hist.initialPla, hist.rules, hist.initialEncorePhase);
    accums[symmetry] = Hash128();
  }

  for(size_t i = 0; i<hist.moveHistory.size(); i++) {
    for(int symmetry = 0; symmetry < numSymmetries; symmetry++) {
      Loc moveLoc = SymmetryHelpers::getSymLoc(hist.moveHistory[i].loc, boardsBySym[symmetry], symmetry);
      Player movePla = hist.moveHistory[i].pla;
      // Add the next
      Hash128 nextHash = boardsBySym[symmetry].pos_hash ^ Board::ZOBRIST_PLAYER_HASH[movePla];
      accums[symmetry].hash0 += nextHash.hash0;
      accums[symmetry].hash1 += nextHash.hash1;
      // Mix it up
      accums[symmetry].hash0 = Hash::splitMix64(accums[symmetry].hash0);
      accums[symmetry].hash1 = Hash::nasam(accums[symmetry].hash1);

      // Assume legal since we're only replaying moves from another history.
      histsBySym[symmetry].makeBoardMoveAssumeLegal(boardsBySym[symmetry], moveLoc, movePla, nullptr);
      if(boardsBySym[symmetry].simpleRepetitionBoundGt(moveLoc, repBound)) {
        accums[symmetry] = Hash128();
      }
    }
  }

  BookHash hashes[SymmetryHelpers::NUM_SYMMETRIES];
  for(int symmetry = 0; symmetry < numSymmetries; symmetry++)
    hashes[symmetry] = BookHash(accums[symmetry] ^ getExtraPosHash(boardsBySym[symmetry]), getStateHash(histsBySym[symmetry]));

  // Use the smallest symmetry that gives us the same hash
  int smallestSymmetry = 0;
  BookHash smallestHash;
  for(int symmetry = 0; symmetry < numSymmetries; symmetry++) {
    if(symmetry == 0 || hashes[symmetry] < smallestHash) {
      smallestSymmetry = symmetry;
      smallestHash = hashes[symmetry];
    }
  }

  hashRet = smallestHash;
  symmetryToAlignRet = smallestSymmetry;

  // Find all symmetries that preserve the smallestHash.
  symmetriesRet.clear();
  for(int symmetry = 0; symmetry < numSymmetries; symmetry++) {
    if(hashes[SymmetryHelpers::compose(smallestSymmetry,symmetry)] == smallestHash)
      symmetriesRet.push_back(symmetry);
  }
}

double BookValues::getAdjustedScoreError(const Rules& rules) const {
  if(rules.gameResultWillBeInteger()) {
    double scoreVariance = scoreStdev * scoreStdev;
    // KataGo's formalization of the score variance in draw-allowed games will be systematically too high by 0.25
    // due to blurring of the score on the half-integer gridpoints.
    // Assumes drawEquivalentWinsForWhite = 0.5.
    double adjustedScoreVariance = scoreVariance - 0.25;
    // Make sure we don't go smaller than some tiny values
    if(adjustedScoreVariance < scoreVariance * 0.05)
      adjustedScoreVariance = scoreVariance * 0.05;
    return std::min(sqrt(adjustedScoreVariance),scoreError);
  }
  else {
    return std::min(scoreStdev,scoreError);
  }
}


// -----------------------------------------------------------------------------------------------------------

BookMove::BookMove()
  :move(Board::NULL_LOC),
   symmetryToAlign(0),
   hash(),
   rawPolicy(0.0),
   costFromRoot(0.0),
   isWLPV(false),
   biggestWLCostFromRoot(0.0)
{}

BookMove::BookMove(Loc mv, int s, BookHash h, double rp)
  :move(mv),
   symmetryToAlign(s),
   hash(h),
   rawPolicy(rp),
   costFromRoot(0.0),
   isWLPV(false),
   biggestWLCostFromRoot(0.0)
{}

BookMove BookMove::getSymBookMove(int symmetry, int xSize, int ySize) {
  BookMove ret(
    SymmetryHelpers::getSymLoc(move,xSize,ySize,symmetry),
    // This needs to be the symmetry that transform's retspace -> childspace
    // symmetry is the transform from orig -> ret
    // symmetryToAlign is the transform from orig -> child.
    // Therefore invert(symmetry) + symmetryToAlign is correct.
    SymmetryHelpers::compose(SymmetryHelpers::invert(symmetry),symmetryToAlign),
    hash,
    rawPolicy
  );
  ret.costFromRoot = costFromRoot;
  return ret;
}

// -----------------------------------------------------------------------------------------------------------


BookNode::BookNode(BookHash h, Book* b, Player p, const vector<int>& syms)
  :hash(h),
   book(b),
   pla(p),
   symmetries(syms),
   thisValuesNotInBook(),
   canExpand(true),
   moves(),
   parents(),
   recursiveValues(),
   minCostFromRoot(0),
   thisNodeExpansionCost(0),
   minCostFromRootWLPV(0),
   expansionIsWLPV(false),
   biggestWLCostFromRoot(0)
{}

BookNode::~BookNode() {
}

// -----------------------------------------------------------------------------------------------------------

SymBookNode::SymBookNode()
  :node(nullptr),
   symmetryOfNode(0),
   invSymmetryOfNode(0)
{}
ConstSymBookNode::ConstSymBookNode()
  :node(nullptr),
   symmetryOfNode(0),
   invSymmetryOfNode(0)
{}
SymBookNode::SymBookNode(std::nullptr_t)
  :node(nullptr),
   symmetryOfNode(0),
   invSymmetryOfNode(0)
{}
ConstSymBookNode::ConstSymBookNode(std::nullptr_t)
  :node(nullptr),
   symmetryOfNode(0),
   invSymmetryOfNode(0)
{}
SymBookNode::SymBookNode(BookNode* n, int s)
  :node(n),
   symmetryOfNode(s),
   invSymmetryOfNode(SymmetryHelpers::invert(s))
{}
ConstSymBookNode::ConstSymBookNode(const BookNode* n, int s)
  :node(n),
   symmetryOfNode(s),
   invSymmetryOfNode(SymmetryHelpers::invert(s))
{}
ConstSymBookNode::ConstSymBookNode(const SymBookNode& other)
  :node(other.node),
   symmetryOfNode(other.symmetryOfNode),
   invSymmetryOfNode(other.invSymmetryOfNode)
{}
ConstSymBookNode& ConstSymBookNode::operator=(const SymBookNode& other) {
  node = other.node;
  symmetryOfNode = other.symmetryOfNode;
  invSymmetryOfNode = other.invSymmetryOfNode;
  return *this;
}

bool SymBookNode::isNull() {
  return node == nullptr;
}
bool ConstSymBookNode::isNull() {
  return node == nullptr;
}

SymBookNode SymBookNode::applySymmetry(int symmetry) {
  // symmetry is the map from thisspace -> retspace
  // symmetryOfNode is the map from nodespace -> thisspace
  // The constructor will want the map from nodespace -> retspace
  return SymBookNode(node,SymmetryHelpers::compose(symmetryOfNode,symmetry));
}
ConstSymBookNode ConstSymBookNode::applySymmetry(int symmetry) {
  return ConstSymBookNode(node,SymmetryHelpers::compose(symmetryOfNode,symmetry));
}

Player SymBookNode::pla() {
  return node->pla;
}
Player ConstSymBookNode::pla() {
  return node->pla;
}

BookHash SymBookNode::hash() {
  return node->hash;
}
BookHash ConstSymBookNode::hash() {
  return node->hash;
}

vector<int> SymBookNode::getSymmetries() {
  vector<int> symmetries;
  for(int symmetry: node->symmetries)
    symmetries.push_back(SymmetryHelpers::compose(invSymmetryOfNode, symmetry, symmetryOfNode));
  return symmetries;
}
vector<int> ConstSymBookNode::getSymmetries() {
  vector<int> symmetries;
  for(int symmetry: node->symmetries)
    symmetries.push_back(SymmetryHelpers::compose(invSymmetryOfNode, symmetry, symmetryOfNode));
  return symmetries;
}


bool SymBookNode::isMoveInBook(Loc move) {
  return ConstSymBookNode(*this).isMoveInBook(move);
}
bool ConstSymBookNode::isMoveInBook(Loc move) {
  assert(node != nullptr);
  for(int symmetry: node->symmetries) {
    // invSymmetryOfNode is the map (symbooknodespace -> nodespace)
    // symmetry is the map nodespace -> nodespace that we should look for alternative versions of a move.
    // There is only one way to compose them, which is to left-compose invSymmetryOfNode, so we left-compose it (and not right-compose it).
    symmetry = SymmetryHelpers::compose(invSymmetryOfNode, symmetry);
    if(contains(node->moves, SymmetryHelpers::getSymLoc(move, node->book->initialBoard, symmetry)))
      return true;
  }
  return false;
}

vector<BookMove> SymBookNode::getUniqueMovesInBook() {
  return ConstSymBookNode(*this).getUniqueMovesInBook();
}
vector<BookMove> ConstSymBookNode::getUniqueMovesInBook() {
  assert(node != nullptr);
  vector<BookMove> ret;
  for(std::pair<Loc,BookMove> kv: node->moves) {
    ret.push_back(kv.second.getSymBookMove(symmetryOfNode, node->book->initialBoard.x_size, node->book->initialBoard.y_size));
  }
  return ret;
}

BookValues& SymBookNode::thisValuesNotInBook() {
  assert(node != nullptr);
  return node->thisValuesNotInBook;
}
const BookValues& ConstSymBookNode::thisValuesNotInBook() {
  assert(node != nullptr);
  return node->thisValuesNotInBook;
}

bool& SymBookNode::canExpand() {
  assert(node != nullptr);
  return node->canExpand;
}
bool ConstSymBookNode::canExpand() {
  assert(node != nullptr);
  return node->canExpand;
}


const RecursiveBookValues& SymBookNode::recursiveValues() {
  assert(node != nullptr);
  return node->recursiveValues;
}
const RecursiveBookValues& ConstSymBookNode::recursiveValues() {
  assert(node != nullptr);
  return node->recursiveValues;
}

double SymBookNode::minCostFromRoot() {
  assert(node != nullptr);
  return node->minCostFromRoot;
}
double ConstSymBookNode::minCostFromRoot() {
  assert(node != nullptr);
  return node->minCostFromRoot;
}
double SymBookNode::totalExpansionCost() {
  assert(node != nullptr);
  return node->minCostFromRoot + node->thisNodeExpansionCost;
}
double ConstSymBookNode::totalExpansionCost() {
  assert(node != nullptr);
  return node->minCostFromRoot + node->thisNodeExpansionCost;
}

SymBookNode SymBookNode::canonicalParent() {
  if(node->parents.size() <= 0)
    return SymBookNode(nullptr);
  BookNode* parent = node->book->get(node->parents[0].first);
  if(parent == nullptr)
    return SymBookNode(nullptr);
  auto iter = parent->moves.find(node->parents[0].second);
  if(iter == parent->moves.end())
    return SymBookNode(nullptr);
  const BookMove& moveFromParent = iter->second;
  // moveFromParent.symmetryToAlign is the map parentspace -> nodespace
  // symmetryOfNode is the map nodespace -> displayspace
  // For the constructor, we need the map parentspace -> displayspace
  return SymBookNode(parent,SymmetryHelpers::compose(moveFromParent.symmetryToAlign,symmetryOfNode));
}
ConstSymBookNode ConstSymBookNode::canonicalParent() {
  if(node->parents.size() <= 0)
    return ConstSymBookNode(nullptr);
  const BookNode* parent = node->book->get(node->parents[0].first);
  if(parent == nullptr)
    return ConstSymBookNode(nullptr);
  auto iter = parent->moves.find(node->parents[0].second);
  if(iter == parent->moves.end())
    return ConstSymBookNode(nullptr);
  const BookMove& moveFromParent = iter->second;
  // moveFromParent.symmetryToAlign is the map parentspace -> nodespace
  // symmetryOfNode is the map nodespace -> displayspace
  // For the constructor, we need the map parentspace -> displayspace
  return ConstSymBookNode(parent,SymmetryHelpers::compose(moveFromParent.symmetryToAlign,symmetryOfNode));
}

SymBookNode SymBookNode::follow(Loc move) {
  assert(node != nullptr);
  for(int symmetry: node->symmetries) {
    // Same logic here, invSymmetryOfNode maps symbooknodespace -> nodespace
    symmetry = SymmetryHelpers::compose(invSymmetryOfNode, symmetry);
    auto iter = node->moves.find(SymmetryHelpers::getSymLoc(move, node->book->initialBoard, symmetry));
    if(iter != node->moves.end()) {
      const BookMove& bookMove = iter->second;
      BookNode* child = node->book->get(bookMove.hash);
      // Symmetry maps symbooknodespace -> nodespace orientation in which the move is found.
      // symmetryToAlign maps nodespace -> childspace
      // The constructor for this child SymBookNode expects the map childspace -> symbooknodespace
      // So we need to compose and invert.
      return SymBookNode(child,SymmetryHelpers::invert(SymmetryHelpers::compose(symmetry,bookMove.symmetryToAlign)));
    }
  }
  return SymBookNode(nullptr);
}
ConstSymBookNode ConstSymBookNode::follow(Loc move) {
  assert(node != nullptr);
  for(int symmetry: node->symmetries) {
    // Same logic here, invSymmetryOfNode maps symbooknodespace -> nodespace
    symmetry = SymmetryHelpers::compose(invSymmetryOfNode, symmetry);
    auto iter = node->moves.find(SymmetryHelpers::getSymLoc(move, node->book->initialBoard, symmetry));
    if(iter != node->moves.end()) {
      const BookMove& bookMove = iter->second;
      const BookNode* child = node->book->get(bookMove.hash);
      // Same logic here, compose and invert.
      return ConstSymBookNode(child,SymmetryHelpers::invert(SymmetryHelpers::compose(symmetry,bookMove.symmetryToAlign)));
    }
  }
  return ConstSymBookNode(nullptr);
}

SymBookNode SymBookNode::playMove(Board& board, BoardHistory& hist, Loc move) {
  SymBookNode ret = follow(move);
  if(ret.isNull())
    return SymBookNode(nullptr);
  if(!hist.isLegal(board,move,node->pla))
    return SymBookNode(nullptr);
  hist.makeBoardMoveAssumeLegal(board,move,node->pla,nullptr);
  return ret;
}
ConstSymBookNode ConstSymBookNode::playMove(Board& board, BoardHistory& hist, Loc move) {
  ConstSymBookNode ret = follow(move);
  if(ret.isNull())
    return ConstSymBookNode(nullptr);
  if(!hist.isLegal(board,move,node->pla))
    return ConstSymBookNode(nullptr);
  hist.makeBoardMoveAssumeLegal(board,move,node->pla,nullptr);
  return ret;
}

SymBookNode SymBookNode::playAndAddMove(Board& board, BoardHistory& hist, Loc move, double rawPolicy, bool& childIsTransposing) {
  assert(node != nullptr);
  assert(!isMoveInBook(move));
  childIsTransposing = false;

  if(!hist.isLegal(board,move,node->pla))
    return SymBookNode(nullptr);

  int xSize = node->book->initialBoard.x_size;
  int ySize = node->book->initialBoard.y_size;

  // Transform the move into the space of the node
  Loc symMove = SymmetryHelpers::getSymLoc(move,xSize,ySize,invSymmetryOfNode);

  // Find the symmetry for move that prefers the upper right corner if possible.
  // Maximize x first, then minimize y next
  // Although, this will only work politely for the initial empty board. Past that, it will really be just whatever orientation gets
  // chosen canonically for the board. Which is fine, I guess.
  // Even if the symmetry equivalence code isn't perfect, this should never choose an outright illegal move except for true hash
  // collisions because the state hash accounts for all ko and superko prohibitions, so at worst we'll be
  // just playing the wrong child node out.
  Loc bestLoc = symMove;
  int bestSymmetry = 0;
  for(int symmetry: node->symmetries) {
    if(symmetry == 0)
      continue;
    Loc symLoc = SymmetryHelpers::getSymLoc(symMove,xSize,ySize,symmetry);
    int symX = Location::getX(symLoc,xSize);
    int symY = Location::getY(symLoc,xSize);
    int bestX = Location::getX(bestLoc,xSize);
    int bestY = Location::getY(bestLoc,xSize);
    if(symX > bestX || (symX == bestX && symY < bestY)) {
      bestLoc = symLoc;
      bestSymmetry = symmetry;
    }
  }

  hist.makeBoardMoveAssumeLegal(board,move,node->pla,nullptr);
  BookHash childHash;
  int symmetryToAlignToChild;
  vector<int> symmetriesOfChild;
  BookHash::getHashAndSymmetry(hist, node->book->repBound, childHash, symmetryToAlignToChild, symmetriesOfChild);

  // Okay...
  // A: invSymmetryOfNode is the transform from SymBookNode space -> BookNode space
  // B: bestSymmetry is the transform from BookNode space -> polite BookNode space
  // C: symmetryToAlignToChild is the transform from SymBookNode space -> child space
  //
  // We need to fill newBookMove with the transform from polite BookNode space -> child space.
  // So we need to compose invert(B) and invert(A) and C
  // We need to return back to the user for the child SymBookNode the symmetry mapping child space -> SymBookNode space
  // So we need to use invert(C)

  BookNode* child = node->book->get(childHash);
  if(child == nullptr) {
    child = new BookNode(childHash, node->book, hist.presumedNextMovePla, symmetriesOfChild);
    bool suc = node->book->add(childHash,child);
    assert(suc);
    (void)suc;
    childIsTransposing = false;
  }
  else {
    childIsTransposing = true;
  }
  child->parents.push_back(std::make_pair(node->hash, bestLoc));

  BookMove newBookMove(
    bestLoc,
    SymmetryHelpers::compose(SymmetryHelpers::invert(bestSymmetry),symmetryOfNode,symmetryToAlignToChild),
    childHash,
    rawPolicy
  );
  node->moves[bestLoc] = newBookMove;
  return SymBookNode(child,SymmetryHelpers::invert(symmetryToAlignToChild));
}


bool SymBookNode::getBoardHistoryReachingHere(BoardHistory& ret, vector<Loc>& moveHistoryRet) {
  return ConstSymBookNode(*this).getBoardHistoryReachingHere(ret,moveHistoryRet);
}
bool ConstSymBookNode::getBoardHistoryReachingHere(BoardHistory& ret, vector<Loc>& moveHistoryRet) {
  assert(node != nullptr);
  const Book* book = node->book;
  vector<const BookNode*> pathFromRoot;
  vector<Loc> movesFromRoot;
  bool suc = node->book->reverseDepthFirstSearchWithMoves(
    node,
    [&book,&pathFromRoot,&movesFromRoot](const vector<const BookNode*>& stack, const vector<Loc>& moveStack) {
      if(stack.back() == book->root) {
        pathFromRoot = vector<const BookNode*>(stack.rbegin(),stack.rend());
        movesFromRoot = vector<Loc>(moveStack.rbegin(),moveStack.rend());
        return Book::DFSAction::abort;
      }
      return Book::DFSAction::recurse;
    }
  );
  (void)suc;
  assert(suc);
  assert(pathFromRoot.size() >= 1);
  assert(movesFromRoot.size() == pathFromRoot.size());

  // Find the total composed symmetry that we will have to apply as we walk down.
  int symmetryAcc = 0;
  for(size_t i = 0; i < pathFromRoot.size()-1; i++) {
    auto iter = pathFromRoot[i]->moves.find(movesFromRoot[i]);
    assert(iter != pathFromRoot[i]->moves.end());
    symmetryAcc = SymmetryHelpers::compose(symmetryAcc,iter->second.symmetryToAlign);
  }
  // At the end, we'll need this symmetry to transform it into SymBookNode space.
  symmetryAcc = SymmetryHelpers::compose(symmetryAcc,symmetryOfNode);
  // Additionally we need to apply the symmetry that maps the book's initial hist into its root node space.
  symmetryAcc = SymmetryHelpers::compose(node->book->initialSymmetry,symmetryAcc);

  // symmetryAcc is the map from initialSpace to desired final histSpace
  // Start out with a board permuted with this symmetry so that the initial stones all end up in the right orientation
  BoardHistory hist = node->book->getInitialHist(symmetryAcc);
  Board board = hist.getRecentBoard(0);
  moveHistoryRet.clear();

  // Invariant: during loop iteration i, symmetryPathNodeToHist is the transform from pathFromRoot[i] space to hist space.
  // symmetryAcc is the map from initialSpace -> histSpace
  // initialSymmetry is the map from initialSpace -> rootSpace.
  // We need to start with the map from rootSpace -> histSpace
  int symmetryPathNodeToHist = SymmetryHelpers::compose(SymmetryHelpers::invert(node->book->initialSymmetry),symmetryAcc);
  for(size_t i = 0; i < pathFromRoot.size()-1; i++) {
    auto iter = pathFromRoot[i]->moves.find(movesFromRoot[i]);
    // Convert move into space of hist and play it
    Loc symMove = SymmetryHelpers::getSymLoc(movesFromRoot[i], node->book->initialBoard, symmetryPathNodeToHist);
    moveHistoryRet.push_back(symMove);

    // Use tolerant rules so that if something weird happens regarding superko in cycles, we just plow through.
    if(!hist.isLegalTolerant(board, symMove, pathFromRoot[i]->pla)) {
      // Something is very wrong, probably a corrupted book data structure.
      return false;
    }
    hist.makeBoardMoveAssumeLegal(board, symMove, pathFromRoot[i]->pla, nullptr);

    // Update symmetryPathNodeToHist for the next loop.
    // symmetryPathNodeToHist is currently space[i] -> histSpace, and needs to become space[i+1] -> histSpace
    // symmetryToAlign[i] is space[i] -> space[i+1].
    symmetryPathNodeToHist = SymmetryHelpers::compose(SymmetryHelpers::invert(iter->second.symmetryToAlign), symmetryPathNodeToHist);
  }

  // Yay
  ret = hist;
  return true;
}


Book::Book(
  const Board& b,
  Rules r,
  Player p,
  int rb,
  double sf,
  double cpm,
  double cpucbwl,
  double cpucbwlp3,
  double cpucbwlp7,
  double cpucbsl,
  double cplp,
  double cpme,
  double cpsme,
  double cwpf,
  double bpwle,
  double bpse,
  double bpssd,
  double bpeup,
  double bfwlpv1,
  double bfwlpv2,
  double bfbwlc,
  double slc,
  double ups,
  double pbsus,
  double uppfs,
  double ssoc
) : initialBoard(b),
    initialRules(r),
    initialPla(p),
    repBound(rb),
    errorFactor(sf),
    costPerMove(cpm),
    costPerUCBWinLossLoss(cpucbwl),
    costPerUCBWinLossLossPow3(cpucbwlp3),
    costPerUCBWinLossLossPow7(cpucbwlp7),
    costPerUCBScoreLoss(cpucbsl),
    costPerLogPolicy(cplp),
    costPerMovesExpanded(cpme),
    costPerSquaredMovesExpanded(cpsme),
    costWhenPassFavored(cwpf),
    bonusPerWinLossError(bpwle),
    bonusPerScoreError(bpse),
    bonusPerSharpScoreDiscrepancy(bpssd),
    bonusPerExcessUnexpandedPolicy(bpeup),
    bonusForWLPV1(bfwlpv1),
    bonusForWLPV2(bfwlpv2),
    bonusForBiggestWLCost(bfbwlc),
    scoreLossCap(slc),
    utilityPerScore(ups),
    policyBoostSoftUtilityScale(pbsus),
    utilityPerPolicyForSorting(uppfs),
    sharpScoreOutlierCap(ssoc),
    initialSymmetry(0),
    root(nullptr),
    nodes(),
    nodeIdxMapsByHash(nullptr)
{
  nodeIdxMapsByHash = new map<BookHash,int64_t>[NUM_HASH_BUCKETS];

  BookHash rootHash;
  int symmetryToAlign;
  vector<int> rootSymmetries;

  int initialEncorePhase = 0;
  BoardHistory initialHist(initialBoard, initialPla, initialRules, initialEncorePhase);
  BookHash::getHashAndSymmetry(initialHist, repBound, rootHash, symmetryToAlign, rootSymmetries);

  initialSymmetry = symmetryToAlign;
  root = new BookNode(rootHash, this, initialPla, rootSymmetries);
  nodeIdxMapsByHash[rootHash.stateHash.hash0 % NUM_HASH_BUCKETS][rootHash] = (int64_t)nodes.size();
  nodes.push_back(root);
}

Book::~Book() {
  for(BookNode* node: nodes)
    delete node;
  delete[] nodeIdxMapsByHash;
}

BoardHistory Book::getInitialHist() const {
  return getInitialHist(0);
}
BoardHistory Book::getInitialHist(int symmetry) const {
  int initialEncorePhase = 0;
  return BoardHistory(SymmetryHelpers::getSymBoard(initialBoard,symmetry), initialPla, initialRules, initialEncorePhase);
}

size_t Book::size() const {
  return nodes.size();
}

double Book::getErrorFactor() const { return errorFactor; }
void Book::setErrorFactor(double d) { errorFactor = d; }
double Book::getCostPerMove() const { return costPerMove; }
void Book::setCostPerMove(double d) { costPerMove = d; }
double Book::getCostPerUCBWinLossLoss() const { return costPerUCBWinLossLoss; }
void Book::setCostPerUCBWinLossLoss(double d) { costPerUCBWinLossLoss = d; }
double Book::getCostPerUCBWinLossLossPow3() const { return costPerUCBWinLossLossPow3; }
void Book::setCostPerUCBWinLossLossPow3(double d) { costPerUCBWinLossLossPow3 = d; }
double Book::getCostPerUCBWinLossLossPow7() const { return costPerUCBWinLossLossPow7; }
void Book::setCostPerUCBWinLossLossPow7(double d) { costPerUCBWinLossLossPow7 = d; }
double Book::getCostPerUCBScoreLoss() const { return costPerUCBScoreLoss; }
void Book::setCostPerUCBScoreLoss(double d) { costPerUCBScoreLoss = d; }
double Book::getCostPerLogPolicy() const { return costPerLogPolicy; }
void Book::setCostPerLogPolicy(double d) { costPerLogPolicy = d; }
double Book::getCostPerMovesExpanded() const { return costPerMovesExpanded; }
void Book::setCostPerMovesExpanded(double d) { costPerMovesExpanded = d; }
double Book::getCostPerSquaredMovesExpanded() const { return costPerSquaredMovesExpanded; }
void Book::setCostPerSquaredMovesExpanded(double d) { costPerSquaredMovesExpanded = d; }
double Book::getCostWhenPassFavored() const { return costWhenPassFavored; }
void Book::setCostWhenPassFavored(double d) { costWhenPassFavored = d; }
double Book::getBonusPerWinLossError() const { return bonusPerWinLossError; }
void Book::setBonusPerWinLossError(double d) { bonusPerWinLossError = d; }
double Book::getBonusPerScoreError() const { return bonusPerScoreError; }
void Book::setBonusPerScoreError(double d) { bonusPerScoreError = d; }
double Book::getBonusPerSharpScoreDiscrepancy() const { return bonusPerSharpScoreDiscrepancy; }
void Book::setBonusPerSharpScoreDiscrepancy(double d) { bonusPerSharpScoreDiscrepancy = d; }
double Book::getBonusPerExcessUnexpandedPolicy() const { return bonusPerExcessUnexpandedPolicy; }
void Book::setBonusPerExcessUnexpandedPolicy(double d) { bonusPerExcessUnexpandedPolicy = d; }
double Book::getBonusForWLPV1() const { return bonusForWLPV1; }
void Book::setBonusForWLPV1(double d) { bonusForWLPV1 = d; }
double Book::getBonusForWLPV2() const { return bonusForWLPV2; }
void Book::setBonusForWLPV2(double d) { bonusForWLPV2 = d; }
double Book::getBonusForBiggestWLCost() const { return bonusForBiggestWLCost; }
void Book::setBonusForBiggestWLCost(double d) { bonusForBiggestWLCost = d; }
double Book::getScoreLossCap() const { return scoreLossCap; }
void Book::setScoreLossCap(double d) { scoreLossCap = d; }
double Book::getUtilityPerScore() const { return utilityPerScore; }
void Book::setUtilityPerScore(double d) { utilityPerScore = d; }
double Book::getPolicyBoostSoftUtilityScale() const { return policyBoostSoftUtilityScale; }
void Book::setPolicyBoostSoftUtilityScale(double d) { policyBoostSoftUtilityScale = d; }
double Book::getUtilityPerPolicyForSorting() const { return utilityPerPolicyForSorting; }
void Book::setUtilityPerPolicyForSorting(double d) { utilityPerPolicyForSorting = d; }
std::map<BookHash,double> Book::getBonusByHash() const { return bonusByHash; }
void Book::setBonusByHash(const std::map<BookHash,double>& d) { bonusByHash = d; }


SymBookNode Book::getRoot() {
  // Invert because SymBookNode needs map from node space -> initial space, but initialSymmetry is map initial space -> node space.
  return SymBookNode(root, SymmetryHelpers::invert(initialSymmetry));
}
ConstSymBookNode Book::getRoot() const {
  return ConstSymBookNode(root, SymmetryHelpers::invert(initialSymmetry));
}

SymBookNode Book::get(const BoardHistory& hist) {
  SymBookNode node = getRoot();
  for(Move move: hist.moveHistory) {
    node = node.follow(move.loc);
    if(node.isNull())
      return node;
  }
  return node;
}
ConstSymBookNode Book::get(const BoardHistory& hist) const {
  ConstSymBookNode node = getRoot();
  for(Move move: hist.moveHistory) {
    node = node.follow(move.loc);
    if(node.isNull())
      return node;
  }
  return node;
}

void Book::recompute(const vector<SymBookNode>& newAndChangedNodes) {
  // Walk up from all changed nodes and mark all parents dirty recursively.
  std::set<BookHash> dirtyNodes;
  std::function<DFSAction(BookNode*)> markDirty = [&dirtyNodes](BookNode* node) {
    // cout << "Mark dirty " << node->hash << " " << node << endl;
    if(contains(dirtyNodes, node->hash))
      return DFSAction::skip;
    dirtyNodes.insert(node->hash);
    return DFSAction::recurse;
  };
  for(SymBookNode node: newAndChangedNodes) {
    reverseDepthFirstSearchWithPostF(node.node, markDirty, std::function<void(BookNode*)>(nullptr));
  }

  // Walk down through all dirty nodes recomputing values
  bool allDirty = false;
  iterateDirtyNodesPostOrder(
    dirtyNodes,
    allDirty,
    [this](BookNode* node) {
      recomputeNodeValues(node);
    }
  );

  // Walk down through entire book, recomputing costs.
  iterateEntireBookPreOrder(
    [this](BookNode* node) {
      recomputeNodeCost(node);
    }
  );
}

void Book::recomputeEverything() {
  // Walk down through all nodes recomputing values
  bool allDirty = true;
  iterateDirtyNodesPostOrder(
    std::set<BookHash>(),
    allDirty,
    [this](BookNode* node) {
      recomputeNodeValues(node);
    }
  );

  // Walk down through entire book, recomputing costs.
  iterateEntireBookPreOrder(
    [this](BookNode* node) {
      recomputeNodeCost(node);
    }
  );
}

vector<SymBookNode> Book::getNextNToExpand(int n) {
  vector<BookNode*> toExpand(n);
  auto end = std::partial_sort_copy(
    nodes.begin(),
    nodes.end(),
    toExpand.begin(),
    toExpand.end(),
    [](BookNode* n0, BookNode* n1) {
      return n0->minCostFromRoot + n0->thisNodeExpansionCost < n1->minCostFromRoot + n1->thisNodeExpansionCost;
    }
  );
  toExpand.resize(end-toExpand.begin());

  vector<SymBookNode> ret;
  for(BookNode* node: toExpand) {
    if(node->canExpand)
      ret.push_back(SymBookNode(node,0));
  }
  return ret;
}

vector<SymBookNode> Book::getAllLeaves(double minVisits) {
  vector<SymBookNode> ret;
  for(BookNode* node: nodes) {
    if(node->recursiveValues.visits >= minVisits) {
      bool allChildrenLess = true;
      for(auto iter = node->moves.begin(); iter != node->moves.end(); ++iter) {
        const BookNode* child = get(iter->second.hash);
        if(child->recursiveValues.visits >= minVisits) {
          allChildrenLess = false;
          break;
        }
      }
      if(allChildrenLess) {
        ret.push_back(SymBookNode(node,0));
      }
    }
  }
  return ret;
}

BookNode* Book::get(BookHash hash) {
  map<BookHash,int64_t>& nodeIdxMap = nodeIdxMapsByHash[hash.stateHash.hash0 % NUM_HASH_BUCKETS];
  auto iter = nodeIdxMap.find(hash);
  if(iter == nodeIdxMap.end())
    return nullptr;
  return nodes[iter->second];
}
const BookNode* Book::get(BookHash hash) const {
  const map<BookHash,int64_t>& nodeIdxMap = nodeIdxMapsByHash[hash.stateHash.hash0 % NUM_HASH_BUCKETS];
  auto iter = nodeIdxMap.find(hash);
  if(iter == nodeIdxMap.end())
    return nullptr;
  return nodes[iter->second];
}

SymBookNode Book::getByHash(BookHash hash) {
  BookNode* node = get(hash);
  if(node == nullptr)
    return SymBookNode(nullptr);
  return SymBookNode(node,0);
}
ConstSymBookNode Book::getByHash(BookHash hash) const {
  const BookNode* node = get(hash);
  if(node == nullptr)
    return ConstSymBookNode(nullptr);
  return ConstSymBookNode(node,0);
}


bool Book::add(BookHash hash, BookNode* node) {
  map<BookHash,int64_t>& nodeIdxMap = nodeIdxMapsByHash[hash.stateHash.hash0 % NUM_HASH_BUCKETS];
  auto iter = nodeIdxMap.find(hash);
  if(iter != nodeIdxMap.end())
    return false;
  nodeIdxMap[hash] = (int64_t)nodes.size();
  nodes.push_back(node);
  return true;
}


// Walk the entire reverse-subtree starting at node, calling f with the current (stack,moveStack) at each step.
// stack is the stack of moves searched. moveStack[i] is the move to make from stack[i] to reach stack[i-1], in stack[i]'s symmetry alignment.
// moveStack[0] is always NULL_LOC.
// Will never walk to any node more than once, if there are backward paths that reverse-transpose or have cycles.
// Stop searching and immediately return true if f ever returns abort.
// Returns false at the end of the whole search if f never aborts.
bool Book::reverseDepthFirstSearchWithMoves(
  const BookNode* initialNode,
  const std::function<Book::DFSAction(const vector<const BookNode*>&, const vector<Loc>&)>& f
) const {
  vector<const BookNode*> stack;
  vector<Loc> moveStack;
  vector<size_t> nextParentIdxToTry;
  std::set<BookHash> visitedHashes;
  stack.push_back(initialNode);
  Loc nullLoc = Board::NULL_LOC; //Workaround for c++14 wonkiness fixed in c++17
  moveStack.push_back(nullLoc);
  nextParentIdxToTry.push_back(0);
  visitedHashes.insert(initialNode->hash);

  while(true) {
    // Handle new found node
    DFSAction action = f(stack,moveStack);
    if(action == DFSAction::abort)
      return true;
    else if(action == DFSAction::skip) {
      nextParentIdxToTry.back() = std::numeric_limits<size_t>::max();
    }

    // Attempt to find the next node
    while(true) {
      // Try walk to next parent
      const BookNode* node = stack.back();
      size_t nextParentIdx = nextParentIdxToTry.back();
      if(nextParentIdx < node->parents.size()) {
        BookHash nextParentHash = node->parents[nextParentIdx].first;
        Loc nextParentLoc = node->parents[nextParentIdx].second;
        nextParentIdxToTry.back() += 1;
        if(!contains(visitedHashes, nextParentHash)) {
          const BookNode* nextParent = get(nextParentHash);
          stack.push_back(nextParent);
          moveStack.push_back(nextParentLoc);
          nextParentIdxToTry.push_back(0);
          visitedHashes.insert(nextParentHash);
          // Found next node, break out of attempt to find the next node.
          break;
        }
        else {
          // Parent already visited, continue at same node to try next parent.
          continue;
        }
      }
      else {
        // Exhausted all parents at this node, go up one level.
        stack.pop_back();
        moveStack.pop_back();
        nextParentIdxToTry.pop_back();
        // If we go up a level and there's nothing, we're done.
        if(stack.size() <= 0)
          return false;
      }
    }
  }
}

// Walk the entire reverse-subtree starting at node, calling f with the current node at each step.
// Will never walk to any node more than once, if there are backward paths that reverse-transpose or have cycles.
// Stop searching and immediately return true if f ever returns abort.
// Returns false at the end of the whole search if f never aborts.
// If postF is povided, calls postF after returning from a node.
bool Book::reverseDepthFirstSearchWithPostF(
  BookNode* initialNode,
  const std::function<Book::DFSAction(BookNode*)>& f,
  const std::function<void(BookNode*)>& postF
) {
  vector<BookNode*> stack;
  vector<size_t> nextParentIdxToTry;
  std::set<BookHash> visitedHashes;
  stack.push_back(initialNode);
  nextParentIdxToTry.push_back(0);
  visitedHashes.insert(initialNode->hash);

  while(true) {
    // Handle new found node
    DFSAction action = f(stack.back());
    if(action == DFSAction::abort)
      return true;
    else if(action == DFSAction::skip) {
      nextParentIdxToTry.back() = std::numeric_limits<size_t>::max();
    }

    // Attempt to find the next node
    while(true) {
      // Try walk to next parent
      BookNode* node = stack.back();
      size_t nextParentIdx = nextParentIdxToTry.back();
      if(nextParentIdx < node->parents.size()) {
        BookHash nextParentHash = node->parents[nextParentIdx].first;
        nextParentIdxToTry.back() += 1;
        if(!contains(visitedHashes, nextParentHash)) {
          BookNode* nextParent = get(nextParentHash);
          stack.push_back(nextParent);
          nextParentIdxToTry.push_back(0);
          visitedHashes.insert(nextParentHash);
          // Found next node, break out of attempt to find the next node.
          break;
        }
        else {
          // Parent already visited, continue at same node to try next parent.
          continue;
        }
      }
      else {
        if(postF)
          postF(stack.back());

        // Exhausted all parents at this node, go up one level.
        stack.pop_back();
        nextParentIdxToTry.pop_back();
        // If we go up a level and there's nothing, we're done.
        if(stack.size() <= 0)
          return false;
      }
    }
  }
}

// Precondition: dirtyNodes has the property that if a node n is in dirtyNodes, all parents of n are in dirtyNodes.
// Calls f on every node in dirtyNodes in an order where all children of any node are called before a node.
// Except if somehow the book has mangaged to be cyclic, in which case the cycle is broken arbitrarily.
// If allDirty is true, ignores dirtyNodes and treats all nodes in the entiry book as dirty.
void Book::iterateDirtyNodesPostOrder(
  const std::set<BookHash>& dirtyNodes,
  bool allDirty,
  const std::function<void(BookNode* node)>& f
) {
  vector<BookNode*> stack;
  vector<map<Loc,BookMove>::iterator> nextChildToTry;
  std::set<BookHash> visitedHashes;

  if(!allDirty && dirtyNodes.size() <= 0)
    return;
  assert(allDirty || contains(dirtyNodes, root->hash));

  stack.push_back(root);
  nextChildToTry.push_back(root->moves.begin());
  visitedHashes.insert(root->hash);

  while(true) {
    // Attempt to find the next node
    while(true) {
      // Try walk to next parent
      BookNode* node = stack.back();
      map<Loc,BookMove>::iterator iter = nextChildToTry.back();

      if(iter != node->moves.end()) {
        BookHash nextChildHash = iter->second.hash;
        ++nextChildToTry.back();
        if(!contains(visitedHashes,nextChildHash) && (allDirty || contains(dirtyNodes,nextChildHash))) {
          BookNode* nextChild = get(nextChildHash);
          stack.push_back(nextChild);
          nextChildToTry.push_back(nextChild->moves.begin());
          visitedHashes.insert(nextChildHash);
          // Found next node, break out of attempt to find the next node.
          break;
        }
        else {
          // Child already visited, continue at same node to try next child.
          continue;
        }
      }
      else {
        // Exhausted all childs at this node. Call f since postorder is done.
        f(node);

        //Then pop up a level.
        stack.pop_back();
        nextChildToTry.pop_back();
        // If we go up a level and there's nothing, we're done.
        if(stack.size() <= 0)
          return;
      }
    }
  }
}

void Book::iterateEntireBookPreOrder(
  const std::function<void(BookNode*)>& f
) {
  std::set<BookHash> visitedHashes;
  for(BookNode* initialNode: nodes) {
    if(contains(visitedHashes, initialNode->hash))
      continue;
    reverseDepthFirstSearchWithPostF(
      initialNode,
      [&visitedHashes](BookNode* node) {
        if(contains(visitedHashes, node->hash))
          return DFSAction::skip;
        return DFSAction::recurse;
      },
      [&visitedHashes,&f](BookNode* node) {
        if(contains(visitedHashes, node->hash))
          return;
        visitedHashes.insert(node->hash);
        f(node);
      }
    );
  }
}

void Book::recomputeNodeValues(BookNode* node) {
  double winLossValue;
  double scoreMean;
  double sharpScoreMean;
  double winLossLCB;
  double scoreLCB;
  double scoreFinalLCB;
  double winLossUCB;
  double scoreUCB;
  double scoreFinalUCB;
  double weight = 0.0;
  double visits = 0.0;

  {
    const BookValues& values = node->thisValuesNotInBook;
    double scoreError = values.getAdjustedScoreError(node->book->initialRules);
    winLossValue = values.winLossValue;
    scoreMean = values.scoreMean;
    sharpScoreMean = values.sharpScoreMean;
    winLossLCB = values.winLossValue - errorFactor * values.winLossError;
    scoreLCB = values.scoreMean - errorFactor * scoreError;
    scoreFinalLCB = values.scoreMean - errorFactor * values.scoreStdev;
    winLossUCB = values.winLossValue + errorFactor * values.winLossError;
    scoreUCB = values.scoreMean + errorFactor * scoreError;
    scoreFinalUCB = values.scoreMean + errorFactor * values.scoreStdev;
    weight += values.weight;
    visits += values.visits;

    // A quick hack to limit the issue of outliers from sharpScore, and adjust the LCB/UCB to reflect the uncertainty
    if(sharpScoreMean > scoreUCB)
      scoreUCB = sharpScoreMean;
    if(sharpScoreMean < scoreLCB)
      scoreLCB = sharpScoreMean;
    if(sharpScoreMean > scoreMean + sharpScoreOutlierCap)
      sharpScoreMean = scoreMean + sharpScoreOutlierCap;
    if(sharpScoreMean < scoreMean - sharpScoreOutlierCap)
      sharpScoreMean = scoreMean - sharpScoreOutlierCap;
  }

  for(auto iter = node->moves.begin(); iter != node->moves.end(); ++iter) {
    const BookNode* child = get(iter->second.hash);
    // cout << "pulling values from child " << child << " hash " << child->hash << endl;
    const RecursiveBookValues& values = child->recursiveValues;
    if(node->pla == P_WHITE) {
      winLossValue = std::max(winLossValue, values.winLossValue);
      scoreMean = std::max(scoreMean, values.scoreMean);
      sharpScoreMean = std::max(sharpScoreMean, values.sharpScoreMean);
      winLossLCB = std::max(winLossLCB, values.winLossLCB);
      scoreLCB = std::max(scoreLCB, values.scoreLCB);
      scoreFinalLCB = std::max(scoreFinalLCB, values.scoreFinalLCB);
      winLossUCB = std::max(winLossUCB, values.winLossUCB);
      scoreUCB = std::max(scoreUCB, values.scoreUCB);
      scoreFinalUCB = std::max(scoreFinalUCB, values.scoreFinalUCB);
      weight += values.weight;
      visits += values.visits;
    }
    else {
      winLossValue = std::min(winLossValue, values.winLossValue);
      scoreMean = std::min(scoreMean, values.scoreMean);
      sharpScoreMean = std::min(sharpScoreMean, values.sharpScoreMean);
      winLossLCB = std::min(winLossLCB, values.winLossLCB);
      scoreLCB = std::min(scoreLCB, values.scoreLCB);
      scoreFinalLCB = std::min(scoreFinalLCB, values.scoreFinalLCB);
      winLossUCB = std::min(winLossUCB, values.winLossUCB);
      scoreUCB = std::min(scoreUCB, values.scoreUCB);
      scoreFinalUCB = std::min(scoreFinalUCB, values.scoreFinalUCB);
      weight += values.weight;
      visits += values.visits;
    }
  }

  RecursiveBookValues& values = node->recursiveValues;
  values.winLossValue = winLossValue;
  values.scoreMean = scoreMean;
  values.sharpScoreMean = sharpScoreMean;
  values.winLossLCB = winLossLCB;
  values.scoreLCB = scoreLCB;
  values.scoreFinalLCB = scoreFinalLCB;
  values.winLossUCB = winLossUCB;
  values.scoreUCB = scoreUCB;
  values.scoreFinalUCB = scoreFinalUCB;
  values.weight = weight;
  values.visits = visits;

  // cout << "Setting " << node->hash << " values" << endl;
  // cout << "Values " << values.winLossLCB << " " << values.winLossValue << " " << values.winLossUCB << endl;
  // cout << "Score " << values.scoreLCB << " " << values.scoreMean << " " << values.scoreUCB << endl;
}

static double square(double x) {
  return x * x;
}
static double pow3(double x) {
  return x * x * x;
}
static double pow7(double x) {
  double cube = x * x * x;
  return cube * cube * x;
}

double Book::getUtility(const RecursiveBookValues& values) const {
  return values.winLossValue + values.scoreMean * utilityPerScore;
}

void Book::recomputeNodeCost(BookNode* node) {
  // Update this node's minCostFromRoot based on cost for moves from parents.
  if(node == root) {
    node->minCostFromRoot = 0.0;
    node->minCostFromRootWLPV = 0.0;
    node->biggestWLCostFromRoot = 0.0;
  }
  else {
    // cout << "Recomputing cost " << node->hash << endl;
    double minCost = 1e100;
    double minCostWLPV = 1e100;
    double bestBiggestWLCostFromRoot = 1e100;
    for(std::pair<BookHash,Loc>& parentInfo: node->parents) {
      const BookNode* parent = get(parentInfo.first);
      auto parentLocAndBookMove = parent->moves.find(parentInfo.second);
      assert(parentLocAndBookMove != parent->moves.end());
      double cost = parentLocAndBookMove->second.costFromRoot;
      double biggestWLCostFromRoot = parentLocAndBookMove->second.biggestWLCostFromRoot;
      if(cost < minCost) {
        minCost = cost;
        bestBiggestWLCostFromRoot = biggestWLCostFromRoot;
      }
      if(parentLocAndBookMove->second.isWLPV) {
        if(parent->minCostFromRootWLPV < minCostWLPV)
          minCostWLPV = parent->minCostFromRootWLPV;
      }
    }
    node->minCostFromRoot = minCost;
    node->minCostFromRootWLPV = minCostWLPV;
    node->biggestWLCostFromRoot = bestBiggestWLCostFromRoot;
  }

  // Apply user-specified bonuses
  if(contains(bonusByHash, node->hash)) {
    double bonus = bonusByHash[node->hash];
    node->minCostFromRoot -= bonus;
  }

  if(node->minCostFromRoot < node->minCostFromRootWLPV)
    node->minCostFromRootWLPV = node->minCostFromRoot;

  // Find the winloss PV for this node
  {
    double bestWinLossThisPerspective = -1e100;
    Loc bestWinLossMove = Board::NULL_LOC;
    for(auto& locAndBookMove: node->moves) {
      locAndBookMove.second.isWLPV = false;
      const BookNode* child = get(locAndBookMove.second.hash);
      double winLossThisPerspective = (node->pla == P_WHITE ? child->recursiveValues.winLossValue : -child->recursiveValues.winLossValue);
      if(winLossThisPerspective > bestWinLossThisPerspective) {
        bestWinLossThisPerspective = winLossThisPerspective;
        bestWinLossMove = locAndBookMove.first;
      }
    }
    {
      node->expansionIsWLPV = false;
      double winLossThisPerspective = (node->pla == P_WHITE ? node->thisValuesNotInBook.winLossValue : -node->thisValuesNotInBook.winLossValue);
      if(winLossThisPerspective > bestWinLossThisPerspective) {
        bestWinLossThisPerspective = winLossThisPerspective;
        bestWinLossMove = Board::NULL_LOC;
      }
    }
    if(bestWinLossMove == Board::NULL_LOC)
      node->expansionIsWLPV = true;
    else
      node->moves[bestWinLossMove].isWLPV = true;
  }

  // Look at other children whose policy is higher, and if this is better than those by a lot
  // softly boost the policy of this move.
  auto boostLogRawPolicy = [&](double logRawPolicy, double childUtility, double rawPolicy) {
    double boostedLogRawPolicy = logRawPolicy;
    for(auto& otherLocAndBookMove: node->moves) {
      if(otherLocAndBookMove.second.rawPolicy <= rawPolicy)
        continue;
      const BookNode* otherChild = get(otherLocAndBookMove.second.hash);
      double otherChildUtility = getUtility(otherChild->recursiveValues);
      double gainOverOtherChild =
        (node->pla == P_WHITE) ?
        childUtility - otherChildUtility :
        otherChildUtility - childUtility;
      double policyBoostFactor = 1.0/(1.0 + exp(-gainOverOtherChild / policyBoostSoftUtilityScale + 1.0));
      double otherLogRawPolicy = log(otherLocAndBookMove.second.rawPolicy + 1e-100);
      double p = logRawPolicy + policyBoostFactor * (otherLogRawPolicy - logRawPolicy);
      if(p > boostedLogRawPolicy)
        boostedLogRawPolicy = p;
      // cout << "Boosting policy " << logRawPolicy << " " << otherLogRawPolicy << " " << p << " " << gainOverOtherChild << endl;
      return boostedLogRawPolicy;
    }
    return logRawPolicy;
  };

  // Figure out whether pass is the favored move
  double passPolicy = 0.0;
  double passUtility = node->pla == P_WHITE ? -1e100 : 1e100;
  const Loc passLoc = Board::PASS_LOC;
  if(node->moves.find(passLoc) != node->moves.end()) {
    passPolicy = node->moves[passLoc].rawPolicy;
    passUtility = getUtility(get(node->moves[passLoc].hash)->recursiveValues);
  }

  // Update cost for moves for children to reference.
  double smallestCostFromUCB = 1e100;
  for(auto& locAndBookMove: node->moves) {
    const BookNode* child = get(locAndBookMove.second.hash);
    double ucbWinLossLoss =
      (node->pla == P_WHITE) ?
      node->recursiveValues.winLossUCB - child->recursiveValues.winLossUCB :
      child->recursiveValues.winLossLCB - node->recursiveValues.winLossLCB;
    double ucbWinLossLossPow3 =
      (node->pla == P_WHITE) ?
      pow3(node->recursiveValues.winLossUCB) - pow3(child->recursiveValues.winLossUCB) :
      pow3(child->recursiveValues.winLossLCB) - pow3(node->recursiveValues.winLossLCB);
    double ucbWinLossLossPow7 =
      (node->pla == P_WHITE) ?
      pow7(node->recursiveValues.winLossUCB) - pow7(child->recursiveValues.winLossUCB) :
      pow7(child->recursiveValues.winLossLCB) - pow7(node->recursiveValues.winLossLCB);
    double ucbScoreLoss =
      (node->pla == P_WHITE) ?
      node->recursiveValues.scoreUCB - child->recursiveValues.scoreUCB :
      child->recursiveValues.scoreLCB - node->recursiveValues.scoreLCB;
    if(ucbScoreLoss > scoreLossCap)
      ucbScoreLoss = scoreLossCap;
    double rawPolicy = locAndBookMove.second.rawPolicy;
    double logRawPolicy = log(rawPolicy + 1e-100);
    double childUtility = getUtility(child->recursiveValues);
    double boostedLogRawPolicy = boostLogRawPolicy(logRawPolicy, childUtility, locAndBookMove.second.rawPolicy);
    bool passFavored = passPolicy > 0.15 && passPolicy > rawPolicy * 0.8 && (
      (node->pla == P_WHITE && passUtility > childUtility - 0.02) ||
      (node->pla == P_BLACK && passUtility < childUtility + 0.02)
    );

    double costFromWL =
      ucbWinLossLoss * costPerUCBWinLossLoss
      + ucbWinLossLossPow3 * costPerUCBWinLossLossPow3
      + ucbWinLossLossPow7 * costPerUCBWinLossLossPow7;
    if(costFromWL > node->biggestWLCostFromRoot)
      costFromWL -= bonusForBiggestWLCost * (costFromWL - node->biggestWLCostFromRoot);
    double costFromUCB =
      costFromWL
      + ucbScoreLoss * costPerUCBScoreLoss;

    double cost =
      node->minCostFromRoot
      + costPerMove
      + costFromUCB
      + (-boostedLogRawPolicy * costPerLogPolicy)
      + (passFavored ? costWhenPassFavored : 0.0);
    locAndBookMove.second.costFromRoot = cost;
    locAndBookMove.second.biggestWLCostFromRoot = std::max(node->biggestWLCostFromRoot, costFromWL);

    if(costFromUCB < smallestCostFromUCB)
      smallestCostFromUCB = costFromUCB;
  }

  if(!node->canExpand) {
    node->thisNodeExpansionCost = 1e100;
  }
  else {
    double scoreError = node->thisValuesNotInBook.getAdjustedScoreError(node->book->initialRules);
    double ucbWinLossLoss =
      (node->pla == P_WHITE) ?
      (node->recursiveValues.winLossUCB - (node->thisValuesNotInBook.winLossValue + errorFactor * node->thisValuesNotInBook.winLossError)) :
      ((node->thisValuesNotInBook.winLossValue - errorFactor * node->thisValuesNotInBook.winLossError) - node->recursiveValues.winLossLCB);
    double ucbWinLossLossPow3 =
      (node->pla == P_WHITE) ?
      (pow3(node->recursiveValues.winLossUCB) - pow3(node->thisValuesNotInBook.winLossValue + errorFactor * node->thisValuesNotInBook.winLossError)) :
      (pow3(node->thisValuesNotInBook.winLossValue - errorFactor * node->thisValuesNotInBook.winLossError) - pow3(node->recursiveValues.winLossLCB));
    double ucbWinLossLossPow7 =
      (node->pla == P_WHITE) ?
      (pow7(node->recursiveValues.winLossUCB) - pow7(node->thisValuesNotInBook.winLossValue + errorFactor * node->thisValuesNotInBook.winLossError)) :
      (pow7(node->thisValuesNotInBook.winLossValue - errorFactor * node->thisValuesNotInBook.winLossError) - pow7(node->recursiveValues.winLossLCB));
    double ucbScoreLoss =
      (node->pla == P_WHITE) ?
      (node->recursiveValues.scoreUCB - (node->thisValuesNotInBook.scoreMean + errorFactor * scoreError)) :
      ((node->thisValuesNotInBook.scoreMean - errorFactor * scoreError) - node->recursiveValues.scoreLCB);
    if(ucbScoreLoss > scoreLossCap)
      ucbScoreLoss = scoreLossCap;
    double rawPolicy = node->thisValuesNotInBook.maxPolicy;
    double logRawPolicy = log(rawPolicy + 1e-100);
    double notInBookUtility = node->thisValuesNotInBook.winLossValue + node->thisValuesNotInBook.scoreMean * utilityPerScore;
    double boostedLogRawPolicy = boostLogRawPolicy(logRawPolicy, notInBookUtility, node->thisValuesNotInBook.maxPolicy);
    bool passFavored = passPolicy > 0.15 && passPolicy > rawPolicy * 0.8 && (
      (node->pla == P_WHITE && passUtility > notInBookUtility - 0.02) ||
      (node->pla == P_BLACK && passUtility < notInBookUtility + 0.02)
    );

    // If we have more than 1/N of unexpanded policy, we cap the penalty for expanded moves at N.
    double movesExpanded = node->moves.size();
    if(movesExpanded > 1.0 / (rawPolicy + 1e-30)) {
      movesExpanded = 1.0 / (rawPolicy + 1e-30);
    }

    // cout << "Expansion thisValues " <<
    //   node->thisValuesNotInBook.winLossValue - errorFactor * node->thisValuesNotInBook.winLossError << " " <<
    //   node->thisValuesNotInBook.winLossValue << " " <<
    //   node->thisValuesNotInBook.winLossValue + errorFactor * node->thisValuesNotInBook.winLossError << endl;
    // cout << "Expansion thisScores " <<
    //   node->thisValuesNotInBook.scoreMean - errorFactor * scoreError << " " <<
    //   node->thisValuesNotInBook.scoreMean << " " <<
    //   node->thisValuesNotInBook.scoreMean + errorFactor * scoreError << endl;
    // cout << "Recursive values " <<
    //   node->recursiveValues.winLossLCB << " " <<
    //   node->recursiveValues.winLossValue << " " <<
    //   node->recursiveValues.winLossUCB << endl;
    // cout << "Recursive scores " <<
    //   node->recursiveValues.scoreLCB << " " <<
    //   node->recursiveValues.scoreMean << " " <<
    //   node->recursiveValues.scoreUCB << endl;
    // cout << "Expansion stats " << ucbWinLossLoss << " " << ucbScoreLoss << " " << rawPolicy << endl;

    double costFromWL =
      ucbWinLossLoss * costPerUCBWinLossLoss
      + ucbWinLossLossPow3 * costPerUCBWinLossLossPow3
      + ucbWinLossLossPow7 * costPerUCBWinLossLossPow7;
    if(costFromWL > node->biggestWLCostFromRoot)
      costFromWL -= bonusForBiggestWLCost * (costFromWL - node->biggestWLCostFromRoot);
    double costFromUCB =
      costFromWL
      + ucbScoreLoss * costPerUCBScoreLoss;

    node->thisNodeExpansionCost =
      costPerMove
      + costFromUCB
      + (-boostedLogRawPolicy * costPerLogPolicy)
      + movesExpanded * costPerMovesExpanded
      + movesExpanded * movesExpanded * costPerSquaredMovesExpanded
      + (passFavored ? costWhenPassFavored : 0.0);

    if(costFromUCB < smallestCostFromUCB)
      smallestCostFromUCB = costFromUCB;
  }

  // Partly replenish moves based on ucb cost conficting, since cost conflicting probably means actually the node is
  // interesting for further expansion.
  if(smallestCostFromUCB > 1e-100) {
    for(auto& locAndBookMove: node->moves) {
      locAndBookMove.second.costFromRoot -= 0.8 * smallestCostFromUCB;
    }
    node->thisNodeExpansionCost -= 0.8 * smallestCostFromUCB;
  }

  // For each move, in order, if its plain winrate is a lot better than the winrate of other moves, then its cost can't be too much worse.
  for(auto& locAndBookMove: node->moves) {
    const BookNode* child = get(locAndBookMove.second.hash);
    double winLoss = (node->pla == P_WHITE) ? child->recursiveValues.winLossValue : -child->recursiveValues.winLossValue;
    double bestOtherCostFromRoot = locAndBookMove.second.costFromRoot;
    for(auto& locAndBookMoveOther: node->moves) {
      if(locAndBookMoveOther.second.costFromRoot < bestOtherCostFromRoot) {
        const BookNode* otherChild = get(locAndBookMoveOther.second.hash);
        double winLossOther = (node->pla == P_WHITE) ? otherChild->recursiveValues.winLossValue : -otherChild->recursiveValues.winLossValue;
        // At least 1.5% better
        if(winLoss > winLossOther + 0.03)
          bestOtherCostFromRoot = locAndBookMoveOther.second.costFromRoot;
      }
    }
    if(bestOtherCostFromRoot < locAndBookMove.second.costFromRoot) {
      // Reduce 50% of cost towards the move that we're better than.
      locAndBookMove.second.costFromRoot += 0.50 * (bestOtherCostFromRoot - locAndBookMove.second.costFromRoot);
    }
  }
  {
    double winLoss = (node->pla == P_WHITE) ? node->thisValuesNotInBook.winLossValue : -node->thisValuesNotInBook.winLossValue;
    double bestOtherCostFromRoot = node->thisNodeExpansionCost + node->minCostFromRoot;
    for(auto& locAndBookMoveOther: node->moves) {
      if(locAndBookMoveOther.second.costFromRoot < bestOtherCostFromRoot) {
        const BookNode* otherChild = get(locAndBookMoveOther.second.hash);
        double winLossOther = (node->pla == P_WHITE) ? otherChild->recursiveValues.winLossValue : -otherChild->recursiveValues.winLossValue;
        // At least 1.5% better
        if(winLoss > winLossOther + 0.03)
          bestOtherCostFromRoot = locAndBookMoveOther.second.costFromRoot;
      }
    }
    if(bestOtherCostFromRoot - node->minCostFromRoot < node->thisNodeExpansionCost) {
      // Reduce 50% of cost towards the move that we're better than.
      node->thisNodeExpansionCost += 0.50 * (bestOtherCostFromRoot - node->minCostFromRoot - node->thisNodeExpansionCost);
    }
  }

  // Apply bonuses to moves now. Apply fully up to 0.75 of the cost.
  for(auto& locAndBookMove: node->moves) {
    const BookNode* child = get(locAndBookMove.second.hash);
    double winLossError = fabs(child->recursiveValues.winLossUCB - child->recursiveValues.winLossLCB) / errorFactor / 2.0;
    double scoreError = fabs(child->recursiveValues.scoreUCB - child->recursiveValues.scoreLCB) / errorFactor / 2.0;
    double sharpScoreDiscrepancy = fabs(child->recursiveValues.sharpScoreMean - child->recursiveValues.scoreMean);
    double bonus =
      bonusPerWinLossError * winLossError +
      bonusPerScoreError * scoreError +
      bonusPerSharpScoreDiscrepancy * sharpScoreDiscrepancy;
    double bonusCap1 = (locAndBookMove.second.costFromRoot - node->minCostFromRoot) * 0.75;
    if(bonus > bonusCap1)
      bonus = bonusCap1;
    locAndBookMove.second.costFromRoot -= bonus;

    if(locAndBookMove.second.isWLPV) {
      double wlPVBonusScale = (locAndBookMove.second.costFromRoot - node->minCostFromRoot);
      if(wlPVBonusScale > 0.0) {
        double factor1 = std::max(0.0, 1.0 - square(child->recursiveValues.winLossValue));
        double factor2 = 4.0 * std::max(0.0, 0.25 - square(0.5 - fabs(child->recursiveValues.winLossValue)));
        locAndBookMove.second.costFromRoot -= wlPVBonusScale * tanh(factor1 * bonusForWLPV1 + factor2 * bonusForWLPV2);
      }
    }
  }
  {
    double winLossError = node->thisValuesNotInBook.winLossError;
    double scoreError = node->thisValuesNotInBook.scoreError;
    double sharpScoreDiscrepancy = fabs(node->thisValuesNotInBook.sharpScoreMean - node->thisValuesNotInBook.scoreMean);

    // If there's an unusually large share of the policy not expanded, add a mild bonus for it.
    // For the final node expansion cost, sharp score discrepancy beyond 1 point is not capped, to encourage expanding the
    // search to better resolve the difference, since sharp score can have some weird outliers.
    double movesExpanded = node->moves.size();
    double excessUnexpandedPolicy = 0.0;
    if(movesExpanded > 0 && node->thisValuesNotInBook.maxPolicy > 1.0 / movesExpanded)
      excessUnexpandedPolicy = node->thisValuesNotInBook.maxPolicy - 1.0 / movesExpanded;
    double bonus =
      bonusPerWinLossError * winLossError +
      bonusPerScoreError * scoreError +
      bonusPerSharpScoreDiscrepancy * std::min(sharpScoreDiscrepancy, 1.0) +
      bonusPerExcessUnexpandedPolicy * excessUnexpandedPolicy;
    double bonusCap1 = node->thisNodeExpansionCost * 0.75;
    if(bonus > bonusCap1)
      bonus = bonusCap1;
    bonus += bonusPerSharpScoreDiscrepancy * std::max(0.0, sharpScoreDiscrepancy - 1.0);

    node->thisNodeExpansionCost -= bonus;

    if(node->expansionIsWLPV) {
      double wlPVBonusScale = node->thisNodeExpansionCost;
      if(wlPVBonusScale > 0.0) {
        double factor1 = std::max(0.0, 1.0 - square(node->thisValuesNotInBook.winLossValue));
        double factor2 = 4.0 * std::max(0.0, 0.25 - square(0.5 - fabs(node->thisValuesNotInBook.winLossValue)));
        node->thisNodeExpansionCost -= wlPVBonusScale * tanh(factor1 * bonusForWLPV1 + factor2 * bonusForWLPV2);
      }
    }

  }

  // cout << "Setting cost " << node->hash << " " << node->minCostFromRoot << " " << node->thisNodeExpansionCost << endl;
}

static const string HTML_TEMPLATE = R"%%(
<html>
<header>
<link rel="stylesheet" href="../book.css">
<script>
$$DATA_VARS
</script>
<script type="text/javascript" src="../book.js"></script>
</header>
<body>
</body>
</html>
)%%";


void Book::exportToHtmlDir(
  const string& dirName,
  const string& rulesLabel,
  const string& rulesLink,
  bool devMode,
  Logger& logger
) {
  MakeDir::make(dirName);
  const char* hexChars = "0123456789ABCDEF";
  for(int i = 0; i<16; i++) {
    for(int j = 0; j<16; j++) {
      MakeDir::make(dirName + "/" + hexChars[i] + hexChars[j]);
    }
  }
  MakeDir::make(dirName + "/root");
  {
    std::ofstream out;
    FileUtils::open(out, dirName + "/book.js");
    out << "const rulesLabel = \"" + rulesLabel + "\";\n";
    out << "const rulesLink = \"" + rulesLink + "\";\n";
    out << "const devMode = " + (devMode ? string("true") : string("false")) + ";\n";
    out << "const bSizeX = " + Global::intToString(initialBoard.x_size) + ";\n";
    out << "const bSizeY = " + Global::intToString(initialBoard.y_size) + ";\n";
    out << Book::BOOK_JS1;
    out << Book::BOOK_JS2;
    out << Book::BOOK_JS3;
    out.close();
  }
  {
    std::ofstream out;
    FileUtils::open(out, dirName + "/book.css");
    out << Book::BOOK_CSS;
    out.close();
  }

  char toStringBuf[1024];
  auto doubleToStringFourDigits = [&](double x) {
    std::sprintf(toStringBuf,"%.4f",x);
    return string(toStringBuf);
  };
  auto doubleToStringTwoDigits = [&](double x) {
    std::sprintf(toStringBuf,"%.2f",x);
    return string(toStringBuf);
  };
  auto doubleToStringZeroDigits = [&](double x) {
    std::sprintf(toStringBuf,"%.0f",x);
    return string(toStringBuf);
  };

  auto getFilePath = [&](BookNode* node, bool relative) {
    string path = relative ? "" : dirName + "/";
    if(node == root)
      path += "root/root";
    else {
      string hashStr = node->hash.toString();
      assert(hashStr.size() > 10);
      // Pull from the middle of the string, to avoid the fact that the hashes are
      // biased towards small hashes due to finding the minimum of the 8 symmetries.
      path += hashStr.substr(8,2) + "/" + node->hash.toString();
    }
    path += ".html";
    return path;
  };

  std::function<void(BookNode*)> f = [&](BookNode* node) {
    // Entirely omit exporting nodes that are simply leaves, to save on the number of files we have to produce and serve.
    // if(node != root && node->moves.size() == 0)
    //   return;

    string filePath = getFilePath(node, false);
    string html = HTML_TEMPLATE;
    auto replace = [&](const string& key, const string& replacement) {
      size_t pos = html.find(key);
      assert(pos != string::npos);
      html.replace(pos, key.size(), replacement);
    };

    const int symmetry = 0;
    SymBookNode symNode(node, symmetry);

    BoardHistory hist;
    vector<Loc> moveHistory;
    bool suc = symNode.getBoardHistoryReachingHere(hist,moveHistory);
    if(!suc) {
      logger.write("WARNING: Failed to get board history reaching node when trying to export to html, probably there is some bug");
      logger.write("or else some hash collision or something else is wrong.");
      logger.write("BookHash of node unable to export: " + symNode.hash().toString());
      std::ostringstream movesOut;
      for(Loc move: moveHistory)
        movesOut << Location::toString(move,initialBoard) << " ";
      logger.write("Moves:");
      logger.write(movesOut.str());
      return;
    }

    // Omit exporting nodes that are past the normal game end.
    if(hist.encorePhase > 0)
      return;

    Board board = hist.getRecentBoard(0);

    if(contains(rulesLabel,'"') || contains(rulesLabel,'\n'))
      throw StringError("rulesLabel cannot contain quotes or newlines");
    if(contains(rulesLink,'"') || contains(rulesLink,'\n'))
      throw StringError("rulesLink cannot contain quotes or newlines");
    string dataVarsStr;
    dataVarsStr += "const nextPla = " + Global::intToString(node->pla) + ";\n";
    {
      SymBookNode parent = symNode.canonicalParent();
      if(parent.isNull()) {
        dataVarsStr += "const pLink = '';\n";
        dataVarsStr += "const pSym = 0;\n";
      }
      else {
        string parentPath = getFilePath(parent.node, true);
        dataVarsStr += "const pLink = '../" + parentPath + "';\n";
        dataVarsStr += "const pSym = " + Global::intToString(parent.symmetryOfNode) + ";\n";
      }
    }
    dataVarsStr += "const board = [";
    for(int y = 0; y<board.y_size; y++) {
      for(int x = 0; x<board.x_size; x++) {
        Loc loc = Location::getLoc(x,y,board.x_size);
        dataVarsStr += Global::intToString(board.colors[loc]) + ",";
      }
    }
    dataVarsStr += "];\n";
    dataVarsStr += "const links = {";
    string linkSymmetriesStr = "const linkSyms = {";
    for(int y = 0; y<board.y_size; y++) {
      for(int x = 0; x<board.x_size; x++) {
        Loc loc = Location::getLoc(x,y,board.x_size);
        SymBookNode child = symNode.follow(loc);
        // Entirely omit linking children that are simply leaves, to save on the number of files we have to produce and serve.
        // if(!child.isNull() && child.node->moves.size() > 0) {
        if(!child.isNull()) {
          string childPath = getFilePath(child.node, true);
          dataVarsStr += Global::intToString(x+y*board.x_size) + ":'../" + childPath + "',";
          linkSymmetriesStr += Global::intToString(x+y*board.x_size) + ":" + Global::intToString(child.symmetryOfNode) + ",";
        }
      }
    }
    // Also handle pass, pass get indexed one after the legal moves
    {
      Loc loc = Board::PASS_LOC;
      // Avoid linking children that would end the phase
      if(!hist.passWouldEndPhase(board,node->pla)) {
        SymBookNode child = symNode.follow(loc);
        // Entirely omit linking children that are simply leaves, to save on the number of files we have to produce and serve.
        // if(!child.isNull() && child.node->moves.size() > 0) {
        if(!child.isNull()) {
          string childPath = getFilePath(child.node, true);
          dataVarsStr += Global::intToString(board.y_size*board.x_size) + ":'../" + childPath + "',";
          linkSymmetriesStr += Global::intToString(board.y_size*board.x_size) + ":" + Global::intToString(child.symmetryOfNode) + ",";
        }
      }
    }
    dataVarsStr += "};\n";
    linkSymmetriesStr += "};\n";
    dataVarsStr += linkSymmetriesStr;

    vector<BookMove> uniqueMovesInBook = symNode.getUniqueMovesInBook();
    vector<RecursiveBookValues> uniqueChildValues;
    vector<double> uniqueChildCosts;
    vector<double> uniqueChildCostsWLPV;
    vector<double> uniqueChildBiggestWLCost;
    vector<size_t> uniqueMoveIdxs;
    for(BookMove& bookMove: uniqueMovesInBook) {
      SymBookNode child = symNode.follow(bookMove.move);
      uniqueChildValues.push_back(child.node->recursiveValues);
      uniqueChildCosts.push_back(child.node->minCostFromRoot);
      uniqueChildCostsWLPV.push_back(child.node->minCostFromRootWLPV);
      uniqueChildBiggestWLCost.push_back(child.node->biggestWLCostFromRoot);
      uniqueMoveIdxs.push_back(uniqueMoveIdxs.size());
    }
    std::sort(
      uniqueMoveIdxs.begin(),uniqueMoveIdxs.end(),
      [&](const size_t& idx0,
          const size_t& idx1) {
        double plaFactor = node->pla == P_WHITE ? 1.0 : -1.0;
        double u0 = plaFactor * (uniqueChildValues[idx0].winLossValue + uniqueChildValues[idx0].sharpScoreMean * utilityPerScore * 0.5)
        + plaFactor * (node->pla == P_WHITE ? uniqueChildValues[idx0].scoreLCB : uniqueChildValues[idx0].scoreUCB) * 0.5 * utilityPerScore
        + utilityPerPolicyForSorting * (0.75 * uniqueMovesInBook[idx0].rawPolicy + 0.5 * log10(uniqueMovesInBook[idx0].rawPolicy + 0.0001)/4.0);
        double u1 = plaFactor * (uniqueChildValues[idx1].winLossValue + uniqueChildValues[idx1].sharpScoreMean * utilityPerScore * 0.5)
        + plaFactor * (node->pla == P_WHITE ? uniqueChildValues[idx1].scoreLCB : uniqueChildValues[idx1].scoreUCB) * 0.5 * utilityPerScore
        + utilityPerPolicyForSorting * (0.75 * uniqueMovesInBook[idx1].rawPolicy + 0.5 * log10(uniqueMovesInBook[idx1].rawPolicy + 0.0001)/4.0);
        return u0 > u1;
      }
    );

    vector<int> equivalentSymmetries = symNode.getSymmetries();
    std::set<Loc> locsHandled;

    dataVarsStr += "const moves = [";
    for(int idx: uniqueMoveIdxs) {
      dataVarsStr += "{";
      const Loc passLoc = Board::PASS_LOC;
      if(uniqueMovesInBook[idx].move != passLoc) {
        dataVarsStr += "'xy':[";
        for(int s: equivalentSymmetries) {
          Loc symMove = SymmetryHelpers::getSymLoc(uniqueMovesInBook[idx].move,initialBoard,s);
          if(contains(locsHandled, symMove))
            continue;
          locsHandled.insert(symMove);
          dataVarsStr += "[";
          dataVarsStr += Global::intToString(Location::getX(symMove, initialBoard.x_size));
          dataVarsStr += ",";
          dataVarsStr += Global::intToString(Location::getY(symMove, initialBoard.x_size));
          dataVarsStr += "],";
        }
        dataVarsStr += "],";
      }
      if(uniqueMovesInBook[idx].move == passLoc)
        dataVarsStr += "'move':'" + Location::toString(uniqueMovesInBook[idx].move, initialBoard) + "',";
      dataVarsStr += "'p':" + doubleToStringFourDigits(uniqueMovesInBook[idx].rawPolicy) + ",";
      dataVarsStr += "'wl':" + doubleToStringFourDigits(uniqueChildValues[idx].winLossValue) + ",";
      if(devMode) {
        dataVarsStr += "'wlUCB':" + doubleToStringFourDigits(uniqueChildValues[idx].winLossUCB) + ",";
        dataVarsStr += "'wlLCB':" + doubleToStringFourDigits(uniqueChildValues[idx].winLossLCB) + ",";
        dataVarsStr += "'sM':" + doubleToStringTwoDigits(uniqueChildValues[idx].scoreMean) + ",";
        dataVarsStr += "'ssM':" + doubleToStringTwoDigits(uniqueChildValues[idx].sharpScoreMean) + ",";
        dataVarsStr += "'sUCB':" + doubleToStringTwoDigits(uniqueChildValues[idx].scoreUCB) + ",";
        dataVarsStr += "'sLCB':" + doubleToStringTwoDigits(uniqueChildValues[idx].scoreLCB) + ",";
        //dataVarsStr += "'w':" + doubleToStringZeroDigits(uniqueChildValues[idx].weight) + ",";
        dataVarsStr += "'v':" + doubleToStringZeroDigits(uniqueChildValues[idx].visits) + ",";
        dataVarsStr += "'cost':" + doubleToStringFourDigits(uniqueMovesInBook[idx].costFromRoot - node->minCostFromRoot) + ",";
        dataVarsStr += "'costRoot':" + doubleToStringFourDigits(uniqueChildCosts[idx]) + ",";
        dataVarsStr += "'costWLPV':" + doubleToStringFourDigits(uniqueChildCostsWLPV[idx]) + ",";
        dataVarsStr += "'bigWLC':" + doubleToStringFourDigits(uniqueChildBiggestWLCost[idx]) + ",";
      }
      else {
        dataVarsStr += "'ssM':" + doubleToStringTwoDigits(uniqueChildValues[idx].sharpScoreMean) + ",";
        dataVarsStr += "'wlRad':" + doubleToStringFourDigits(0.5*(uniqueChildValues[idx].winLossUCB - uniqueChildValues[idx].winLossLCB)) + ",";
        dataVarsStr += "'sRad':" + doubleToStringTwoDigits(0.5*(uniqueChildValues[idx].scoreUCB - uniqueChildValues[idx].scoreLCB)) + ",";
        dataVarsStr += "'v':" + doubleToStringZeroDigits(uniqueChildValues[idx].visits) + ",";
      }
      dataVarsStr += "},";
    }
    {
      BookValues& values = node->thisValuesNotInBook;
      if(values.maxPolicy > 0.0) {
        double scoreError = values.getAdjustedScoreError(node->book->initialRules);
        double winLossValueUCB = values.winLossValue + errorFactor * values.winLossError;
        double winLossValueLCB = values.winLossValue - errorFactor * values.winLossError;
        double scoreUCB = values.scoreMean + errorFactor * scoreError;
        double scoreLCB = values.scoreMean - errorFactor * scoreError;
        // double scoreFinalUCB = values.scoreMean + errorFactor * values.scoreStdev;
        // double scoreFinalLCB = values.scoreMean - errorFactor * values.scoreStdev;

        // A quick hack to limit the issue of outliers from sharpScore, and adjust the LCB/UCB to reflect the uncertainty
        double scoreMean = values.scoreMean;
        double sharpScoreMean = values.sharpScoreMean;
        if(sharpScoreMean > scoreUCB)
          scoreUCB = sharpScoreMean;
        if(sharpScoreMean < scoreLCB)
          scoreLCB = sharpScoreMean;
        if(sharpScoreMean > scoreMean + sharpScoreOutlierCap)
          sharpScoreMean = scoreMean + sharpScoreOutlierCap;
        if(sharpScoreMean < scoreMean - sharpScoreOutlierCap)
          sharpScoreMean = scoreMean - sharpScoreOutlierCap;

        dataVarsStr += "{";
        dataVarsStr += "'move':'other',";
        dataVarsStr += "'p':" + doubleToStringFourDigits(values.maxPolicy) + ",";
        dataVarsStr += "'wl':" + doubleToStringFourDigits(values.winLossValue) + ",";
        if(devMode) {
          dataVarsStr += "'wlUCB':" + doubleToStringFourDigits(winLossValueUCB) + ",";
          dataVarsStr += "'wlLCB':" + doubleToStringFourDigits(winLossValueLCB) + ",";
          dataVarsStr += "'sM':" + doubleToStringTwoDigits(scoreMean) + ",";
          dataVarsStr += "'ssM':" + doubleToStringTwoDigits(sharpScoreMean) + ",";
          dataVarsStr += "'sUCB':" + doubleToStringTwoDigits(scoreUCB) + ",";
          dataVarsStr += "'sLCB':" + doubleToStringTwoDigits(scoreLCB) + ",";
          dataVarsStr += "'w':" + doubleToStringZeroDigits(values.weight) + ",";
          dataVarsStr += "'v':" + doubleToStringZeroDigits(values.visits) + ",";
          dataVarsStr += "'cost':" + doubleToStringFourDigits(node->thisNodeExpansionCost) + ",";
          dataVarsStr += "'costRoot':" + doubleToStringFourDigits(node->minCostFromRoot + node->thisNodeExpansionCost) + ",";
          dataVarsStr += "'costWLPV':" + doubleToStringFourDigits(node->expansionIsWLPV ? node->minCostFromRootWLPV : node->minCostFromRoot + node->thisNodeExpansionCost) + ",";
          dataVarsStr += "'bigWLC':" + doubleToStringFourDigits(node->biggestWLCostFromRoot) + ",";
        }
        else {
          dataVarsStr += "'ssM':" + doubleToStringTwoDigits(sharpScoreMean) + ",";
          dataVarsStr += "'wlRad':" + doubleToStringFourDigits(0.5*(winLossValueUCB-winLossValueLCB)) + ",";
          dataVarsStr += "'sRad':" + doubleToStringTwoDigits(0.5*(scoreUCB-scoreLCB)) + ",";
          dataVarsStr += "'v':" + doubleToStringZeroDigits(values.visits) + ",";
        }
        dataVarsStr += "},";
      }
    }
    dataVarsStr += "];\n";

    replace("$$DATA_VARS",dataVarsStr);

    std::ofstream out;
    FileUtils::open(out, filePath);
    out << html;
    out.close();
  };
  iterateEntireBookPreOrder(f);
}

static const int SAVE_FILE_VERSION = 1;
static const char BOARD_LINE_DELIMITER = '|';

void Book::saveToFile(const string& fileName) const {
  string tmpFileName = fileName + ".tmp";
  std::ofstream out;
  FileUtils::open(out, tmpFileName);

  {
    json params;
    params["version"] = SAVE_FILE_VERSION;
    params["initialBoard"] = Board::toJson(initialBoard);
    params["initialRules"] = initialRules.toJson();
    params["initialPla"] = PlayerIO::playerToString(initialPla);
    params["repBound"] = repBound;
    params["errorFactor"] = errorFactor;
    params["costPerMove"] = costPerMove;
    params["costPerUCBWinLossLoss"] = costPerUCBWinLossLoss;
    params["costPerUCBWinLossLossPow3"] = costPerUCBWinLossLossPow3;
    params["costPerUCBWinLossLossPow7"] = costPerUCBWinLossLossPow7;
    params["costPerUCBScoreLoss"] = costPerUCBScoreLoss;
    params["costPerLogPolicy"] = costPerLogPolicy;
    params["costPerMovesExpanded"] = costPerMovesExpanded;
    params["costPerSquaredMovesExpanded"] = costPerSquaredMovesExpanded;
    params["costWhenPassFavored"] = costWhenPassFavored;
    params["bonusPerWinLossError"] = bonusPerWinLossError;
    params["bonusPerScoreError"] = bonusPerScoreError;
    params["bonusPerSharpScoreDiscrepancy"] = bonusPerSharpScoreDiscrepancy;
    params["bonusPerExcessUnexpandedPolicy"] = bonusPerExcessUnexpandedPolicy;
    params["bonusForWLPV1"] = bonusForWLPV1;
    params["bonusForWLPV2"] = bonusForWLPV2;
    params["bonusForBiggestWLCost"] = bonusForBiggestWLCost;
    params["scoreLossCap"] = scoreLossCap;
    params["utilityPerScore"] = utilityPerScore;
    params["policyBoostSoftUtilityScale"] = policyBoostSoftUtilityScale;
    params["utilityPerPolicyForSorting"] = utilityPerPolicyForSorting;
    params["initialSymmetry"] = initialSymmetry;
    out << params.dump() << endl;
  }

  for(BookNode* node: nodes) {
    json nodeData = json::object();
    nodeData["hash"] = node->hash.toString();
    nodeData["pla"] = PlayerIO::playerToString(node->pla);
    nodeData["symmetries"] = node->symmetries;
    nodeData["winLossValue"] = node->thisValuesNotInBook.winLossValue;
    nodeData["scoreMean"] = node->thisValuesNotInBook.scoreMean;
    nodeData["sharpScoreMean"] = node->thisValuesNotInBook.sharpScoreMean;
    nodeData["winLossError"] = node->thisValuesNotInBook.winLossError;
    nodeData["scoreError"] = node->thisValuesNotInBook.scoreError;
    nodeData["scoreStdev"] = node->thisValuesNotInBook.scoreStdev;
    nodeData["maxPolicy"] = node->thisValuesNotInBook.maxPolicy;
    nodeData["weight"] = node->thisValuesNotInBook.weight;
    nodeData["visits"] = node->thisValuesNotInBook.visits;
    nodeData["canExpand"] = node->canExpand;

    nodeData["moves"] = json::array();
    for(auto& locAndBookMove: node->moves) {
      json moveData;
      moveData["move"] = Location::toString(locAndBookMove.second.move,initialBoard);
      moveData["symmetryToAlign"] = locAndBookMove.second.symmetryToAlign;
      moveData["hash"] = locAndBookMove.second.hash.toString();
      moveData["rawPolicy"] = locAndBookMove.second.rawPolicy;
      nodeData["moves"].push_back(moveData);
    }
    nodeData["parents"] = json::array();
    for(auto& hashAndLoc: node->parents) {
      json parentData;
      parentData["hash"] = hashAndLoc.first.toString();
      parentData["loc"] = Location::toString(hashAndLoc.second,initialBoard);
      nodeData["parents"].push_back(parentData);
    }
    out << nodeData << endl;
  }
  out.close();

  // Just in case, avoid any possible racing for file system
  std::this_thread::sleep_for(std::chrono::duration<double>(1));
  FileUtils::rename(tmpFileName,fileName);
}

Book* Book::loadFromFile(const std::string& fileName, double sharpScoreOutlierCap) {
  std::ifstream in;
  FileUtils::open(in, fileName);
  std::string line;
  Book* ret = NULL;
  try {
    getline(in,line);
    if(!in)
      throw IOError("Could not load initial metadata line");
    auto assertContains = [&](const json& data, const string& key) {
      if(!data.contains(key))
        throw IOError("Could not parse json or find expected key " + key);
    };

    std::unique_ptr<Book> book;
    {
      json params = json::parse(line);
      assertContains(params,"version");
      int version = params["version"].get<int>();
      if(version != 1)
        throw IOError("Unsupported book version: " + Global::intToString(version));

      assertContains(params,"initialBoard");
      Board initialBoard = Board::ofJson(params["initialBoard"]);
      assertContains(params,"initialRules");
      Rules initialRules = Rules::parseRules(params["initialRules"].dump());
      Player initialPla = PlayerIO::parsePlayer(params["initialPla"].get<string>());
      int repBound = params["repBound"].get<int>();
      double errorFactor = params["errorFactor"].get<double>();
      double costPerMove = params["costPerMove"].get<double>();
      double costPerUCBWinLossLoss = params["costPerUCBWinLossLoss"].get<double>();
      double costPerUCBWinLossLossPow3 = params.contains("costPerUCBWinLossLossPow3") ? params["costPerUCBWinLossLossPow3"].get<double>() : 0.0;
      double costPerUCBWinLossLossPow7 = params.contains("costPerUCBWinLossLossPow7") ? params["costPerUCBWinLossLossPow7"].get<double>() : 0.0;
      double costPerUCBScoreLoss = params["costPerUCBScoreLoss"].get<double>();
      double costPerLogPolicy = params["costPerLogPolicy"].get<double>();
      double costPerMovesExpanded = params["costPerMovesExpanded"].get<double>();
      double costPerSquaredMovesExpanded = params["costPerSquaredMovesExpanded"].get<double>();
      double costWhenPassFavored = params["costWhenPassFavored"].get<double>();
      double bonusPerWinLossError = params["bonusPerWinLossError"].get<double>();
      double bonusPerScoreError = params.contains("bonusPerScoreError") ? params["bonusPerScoreError"].get<double>() : 0.0;
      double bonusPerSharpScoreDiscrepancy = params.contains("bonusPerSharpScoreDiscrepancy") ? params["bonusPerSharpScoreDiscrepancy"].get<double>() : 0.0;
      double bonusPerExcessUnexpandedPolicy = params.contains("bonusPerExcessUnexpandedPolicy") ? params["bonusPerExcessUnexpandedPolicy"].get<double>() : 0.0;
      double bonusForWLPV1 = params.contains("bonusForWLPV1") ? params["bonusForWLPV1"].get<double>() : 0.0;
      double bonusForWLPV2 = params.contains("bonusForWLPV2") ? params["bonusForWLPV2"].get<double>() : 0.0;
      double bonusForBiggestWLCost = params.contains("bonusForBiggestWLCost") ? params["bonusForBiggestWLCost"].get<double>() : 0.0;
      double scoreLossCap = params["scoreLossCap"].get<double>();
      double utilityPerScore = params["utilityPerScore"].get<double>();
      double policyBoostSoftUtilityScale = params["policyBoostSoftUtilityScale"].get<double>();
      double utilityPerPolicyForSorting = params["utilityPerPolicyForSorting"].get<double>();

      book = std::make_unique<Book>(
        initialBoard,
        initialRules,
        initialPla,
        repBound,
        errorFactor,
        costPerMove,
        costPerUCBWinLossLoss,
        costPerUCBWinLossLossPow3,
        costPerUCBWinLossLossPow7,
        costPerUCBScoreLoss,
        costPerLogPolicy,
        costPerMovesExpanded,
        costPerSquaredMovesExpanded,
        costWhenPassFavored,
        bonusPerWinLossError,
        bonusPerScoreError,
        bonusPerSharpScoreDiscrepancy,
        bonusPerExcessUnexpandedPolicy,
        bonusForWLPV1,
        bonusForWLPV2,
        bonusForBiggestWLCost,
        scoreLossCap,
        utilityPerScore,
        policyBoostSoftUtilityScale,
        utilityPerPolicyForSorting,
        sharpScoreOutlierCap
      );

      int initialSymmetry = params["initialSymmetry"].get<int>();
      if(book->initialSymmetry != initialSymmetry)
        throw IOError("Inconsistent initial symmetry with initialization");
    }

    while(getline(in,line)) {
      if(line.size() <= 0)
        break;
      json nodeData = json::parse(line);
      BookHash hash = BookHash::ofString(nodeData["hash"].get<string>());
      Player pla = PlayerIO::parsePlayer(nodeData["pla"].get<string>());
      vector<int> symmetries = nodeData["symmetries"].get<vector<int>>();

      BookNode* node = book->get(hash);
      if(node != NULL) {
        if(node->hash != hash) throw IOError("Inconsistent hash for root node with initialization");
        if(node->pla != pla) throw IOError("Inconsistent pla for root node with initialization");
        if(node->symmetries != symmetries) throw IOError("Inconsistent symmetries for root node with initialization");
      }
      else {
        node = new BookNode(hash, book.get(), pla, symmetries);
        book->add(hash, node);
      }

      node->thisValuesNotInBook.winLossValue = nodeData["winLossValue"].get<double>();
      node->thisValuesNotInBook.scoreMean = nodeData["scoreMean"].get<double>();
      node->thisValuesNotInBook.sharpScoreMean = nodeData["sharpScoreMean"].get<double>();
      node->thisValuesNotInBook.winLossError = nodeData["winLossError"].get<double>();
      node->thisValuesNotInBook.scoreError = nodeData["scoreError"].get<double>();
      node->thisValuesNotInBook.scoreStdev = nodeData["scoreStdev"].get<double>();
      node->thisValuesNotInBook.maxPolicy = nodeData["maxPolicy"].get<double>();
      node->thisValuesNotInBook.weight = nodeData["weight"].get<double>();
      node->thisValuesNotInBook.visits = nodeData["visits"].get<double>();
      node->canExpand = true; // nodeData["canExpand"].get<bool>(); //TODO

      for(json& moveData: nodeData["moves"]) {
        BookMove move;
        move.move = Location::ofString(moveData["move"].get<string>(),book->initialBoard);
        move.symmetryToAlign = moveData["symmetryToAlign"].get<int>();
        move.hash = BookHash::ofString(moveData["hash"].get<string>());
        move.rawPolicy = moveData["rawPolicy"].get<double>();
        node->moves[move.move] = move;
      }

      for(json& parentData: nodeData["parents"]) {
        BookHash parentHash = BookHash::ofString(parentData["hash"].get<string>());
        Loc loc = Location::ofString(parentData["loc"].get<string>(),book->initialBoard);
        node->parents.push_back(std::make_pair(parentHash,loc));
      }
    }
    book->recomputeEverything();
    ret = book.release();
  }
  catch(const std::exception& e) {
    throw IOError("When parsing book file " + fileName + ": " + e.what() + "\nFurthest line read was:\n" + line.substr(0,10000));
  }
  return ret;
}


