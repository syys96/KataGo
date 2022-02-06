#ifndef GAME_RULES_H_
#define GAME_RULES_H_

#include "../core/global.h"
#include "../core/hash.h"

#include "../external/nlohmann_json/json.hpp"

struct Rules {

  static const int END_NONE = 0;
  static const int END_STANDARD = 1;
  int endRule;

  static const int SCOREING_NONE = 0;
  static const int SCOREING_NUM = 1;
  int scoringRule;

  float komi;
  //Min and max acceptable komi in various places involving user input validation
  static constexpr float MIN_USER_KOMI = -150.0f;
  static constexpr float MAX_USER_KOMI = 150.0f;

  Rules();
  Rules(
    int eRule,
    int sRule,
    float komi
  );
  ~Rules();

  bool operator==(const Rules& other) const;
  bool operator!=(const Rules& other) const;

  bool equalsIgnoringKomi(const Rules& other) const;
  bool gameResultWillBeInteger() const;

  static Rules getStanddard();

  static std::set<std::string> endRuleStrings();
  static int parseEndRule(const std::string& s);
  static std::string writeEndRule(int koRule);

  static bool komiIsIntOrHalfInt(float komi);

  static Rules parseRules(const std::string& str);
  static Rules parseRulesWithoutKomi(const std::string& str, float komi);
  static bool tryParseRules(const std::string& str, Rules& buf);
  static bool tryParseRulesWithoutKomi(const std::string& str, Rules& buf, float komi);

  static Rules updateRules(const std::string& key, const std::string& value, Rules priorRules);

  friend std::ostream& operator<<(std::ostream& out, const Rules& rules);
  std::string toString() const;
  std::string toStringNoKomi() const;
  std::string toStringNoKomiMaybeNice() const;
  std::string toJsonString() const;
  std::string toJsonStringNoKomi() const;
  std::string toJsonStringNoKomiMaybeOmitStuff() const;
  nlohmann::json toJson() const;
  nlohmann::json toJsonNoKomi() const;
  nlohmann::json toJsonNoKomiMaybeOmitStuff() const;

  static const Hash128 ZOBRIST_KO_RULE_HASH[4];
  static const Hash128 ZOBRIST_SCORING_RULE_HASH[2];
  static const Hash128 ZOBRIST_TAX_RULE_HASH[3];
  static const Hash128 ZOBRIST_MULTI_STONE_SUICIDE_HASH;
  static const Hash128 ZOBRIST_BUTTON_HASH;

private:
  nlohmann::json toJsonHelper(bool omitKomi, bool omitDefaults) const;
};

#endif  // GAME_RULES_H_
