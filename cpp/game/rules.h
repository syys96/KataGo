#ifndef GAME_RULES_H_
#define GAME_RULES_H_

#include "../core/global.h"
#include "../core/hash.h"

#include "../external/nlohmann_json/json.hpp"

struct Rules {

  enum GameRule{GAME_ATTAX_STANDARD = 0, GAME_GOMOKU_STANDARD = 1};
  GameRule gameRule;

  Rules();
  Rules(
    GameRule gRule
  );
  ~Rules();

  bool operator==(const Rules& other) const;
  bool operator!=(const Rules& other) const;

  static Rules getStanddard();

  static std::set<std::string> gameRuleStrings();
  static GameRule parseGameRule(const std::string& s);
  static std::string writeGameRule(GameRule endRule);

  bool equals(const Rules& other) const;

  static Rules parseRules(const std::string& str);
  static bool tryParseRules(const std::string& str, Rules& buf);

  static Rules updateRules(const std::string& key, const std::string& value, Rules priorRules);

  friend std::ostream& operator<<(std::ostream& out, const Rules& rules);
  std::string toString() const;
  std::string toStringMaybeNice() const;
  std::string toJsonString() const;
  nlohmann::json toJson() const;

  static const Hash128 ZOBRIST_GAME_RULE_HASH[2];

private:
  nlohmann::json toJsonHelper() const;
};

#endif  // GAME_RULES_H_
