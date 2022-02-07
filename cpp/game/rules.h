#ifndef GAME_RULES_H_
#define GAME_RULES_H_

#include "../core/global.h"
#include "../core/hash.h"

#include "../external/nlohmann_json/json.hpp"

struct Rules {

  // Attax ends when no moves to go
  static const int END_ATTAX_STANDARD = 0;
  // Gomoku ends when full board or fix in a row
  static const int END_GOMOKU_STANDARD = 1;
  int endRule;

  // Attax scores according to stone num
  static const int SCORING_ATTAX_STANDARD = 0;
  // Gpmoku scores according to who win
  static const int SCORING_GOMOKU_STANDARD = 1;
  int scoringRule;

  Rules();
  Rules(
    int eRule,
    int sRule
  );
  ~Rules();

  bool operator==(const Rules& other) const;
  bool operator!=(const Rules& other) const;

  static Rules getStanddard();

  static std::set<std::string> endRuleStrings();
  static int parseEndRule(const std::string& s);
  static std::string writeEndRule(int endRule);
  static std::set<std::string> scoringRuleStrings();
  static int parseScoringRule(const std::string& s);
  static std::string writeScoringRule(int scoringRule);

  bool equals(const Rules& other) const;

  static Rules parseRules(const std::string& str);
  static bool tryParseRules(const std::string& str, Rules& buf);

  static Rules updateRules(const std::string& key, const std::string& value, Rules priorRules);

  friend std::ostream& operator<<(std::ostream& out, const Rules& rules);
  std::string toString() const;
  std::string toStringMaybeNice() const;
  std::string toJsonString() const;
  nlohmann::json toJson() const;

  static const Hash128 ZOBRIST_KO_RULE_HASH[4];
  static const Hash128 ZOBRIST_SCORING_RULE_HASH[2];
  static const Hash128 ZOBRIST_TAX_RULE_HASH[3];
  static const Hash128 ZOBRIST_MULTI_STONE_SUICIDE_HASH;
  static const Hash128 ZOBRIST_BUTTON_HASH;

private:
  nlohmann::json toJsonHelper() const;
};

#endif  // GAME_RULES_H_
