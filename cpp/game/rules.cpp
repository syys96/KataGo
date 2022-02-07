#include "../game/rules.h"

#include "../external/nlohmann_json/json.hpp"

#include <sstream>

using namespace std;
using json = nlohmann::json;

Rules::Rules() {
  //Defaults to Attax rules
  gameRule = GAME_ATTAX_STANDARD;
}

Rules::Rules(
  GameRule gRule
)
  :gameRule(gRule)
{}

Rules::~Rules() = default;

bool Rules::operator==(const Rules& other) const {
  return
    gameRule == other.gameRule;
}

bool Rules::operator!=(const Rules& other) const {
  return
    gameRule != other.gameRule;
}


Rules Rules::getStanddard() {
    Rules rules;
    rules.gameRule = GAME_ATTAX_STANDARD;
    return rules;
}


set<string> Rules::gameRuleStrings() {
  return {"GAME_ATTAX_STANDARD, GAME_GOMOKU_STANDARD"};
}

Rules::GameRule Rules::parseGameRule(const string& s) {
  if(s == "GAME_ATTAX_STANDARD") return Rules::GAME_ATTAX_STANDARD;
  else if(s == "GAME_GOMOKU_STANDARD") return Rules::GAME_GOMOKU_STANDARD;
  else throw IOError("Rules::parseGameRule: Invalid end rule: " + s);
}

string Rules::writeGameRule(GameRule endRule) {
  if(endRule == Rules::GAME_ATTAX_STANDARD) return string("GAME_ATTAX_STANDARD");
  else if(endRule == Rules::GAME_GOMOKU_STANDARD) return string("GAME_GOMOKU_STANDARD");
  return string("UNKNOWN");
}

ostream& operator<<(ostream& out, const Rules& rules) {
  out << "game" << Rules::writeGameRule(rules.gameRule);
  return out;
}


string Rules::toString() const {
  ostringstream out;
  out << (*this);
  return out.str();
}

json Rules::toJsonHelper() const {
  json ret;
  ret["game"] = writeGameRule(gameRule);
  return ret;
}

json Rules::toJson() const {
  return toJsonHelper();
}

string Rules::toJsonString() const {
  return toJsonHelper().dump();
}

Rules Rules::updateRules(const string& k, const string& v, Rules oldRules) {
  Rules rules = oldRules;
  string key = Global::trim(k);
  string value = Global::trim(Global::toUpper(v));
  if(key == "game") rules.gameRule = Rules::parseGameRule(value);
  else throw IOError("Unknown rules option: " + key);
  return rules;
}

static Rules parseRulesHelper(const string& sOrig) {
  Rules rules;
  string lowercased = Global::trim(Global::toLower(sOrig));
  if(lowercased == "attax_standard") {
    rules.gameRule = Rules::GAME_ATTAX_STANDARD;
  }
  else if (lowercased == "gomoku_standard") {
      rules.gameRule = Rules::GAME_GOMOKU_STANDARD;
  }
  else if(sOrig.length() > 0 && sOrig[0] == '{') {
    //Default if not specified
    rules = Rules::getStanddard();
    try {
      json input = json::parse(sOrig);
      string s;
      for(json::iterator iter = input.begin(); iter != input.end(); ++iter) {
        const string key = iter.key();
        if(key == "game")
          rules.gameRule = Rules::parseGameRule(iter.value().get<string>());
        else
          throw IOError("Unknown rules option: " + key);
      }
    }
    catch(nlohmann::detail::exception&) {
      throw IOError("Could not parse rules: " + sOrig);
    }
  }

  //This is more of a legacy internal format, not recommended for users to provide
  else {
    auto startsWithAndStrip = [](string& str, const string& prefix) {
      bool matches = str.length() >= prefix.length() && str.substr(0,prefix.length()) == prefix;
      if(matches)
        str = str.substr(prefix.length());
      str = Global::trim(str);
      return matches;
    };

    //Default if not specified
    rules = Rules::getStanddard();

    string s = sOrig;
    s = Global::trim(s);

    //But don't allow the empty string
    if(s.length() <= 0)
      throw IOError("Could not parse rules: " + sOrig);

    while(true) {
      if(s.length() <= 0)
        break;
      if(startsWithAndStrip(s,"game")) {
        if(startsWithAndStrip(s,"attax_standard")) rules.gameRule = Rules::GAME_ATTAX_STANDARD;
        else if(startsWithAndStrip(s,"gomoku_standard")) rules.gameRule = Rules::GAME_GOMOKU_STANDARD;
        else throw IOError("Could not parse rules: " + sOrig);
        continue;
      }
      //Unknown rules format
      else throw IOError("Could not parse rules: " + sOrig);
    }
  }

  return rules;
}

Rules Rules::parseRules(const string& sOrig) {
  return parseRulesHelper(sOrig);
}

bool Rules::tryParseRules(const string& sOrig, Rules& buf) {
  Rules rules;
  try { rules = parseRulesHelper(sOrig); }
  catch(const StringError&) { return false; }
  buf = rules;
  return true;
}

bool Rules::equals(const Rules& other) const {
    return gameRule == other.gameRule;
}

string Rules::toStringMaybeNice() const {
  if(equals(parseRulesHelper("attax_standard")))
    return "ATTAX_STANDARD";
  if(equals(parseRulesHelper("gomoku_standard")))
    return "GOMOKU_STANDARD";
  return toString();
}


const Hash128 Rules::ZOBRIST_GAME_RULE_HASH[2] = {
  Hash128(0xc60041f6e7c499b0ULL, 0x0fe1857baf3536ebULL),  //Based on sha256 hash of Rules::GAME_ATTAX_STANDARD
  Hash128(0x6de09fdf2b079b93ULL, 0x8b23ae2f5b10a313ULL),  //Based on sha256 hash of Rules::GAME_GOMOKU_STANDARD
};
