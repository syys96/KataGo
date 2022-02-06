#include "../game/rules.h"

#include "../external/nlohmann_json/json.hpp"

#include <sstream>

using namespace std;
using json = nlohmann::json;

Rules::Rules() {
  //Defaults if not set - closest match to TT rules
  endRule = END_STANDARD;
  komi = 0.0f;
}

Rules::Rules(
  int eRule,
  int sRule,
  float km
)
  :endRule(eRule),
   scoringRule(sRule),
   komi(km)
{}

Rules::~Rules() {
}

bool Rules::operator==(const Rules& other) const {
  return
    endRule == other.endRule &&
    scoringRule == other.scoringRule &&
    komi == other.komi;
}

bool Rules::operator!=(const Rules& other) const {
  return
    endRule != other.endRule ||
    scoringRule != other.scoringRule ||
    komi != other.komi;
}

bool Rules::equalsIgnoringKomi(const Rules& other) const {
  return
    endRule == other.endRule;
}

bool Rules::gameResultWillBeInteger() const {
  bool komiIsInteger = ((int)komi) == komi;
  return komiIsInteger;
}

Rules Rules::getStanddard() {
    Rules rules;
    rules.endRule = END_STANDARD;
    rules.komi = 0.0f;
    return rules;
}


bool Rules::komiIsIntOrHalfInt(float komi) {
  return std::isfinite(komi) && komi * 2 == (int)(komi * 2);
}

set<string> Rules::endRuleStrings() {
  return {"STANDARD"};
}

int Rules::parseEndRule(const string& s) {
  if(s == "STANDARD") return Rules::END_STANDARD;
  else throw IOError("Rules::parseEndRule: Invalid end rule: " + s);
}

string Rules::writeEndRule(int koRule) {
  if(koRule == Rules::END_STANDARD) return string("STANDARD");
  return string("UNKNOWN");
}

ostream& operator<<(ostream& out, const Rules& rules) {
  out << "end" << Rules::writeEndRule(rules.endRule);
  out << "komi" << rules.komi;
  return out;
}

string Rules::toStringNoKomi() const {
  ostringstream out;
  out << "end" << Rules::writeEndRule(endRule);
  return out.str();
}

string Rules::toString() const {
  ostringstream out;
  out << (*this);
  return out.str();
}

//omitDefaults: Takes up a lot of string space to include stuff, so omit some less common things if matches tromp-taylor rules
//which is the default for parsing and if not otherwise specified
json Rules::toJsonHelper(bool omitKomi, bool omitDefaults) const {
  json ret;
  ret["end"] = writeEndRule(endRule);
  if(!omitKomi)
    ret["komi"] = komi;
  return ret;
}

json Rules::toJson() const {
  return toJsonHelper(false,false);
}

json Rules::toJsonNoKomi() const {
  return toJsonHelper(true,false);
}

json Rules::toJsonNoKomiMaybeOmitStuff() const {
  return toJsonHelper(true,true);
}

string Rules::toJsonString() const {
  return toJsonHelper(false,false).dump();
}

string Rules::toJsonStringNoKomi() const {
  return toJsonHelper(true,false).dump();
}

string Rules::toJsonStringNoKomiMaybeOmitStuff() const {
  return toJsonHelper(true,true).dump();
}

Rules Rules::updateRules(const string& k, const string& v, Rules oldRules) {
  Rules rules = oldRules;
  string key = Global::trim(k);
  string value = Global::trim(Global::toUpper(v));
  if(key == "end") rules.endRule = Rules::parseEndRule(value);
  else throw IOError("Unknown rules option: " + key);
  return rules;
}

static Rules parseRulesHelper(const string& sOrig, bool allowKomi) {
  Rules rules;
  string lowercased = Global::trim(Global::toLower(sOrig));
  if(lowercased == "standard") {
    rules.endRule = Rules::END_STANDARD;
    rules.komi = 0.0f;
  }
  else if(sOrig.length() > 0 && sOrig[0] == '{') {
    //Default if not specified
    rules = Rules::getStanddard();
    bool komiSpecified = false;
    try {
      json input = json::parse(sOrig);
      string s;
      for(json::iterator iter = input.begin(); iter != input.end(); ++iter) {
        string key = iter.key();
        if(key == "end")
          rules.endRule = Rules::parseEndRule(iter.value().get<string>());
        else if(key == "komi") {
          if(!allowKomi)
            throw IOError("Unknown rules option: " + key);
          rules.komi = iter.value().get<float>();
          komiSpecified = true;
          if(rules.komi < Rules::MIN_USER_KOMI || rules.komi > Rules::MAX_USER_KOMI || !Rules::komiIsIntOrHalfInt(rules.komi))
            throw IOError("Komi value is not a half-integer or is too extreme");
        }
        else
          throw IOError("Unknown rules option: " + key);
      }
    }
    catch(nlohmann::detail::exception&) {
      throw IOError("Could not parse rules: " + sOrig);
    }
    if(!komiSpecified) {
      //Drop default komi to 0.0
      rules.komi = 0.0f;
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

    bool komiSpecified = false;
    while(true) {
      if(s.length() <= 0)
        break;

      if(startsWithAndStrip(s,"komi")) {
        if(!allowKomi)
          throw IOError("Could not parse rules: " + sOrig);
        int endIdx = 0;
        while(endIdx < s.length() && !Global::isAlpha(s[endIdx] && !Global::isWhitespace(s[endIdx])))
          endIdx++;
        float komi;
        bool suc = Global::tryStringToFloat(s.substr(0,endIdx),komi);
        if(!suc)
          throw IOError("Could not parse rules: " + sOrig);
        if(!std::isfinite(komi) || komi > 1e5 || komi < -1e5)
          throw IOError("Could not parse rules: " + sOrig);
        rules.komi = komi;
        komiSpecified = true;
        s = s.substr(endIdx);
        s = Global::trim(s);
        continue;
      }
      if(startsWithAndStrip(s,"end")) {
        if(startsWithAndStrip(s,"STANDARD")) rules.endRule = Rules::END_STANDARD;
        else throw IOError("Could not parse rules: " + sOrig);
        continue;
      }
      //Unknown rules format
      else throw IOError("Could not parse rules: " + sOrig);
    }
    if(!komiSpecified) {
      //Drop default komi to 0.0
      rules.komi = 0.0f;
    }
  }

  return rules;
}

Rules Rules::parseRules(const string& sOrig) {
  return parseRulesHelper(sOrig,true);
}
Rules Rules::parseRulesWithoutKomi(const string& sOrig, float komi) {
  Rules rules = parseRulesHelper(sOrig,false);
  rules.komi = komi;
  return rules;
}

bool Rules::tryParseRules(const string& sOrig, Rules& buf) {
  Rules rules;
  try { rules = parseRulesHelper(sOrig,true); }
  catch(const StringError&) { return false; }
  buf = rules;
  return true;
}
bool Rules::tryParseRulesWithoutKomi(const string& sOrig, Rules& buf, float komi) {
  Rules rules;
  try { rules = parseRulesHelper(sOrig,false); }
  catch(const StringError&) { return false; }
  rules.komi = komi;
  buf = rules;
  return true;
}

string Rules::toStringNoKomiMaybeNice() const {
  if(equalsIgnoringKomi(parseRulesHelper("TrompTaylor",false)))
    return "TrompTaylor";
  if(equalsIgnoringKomi(parseRulesHelper("Japanese",false)))
    return "Japanese";
  if(equalsIgnoringKomi(parseRulesHelper("Chinese",false)))
    return "Chinese";
  if(equalsIgnoringKomi(parseRulesHelper("Chinese-OGS",false)))
    return "Chinese-OGS";
  if(equalsIgnoringKomi(parseRulesHelper("AGA",false)))
    return "AGA";
  if(equalsIgnoringKomi(parseRulesHelper("StoneScoring",false)))
    return "StoneScoring";
  if(equalsIgnoringKomi(parseRulesHelper("NewZealand",false)))
    return "NewZealand";
  return toStringNoKomi();
}


const Hash128 Rules::ZOBRIST_KO_RULE_HASH[4] = {
  Hash128(0x3cc7e0bf846820f6ULL, 0x1fb7fbde5fc6ba4eULL),  //Based on sha256 hash of Rules::KO_SIMPLE
  Hash128(0xcc18f5d47188554aULL, 0x3a63152c23e4128dULL),  //Based on sha256 hash of Rules::KO_POSITIONAL
  Hash128(0x3bc55e42b23b35bfULL, 0xc75fa1e615621dcdULL),  //Based on sha256 hash of Rules::KO_SITUATIONAL
  Hash128(0x5b2096e48241d21bULL, 0x23cc18d4e85cd67fULL),  //Based on sha256 hash of Rules::KO_SPIGHT
};

const Hash128 Rules::ZOBRIST_SCORING_RULE_HASH[2] = {
  //Based on sha256 hash of Rules::SCORING_AREA, but also mixing none tax rule hash, to preserve legacy hashes
  Hash128(0x8b3ed7598f901494ULL ^ 0x72eeccc72c82a5e7ULL, 0x1dfd47ac77bce5f8ULL ^ 0x0d1265e413623e2bULL),
  //Based on sha256 hash of Rules::SCORING_TERRITORY, but also mixing seki tax rule hash, to preserve legacy hashes
  Hash128(0x381345dc357ec982ULL ^ 0x125bfe48a41042d5ULL, 0x03ba55c026026b56ULL ^ 0x061866b5f2b98a79ULL),
};
const Hash128 Rules::ZOBRIST_TAX_RULE_HASH[3] = {
  Hash128(0x72eeccc72c82a5e7ULL, 0x0d1265e413623e2bULL),  //Based on sha256 hash of Rules::TAX_NONE
  Hash128(0x125bfe48a41042d5ULL, 0x061866b5f2b98a79ULL),  //Based on sha256 hash of Rules::TAX_SEKI
  Hash128(0xa384ece9d8ee713cULL, 0xfdc9f3b5d1f3732bULL),  //Based on sha256 hash of Rules::TAX_ALL
};

const Hash128 Rules::ZOBRIST_MULTI_STONE_SUICIDE_HASH =   //Based on sha256 hash of Rules::ZOBRIST_MULTI_STONE_SUICIDE_HASH
  Hash128(0xf9b475b3bbf35e37ULL, 0xefa19d8b1e5b3e5aULL);

const Hash128 Rules::ZOBRIST_BUTTON_HASH =   //Based on sha256 hash of Rules::ZOBRIST_BUTTON_HASH
  Hash128(0xb8b914c9234ece84ULL, 0x3d759cddebe29c14ULL);
