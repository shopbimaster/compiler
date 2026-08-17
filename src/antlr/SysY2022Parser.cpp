
// Generated from /mnt/d/VSCodeProjects/compiler/grammar/SysY2022Parser.g4 by ANTLR 4.13.1


#include "SysY2022ParserListener.h"
#include "SysY2022ParserVisitor.h"

#include "SysY2022Parser.h"


using namespace antlrcpp;

using namespace antlr4;

namespace {

struct SysY2022ParserStaticData final {
  SysY2022ParserStaticData(std::vector<std::string> ruleNames,
                        std::vector<std::string> literalNames,
                        std::vector<std::string> symbolicNames)
      : ruleNames(std::move(ruleNames)), literalNames(std::move(literalNames)),
        symbolicNames(std::move(symbolicNames)),
        vocabulary(this->literalNames, this->symbolicNames) {}

  SysY2022ParserStaticData(const SysY2022ParserStaticData&) = delete;
  SysY2022ParserStaticData(SysY2022ParserStaticData&&) = delete;
  SysY2022ParserStaticData& operator=(const SysY2022ParserStaticData&) = delete;
  SysY2022ParserStaticData& operator=(SysY2022ParserStaticData&&) = delete;

  std::vector<antlr4::dfa::DFA> decisionToDFA;
  antlr4::atn::PredictionContextCache sharedContextCache;
  const std::vector<std::string> ruleNames;
  const std::vector<std::string> literalNames;
  const std::vector<std::string> symbolicNames;
  const antlr4::dfa::Vocabulary vocabulary;
  antlr4::atn::SerializedATNView serializedATN;
  std::unique_ptr<antlr4::atn::ATN> atn;
};

::antlr4::internal::OnceFlag sysy2022parserParserOnceFlag;
#if ANTLR4_USE_THREAD_LOCAL_CACHE
static thread_local
#endif
SysY2022ParserStaticData *sysy2022parserParserStaticData = nullptr;

void sysy2022parserParserInitialize() {
#if ANTLR4_USE_THREAD_LOCAL_CACHE
  if (sysy2022parserParserStaticData != nullptr) {
    return;
  }
#else
  assert(sysy2022parserParserStaticData == nullptr);
#endif
  auto staticData = std::make_unique<SysY2022ParserStaticData>(
    std::vector<std::string>{
      "compilationUnit", "decl", "vecDecl", "vecInit", "constDecl", "bType", 
      "constDef", "constInitVal", "varDecl", "varDef", "initVal", "funcDef", 
      "funcType", "funcFParams", "funcFParam", "block", "blockItem", "stmt", 
      "exp", "cond", "lVal", "primaryExp", "number", "unaryExp", "unaryOp", 
      "funcRParams", "mulExp", "addExp", "relExp", "eqExp", "lAndExp", "lOrExp", 
      "constExp"
    },
    std::vector<std::string>{
      "", "'int'", "'float'", "'void'", "'const'", "'if'", "'else'", "'while'", 
      "'break'", "'continue'", "'return'", "", "'('", "')'", "'['", "']'", 
      "'{'", "'}'", "','", "';'", "'\\u003F'", "':'", "'+'", "'-'", "'*'", 
      "'/'", "'%'", "'!'", "'&&'", "'||'", "'<'", "'>'", "'<='", "'>='", 
      "'=='", "'!='", "'='"
    },
    std::vector<std::string>{
      "", "INT", "FLOAT", "VOID", "CONST", "IF", "ELSE", "WHILE", "BREAK", 
      "CONTINUE", "RETURN", "VEC_N", "L_PAREN", "R_PAREN", "L_BRACKET", 
      "R_BRACKET", "L_BRACE", "R_BRACE", "COMMA", "SEMICOLON", "QUESTION", 
      "COLON", "PLUS", "MINUS", "STAR", "DIV", "MOD", "NOT", "AND", "OR", 
      "LT", "GT", "LE", "GE", "EQ", "NE", "ASSIGN", "IDENTIFIER", "INTCONST", 
      "FLOATCONST", "WHITESPACE", "LINE_COMMENT", "BLOCK_COMMENT"
    }
  );
  static const int32_t serializedATNSegment[] = {
  	4,1,42,392,2,0,7,0,2,1,7,1,2,2,7,2,2,3,7,3,2,4,7,4,2,5,7,5,2,6,7,6,2,
  	7,7,7,2,8,7,8,2,9,7,9,2,10,7,10,2,11,7,11,2,12,7,12,2,13,7,13,2,14,7,
  	14,2,15,7,15,2,16,7,16,2,17,7,17,2,18,7,18,2,19,7,19,2,20,7,20,2,21,7,
  	21,2,22,7,22,2,23,7,23,2,24,7,24,2,25,7,25,2,26,7,26,2,27,7,27,2,28,7,
  	28,2,29,7,29,2,30,7,30,2,31,7,31,2,32,7,32,1,0,1,0,5,0,69,8,0,10,0,12,
  	0,72,9,0,1,0,1,0,1,1,1,1,1,1,3,1,79,8,1,1,2,1,2,1,2,1,2,1,2,3,2,86,8,
  	2,3,2,88,8,2,1,2,1,2,1,3,1,3,1,3,1,3,5,3,96,8,3,10,3,12,3,99,9,3,1,3,
  	1,3,1,4,1,4,1,4,1,4,1,4,5,4,108,8,4,10,4,12,4,111,9,4,1,4,1,4,1,5,1,5,
  	1,6,1,6,1,6,1,6,1,6,5,6,122,8,6,10,6,12,6,125,9,6,1,6,1,6,1,6,1,7,1,7,
  	1,7,1,7,1,7,5,7,135,8,7,10,7,12,7,138,9,7,3,7,140,8,7,1,7,3,7,143,8,7,
  	1,8,1,8,1,8,1,8,5,8,149,8,8,10,8,12,8,152,9,8,1,8,1,8,1,9,1,9,1,9,1,9,
  	1,9,5,9,161,8,9,10,9,12,9,164,9,9,1,9,1,9,1,9,1,9,1,9,5,9,171,8,9,10,
  	9,12,9,174,9,9,1,9,1,9,3,9,178,8,9,1,10,1,10,1,10,1,10,1,10,5,10,185,
  	8,10,10,10,12,10,188,9,10,3,10,190,8,10,1,10,3,10,193,8,10,1,11,1,11,
  	1,11,1,11,3,11,199,8,11,1,11,1,11,1,11,1,12,1,12,1,13,1,13,1,13,5,13,
  	209,8,13,10,13,12,13,212,9,13,1,14,1,14,1,14,1,14,1,14,1,14,1,14,1,14,
  	5,14,222,8,14,10,14,12,14,225,9,14,3,14,227,8,14,1,15,1,15,5,15,231,8,
  	15,10,15,12,15,234,9,15,1,15,1,15,1,16,1,16,3,16,240,8,16,1,17,1,17,1,
  	17,1,17,1,17,1,17,3,17,248,8,17,1,17,1,17,1,17,1,17,1,17,1,17,1,17,1,
  	17,1,17,3,17,259,8,17,1,17,1,17,1,17,1,17,1,17,1,17,1,17,1,17,1,17,1,
  	17,1,17,1,17,3,17,273,8,17,1,17,3,17,276,8,17,1,18,1,18,1,19,1,19,1,20,
  	1,20,1,20,1,20,1,20,5,20,287,8,20,10,20,12,20,290,9,20,1,21,1,21,1,21,
  	1,21,1,21,1,21,3,21,298,8,21,1,22,1,22,1,23,1,23,1,23,1,23,3,23,306,8,
  	23,1,23,1,23,1,23,1,23,3,23,312,8,23,1,24,1,24,1,25,1,25,1,25,5,25,319,
  	8,25,10,25,12,25,322,9,25,1,26,1,26,1,26,1,26,1,26,1,26,5,26,330,8,26,
  	10,26,12,26,333,9,26,1,27,1,27,1,27,1,27,1,27,1,27,5,27,341,8,27,10,27,
  	12,27,344,9,27,1,28,1,28,1,28,1,28,1,28,1,28,5,28,352,8,28,10,28,12,28,
  	355,9,28,1,29,1,29,1,29,1,29,1,29,1,29,5,29,363,8,29,10,29,12,29,366,
  	9,29,1,30,1,30,1,30,1,30,1,30,1,30,5,30,374,8,30,10,30,12,30,377,9,30,
  	1,31,1,31,1,31,1,31,1,31,1,31,5,31,385,8,31,10,31,12,31,388,9,31,1,32,
  	1,32,1,32,0,6,52,54,56,58,60,62,33,0,2,4,6,8,10,12,14,16,18,20,22,24,
  	26,28,30,32,34,36,38,40,42,44,46,48,50,52,54,56,58,60,62,64,0,8,1,0,1,
  	2,1,0,1,3,1,0,38,39,2,0,22,23,27,27,1,0,24,26,1,0,22,23,1,0,30,33,1,0,
  	34,35,406,0,70,1,0,0,0,2,78,1,0,0,0,4,80,1,0,0,0,6,91,1,0,0,0,8,102,1,
  	0,0,0,10,114,1,0,0,0,12,116,1,0,0,0,14,142,1,0,0,0,16,144,1,0,0,0,18,
  	177,1,0,0,0,20,192,1,0,0,0,22,194,1,0,0,0,24,203,1,0,0,0,26,205,1,0,0,
  	0,28,213,1,0,0,0,30,228,1,0,0,0,32,239,1,0,0,0,34,275,1,0,0,0,36,277,
  	1,0,0,0,38,279,1,0,0,0,40,281,1,0,0,0,42,297,1,0,0,0,44,299,1,0,0,0,46,
  	311,1,0,0,0,48,313,1,0,0,0,50,315,1,0,0,0,52,323,1,0,0,0,54,334,1,0,0,
  	0,56,345,1,0,0,0,58,356,1,0,0,0,60,367,1,0,0,0,62,378,1,0,0,0,64,389,
  	1,0,0,0,66,69,3,2,1,0,67,69,3,22,11,0,68,66,1,0,0,0,68,67,1,0,0,0,69,
  	72,1,0,0,0,70,68,1,0,0,0,70,71,1,0,0,0,71,73,1,0,0,0,72,70,1,0,0,0,73,
  	74,5,0,0,1,74,1,1,0,0,0,75,79,3,8,4,0,76,79,3,16,8,0,77,79,3,4,2,0,78,
  	75,1,0,0,0,78,76,1,0,0,0,78,77,1,0,0,0,79,3,1,0,0,0,80,81,5,11,0,0,81,
  	87,5,37,0,0,82,85,5,36,0,0,83,86,3,6,3,0,84,86,3,36,18,0,85,83,1,0,0,
  	0,85,84,1,0,0,0,86,88,1,0,0,0,87,82,1,0,0,0,87,88,1,0,0,0,88,89,1,0,0,
  	0,89,90,5,19,0,0,90,5,1,0,0,0,91,92,5,16,0,0,92,97,3,36,18,0,93,94,5,
  	18,0,0,94,96,3,36,18,0,95,93,1,0,0,0,96,99,1,0,0,0,97,95,1,0,0,0,97,98,
  	1,0,0,0,98,100,1,0,0,0,99,97,1,0,0,0,100,101,5,17,0,0,101,7,1,0,0,0,102,
  	103,5,4,0,0,103,104,3,10,5,0,104,109,3,12,6,0,105,106,5,18,0,0,106,108,
  	3,12,6,0,107,105,1,0,0,0,108,111,1,0,0,0,109,107,1,0,0,0,109,110,1,0,
  	0,0,110,112,1,0,0,0,111,109,1,0,0,0,112,113,5,19,0,0,113,9,1,0,0,0,114,
  	115,7,0,0,0,115,11,1,0,0,0,116,123,5,37,0,0,117,118,5,14,0,0,118,119,
  	3,64,32,0,119,120,5,15,0,0,120,122,1,0,0,0,121,117,1,0,0,0,122,125,1,
  	0,0,0,123,121,1,0,0,0,123,124,1,0,0,0,124,126,1,0,0,0,125,123,1,0,0,0,
  	126,127,5,36,0,0,127,128,3,14,7,0,128,13,1,0,0,0,129,143,3,64,32,0,130,
  	139,5,16,0,0,131,136,3,14,7,0,132,133,5,18,0,0,133,135,3,14,7,0,134,132,
  	1,0,0,0,135,138,1,0,0,0,136,134,1,0,0,0,136,137,1,0,0,0,137,140,1,0,0,
  	0,138,136,1,0,0,0,139,131,1,0,0,0,139,140,1,0,0,0,140,141,1,0,0,0,141,
  	143,5,17,0,0,142,129,1,0,0,0,142,130,1,0,0,0,143,15,1,0,0,0,144,145,3,
  	10,5,0,145,150,3,18,9,0,146,147,5,18,0,0,147,149,3,18,9,0,148,146,1,0,
  	0,0,149,152,1,0,0,0,150,148,1,0,0,0,150,151,1,0,0,0,151,153,1,0,0,0,152,
  	150,1,0,0,0,153,154,5,19,0,0,154,17,1,0,0,0,155,162,5,37,0,0,156,157,
  	5,14,0,0,157,158,3,64,32,0,158,159,5,15,0,0,159,161,1,0,0,0,160,156,1,
  	0,0,0,161,164,1,0,0,0,162,160,1,0,0,0,162,163,1,0,0,0,163,178,1,0,0,0,
  	164,162,1,0,0,0,165,172,5,37,0,0,166,167,5,14,0,0,167,168,3,64,32,0,168,
  	169,5,15,0,0,169,171,1,0,0,0,170,166,1,0,0,0,171,174,1,0,0,0,172,170,
  	1,0,0,0,172,173,1,0,0,0,173,175,1,0,0,0,174,172,1,0,0,0,175,176,5,36,
  	0,0,176,178,3,20,10,0,177,155,1,0,0,0,177,165,1,0,0,0,178,19,1,0,0,0,
  	179,193,3,36,18,0,180,189,5,16,0,0,181,186,3,20,10,0,182,183,5,18,0,0,
  	183,185,3,20,10,0,184,182,1,0,0,0,185,188,1,0,0,0,186,184,1,0,0,0,186,
  	187,1,0,0,0,187,190,1,0,0,0,188,186,1,0,0,0,189,181,1,0,0,0,189,190,1,
  	0,0,0,190,191,1,0,0,0,191,193,5,17,0,0,192,179,1,0,0,0,192,180,1,0,0,
  	0,193,21,1,0,0,0,194,195,3,24,12,0,195,196,5,37,0,0,196,198,5,12,0,0,
  	197,199,3,26,13,0,198,197,1,0,0,0,198,199,1,0,0,0,199,200,1,0,0,0,200,
  	201,5,13,0,0,201,202,3,30,15,0,202,23,1,0,0,0,203,204,7,1,0,0,204,25,
  	1,0,0,0,205,210,3,28,14,0,206,207,5,18,0,0,207,209,3,28,14,0,208,206,
  	1,0,0,0,209,212,1,0,0,0,210,208,1,0,0,0,210,211,1,0,0,0,211,27,1,0,0,
  	0,212,210,1,0,0,0,213,214,3,10,5,0,214,226,5,37,0,0,215,216,5,14,0,0,
  	216,223,5,15,0,0,217,218,5,14,0,0,218,219,3,36,18,0,219,220,5,15,0,0,
  	220,222,1,0,0,0,221,217,1,0,0,0,222,225,1,0,0,0,223,221,1,0,0,0,223,224,
  	1,0,0,0,224,227,1,0,0,0,225,223,1,0,0,0,226,215,1,0,0,0,226,227,1,0,0,
  	0,227,29,1,0,0,0,228,232,5,16,0,0,229,231,3,32,16,0,230,229,1,0,0,0,231,
  	234,1,0,0,0,232,230,1,0,0,0,232,233,1,0,0,0,233,235,1,0,0,0,234,232,1,
  	0,0,0,235,236,5,17,0,0,236,31,1,0,0,0,237,240,3,2,1,0,238,240,3,34,17,
  	0,239,237,1,0,0,0,239,238,1,0,0,0,240,33,1,0,0,0,241,242,3,40,20,0,242,
  	243,5,36,0,0,243,244,3,36,18,0,244,245,5,19,0,0,245,276,1,0,0,0,246,248,
  	3,36,18,0,247,246,1,0,0,0,247,248,1,0,0,0,248,249,1,0,0,0,249,276,5,19,
  	0,0,250,276,3,30,15,0,251,252,5,5,0,0,252,253,5,12,0,0,253,254,3,38,19,
  	0,254,255,5,13,0,0,255,258,3,34,17,0,256,257,5,6,0,0,257,259,3,34,17,
  	0,258,256,1,0,0,0,258,259,1,0,0,0,259,276,1,0,0,0,260,261,5,7,0,0,261,
  	262,5,12,0,0,262,263,3,38,19,0,263,264,5,13,0,0,264,265,3,34,17,0,265,
  	276,1,0,0,0,266,267,5,8,0,0,267,276,5,19,0,0,268,269,5,9,0,0,269,276,
  	5,19,0,0,270,272,5,10,0,0,271,273,3,36,18,0,272,271,1,0,0,0,272,273,1,
  	0,0,0,273,274,1,0,0,0,274,276,5,19,0,0,275,241,1,0,0,0,275,247,1,0,0,
  	0,275,250,1,0,0,0,275,251,1,0,0,0,275,260,1,0,0,0,275,266,1,0,0,0,275,
  	268,1,0,0,0,275,270,1,0,0,0,276,35,1,0,0,0,277,278,3,54,27,0,278,37,1,
  	0,0,0,279,280,3,62,31,0,280,39,1,0,0,0,281,288,5,37,0,0,282,283,5,14,
  	0,0,283,284,3,36,18,0,284,285,5,15,0,0,285,287,1,0,0,0,286,282,1,0,0,
  	0,287,290,1,0,0,0,288,286,1,0,0,0,288,289,1,0,0,0,289,41,1,0,0,0,290,
  	288,1,0,0,0,291,292,5,12,0,0,292,293,3,36,18,0,293,294,5,13,0,0,294,298,
  	1,0,0,0,295,298,3,40,20,0,296,298,3,44,22,0,297,291,1,0,0,0,297,295,1,
  	0,0,0,297,296,1,0,0,0,298,43,1,0,0,0,299,300,7,2,0,0,300,45,1,0,0,0,301,
  	312,3,42,21,0,302,303,5,37,0,0,303,305,5,12,0,0,304,306,3,50,25,0,305,
  	304,1,0,0,0,305,306,1,0,0,0,306,307,1,0,0,0,307,312,5,13,0,0,308,309,
  	3,48,24,0,309,310,3,46,23,0,310,312,1,0,0,0,311,301,1,0,0,0,311,302,1,
  	0,0,0,311,308,1,0,0,0,312,47,1,0,0,0,313,314,7,3,0,0,314,49,1,0,0,0,315,
  	320,3,36,18,0,316,317,5,18,0,0,317,319,3,36,18,0,318,316,1,0,0,0,319,
  	322,1,0,0,0,320,318,1,0,0,0,320,321,1,0,0,0,321,51,1,0,0,0,322,320,1,
  	0,0,0,323,324,6,26,-1,0,324,325,3,46,23,0,325,331,1,0,0,0,326,327,10,
  	1,0,0,327,328,7,4,0,0,328,330,3,46,23,0,329,326,1,0,0,0,330,333,1,0,0,
  	0,331,329,1,0,0,0,331,332,1,0,0,0,332,53,1,0,0,0,333,331,1,0,0,0,334,
  	335,6,27,-1,0,335,336,3,52,26,0,336,342,1,0,0,0,337,338,10,1,0,0,338,
  	339,7,5,0,0,339,341,3,52,26,0,340,337,1,0,0,0,341,344,1,0,0,0,342,340,
  	1,0,0,0,342,343,1,0,0,0,343,55,1,0,0,0,344,342,1,0,0,0,345,346,6,28,-1,
  	0,346,347,3,54,27,0,347,353,1,0,0,0,348,349,10,1,0,0,349,350,7,6,0,0,
  	350,352,3,54,27,0,351,348,1,0,0,0,352,355,1,0,0,0,353,351,1,0,0,0,353,
  	354,1,0,0,0,354,57,1,0,0,0,355,353,1,0,0,0,356,357,6,29,-1,0,357,358,
  	3,56,28,0,358,364,1,0,0,0,359,360,10,1,0,0,360,361,7,7,0,0,361,363,3,
  	56,28,0,362,359,1,0,0,0,363,366,1,0,0,0,364,362,1,0,0,0,364,365,1,0,0,
  	0,365,59,1,0,0,0,366,364,1,0,0,0,367,368,6,30,-1,0,368,369,3,58,29,0,
  	369,375,1,0,0,0,370,371,10,1,0,0,371,372,5,28,0,0,372,374,3,58,29,0,373,
  	370,1,0,0,0,374,377,1,0,0,0,375,373,1,0,0,0,375,376,1,0,0,0,376,61,1,
  	0,0,0,377,375,1,0,0,0,378,379,6,31,-1,0,379,380,3,60,30,0,380,386,1,0,
  	0,0,381,382,10,1,0,0,382,383,5,29,0,0,383,385,3,60,30,0,384,381,1,0,0,
  	0,385,388,1,0,0,0,386,384,1,0,0,0,386,387,1,0,0,0,387,63,1,0,0,0,388,
  	386,1,0,0,0,389,390,3,54,27,0,390,65,1,0,0,0,39,68,70,78,85,87,97,109,
  	123,136,139,142,150,162,172,177,186,189,192,198,210,223,226,232,239,247,
  	258,272,275,288,297,305,311,320,331,342,353,364,375,386
  };
  staticData->serializedATN = antlr4::atn::SerializedATNView(serializedATNSegment, sizeof(serializedATNSegment) / sizeof(serializedATNSegment[0]));

  antlr4::atn::ATNDeserializer deserializer;
  staticData->atn = deserializer.deserialize(staticData->serializedATN);

  const size_t count = staticData->atn->getNumberOfDecisions();
  staticData->decisionToDFA.reserve(count);
  for (size_t i = 0; i < count; i++) { 
    staticData->decisionToDFA.emplace_back(staticData->atn->getDecisionState(i), i);
  }
  sysy2022parserParserStaticData = staticData.release();
}

}

SysY2022Parser::SysY2022Parser(TokenStream *input) : SysY2022Parser(input, antlr4::atn::ParserATNSimulatorOptions()) {}

SysY2022Parser::SysY2022Parser(TokenStream *input, const antlr4::atn::ParserATNSimulatorOptions &options) : Parser(input) {
  SysY2022Parser::initialize();
  _interpreter = new atn::ParserATNSimulator(this, *sysy2022parserParserStaticData->atn, sysy2022parserParserStaticData->decisionToDFA, sysy2022parserParserStaticData->sharedContextCache, options);
}

SysY2022Parser::~SysY2022Parser() {
  delete _interpreter;
}

const atn::ATN& SysY2022Parser::getATN() const {
  return *sysy2022parserParserStaticData->atn;
}

std::string SysY2022Parser::getGrammarFileName() const {
  return "SysY2022Parser.g4";
}

const std::vector<std::string>& SysY2022Parser::getRuleNames() const {
  return sysy2022parserParserStaticData->ruleNames;
}

const dfa::Vocabulary& SysY2022Parser::getVocabulary() const {
  return sysy2022parserParserStaticData->vocabulary;
}

antlr4::atn::SerializedATNView SysY2022Parser::getSerializedATN() const {
  return sysy2022parserParserStaticData->serializedATN;
}


//----------------- CompilationUnitContext ------------------------------------------------------------------

SysY2022Parser::CompilationUnitContext::CompilationUnitContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* SysY2022Parser::CompilationUnitContext::EOF() {
  return getToken(SysY2022Parser::EOF, 0);
}

std::vector<SysY2022Parser::DeclContext *> SysY2022Parser::CompilationUnitContext::decl() {
  return getRuleContexts<SysY2022Parser::DeclContext>();
}

SysY2022Parser::DeclContext* SysY2022Parser::CompilationUnitContext::decl(size_t i) {
  return getRuleContext<SysY2022Parser::DeclContext>(i);
}

std::vector<SysY2022Parser::FuncDefContext *> SysY2022Parser::CompilationUnitContext::funcDef() {
  return getRuleContexts<SysY2022Parser::FuncDefContext>();
}

SysY2022Parser::FuncDefContext* SysY2022Parser::CompilationUnitContext::funcDef(size_t i) {
  return getRuleContext<SysY2022Parser::FuncDefContext>(i);
}


size_t SysY2022Parser::CompilationUnitContext::getRuleIndex() const {
  return SysY2022Parser::RuleCompilationUnit;
}

void SysY2022Parser::CompilationUnitContext::enterRule(tree::ParseTreeListener *listener) {
  auto parserListener = dynamic_cast<SysY2022ParserListener *>(listener);
  if (parserListener != nullptr)
    parserListener->enterCompilationUnit(this);
}

void SysY2022Parser::CompilationUnitContext::exitRule(tree::ParseTreeListener *listener) {
  auto parserListener = dynamic_cast<SysY2022ParserListener *>(listener);
  if (parserListener != nullptr)
    parserListener->exitCompilationUnit(this);
}


std::any SysY2022Parser::CompilationUnitContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<SysY2022ParserVisitor*>(visitor))
    return parserVisitor->visitCompilationUnit(this);
  else
    return visitor->visitChildren(this);
}

SysY2022Parser::CompilationUnitContext* SysY2022Parser::compilationUnit() {
  CompilationUnitContext *_localctx = _tracker.createInstance<CompilationUnitContext>(_ctx, getState());
  enterRule(_localctx, 0, SysY2022Parser::RuleCompilationUnit);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(70);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 2078) != 0)) {
      setState(68);
      _errHandler->sync(this);
      switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 0, _ctx)) {
      case 1: {
        setState(66);
        decl();
        break;
      }

      case 2: {
        setState(67);
        funcDef();
        break;
      }

      default:
        break;
      }
      setState(72);
      _errHandler->sync(this);
      _la = _input->LA(1);
    }
    setState(73);
    match(SysY2022Parser::EOF);
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- DeclContext ------------------------------------------------------------------

SysY2022Parser::DeclContext::DeclContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

SysY2022Parser::ConstDeclContext* SysY2022Parser::DeclContext::constDecl() {
  return getRuleContext<SysY2022Parser::ConstDeclContext>(0);
}

SysY2022Parser::VarDeclContext* SysY2022Parser::DeclContext::varDecl() {
  return getRuleContext<SysY2022Parser::VarDeclContext>(0);
}

SysY2022Parser::VecDeclContext* SysY2022Parser::DeclContext::vecDecl() {
  return getRuleContext<SysY2022Parser::VecDeclContext>(0);
}


size_t SysY2022Parser::DeclContext::getRuleIndex() const {
  return SysY2022Parser::RuleDecl;
}

void SysY2022Parser::DeclContext::enterRule(tree::ParseTreeListener *listener) {
  auto parserListener = dynamic_cast<SysY2022ParserListener *>(listener);
  if (parserListener != nullptr)
    parserListener->enterDecl(this);
}

void SysY2022Parser::DeclContext::exitRule(tree::ParseTreeListener *listener) {
  auto parserListener = dynamic_cast<SysY2022ParserListener *>(listener);
  if (parserListener != nullptr)
    parserListener->exitDecl(this);
}


std::any SysY2022Parser::DeclContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<SysY2022ParserVisitor*>(visitor))
    return parserVisitor->visitDecl(this);
  else
    return visitor->visitChildren(this);
}

SysY2022Parser::DeclContext* SysY2022Parser::decl() {
  DeclContext *_localctx = _tracker.createInstance<DeclContext>(_ctx, getState());
  enterRule(_localctx, 2, SysY2022Parser::RuleDecl);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(78);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case SysY2022Parser::CONST: {
        enterOuterAlt(_localctx, 1);
        setState(75);
        constDecl();
        break;
      }

      case SysY2022Parser::INT:
      case SysY2022Parser::FLOAT: {
        enterOuterAlt(_localctx, 2);
        setState(76);
        varDecl();
        break;
      }

      case SysY2022Parser::VEC_N: {
        enterOuterAlt(_localctx, 3);
        setState(77);
        vecDecl();
        break;
      }

    default:
      throw NoViableAltException(this);
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- VecDeclContext ------------------------------------------------------------------

SysY2022Parser::VecDeclContext::VecDeclContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* SysY2022Parser::VecDeclContext::VEC_N() {
  return getToken(SysY2022Parser::VEC_N, 0);
}

tree::TerminalNode* SysY2022Parser::VecDeclContext::IDENTIFIER() {
  return getToken(SysY2022Parser::IDENTIFIER, 0);
}

tree::TerminalNode* SysY2022Parser::VecDeclContext::SEMICOLON() {
  return getToken(SysY2022Parser::SEMICOLON, 0);
}

tree::TerminalNode* SysY2022Parser::VecDeclContext::ASSIGN() {
  return getToken(SysY2022Parser::ASSIGN, 0);
}

SysY2022Parser::VecInitContext* SysY2022Parser::VecDeclContext::vecInit() {
  return getRuleContext<SysY2022Parser::VecInitContext>(0);
}

SysY2022Parser::ExpContext* SysY2022Parser::VecDeclContext::exp() {
  return getRuleContext<SysY2022Parser::ExpContext>(0);
}


size_t SysY2022Parser::VecDeclContext::getRuleIndex() const {
  return SysY2022Parser::RuleVecDecl;
}

void SysY2022Parser::VecDeclContext::enterRule(tree::ParseTreeListener *listener) {
  auto parserListener = dynamic_cast<SysY2022ParserListener *>(listener);
  if (parserListener != nullptr)
    parserListener->enterVecDecl(this);
}

void SysY2022Parser::VecDeclContext::exitRule(tree::ParseTreeListener *listener) {
  auto parserListener = dynamic_cast<SysY2022ParserListener *>(listener);
  if (parserListener != nullptr)
    parserListener->exitVecDecl(this);
}


std::any SysY2022Parser::VecDeclContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<SysY2022ParserVisitor*>(visitor))
    return parserVisitor->visitVecDecl(this);
  else
    return visitor->visitChildren(this);
}

SysY2022Parser::VecDeclContext* SysY2022Parser::vecDecl() {
  VecDeclContext *_localctx = _tracker.createInstance<VecDeclContext>(_ctx, getState());
  enterRule(_localctx, 4, SysY2022Parser::RuleVecDecl);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(80);
    match(SysY2022Parser::VEC_N);
    setState(81);
    match(SysY2022Parser::IDENTIFIER);
    setState(87);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == SysY2022Parser::ASSIGN) {
      setState(82);
      match(SysY2022Parser::ASSIGN);
      setState(85);
      _errHandler->sync(this);
      switch (_input->LA(1)) {
        case SysY2022Parser::L_BRACE: {
          setState(83);
          vecInit();
          break;
        }

        case SysY2022Parser::L_PAREN:
        case SysY2022Parser::PLUS:
        case SysY2022Parser::MINUS:
        case SysY2022Parser::NOT:
        case SysY2022Parser::IDENTIFIER:
        case SysY2022Parser::INTCONST:
        case SysY2022Parser::FLOATCONST: {
          setState(84);
          exp();
          break;
        }

      default:
        throw NoViableAltException(this);
      }
    }
    setState(89);
    match(SysY2022Parser::SEMICOLON);
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- VecInitContext ------------------------------------------------------------------

SysY2022Parser::VecInitContext::VecInitContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* SysY2022Parser::VecInitContext::L_BRACE() {
  return getToken(SysY2022Parser::L_BRACE, 0);
}

std::vector<SysY2022Parser::ExpContext *> SysY2022Parser::VecInitContext::exp() {
  return getRuleContexts<SysY2022Parser::ExpContext>();
}

SysY2022Parser::ExpContext* SysY2022Parser::VecInitContext::exp(size_t i) {
  return getRuleContext<SysY2022Parser::ExpContext>(i);
}

tree::TerminalNode* SysY2022Parser::VecInitContext::R_BRACE() {
  return getToken(SysY2022Parser::R_BRACE, 0);
}

std::vector<tree::TerminalNode *> SysY2022Parser::VecInitContext::COMMA() {
  return getTokens(SysY2022Parser::COMMA);
}

tree::TerminalNode* SysY2022Parser::VecInitContext::COMMA(size_t i) {
  return getToken(SysY2022Parser::COMMA, i);
}


size_t SysY2022Parser::VecInitContext::getRuleIndex() const {
  return SysY2022Parser::RuleVecInit;
}

void SysY2022Parser::VecInitContext::enterRule(tree::ParseTreeListener *listener) {
  auto parserListener = dynamic_cast<SysY2022ParserListener *>(listener);
  if (parserListener != nullptr)
    parserListener->enterVecInit(this);
}

void SysY2022Parser::VecInitContext::exitRule(tree::ParseTreeListener *listener) {
  auto parserListener = dynamic_cast<SysY2022ParserListener *>(listener);
  if (parserListener != nullptr)
    parserListener->exitVecInit(this);
}


std::any SysY2022Parser::VecInitContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<SysY2022ParserVisitor*>(visitor))
    return parserVisitor->visitVecInit(this);
  else
    return visitor->visitChildren(this);
}

SysY2022Parser::VecInitContext* SysY2022Parser::vecInit() {
  VecInitContext *_localctx = _tracker.createInstance<VecInitContext>(_ctx, getState());
  enterRule(_localctx, 6, SysY2022Parser::RuleVecInit);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(91);
    match(SysY2022Parser::L_BRACE);
    setState(92);
    exp();
    setState(97);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == SysY2022Parser::COMMA) {
      setState(93);
      match(SysY2022Parser::COMMA);
      setState(94);
      exp();
      setState(99);
      _errHandler->sync(this);
      _la = _input->LA(1);
    }
    setState(100);
    match(SysY2022Parser::R_BRACE);
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- ConstDeclContext ------------------------------------------------------------------

SysY2022Parser::ConstDeclContext::ConstDeclContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* SysY2022Parser::ConstDeclContext::CONST() {
  return getToken(SysY2022Parser::CONST, 0);
}

SysY2022Parser::BTypeContext* SysY2022Parser::ConstDeclContext::bType() {
  return getRuleContext<SysY2022Parser::BTypeContext>(0);
}

std::vector<SysY2022Parser::ConstDefContext *> SysY2022Parser::ConstDeclContext::constDef() {
  return getRuleContexts<SysY2022Parser::ConstDefContext>();
}

SysY2022Parser::ConstDefContext* SysY2022Parser::ConstDeclContext::constDef(size_t i) {
  return getRuleContext<SysY2022Parser::ConstDefContext>(i);
}

tree::TerminalNode* SysY2022Parser::ConstDeclContext::SEMICOLON() {
  return getToken(SysY2022Parser::SEMICOLON, 0);
}

std::vector<tree::TerminalNode *> SysY2022Parser::ConstDeclContext::COMMA() {
  return getTokens(SysY2022Parser::COMMA);
}

tree::TerminalNode* SysY2022Parser::ConstDeclContext::COMMA(size_t i) {
  return getToken(SysY2022Parser::COMMA, i);
}


size_t SysY2022Parser::ConstDeclContext::getRuleIndex() const {
  return SysY2022Parser::RuleConstDecl;
}

void SysY2022Parser::ConstDeclContext::enterRule(tree::ParseTreeListener *listener) {
  auto parserListener = dynamic_cast<SysY2022ParserListener *>(listener);
  if (parserListener != nullptr)
    parserListener->enterConstDecl(this);
}

void SysY2022Parser::ConstDeclContext::exitRule(tree::ParseTreeListener *listener) {
  auto parserListener = dynamic_cast<SysY2022ParserListener *>(listener);
  if (parserListener != nullptr)
    parserListener->exitConstDecl(this);
}


std::any SysY2022Parser::ConstDeclContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<SysY2022ParserVisitor*>(visitor))
    return parserVisitor->visitConstDecl(this);
  else
    return visitor->visitChildren(this);
}

SysY2022Parser::ConstDeclContext* SysY2022Parser::constDecl() {
  ConstDeclContext *_localctx = _tracker.createInstance<ConstDeclContext>(_ctx, getState());
  enterRule(_localctx, 8, SysY2022Parser::RuleConstDecl);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(102);
    match(SysY2022Parser::CONST);
    setState(103);
    bType();
    setState(104);
    constDef();
    setState(109);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == SysY2022Parser::COMMA) {
      setState(105);
      match(SysY2022Parser::COMMA);
      setState(106);
      constDef();
      setState(111);
      _errHandler->sync(this);
      _la = _input->LA(1);
    }
    setState(112);
    match(SysY2022Parser::SEMICOLON);
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- BTypeContext ------------------------------------------------------------------

SysY2022Parser::BTypeContext::BTypeContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* SysY2022Parser::BTypeContext::INT() {
  return getToken(SysY2022Parser::INT, 0);
}

tree::TerminalNode* SysY2022Parser::BTypeContext::FLOAT() {
  return getToken(SysY2022Parser::FLOAT, 0);
}


size_t SysY2022Parser::BTypeContext::getRuleIndex() const {
  return SysY2022Parser::RuleBType;
}

void SysY2022Parser::BTypeContext::enterRule(tree::ParseTreeListener *listener) {
  auto parserListener = dynamic_cast<SysY2022ParserListener *>(listener);
  if (parserListener != nullptr)
    parserListener->enterBType(this);
}

void SysY2022Parser::BTypeContext::exitRule(tree::ParseTreeListener *listener) {
  auto parserListener = dynamic_cast<SysY2022ParserListener *>(listener);
  if (parserListener != nullptr)
    parserListener->exitBType(this);
}


std::any SysY2022Parser::BTypeContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<SysY2022ParserVisitor*>(visitor))
    return parserVisitor->visitBType(this);
  else
    return visitor->visitChildren(this);
}

SysY2022Parser::BTypeContext* SysY2022Parser::bType() {
  BTypeContext *_localctx = _tracker.createInstance<BTypeContext>(_ctx, getState());
  enterRule(_localctx, 10, SysY2022Parser::RuleBType);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(114);
    _la = _input->LA(1);
    if (!(_la == SysY2022Parser::INT

    || _la == SysY2022Parser::FLOAT)) {
    _errHandler->recoverInline(this);
    }
    else {
      _errHandler->reportMatch(this);
      consume();
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- ConstDefContext ------------------------------------------------------------------

SysY2022Parser::ConstDefContext::ConstDefContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* SysY2022Parser::ConstDefContext::IDENTIFIER() {
  return getToken(SysY2022Parser::IDENTIFIER, 0);
}

tree::TerminalNode* SysY2022Parser::ConstDefContext::ASSIGN() {
  return getToken(SysY2022Parser::ASSIGN, 0);
}

SysY2022Parser::ConstInitValContext* SysY2022Parser::ConstDefContext::constInitVal() {
  return getRuleContext<SysY2022Parser::ConstInitValContext>(0);
}

std::vector<tree::TerminalNode *> SysY2022Parser::ConstDefContext::L_BRACKET() {
  return getTokens(SysY2022Parser::L_BRACKET);
}

tree::TerminalNode* SysY2022Parser::ConstDefContext::L_BRACKET(size_t i) {
  return getToken(SysY2022Parser::L_BRACKET, i);
}

std::vector<SysY2022Parser::ConstExpContext *> SysY2022Parser::ConstDefContext::constExp() {
  return getRuleContexts<SysY2022Parser::ConstExpContext>();
}

SysY2022Parser::ConstExpContext* SysY2022Parser::ConstDefContext::constExp(size_t i) {
  return getRuleContext<SysY2022Parser::ConstExpContext>(i);
}

std::vector<tree::TerminalNode *> SysY2022Parser::ConstDefContext::R_BRACKET() {
  return getTokens(SysY2022Parser::R_BRACKET);
}

tree::TerminalNode* SysY2022Parser::ConstDefContext::R_BRACKET(size_t i) {
  return getToken(SysY2022Parser::R_BRACKET, i);
}


size_t SysY2022Parser::ConstDefContext::getRuleIndex() const {
  return SysY2022Parser::RuleConstDef;
}

void SysY2022Parser::ConstDefContext::enterRule(tree::ParseTreeListener *listener) {
  auto parserListener = dynamic_cast<SysY2022ParserListener *>(listener);
  if (parserListener != nullptr)
    parserListener->enterConstDef(this);
}

void SysY2022Parser::ConstDefContext::exitRule(tree::ParseTreeListener *listener) {
  auto parserListener = dynamic_cast<SysY2022ParserListener *>(listener);
  if (parserListener != nullptr)
    parserListener->exitConstDef(this);
}


std::any SysY2022Parser::ConstDefContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<SysY2022ParserVisitor*>(visitor))
    return parserVisitor->visitConstDef(this);
  else
    return visitor->visitChildren(this);
}

SysY2022Parser::ConstDefContext* SysY2022Parser::constDef() {
  ConstDefContext *_localctx = _tracker.createInstance<ConstDefContext>(_ctx, getState());
  enterRule(_localctx, 12, SysY2022Parser::RuleConstDef);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(116);
    match(SysY2022Parser::IDENTIFIER);
    setState(123);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == SysY2022Parser::L_BRACKET) {
      setState(117);
      match(SysY2022Parser::L_BRACKET);
      setState(118);
      constExp();
      setState(119);
      match(SysY2022Parser::R_BRACKET);
      setState(125);
      _errHandler->sync(this);
      _la = _input->LA(1);
    }
    setState(126);
    match(SysY2022Parser::ASSIGN);
    setState(127);
    constInitVal();
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- ConstInitValContext ------------------------------------------------------------------

SysY2022Parser::ConstInitValContext::ConstInitValContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

SysY2022Parser::ConstExpContext* SysY2022Parser::ConstInitValContext::constExp() {
  return getRuleContext<SysY2022Parser::ConstExpContext>(0);
}

tree::TerminalNode* SysY2022Parser::ConstInitValContext::L_BRACE() {
  return getToken(SysY2022Parser::L_BRACE, 0);
}

tree::TerminalNode* SysY2022Parser::ConstInitValContext::R_BRACE() {
  return getToken(SysY2022Parser::R_BRACE, 0);
}

std::vector<SysY2022Parser::ConstInitValContext *> SysY2022Parser::ConstInitValContext::constInitVal() {
  return getRuleContexts<SysY2022Parser::ConstInitValContext>();
}

SysY2022Parser::ConstInitValContext* SysY2022Parser::ConstInitValContext::constInitVal(size_t i) {
  return getRuleContext<SysY2022Parser::ConstInitValContext>(i);
}

std::vector<tree::TerminalNode *> SysY2022Parser::ConstInitValContext::COMMA() {
  return getTokens(SysY2022Parser::COMMA);
}

tree::TerminalNode* SysY2022Parser::ConstInitValContext::COMMA(size_t i) {
  return getToken(SysY2022Parser::COMMA, i);
}


size_t SysY2022Parser::ConstInitValContext::getRuleIndex() const {
  return SysY2022Parser::RuleConstInitVal;
}

void SysY2022Parser::ConstInitValContext::enterRule(tree::ParseTreeListener *listener) {
  auto parserListener = dynamic_cast<SysY2022ParserListener *>(listener);
  if (parserListener != nullptr)
    parserListener->enterConstInitVal(this);
}

void SysY2022Parser::ConstInitValContext::exitRule(tree::ParseTreeListener *listener) {
  auto parserListener = dynamic_cast<SysY2022ParserListener *>(listener);
  if (parserListener != nullptr)
    parserListener->exitConstInitVal(this);
}


std::any SysY2022Parser::ConstInitValContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<SysY2022ParserVisitor*>(visitor))
    return parserVisitor->visitConstInitVal(this);
  else
    return visitor->visitChildren(this);
}

SysY2022Parser::ConstInitValContext* SysY2022Parser::constInitVal() {
  ConstInitValContext *_localctx = _tracker.createInstance<ConstInitValContext>(_ctx, getState());
  enterRule(_localctx, 14, SysY2022Parser::RuleConstInitVal);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(142);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case SysY2022Parser::L_PAREN:
      case SysY2022Parser::PLUS:
      case SysY2022Parser::MINUS:
      case SysY2022Parser::NOT:
      case SysY2022Parser::IDENTIFIER:
      case SysY2022Parser::INTCONST:
      case SysY2022Parser::FLOATCONST: {
        enterOuterAlt(_localctx, 1);
        setState(129);
        constExp();
        break;
      }

      case SysY2022Parser::L_BRACE: {
        enterOuterAlt(_localctx, 2);
        setState(130);
        match(SysY2022Parser::L_BRACE);
        setState(139);
        _errHandler->sync(this);

        _la = _input->LA(1);
        if ((((_la & ~ 0x3fULL) == 0) &&
          ((1ULL << _la) & 962219544576) != 0)) {
          setState(131);
          constInitVal();
          setState(136);
          _errHandler->sync(this);
          _la = _input->LA(1);
          while (_la == SysY2022Parser::COMMA) {
            setState(132);
            match(SysY2022Parser::COMMA);
            setState(133);
            constInitVal();
            setState(138);
            _errHandler->sync(this);
            _la = _input->LA(1);
          }
        }
        setState(141);
        match(SysY2022Parser::R_BRACE);
        break;
      }

    default:
      throw NoViableAltException(this);
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- VarDeclContext ------------------------------------------------------------------

SysY2022Parser::VarDeclContext::VarDeclContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

SysY2022Parser::BTypeContext* SysY2022Parser::VarDeclContext::bType() {
  return getRuleContext<SysY2022Parser::BTypeContext>(0);
}

std::vector<SysY2022Parser::VarDefContext *> SysY2022Parser::VarDeclContext::varDef() {
  return getRuleContexts<SysY2022Parser::VarDefContext>();
}

SysY2022Parser::VarDefContext* SysY2022Parser::VarDeclContext::varDef(size_t i) {
  return getRuleContext<SysY2022Parser::VarDefContext>(i);
}

tree::TerminalNode* SysY2022Parser::VarDeclContext::SEMICOLON() {
  return getToken(SysY2022Parser::SEMICOLON, 0);
}

std::vector<tree::TerminalNode *> SysY2022Parser::VarDeclContext::COMMA() {
  return getTokens(SysY2022Parser::COMMA);
}

tree::TerminalNode* SysY2022Parser::VarDeclContext::COMMA(size_t i) {
  return getToken(SysY2022Parser::COMMA, i);
}


size_t SysY2022Parser::VarDeclContext::getRuleIndex() const {
  return SysY2022Parser::RuleVarDecl;
}

void SysY2022Parser::VarDeclContext::enterRule(tree::ParseTreeListener *listener) {
  auto parserListener = dynamic_cast<SysY2022ParserListener *>(listener);
  if (parserListener != nullptr)
    parserListener->enterVarDecl(this);
}

void SysY2022Parser::VarDeclContext::exitRule(tree::ParseTreeListener *listener) {
  auto parserListener = dynamic_cast<SysY2022ParserListener *>(listener);
  if (parserListener != nullptr)
    parserListener->exitVarDecl(this);
}


std::any SysY2022Parser::VarDeclContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<SysY2022ParserVisitor*>(visitor))
    return parserVisitor->visitVarDecl(this);
  else
    return visitor->visitChildren(this);
}

SysY2022Parser::VarDeclContext* SysY2022Parser::varDecl() {
  VarDeclContext *_localctx = _tracker.createInstance<VarDeclContext>(_ctx, getState());
  enterRule(_localctx, 16, SysY2022Parser::RuleVarDecl);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(144);
    bType();
    setState(145);
    varDef();
    setState(150);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == SysY2022Parser::COMMA) {
      setState(146);
      match(SysY2022Parser::COMMA);
      setState(147);
      varDef();
      setState(152);
      _errHandler->sync(this);
      _la = _input->LA(1);
    }
    setState(153);
    match(SysY2022Parser::SEMICOLON);
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- VarDefContext ------------------------------------------------------------------

SysY2022Parser::VarDefContext::VarDefContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* SysY2022Parser::VarDefContext::IDENTIFIER() {
  return getToken(SysY2022Parser::IDENTIFIER, 0);
}

std::vector<tree::TerminalNode *> SysY2022Parser::VarDefContext::L_BRACKET() {
  return getTokens(SysY2022Parser::L_BRACKET);
}

tree::TerminalNode* SysY2022Parser::VarDefContext::L_BRACKET(size_t i) {
  return getToken(SysY2022Parser::L_BRACKET, i);
}

std::vector<SysY2022Parser::ConstExpContext *> SysY2022Parser::VarDefContext::constExp() {
  return getRuleContexts<SysY2022Parser::ConstExpContext>();
}

SysY2022Parser::ConstExpContext* SysY2022Parser::VarDefContext::constExp(size_t i) {
  return getRuleContext<SysY2022Parser::ConstExpContext>(i);
}

std::vector<tree::TerminalNode *> SysY2022Parser::VarDefContext::R_BRACKET() {
  return getTokens(SysY2022Parser::R_BRACKET);
}

tree::TerminalNode* SysY2022Parser::VarDefContext::R_BRACKET(size_t i) {
  return getToken(SysY2022Parser::R_BRACKET, i);
}

tree::TerminalNode* SysY2022Parser::VarDefContext::ASSIGN() {
  return getToken(SysY2022Parser::ASSIGN, 0);
}

SysY2022Parser::InitValContext* SysY2022Parser::VarDefContext::initVal() {
  return getRuleContext<SysY2022Parser::InitValContext>(0);
}


size_t SysY2022Parser::VarDefContext::getRuleIndex() const {
  return SysY2022Parser::RuleVarDef;
}

void SysY2022Parser::VarDefContext::enterRule(tree::ParseTreeListener *listener) {
  auto parserListener = dynamic_cast<SysY2022ParserListener *>(listener);
  if (parserListener != nullptr)
    parserListener->enterVarDef(this);
}

void SysY2022Parser::VarDefContext::exitRule(tree::ParseTreeListener *listener) {
  auto parserListener = dynamic_cast<SysY2022ParserListener *>(listener);
  if (parserListener != nullptr)
    parserListener->exitVarDef(this);
}


std::any SysY2022Parser::VarDefContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<SysY2022ParserVisitor*>(visitor))
    return parserVisitor->visitVarDef(this);
  else
    return visitor->visitChildren(this);
}

SysY2022Parser::VarDefContext* SysY2022Parser::varDef() {
  VarDefContext *_localctx = _tracker.createInstance<VarDefContext>(_ctx, getState());
  enterRule(_localctx, 18, SysY2022Parser::RuleVarDef);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(177);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 14, _ctx)) {
    case 1: {
      enterOuterAlt(_localctx, 1);
      setState(155);
      match(SysY2022Parser::IDENTIFIER);
      setState(162);
      _errHandler->sync(this);
      _la = _input->LA(1);
      while (_la == SysY2022Parser::L_BRACKET) {
        setState(156);
        match(SysY2022Parser::L_BRACKET);
        setState(157);
        constExp();
        setState(158);
        match(SysY2022Parser::R_BRACKET);
        setState(164);
        _errHandler->sync(this);
        _la = _input->LA(1);
      }
      break;
    }

    case 2: {
      enterOuterAlt(_localctx, 2);
      setState(165);
      match(SysY2022Parser::IDENTIFIER);
      setState(172);
      _errHandler->sync(this);
      _la = _input->LA(1);
      while (_la == SysY2022Parser::L_BRACKET) {
        setState(166);
        match(SysY2022Parser::L_BRACKET);
        setState(167);
        constExp();
        setState(168);
        match(SysY2022Parser::R_BRACKET);
        setState(174);
        _errHandler->sync(this);
        _la = _input->LA(1);
      }
      setState(175);
      match(SysY2022Parser::ASSIGN);
      setState(176);
      initVal();
      break;
    }

    default:
      break;
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- InitValContext ------------------------------------------------------------------

SysY2022Parser::InitValContext::InitValContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

SysY2022Parser::ExpContext* SysY2022Parser::InitValContext::exp() {
  return getRuleContext<SysY2022Parser::ExpContext>(0);
}

tree::TerminalNode* SysY2022Parser::InitValContext::L_BRACE() {
  return getToken(SysY2022Parser::L_BRACE, 0);
}

tree::TerminalNode* SysY2022Parser::InitValContext::R_BRACE() {
  return getToken(SysY2022Parser::R_BRACE, 0);
}

std::vector<SysY2022Parser::InitValContext *> SysY2022Parser::InitValContext::initVal() {
  return getRuleContexts<SysY2022Parser::InitValContext>();
}

SysY2022Parser::InitValContext* SysY2022Parser::InitValContext::initVal(size_t i) {
  return getRuleContext<SysY2022Parser::InitValContext>(i);
}

std::vector<tree::TerminalNode *> SysY2022Parser::InitValContext::COMMA() {
  return getTokens(SysY2022Parser::COMMA);
}

tree::TerminalNode* SysY2022Parser::InitValContext::COMMA(size_t i) {
  return getToken(SysY2022Parser::COMMA, i);
}


size_t SysY2022Parser::InitValContext::getRuleIndex() const {
  return SysY2022Parser::RuleInitVal;
}

void SysY2022Parser::InitValContext::enterRule(tree::ParseTreeListener *listener) {
  auto parserListener = dynamic_cast<SysY2022ParserListener *>(listener);
  if (parserListener != nullptr)
    parserListener->enterInitVal(this);
}

void SysY2022Parser::InitValContext::exitRule(tree::ParseTreeListener *listener) {
  auto parserListener = dynamic_cast<SysY2022ParserListener *>(listener);
  if (parserListener != nullptr)
    parserListener->exitInitVal(this);
}


std::any SysY2022Parser::InitValContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<SysY2022ParserVisitor*>(visitor))
    return parserVisitor->visitInitVal(this);
  else
    return visitor->visitChildren(this);
}

SysY2022Parser::InitValContext* SysY2022Parser::initVal() {
  InitValContext *_localctx = _tracker.createInstance<InitValContext>(_ctx, getState());
  enterRule(_localctx, 20, SysY2022Parser::RuleInitVal);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(192);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case SysY2022Parser::L_PAREN:
      case SysY2022Parser::PLUS:
      case SysY2022Parser::MINUS:
      case SysY2022Parser::NOT:
      case SysY2022Parser::IDENTIFIER:
      case SysY2022Parser::INTCONST:
      case SysY2022Parser::FLOATCONST: {
        enterOuterAlt(_localctx, 1);
        setState(179);
        exp();
        break;
      }

      case SysY2022Parser::L_BRACE: {
        enterOuterAlt(_localctx, 2);
        setState(180);
        match(SysY2022Parser::L_BRACE);
        setState(189);
        _errHandler->sync(this);

        _la = _input->LA(1);
        if ((((_la & ~ 0x3fULL) == 0) &&
          ((1ULL << _la) & 962219544576) != 0)) {
          setState(181);
          initVal();
          setState(186);
          _errHandler->sync(this);
          _la = _input->LA(1);
          while (_la == SysY2022Parser::COMMA) {
            setState(182);
            match(SysY2022Parser::COMMA);
            setState(183);
            initVal();
            setState(188);
            _errHandler->sync(this);
            _la = _input->LA(1);
          }
        }
        setState(191);
        match(SysY2022Parser::R_BRACE);
        break;
      }

    default:
      throw NoViableAltException(this);
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- FuncDefContext ------------------------------------------------------------------

SysY2022Parser::FuncDefContext::FuncDefContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

SysY2022Parser::FuncTypeContext* SysY2022Parser::FuncDefContext::funcType() {
  return getRuleContext<SysY2022Parser::FuncTypeContext>(0);
}

tree::TerminalNode* SysY2022Parser::FuncDefContext::IDENTIFIER() {
  return getToken(SysY2022Parser::IDENTIFIER, 0);
}

tree::TerminalNode* SysY2022Parser::FuncDefContext::L_PAREN() {
  return getToken(SysY2022Parser::L_PAREN, 0);
}

tree::TerminalNode* SysY2022Parser::FuncDefContext::R_PAREN() {
  return getToken(SysY2022Parser::R_PAREN, 0);
}

SysY2022Parser::BlockContext* SysY2022Parser::FuncDefContext::block() {
  return getRuleContext<SysY2022Parser::BlockContext>(0);
}

SysY2022Parser::FuncFParamsContext* SysY2022Parser::FuncDefContext::funcFParams() {
  return getRuleContext<SysY2022Parser::FuncFParamsContext>(0);
}


size_t SysY2022Parser::FuncDefContext::getRuleIndex() const {
  return SysY2022Parser::RuleFuncDef;
}

void SysY2022Parser::FuncDefContext::enterRule(tree::ParseTreeListener *listener) {
  auto parserListener = dynamic_cast<SysY2022ParserListener *>(listener);
  if (parserListener != nullptr)
    parserListener->enterFuncDef(this);
}

void SysY2022Parser::FuncDefContext::exitRule(tree::ParseTreeListener *listener) {
  auto parserListener = dynamic_cast<SysY2022ParserListener *>(listener);
  if (parserListener != nullptr)
    parserListener->exitFuncDef(this);
}


std::any SysY2022Parser::FuncDefContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<SysY2022ParserVisitor*>(visitor))
    return parserVisitor->visitFuncDef(this);
  else
    return visitor->visitChildren(this);
}

SysY2022Parser::FuncDefContext* SysY2022Parser::funcDef() {
  FuncDefContext *_localctx = _tracker.createInstance<FuncDefContext>(_ctx, getState());
  enterRule(_localctx, 22, SysY2022Parser::RuleFuncDef);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(194);
    funcType();
    setState(195);
    match(SysY2022Parser::IDENTIFIER);
    setState(196);
    match(SysY2022Parser::L_PAREN);
    setState(198);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == SysY2022Parser::INT

    || _la == SysY2022Parser::FLOAT) {
      setState(197);
      funcFParams();
    }
    setState(200);
    match(SysY2022Parser::R_PAREN);
    setState(201);
    block();
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- FuncTypeContext ------------------------------------------------------------------

SysY2022Parser::FuncTypeContext::FuncTypeContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* SysY2022Parser::FuncTypeContext::VOID() {
  return getToken(SysY2022Parser::VOID, 0);
}

tree::TerminalNode* SysY2022Parser::FuncTypeContext::INT() {
  return getToken(SysY2022Parser::INT, 0);
}

tree::TerminalNode* SysY2022Parser::FuncTypeContext::FLOAT() {
  return getToken(SysY2022Parser::FLOAT, 0);
}


size_t SysY2022Parser::FuncTypeContext::getRuleIndex() const {
  return SysY2022Parser::RuleFuncType;
}

void SysY2022Parser::FuncTypeContext::enterRule(tree::ParseTreeListener *listener) {
  auto parserListener = dynamic_cast<SysY2022ParserListener *>(listener);
  if (parserListener != nullptr)
    parserListener->enterFuncType(this);
}

void SysY2022Parser::FuncTypeContext::exitRule(tree::ParseTreeListener *listener) {
  auto parserListener = dynamic_cast<SysY2022ParserListener *>(listener);
  if (parserListener != nullptr)
    parserListener->exitFuncType(this);
}


std::any SysY2022Parser::FuncTypeContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<SysY2022ParserVisitor*>(visitor))
    return parserVisitor->visitFuncType(this);
  else
    return visitor->visitChildren(this);
}

SysY2022Parser::FuncTypeContext* SysY2022Parser::funcType() {
  FuncTypeContext *_localctx = _tracker.createInstance<FuncTypeContext>(_ctx, getState());
  enterRule(_localctx, 24, SysY2022Parser::RuleFuncType);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(203);
    _la = _input->LA(1);
    if (!((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 14) != 0))) {
    _errHandler->recoverInline(this);
    }
    else {
      _errHandler->reportMatch(this);
      consume();
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- FuncFParamsContext ------------------------------------------------------------------

SysY2022Parser::FuncFParamsContext::FuncFParamsContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

std::vector<SysY2022Parser::FuncFParamContext *> SysY2022Parser::FuncFParamsContext::funcFParam() {
  return getRuleContexts<SysY2022Parser::FuncFParamContext>();
}

SysY2022Parser::FuncFParamContext* SysY2022Parser::FuncFParamsContext::funcFParam(size_t i) {
  return getRuleContext<SysY2022Parser::FuncFParamContext>(i);
}

std::vector<tree::TerminalNode *> SysY2022Parser::FuncFParamsContext::COMMA() {
  return getTokens(SysY2022Parser::COMMA);
}

tree::TerminalNode* SysY2022Parser::FuncFParamsContext::COMMA(size_t i) {
  return getToken(SysY2022Parser::COMMA, i);
}


size_t SysY2022Parser::FuncFParamsContext::getRuleIndex() const {
  return SysY2022Parser::RuleFuncFParams;
}

void SysY2022Parser::FuncFParamsContext::enterRule(tree::ParseTreeListener *listener) {
  auto parserListener = dynamic_cast<SysY2022ParserListener *>(listener);
  if (parserListener != nullptr)
    parserListener->enterFuncFParams(this);
}

void SysY2022Parser::FuncFParamsContext::exitRule(tree::ParseTreeListener *listener) {
  auto parserListener = dynamic_cast<SysY2022ParserListener *>(listener);
  if (parserListener != nullptr)
    parserListener->exitFuncFParams(this);
}


std::any SysY2022Parser::FuncFParamsContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<SysY2022ParserVisitor*>(visitor))
    return parserVisitor->visitFuncFParams(this);
  else
    return visitor->visitChildren(this);
}

SysY2022Parser::FuncFParamsContext* SysY2022Parser::funcFParams() {
  FuncFParamsContext *_localctx = _tracker.createInstance<FuncFParamsContext>(_ctx, getState());
  enterRule(_localctx, 26, SysY2022Parser::RuleFuncFParams);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(205);
    funcFParam();
    setState(210);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == SysY2022Parser::COMMA) {
      setState(206);
      match(SysY2022Parser::COMMA);
      setState(207);
      funcFParam();
      setState(212);
      _errHandler->sync(this);
      _la = _input->LA(1);
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- FuncFParamContext ------------------------------------------------------------------

SysY2022Parser::FuncFParamContext::FuncFParamContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

SysY2022Parser::BTypeContext* SysY2022Parser::FuncFParamContext::bType() {
  return getRuleContext<SysY2022Parser::BTypeContext>(0);
}

tree::TerminalNode* SysY2022Parser::FuncFParamContext::IDENTIFIER() {
  return getToken(SysY2022Parser::IDENTIFIER, 0);
}

std::vector<tree::TerminalNode *> SysY2022Parser::FuncFParamContext::L_BRACKET() {
  return getTokens(SysY2022Parser::L_BRACKET);
}

tree::TerminalNode* SysY2022Parser::FuncFParamContext::L_BRACKET(size_t i) {
  return getToken(SysY2022Parser::L_BRACKET, i);
}

std::vector<tree::TerminalNode *> SysY2022Parser::FuncFParamContext::R_BRACKET() {
  return getTokens(SysY2022Parser::R_BRACKET);
}

tree::TerminalNode* SysY2022Parser::FuncFParamContext::R_BRACKET(size_t i) {
  return getToken(SysY2022Parser::R_BRACKET, i);
}

std::vector<SysY2022Parser::ExpContext *> SysY2022Parser::FuncFParamContext::exp() {
  return getRuleContexts<SysY2022Parser::ExpContext>();
}

SysY2022Parser::ExpContext* SysY2022Parser::FuncFParamContext::exp(size_t i) {
  return getRuleContext<SysY2022Parser::ExpContext>(i);
}


size_t SysY2022Parser::FuncFParamContext::getRuleIndex() const {
  return SysY2022Parser::RuleFuncFParam;
}

void SysY2022Parser::FuncFParamContext::enterRule(tree::ParseTreeListener *listener) {
  auto parserListener = dynamic_cast<SysY2022ParserListener *>(listener);
  if (parserListener != nullptr)
    parserListener->enterFuncFParam(this);
}

void SysY2022Parser::FuncFParamContext::exitRule(tree::ParseTreeListener *listener) {
  auto parserListener = dynamic_cast<SysY2022ParserListener *>(listener);
  if (parserListener != nullptr)
    parserListener->exitFuncFParam(this);
}


std::any SysY2022Parser::FuncFParamContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<SysY2022ParserVisitor*>(visitor))
    return parserVisitor->visitFuncFParam(this);
  else
    return visitor->visitChildren(this);
}

SysY2022Parser::FuncFParamContext* SysY2022Parser::funcFParam() {
  FuncFParamContext *_localctx = _tracker.createInstance<FuncFParamContext>(_ctx, getState());
  enterRule(_localctx, 28, SysY2022Parser::RuleFuncFParam);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(213);
    bType();
    setState(214);
    match(SysY2022Parser::IDENTIFIER);
    setState(226);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == SysY2022Parser::L_BRACKET) {
      setState(215);
      match(SysY2022Parser::L_BRACKET);
      setState(216);
      match(SysY2022Parser::R_BRACKET);
      setState(223);
      _errHandler->sync(this);
      _la = _input->LA(1);
      while (_la == SysY2022Parser::L_BRACKET) {
        setState(217);
        match(SysY2022Parser::L_BRACKET);
        setState(218);
        exp();
        setState(219);
        match(SysY2022Parser::R_BRACKET);
        setState(225);
        _errHandler->sync(this);
        _la = _input->LA(1);
      }
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- BlockContext ------------------------------------------------------------------

SysY2022Parser::BlockContext::BlockContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* SysY2022Parser::BlockContext::L_BRACE() {
  return getToken(SysY2022Parser::L_BRACE, 0);
}

tree::TerminalNode* SysY2022Parser::BlockContext::R_BRACE() {
  return getToken(SysY2022Parser::R_BRACE, 0);
}

std::vector<SysY2022Parser::BlockItemContext *> SysY2022Parser::BlockContext::blockItem() {
  return getRuleContexts<SysY2022Parser::BlockItemContext>();
}

SysY2022Parser::BlockItemContext* SysY2022Parser::BlockContext::blockItem(size_t i) {
  return getRuleContext<SysY2022Parser::BlockItemContext>(i);
}


size_t SysY2022Parser::BlockContext::getRuleIndex() const {
  return SysY2022Parser::RuleBlock;
}

void SysY2022Parser::BlockContext::enterRule(tree::ParseTreeListener *listener) {
  auto parserListener = dynamic_cast<SysY2022ParserListener *>(listener);
  if (parserListener != nullptr)
    parserListener->enterBlock(this);
}

void SysY2022Parser::BlockContext::exitRule(tree::ParseTreeListener *listener) {
  auto parserListener = dynamic_cast<SysY2022ParserListener *>(listener);
  if (parserListener != nullptr)
    parserListener->exitBlock(this);
}


std::any SysY2022Parser::BlockContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<SysY2022ParserVisitor*>(visitor))
    return parserVisitor->visitBlock(this);
  else
    return visitor->visitChildren(this);
}

SysY2022Parser::BlockContext* SysY2022Parser::block() {
  BlockContext *_localctx = _tracker.createInstance<BlockContext>(_ctx, getState());
  enterRule(_localctx, 30, SysY2022Parser::RuleBlock);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(228);
    match(SysY2022Parser::L_BRACE);
    setState(232);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 962220072886) != 0)) {
      setState(229);
      blockItem();
      setState(234);
      _errHandler->sync(this);
      _la = _input->LA(1);
    }
    setState(235);
    match(SysY2022Parser::R_BRACE);
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- BlockItemContext ------------------------------------------------------------------

SysY2022Parser::BlockItemContext::BlockItemContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

SysY2022Parser::DeclContext* SysY2022Parser::BlockItemContext::decl() {
  return getRuleContext<SysY2022Parser::DeclContext>(0);
}

SysY2022Parser::StmtContext* SysY2022Parser::BlockItemContext::stmt() {
  return getRuleContext<SysY2022Parser::StmtContext>(0);
}


size_t SysY2022Parser::BlockItemContext::getRuleIndex() const {
  return SysY2022Parser::RuleBlockItem;
}

void SysY2022Parser::BlockItemContext::enterRule(tree::ParseTreeListener *listener) {
  auto parserListener = dynamic_cast<SysY2022ParserListener *>(listener);
  if (parserListener != nullptr)
    parserListener->enterBlockItem(this);
}

void SysY2022Parser::BlockItemContext::exitRule(tree::ParseTreeListener *listener) {
  auto parserListener = dynamic_cast<SysY2022ParserListener *>(listener);
  if (parserListener != nullptr)
    parserListener->exitBlockItem(this);
}


std::any SysY2022Parser::BlockItemContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<SysY2022ParserVisitor*>(visitor))
    return parserVisitor->visitBlockItem(this);
  else
    return visitor->visitChildren(this);
}

SysY2022Parser::BlockItemContext* SysY2022Parser::blockItem() {
  BlockItemContext *_localctx = _tracker.createInstance<BlockItemContext>(_ctx, getState());
  enterRule(_localctx, 32, SysY2022Parser::RuleBlockItem);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(239);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case SysY2022Parser::INT:
      case SysY2022Parser::FLOAT:
      case SysY2022Parser::CONST:
      case SysY2022Parser::VEC_N: {
        enterOuterAlt(_localctx, 1);
        setState(237);
        decl();
        break;
      }

      case SysY2022Parser::IF:
      case SysY2022Parser::WHILE:
      case SysY2022Parser::BREAK:
      case SysY2022Parser::CONTINUE:
      case SysY2022Parser::RETURN:
      case SysY2022Parser::L_PAREN:
      case SysY2022Parser::L_BRACE:
      case SysY2022Parser::SEMICOLON:
      case SysY2022Parser::PLUS:
      case SysY2022Parser::MINUS:
      case SysY2022Parser::NOT:
      case SysY2022Parser::IDENTIFIER:
      case SysY2022Parser::INTCONST:
      case SysY2022Parser::FLOATCONST: {
        enterOuterAlt(_localctx, 2);
        setState(238);
        stmt();
        break;
      }

    default:
      throw NoViableAltException(this);
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- StmtContext ------------------------------------------------------------------

SysY2022Parser::StmtContext::StmtContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

SysY2022Parser::LValContext* SysY2022Parser::StmtContext::lVal() {
  return getRuleContext<SysY2022Parser::LValContext>(0);
}

tree::TerminalNode* SysY2022Parser::StmtContext::ASSIGN() {
  return getToken(SysY2022Parser::ASSIGN, 0);
}

SysY2022Parser::ExpContext* SysY2022Parser::StmtContext::exp() {
  return getRuleContext<SysY2022Parser::ExpContext>(0);
}

tree::TerminalNode* SysY2022Parser::StmtContext::SEMICOLON() {
  return getToken(SysY2022Parser::SEMICOLON, 0);
}

SysY2022Parser::BlockContext* SysY2022Parser::StmtContext::block() {
  return getRuleContext<SysY2022Parser::BlockContext>(0);
}

tree::TerminalNode* SysY2022Parser::StmtContext::IF() {
  return getToken(SysY2022Parser::IF, 0);
}

tree::TerminalNode* SysY2022Parser::StmtContext::L_PAREN() {
  return getToken(SysY2022Parser::L_PAREN, 0);
}

SysY2022Parser::CondContext* SysY2022Parser::StmtContext::cond() {
  return getRuleContext<SysY2022Parser::CondContext>(0);
}

tree::TerminalNode* SysY2022Parser::StmtContext::R_PAREN() {
  return getToken(SysY2022Parser::R_PAREN, 0);
}

std::vector<SysY2022Parser::StmtContext *> SysY2022Parser::StmtContext::stmt() {
  return getRuleContexts<SysY2022Parser::StmtContext>();
}

SysY2022Parser::StmtContext* SysY2022Parser::StmtContext::stmt(size_t i) {
  return getRuleContext<SysY2022Parser::StmtContext>(i);
}

tree::TerminalNode* SysY2022Parser::StmtContext::ELSE() {
  return getToken(SysY2022Parser::ELSE, 0);
}

tree::TerminalNode* SysY2022Parser::StmtContext::WHILE() {
  return getToken(SysY2022Parser::WHILE, 0);
}

tree::TerminalNode* SysY2022Parser::StmtContext::BREAK() {
  return getToken(SysY2022Parser::BREAK, 0);
}

tree::TerminalNode* SysY2022Parser::StmtContext::CONTINUE() {
  return getToken(SysY2022Parser::CONTINUE, 0);
}

tree::TerminalNode* SysY2022Parser::StmtContext::RETURN() {
  return getToken(SysY2022Parser::RETURN, 0);
}


size_t SysY2022Parser::StmtContext::getRuleIndex() const {
  return SysY2022Parser::RuleStmt;
}

void SysY2022Parser::StmtContext::enterRule(tree::ParseTreeListener *listener) {
  auto parserListener = dynamic_cast<SysY2022ParserListener *>(listener);
  if (parserListener != nullptr)
    parserListener->enterStmt(this);
}

void SysY2022Parser::StmtContext::exitRule(tree::ParseTreeListener *listener) {
  auto parserListener = dynamic_cast<SysY2022ParserListener *>(listener);
  if (parserListener != nullptr)
    parserListener->exitStmt(this);
}


std::any SysY2022Parser::StmtContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<SysY2022ParserVisitor*>(visitor))
    return parserVisitor->visitStmt(this);
  else
    return visitor->visitChildren(this);
}

SysY2022Parser::StmtContext* SysY2022Parser::stmt() {
  StmtContext *_localctx = _tracker.createInstance<StmtContext>(_ctx, getState());
  enterRule(_localctx, 34, SysY2022Parser::RuleStmt);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(275);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 27, _ctx)) {
    case 1: {
      enterOuterAlt(_localctx, 1);
      setState(241);
      lVal();
      setState(242);
      match(SysY2022Parser::ASSIGN);
      setState(243);
      exp();
      setState(244);
      match(SysY2022Parser::SEMICOLON);
      break;
    }

    case 2: {
      enterOuterAlt(_localctx, 2);
      setState(247);
      _errHandler->sync(this);

      _la = _input->LA(1);
      if ((((_la & ~ 0x3fULL) == 0) &&
        ((1ULL << _la) & 962219479040) != 0)) {
        setState(246);
        exp();
      }
      setState(249);
      match(SysY2022Parser::SEMICOLON);
      break;
    }

    case 3: {
      enterOuterAlt(_localctx, 3);
      setState(250);
      block();
      break;
    }

    case 4: {
      enterOuterAlt(_localctx, 4);
      setState(251);
      match(SysY2022Parser::IF);
      setState(252);
      match(SysY2022Parser::L_PAREN);
      setState(253);
      cond();
      setState(254);
      match(SysY2022Parser::R_PAREN);
      setState(255);
      stmt();
      setState(258);
      _errHandler->sync(this);

      switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 25, _ctx)) {
      case 1: {
        setState(256);
        match(SysY2022Parser::ELSE);
        setState(257);
        stmt();
        break;
      }

      default:
        break;
      }
      break;
    }

    case 5: {
      enterOuterAlt(_localctx, 5);
      setState(260);
      match(SysY2022Parser::WHILE);
      setState(261);
      match(SysY2022Parser::L_PAREN);
      setState(262);
      cond();
      setState(263);
      match(SysY2022Parser::R_PAREN);
      setState(264);
      stmt();
      break;
    }

    case 6: {
      enterOuterAlt(_localctx, 6);
      setState(266);
      match(SysY2022Parser::BREAK);
      setState(267);
      match(SysY2022Parser::SEMICOLON);
      break;
    }

    case 7: {
      enterOuterAlt(_localctx, 7);
      setState(268);
      match(SysY2022Parser::CONTINUE);
      setState(269);
      match(SysY2022Parser::SEMICOLON);
      break;
    }

    case 8: {
      enterOuterAlt(_localctx, 8);
      setState(270);
      match(SysY2022Parser::RETURN);
      setState(272);
      _errHandler->sync(this);

      _la = _input->LA(1);
      if ((((_la & ~ 0x3fULL) == 0) &&
        ((1ULL << _la) & 962219479040) != 0)) {
        setState(271);
        exp();
      }
      setState(274);
      match(SysY2022Parser::SEMICOLON);
      break;
    }

    default:
      break;
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- ExpContext ------------------------------------------------------------------

SysY2022Parser::ExpContext::ExpContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

SysY2022Parser::AddExpContext* SysY2022Parser::ExpContext::addExp() {
  return getRuleContext<SysY2022Parser::AddExpContext>(0);
}


size_t SysY2022Parser::ExpContext::getRuleIndex() const {
  return SysY2022Parser::RuleExp;
}

void SysY2022Parser::ExpContext::enterRule(tree::ParseTreeListener *listener) {
  auto parserListener = dynamic_cast<SysY2022ParserListener *>(listener);
  if (parserListener != nullptr)
    parserListener->enterExp(this);
}

void SysY2022Parser::ExpContext::exitRule(tree::ParseTreeListener *listener) {
  auto parserListener = dynamic_cast<SysY2022ParserListener *>(listener);
  if (parserListener != nullptr)
    parserListener->exitExp(this);
}


std::any SysY2022Parser::ExpContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<SysY2022ParserVisitor*>(visitor))
    return parserVisitor->visitExp(this);
  else
    return visitor->visitChildren(this);
}

SysY2022Parser::ExpContext* SysY2022Parser::exp() {
  ExpContext *_localctx = _tracker.createInstance<ExpContext>(_ctx, getState());
  enterRule(_localctx, 36, SysY2022Parser::RuleExp);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(277);
    addExp(0);
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- CondContext ------------------------------------------------------------------

SysY2022Parser::CondContext::CondContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

SysY2022Parser::LOrExpContext* SysY2022Parser::CondContext::lOrExp() {
  return getRuleContext<SysY2022Parser::LOrExpContext>(0);
}


size_t SysY2022Parser::CondContext::getRuleIndex() const {
  return SysY2022Parser::RuleCond;
}

void SysY2022Parser::CondContext::enterRule(tree::ParseTreeListener *listener) {
  auto parserListener = dynamic_cast<SysY2022ParserListener *>(listener);
  if (parserListener != nullptr)
    parserListener->enterCond(this);
}

void SysY2022Parser::CondContext::exitRule(tree::ParseTreeListener *listener) {
  auto parserListener = dynamic_cast<SysY2022ParserListener *>(listener);
  if (parserListener != nullptr)
    parserListener->exitCond(this);
}


std::any SysY2022Parser::CondContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<SysY2022ParserVisitor*>(visitor))
    return parserVisitor->visitCond(this);
  else
    return visitor->visitChildren(this);
}

SysY2022Parser::CondContext* SysY2022Parser::cond() {
  CondContext *_localctx = _tracker.createInstance<CondContext>(_ctx, getState());
  enterRule(_localctx, 38, SysY2022Parser::RuleCond);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(279);
    lOrExp(0);
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- LValContext ------------------------------------------------------------------

SysY2022Parser::LValContext::LValContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* SysY2022Parser::LValContext::IDENTIFIER() {
  return getToken(SysY2022Parser::IDENTIFIER, 0);
}

std::vector<tree::TerminalNode *> SysY2022Parser::LValContext::L_BRACKET() {
  return getTokens(SysY2022Parser::L_BRACKET);
}

tree::TerminalNode* SysY2022Parser::LValContext::L_BRACKET(size_t i) {
  return getToken(SysY2022Parser::L_BRACKET, i);
}

std::vector<SysY2022Parser::ExpContext *> SysY2022Parser::LValContext::exp() {
  return getRuleContexts<SysY2022Parser::ExpContext>();
}

SysY2022Parser::ExpContext* SysY2022Parser::LValContext::exp(size_t i) {
  return getRuleContext<SysY2022Parser::ExpContext>(i);
}

std::vector<tree::TerminalNode *> SysY2022Parser::LValContext::R_BRACKET() {
  return getTokens(SysY2022Parser::R_BRACKET);
}

tree::TerminalNode* SysY2022Parser::LValContext::R_BRACKET(size_t i) {
  return getToken(SysY2022Parser::R_BRACKET, i);
}


size_t SysY2022Parser::LValContext::getRuleIndex() const {
  return SysY2022Parser::RuleLVal;
}

void SysY2022Parser::LValContext::enterRule(tree::ParseTreeListener *listener) {
  auto parserListener = dynamic_cast<SysY2022ParserListener *>(listener);
  if (parserListener != nullptr)
    parserListener->enterLVal(this);
}

void SysY2022Parser::LValContext::exitRule(tree::ParseTreeListener *listener) {
  auto parserListener = dynamic_cast<SysY2022ParserListener *>(listener);
  if (parserListener != nullptr)
    parserListener->exitLVal(this);
}


std::any SysY2022Parser::LValContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<SysY2022ParserVisitor*>(visitor))
    return parserVisitor->visitLVal(this);
  else
    return visitor->visitChildren(this);
}

SysY2022Parser::LValContext* SysY2022Parser::lVal() {
  LValContext *_localctx = _tracker.createInstance<LValContext>(_ctx, getState());
  enterRule(_localctx, 40, SysY2022Parser::RuleLVal);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    size_t alt;
    enterOuterAlt(_localctx, 1);
    setState(281);
    match(SysY2022Parser::IDENTIFIER);
    setState(288);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 28, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(282);
        match(SysY2022Parser::L_BRACKET);
        setState(283);
        exp();
        setState(284);
        match(SysY2022Parser::R_BRACKET); 
      }
      setState(290);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 28, _ctx);
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- PrimaryExpContext ------------------------------------------------------------------

SysY2022Parser::PrimaryExpContext::PrimaryExpContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* SysY2022Parser::PrimaryExpContext::L_PAREN() {
  return getToken(SysY2022Parser::L_PAREN, 0);
}

SysY2022Parser::ExpContext* SysY2022Parser::PrimaryExpContext::exp() {
  return getRuleContext<SysY2022Parser::ExpContext>(0);
}

tree::TerminalNode* SysY2022Parser::PrimaryExpContext::R_PAREN() {
  return getToken(SysY2022Parser::R_PAREN, 0);
}

SysY2022Parser::LValContext* SysY2022Parser::PrimaryExpContext::lVal() {
  return getRuleContext<SysY2022Parser::LValContext>(0);
}

SysY2022Parser::NumberContext* SysY2022Parser::PrimaryExpContext::number() {
  return getRuleContext<SysY2022Parser::NumberContext>(0);
}


size_t SysY2022Parser::PrimaryExpContext::getRuleIndex() const {
  return SysY2022Parser::RulePrimaryExp;
}

void SysY2022Parser::PrimaryExpContext::enterRule(tree::ParseTreeListener *listener) {
  auto parserListener = dynamic_cast<SysY2022ParserListener *>(listener);
  if (parserListener != nullptr)
    parserListener->enterPrimaryExp(this);
}

void SysY2022Parser::PrimaryExpContext::exitRule(tree::ParseTreeListener *listener) {
  auto parserListener = dynamic_cast<SysY2022ParserListener *>(listener);
  if (parserListener != nullptr)
    parserListener->exitPrimaryExp(this);
}


std::any SysY2022Parser::PrimaryExpContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<SysY2022ParserVisitor*>(visitor))
    return parserVisitor->visitPrimaryExp(this);
  else
    return visitor->visitChildren(this);
}

SysY2022Parser::PrimaryExpContext* SysY2022Parser::primaryExp() {
  PrimaryExpContext *_localctx = _tracker.createInstance<PrimaryExpContext>(_ctx, getState());
  enterRule(_localctx, 42, SysY2022Parser::RulePrimaryExp);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(297);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case SysY2022Parser::L_PAREN: {
        enterOuterAlt(_localctx, 1);
        setState(291);
        match(SysY2022Parser::L_PAREN);
        setState(292);
        exp();
        setState(293);
        match(SysY2022Parser::R_PAREN);
        break;
      }

      case SysY2022Parser::IDENTIFIER: {
        enterOuterAlt(_localctx, 2);
        setState(295);
        lVal();
        break;
      }

      case SysY2022Parser::INTCONST:
      case SysY2022Parser::FLOATCONST: {
        enterOuterAlt(_localctx, 3);
        setState(296);
        number();
        break;
      }

    default:
      throw NoViableAltException(this);
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- NumberContext ------------------------------------------------------------------

SysY2022Parser::NumberContext::NumberContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* SysY2022Parser::NumberContext::INTCONST() {
  return getToken(SysY2022Parser::INTCONST, 0);
}

tree::TerminalNode* SysY2022Parser::NumberContext::FLOATCONST() {
  return getToken(SysY2022Parser::FLOATCONST, 0);
}


size_t SysY2022Parser::NumberContext::getRuleIndex() const {
  return SysY2022Parser::RuleNumber;
}

void SysY2022Parser::NumberContext::enterRule(tree::ParseTreeListener *listener) {
  auto parserListener = dynamic_cast<SysY2022ParserListener *>(listener);
  if (parserListener != nullptr)
    parserListener->enterNumber(this);
}

void SysY2022Parser::NumberContext::exitRule(tree::ParseTreeListener *listener) {
  auto parserListener = dynamic_cast<SysY2022ParserListener *>(listener);
  if (parserListener != nullptr)
    parserListener->exitNumber(this);
}


std::any SysY2022Parser::NumberContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<SysY2022ParserVisitor*>(visitor))
    return parserVisitor->visitNumber(this);
  else
    return visitor->visitChildren(this);
}

SysY2022Parser::NumberContext* SysY2022Parser::number() {
  NumberContext *_localctx = _tracker.createInstance<NumberContext>(_ctx, getState());
  enterRule(_localctx, 44, SysY2022Parser::RuleNumber);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(299);
    _la = _input->LA(1);
    if (!(_la == SysY2022Parser::INTCONST

    || _la == SysY2022Parser::FLOATCONST)) {
    _errHandler->recoverInline(this);
    }
    else {
      _errHandler->reportMatch(this);
      consume();
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- UnaryExpContext ------------------------------------------------------------------

SysY2022Parser::UnaryExpContext::UnaryExpContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

SysY2022Parser::PrimaryExpContext* SysY2022Parser::UnaryExpContext::primaryExp() {
  return getRuleContext<SysY2022Parser::PrimaryExpContext>(0);
}

tree::TerminalNode* SysY2022Parser::UnaryExpContext::IDENTIFIER() {
  return getToken(SysY2022Parser::IDENTIFIER, 0);
}

tree::TerminalNode* SysY2022Parser::UnaryExpContext::L_PAREN() {
  return getToken(SysY2022Parser::L_PAREN, 0);
}

tree::TerminalNode* SysY2022Parser::UnaryExpContext::R_PAREN() {
  return getToken(SysY2022Parser::R_PAREN, 0);
}

SysY2022Parser::FuncRParamsContext* SysY2022Parser::UnaryExpContext::funcRParams() {
  return getRuleContext<SysY2022Parser::FuncRParamsContext>(0);
}

SysY2022Parser::UnaryOpContext* SysY2022Parser::UnaryExpContext::unaryOp() {
  return getRuleContext<SysY2022Parser::UnaryOpContext>(0);
}

SysY2022Parser::UnaryExpContext* SysY2022Parser::UnaryExpContext::unaryExp() {
  return getRuleContext<SysY2022Parser::UnaryExpContext>(0);
}


size_t SysY2022Parser::UnaryExpContext::getRuleIndex() const {
  return SysY2022Parser::RuleUnaryExp;
}

void SysY2022Parser::UnaryExpContext::enterRule(tree::ParseTreeListener *listener) {
  auto parserListener = dynamic_cast<SysY2022ParserListener *>(listener);
  if (parserListener != nullptr)
    parserListener->enterUnaryExp(this);
}

void SysY2022Parser::UnaryExpContext::exitRule(tree::ParseTreeListener *listener) {
  auto parserListener = dynamic_cast<SysY2022ParserListener *>(listener);
  if (parserListener != nullptr)
    parserListener->exitUnaryExp(this);
}


std::any SysY2022Parser::UnaryExpContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<SysY2022ParserVisitor*>(visitor))
    return parserVisitor->visitUnaryExp(this);
  else
    return visitor->visitChildren(this);
}

SysY2022Parser::UnaryExpContext* SysY2022Parser::unaryExp() {
  UnaryExpContext *_localctx = _tracker.createInstance<UnaryExpContext>(_ctx, getState());
  enterRule(_localctx, 46, SysY2022Parser::RuleUnaryExp);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(311);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 31, _ctx)) {
    case 1: {
      enterOuterAlt(_localctx, 1);
      setState(301);
      primaryExp();
      break;
    }

    case 2: {
      enterOuterAlt(_localctx, 2);
      setState(302);
      match(SysY2022Parser::IDENTIFIER);
      setState(303);
      match(SysY2022Parser::L_PAREN);
      setState(305);
      _errHandler->sync(this);

      _la = _input->LA(1);
      if ((((_la & ~ 0x3fULL) == 0) &&
        ((1ULL << _la) & 962219479040) != 0)) {
        setState(304);
        funcRParams();
      }
      setState(307);
      match(SysY2022Parser::R_PAREN);
      break;
    }

    case 3: {
      enterOuterAlt(_localctx, 3);
      setState(308);
      unaryOp();
      setState(309);
      unaryExp();
      break;
    }

    default:
      break;
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- UnaryOpContext ------------------------------------------------------------------

SysY2022Parser::UnaryOpContext::UnaryOpContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* SysY2022Parser::UnaryOpContext::PLUS() {
  return getToken(SysY2022Parser::PLUS, 0);
}

tree::TerminalNode* SysY2022Parser::UnaryOpContext::MINUS() {
  return getToken(SysY2022Parser::MINUS, 0);
}

tree::TerminalNode* SysY2022Parser::UnaryOpContext::NOT() {
  return getToken(SysY2022Parser::NOT, 0);
}


size_t SysY2022Parser::UnaryOpContext::getRuleIndex() const {
  return SysY2022Parser::RuleUnaryOp;
}

void SysY2022Parser::UnaryOpContext::enterRule(tree::ParseTreeListener *listener) {
  auto parserListener = dynamic_cast<SysY2022ParserListener *>(listener);
  if (parserListener != nullptr)
    parserListener->enterUnaryOp(this);
}

void SysY2022Parser::UnaryOpContext::exitRule(tree::ParseTreeListener *listener) {
  auto parserListener = dynamic_cast<SysY2022ParserListener *>(listener);
  if (parserListener != nullptr)
    parserListener->exitUnaryOp(this);
}


std::any SysY2022Parser::UnaryOpContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<SysY2022ParserVisitor*>(visitor))
    return parserVisitor->visitUnaryOp(this);
  else
    return visitor->visitChildren(this);
}

SysY2022Parser::UnaryOpContext* SysY2022Parser::unaryOp() {
  UnaryOpContext *_localctx = _tracker.createInstance<UnaryOpContext>(_ctx, getState());
  enterRule(_localctx, 48, SysY2022Parser::RuleUnaryOp);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(313);
    _la = _input->LA(1);
    if (!((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 146800640) != 0))) {
    _errHandler->recoverInline(this);
    }
    else {
      _errHandler->reportMatch(this);
      consume();
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- FuncRParamsContext ------------------------------------------------------------------

SysY2022Parser::FuncRParamsContext::FuncRParamsContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

std::vector<SysY2022Parser::ExpContext *> SysY2022Parser::FuncRParamsContext::exp() {
  return getRuleContexts<SysY2022Parser::ExpContext>();
}

SysY2022Parser::ExpContext* SysY2022Parser::FuncRParamsContext::exp(size_t i) {
  return getRuleContext<SysY2022Parser::ExpContext>(i);
}

std::vector<tree::TerminalNode *> SysY2022Parser::FuncRParamsContext::COMMA() {
  return getTokens(SysY2022Parser::COMMA);
}

tree::TerminalNode* SysY2022Parser::FuncRParamsContext::COMMA(size_t i) {
  return getToken(SysY2022Parser::COMMA, i);
}


size_t SysY2022Parser::FuncRParamsContext::getRuleIndex() const {
  return SysY2022Parser::RuleFuncRParams;
}

void SysY2022Parser::FuncRParamsContext::enterRule(tree::ParseTreeListener *listener) {
  auto parserListener = dynamic_cast<SysY2022ParserListener *>(listener);
  if (parserListener != nullptr)
    parserListener->enterFuncRParams(this);
}

void SysY2022Parser::FuncRParamsContext::exitRule(tree::ParseTreeListener *listener) {
  auto parserListener = dynamic_cast<SysY2022ParserListener *>(listener);
  if (parserListener != nullptr)
    parserListener->exitFuncRParams(this);
}


std::any SysY2022Parser::FuncRParamsContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<SysY2022ParserVisitor*>(visitor))
    return parserVisitor->visitFuncRParams(this);
  else
    return visitor->visitChildren(this);
}

SysY2022Parser::FuncRParamsContext* SysY2022Parser::funcRParams() {
  FuncRParamsContext *_localctx = _tracker.createInstance<FuncRParamsContext>(_ctx, getState());
  enterRule(_localctx, 50, SysY2022Parser::RuleFuncRParams);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(315);
    exp();
    setState(320);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == SysY2022Parser::COMMA) {
      setState(316);
      match(SysY2022Parser::COMMA);
      setState(317);
      exp();
      setState(322);
      _errHandler->sync(this);
      _la = _input->LA(1);
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- MulExpContext ------------------------------------------------------------------

SysY2022Parser::MulExpContext::MulExpContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

SysY2022Parser::UnaryExpContext* SysY2022Parser::MulExpContext::unaryExp() {
  return getRuleContext<SysY2022Parser::UnaryExpContext>(0);
}

SysY2022Parser::MulExpContext* SysY2022Parser::MulExpContext::mulExp() {
  return getRuleContext<SysY2022Parser::MulExpContext>(0);
}

tree::TerminalNode* SysY2022Parser::MulExpContext::STAR() {
  return getToken(SysY2022Parser::STAR, 0);
}

tree::TerminalNode* SysY2022Parser::MulExpContext::DIV() {
  return getToken(SysY2022Parser::DIV, 0);
}

tree::TerminalNode* SysY2022Parser::MulExpContext::MOD() {
  return getToken(SysY2022Parser::MOD, 0);
}


size_t SysY2022Parser::MulExpContext::getRuleIndex() const {
  return SysY2022Parser::RuleMulExp;
}

void SysY2022Parser::MulExpContext::enterRule(tree::ParseTreeListener *listener) {
  auto parserListener = dynamic_cast<SysY2022ParserListener *>(listener);
  if (parserListener != nullptr)
    parserListener->enterMulExp(this);
}

void SysY2022Parser::MulExpContext::exitRule(tree::ParseTreeListener *listener) {
  auto parserListener = dynamic_cast<SysY2022ParserListener *>(listener);
  if (parserListener != nullptr)
    parserListener->exitMulExp(this);
}


std::any SysY2022Parser::MulExpContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<SysY2022ParserVisitor*>(visitor))
    return parserVisitor->visitMulExp(this);
  else
    return visitor->visitChildren(this);
}


SysY2022Parser::MulExpContext* SysY2022Parser::mulExp() {
   return mulExp(0);
}

SysY2022Parser::MulExpContext* SysY2022Parser::mulExp(int precedence) {
  ParserRuleContext *parentContext = _ctx;
  size_t parentState = getState();
  SysY2022Parser::MulExpContext *_localctx = _tracker.createInstance<MulExpContext>(_ctx, parentState);
  SysY2022Parser::MulExpContext *previousContext = _localctx;
  (void)previousContext; // Silence compiler, in case the context is not used by generated code.
  size_t startState = 52;
  enterRecursionRule(_localctx, 52, SysY2022Parser::RuleMulExp, precedence);

    size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    unrollRecursionContexts(parentContext);
  });
  try {
    size_t alt;
    enterOuterAlt(_localctx, 1);
    setState(324);
    unaryExp();
    _ctx->stop = _input->LT(-1);
    setState(331);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 33, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        if (!_parseListeners.empty())
          triggerExitRuleEvent();
        previousContext = _localctx;
        _localctx = _tracker.createInstance<MulExpContext>(parentContext, parentState);
        pushNewRecursionContext(_localctx, startState, RuleMulExp);
        setState(326);

        if (!(precpred(_ctx, 1))) throw FailedPredicateException(this, "precpred(_ctx, 1)");
        setState(327);
        _la = _input->LA(1);
        if (!((((_la & ~ 0x3fULL) == 0) &&
          ((1ULL << _la) & 117440512) != 0))) {
        _errHandler->recoverInline(this);
        }
        else {
          _errHandler->reportMatch(this);
          consume();
        }
        setState(328);
        unaryExp(); 
      }
      setState(333);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 33, _ctx);
    }
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }
  return _localctx;
}

//----------------- AddExpContext ------------------------------------------------------------------

SysY2022Parser::AddExpContext::AddExpContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

SysY2022Parser::MulExpContext* SysY2022Parser::AddExpContext::mulExp() {
  return getRuleContext<SysY2022Parser::MulExpContext>(0);
}

SysY2022Parser::AddExpContext* SysY2022Parser::AddExpContext::addExp() {
  return getRuleContext<SysY2022Parser::AddExpContext>(0);
}

tree::TerminalNode* SysY2022Parser::AddExpContext::PLUS() {
  return getToken(SysY2022Parser::PLUS, 0);
}

tree::TerminalNode* SysY2022Parser::AddExpContext::MINUS() {
  return getToken(SysY2022Parser::MINUS, 0);
}


size_t SysY2022Parser::AddExpContext::getRuleIndex() const {
  return SysY2022Parser::RuleAddExp;
}

void SysY2022Parser::AddExpContext::enterRule(tree::ParseTreeListener *listener) {
  auto parserListener = dynamic_cast<SysY2022ParserListener *>(listener);
  if (parserListener != nullptr)
    parserListener->enterAddExp(this);
}

void SysY2022Parser::AddExpContext::exitRule(tree::ParseTreeListener *listener) {
  auto parserListener = dynamic_cast<SysY2022ParserListener *>(listener);
  if (parserListener != nullptr)
    parserListener->exitAddExp(this);
}


std::any SysY2022Parser::AddExpContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<SysY2022ParserVisitor*>(visitor))
    return parserVisitor->visitAddExp(this);
  else
    return visitor->visitChildren(this);
}


SysY2022Parser::AddExpContext* SysY2022Parser::addExp() {
   return addExp(0);
}

SysY2022Parser::AddExpContext* SysY2022Parser::addExp(int precedence) {
  ParserRuleContext *parentContext = _ctx;
  size_t parentState = getState();
  SysY2022Parser::AddExpContext *_localctx = _tracker.createInstance<AddExpContext>(_ctx, parentState);
  SysY2022Parser::AddExpContext *previousContext = _localctx;
  (void)previousContext; // Silence compiler, in case the context is not used by generated code.
  size_t startState = 54;
  enterRecursionRule(_localctx, 54, SysY2022Parser::RuleAddExp, precedence);

    size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    unrollRecursionContexts(parentContext);
  });
  try {
    size_t alt;
    enterOuterAlt(_localctx, 1);
    setState(335);
    mulExp(0);
    _ctx->stop = _input->LT(-1);
    setState(342);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 34, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        if (!_parseListeners.empty())
          triggerExitRuleEvent();
        previousContext = _localctx;
        _localctx = _tracker.createInstance<AddExpContext>(parentContext, parentState);
        pushNewRecursionContext(_localctx, startState, RuleAddExp);
        setState(337);

        if (!(precpred(_ctx, 1))) throw FailedPredicateException(this, "precpred(_ctx, 1)");
        setState(338);
        _la = _input->LA(1);
        if (!(_la == SysY2022Parser::PLUS

        || _la == SysY2022Parser::MINUS)) {
        _errHandler->recoverInline(this);
        }
        else {
          _errHandler->reportMatch(this);
          consume();
        }
        setState(339);
        mulExp(0); 
      }
      setState(344);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 34, _ctx);
    }
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }
  return _localctx;
}

//----------------- RelExpContext ------------------------------------------------------------------

SysY2022Parser::RelExpContext::RelExpContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

SysY2022Parser::AddExpContext* SysY2022Parser::RelExpContext::addExp() {
  return getRuleContext<SysY2022Parser::AddExpContext>(0);
}

SysY2022Parser::RelExpContext* SysY2022Parser::RelExpContext::relExp() {
  return getRuleContext<SysY2022Parser::RelExpContext>(0);
}

tree::TerminalNode* SysY2022Parser::RelExpContext::LT() {
  return getToken(SysY2022Parser::LT, 0);
}

tree::TerminalNode* SysY2022Parser::RelExpContext::GT() {
  return getToken(SysY2022Parser::GT, 0);
}

tree::TerminalNode* SysY2022Parser::RelExpContext::LE() {
  return getToken(SysY2022Parser::LE, 0);
}

tree::TerminalNode* SysY2022Parser::RelExpContext::GE() {
  return getToken(SysY2022Parser::GE, 0);
}


size_t SysY2022Parser::RelExpContext::getRuleIndex() const {
  return SysY2022Parser::RuleRelExp;
}

void SysY2022Parser::RelExpContext::enterRule(tree::ParseTreeListener *listener) {
  auto parserListener = dynamic_cast<SysY2022ParserListener *>(listener);
  if (parserListener != nullptr)
    parserListener->enterRelExp(this);
}

void SysY2022Parser::RelExpContext::exitRule(tree::ParseTreeListener *listener) {
  auto parserListener = dynamic_cast<SysY2022ParserListener *>(listener);
  if (parserListener != nullptr)
    parserListener->exitRelExp(this);
}


std::any SysY2022Parser::RelExpContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<SysY2022ParserVisitor*>(visitor))
    return parserVisitor->visitRelExp(this);
  else
    return visitor->visitChildren(this);
}


SysY2022Parser::RelExpContext* SysY2022Parser::relExp() {
   return relExp(0);
}

SysY2022Parser::RelExpContext* SysY2022Parser::relExp(int precedence) {
  ParserRuleContext *parentContext = _ctx;
  size_t parentState = getState();
  SysY2022Parser::RelExpContext *_localctx = _tracker.createInstance<RelExpContext>(_ctx, parentState);
  SysY2022Parser::RelExpContext *previousContext = _localctx;
  (void)previousContext; // Silence compiler, in case the context is not used by generated code.
  size_t startState = 56;
  enterRecursionRule(_localctx, 56, SysY2022Parser::RuleRelExp, precedence);

    size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    unrollRecursionContexts(parentContext);
  });
  try {
    size_t alt;
    enterOuterAlt(_localctx, 1);
    setState(346);
    addExp(0);
    _ctx->stop = _input->LT(-1);
    setState(353);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 35, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        if (!_parseListeners.empty())
          triggerExitRuleEvent();
        previousContext = _localctx;
        _localctx = _tracker.createInstance<RelExpContext>(parentContext, parentState);
        pushNewRecursionContext(_localctx, startState, RuleRelExp);
        setState(348);

        if (!(precpred(_ctx, 1))) throw FailedPredicateException(this, "precpred(_ctx, 1)");
        setState(349);
        _la = _input->LA(1);
        if (!((((_la & ~ 0x3fULL) == 0) &&
          ((1ULL << _la) & 16106127360) != 0))) {
        _errHandler->recoverInline(this);
        }
        else {
          _errHandler->reportMatch(this);
          consume();
        }
        setState(350);
        addExp(0); 
      }
      setState(355);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 35, _ctx);
    }
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }
  return _localctx;
}

//----------------- EqExpContext ------------------------------------------------------------------

SysY2022Parser::EqExpContext::EqExpContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

SysY2022Parser::RelExpContext* SysY2022Parser::EqExpContext::relExp() {
  return getRuleContext<SysY2022Parser::RelExpContext>(0);
}

SysY2022Parser::EqExpContext* SysY2022Parser::EqExpContext::eqExp() {
  return getRuleContext<SysY2022Parser::EqExpContext>(0);
}

tree::TerminalNode* SysY2022Parser::EqExpContext::EQ() {
  return getToken(SysY2022Parser::EQ, 0);
}

tree::TerminalNode* SysY2022Parser::EqExpContext::NE() {
  return getToken(SysY2022Parser::NE, 0);
}


size_t SysY2022Parser::EqExpContext::getRuleIndex() const {
  return SysY2022Parser::RuleEqExp;
}

void SysY2022Parser::EqExpContext::enterRule(tree::ParseTreeListener *listener) {
  auto parserListener = dynamic_cast<SysY2022ParserListener *>(listener);
  if (parserListener != nullptr)
    parserListener->enterEqExp(this);
}

void SysY2022Parser::EqExpContext::exitRule(tree::ParseTreeListener *listener) {
  auto parserListener = dynamic_cast<SysY2022ParserListener *>(listener);
  if (parserListener != nullptr)
    parserListener->exitEqExp(this);
}


std::any SysY2022Parser::EqExpContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<SysY2022ParserVisitor*>(visitor))
    return parserVisitor->visitEqExp(this);
  else
    return visitor->visitChildren(this);
}


SysY2022Parser::EqExpContext* SysY2022Parser::eqExp() {
   return eqExp(0);
}

SysY2022Parser::EqExpContext* SysY2022Parser::eqExp(int precedence) {
  ParserRuleContext *parentContext = _ctx;
  size_t parentState = getState();
  SysY2022Parser::EqExpContext *_localctx = _tracker.createInstance<EqExpContext>(_ctx, parentState);
  SysY2022Parser::EqExpContext *previousContext = _localctx;
  (void)previousContext; // Silence compiler, in case the context is not used by generated code.
  size_t startState = 58;
  enterRecursionRule(_localctx, 58, SysY2022Parser::RuleEqExp, precedence);

    size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    unrollRecursionContexts(parentContext);
  });
  try {
    size_t alt;
    enterOuterAlt(_localctx, 1);
    setState(357);
    relExp(0);
    _ctx->stop = _input->LT(-1);
    setState(364);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 36, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        if (!_parseListeners.empty())
          triggerExitRuleEvent();
        previousContext = _localctx;
        _localctx = _tracker.createInstance<EqExpContext>(parentContext, parentState);
        pushNewRecursionContext(_localctx, startState, RuleEqExp);
        setState(359);

        if (!(precpred(_ctx, 1))) throw FailedPredicateException(this, "precpred(_ctx, 1)");
        setState(360);
        _la = _input->LA(1);
        if (!(_la == SysY2022Parser::EQ

        || _la == SysY2022Parser::NE)) {
        _errHandler->recoverInline(this);
        }
        else {
          _errHandler->reportMatch(this);
          consume();
        }
        setState(361);
        relExp(0); 
      }
      setState(366);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 36, _ctx);
    }
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }
  return _localctx;
}

//----------------- LAndExpContext ------------------------------------------------------------------

SysY2022Parser::LAndExpContext::LAndExpContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

SysY2022Parser::EqExpContext* SysY2022Parser::LAndExpContext::eqExp() {
  return getRuleContext<SysY2022Parser::EqExpContext>(0);
}

SysY2022Parser::LAndExpContext* SysY2022Parser::LAndExpContext::lAndExp() {
  return getRuleContext<SysY2022Parser::LAndExpContext>(0);
}

tree::TerminalNode* SysY2022Parser::LAndExpContext::AND() {
  return getToken(SysY2022Parser::AND, 0);
}


size_t SysY2022Parser::LAndExpContext::getRuleIndex() const {
  return SysY2022Parser::RuleLAndExp;
}

void SysY2022Parser::LAndExpContext::enterRule(tree::ParseTreeListener *listener) {
  auto parserListener = dynamic_cast<SysY2022ParserListener *>(listener);
  if (parserListener != nullptr)
    parserListener->enterLAndExp(this);
}

void SysY2022Parser::LAndExpContext::exitRule(tree::ParseTreeListener *listener) {
  auto parserListener = dynamic_cast<SysY2022ParserListener *>(listener);
  if (parserListener != nullptr)
    parserListener->exitLAndExp(this);
}


std::any SysY2022Parser::LAndExpContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<SysY2022ParserVisitor*>(visitor))
    return parserVisitor->visitLAndExp(this);
  else
    return visitor->visitChildren(this);
}


SysY2022Parser::LAndExpContext* SysY2022Parser::lAndExp() {
   return lAndExp(0);
}

SysY2022Parser::LAndExpContext* SysY2022Parser::lAndExp(int precedence) {
  ParserRuleContext *parentContext = _ctx;
  size_t parentState = getState();
  SysY2022Parser::LAndExpContext *_localctx = _tracker.createInstance<LAndExpContext>(_ctx, parentState);
  SysY2022Parser::LAndExpContext *previousContext = _localctx;
  (void)previousContext; // Silence compiler, in case the context is not used by generated code.
  size_t startState = 60;
  enterRecursionRule(_localctx, 60, SysY2022Parser::RuleLAndExp, precedence);

    

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    unrollRecursionContexts(parentContext);
  });
  try {
    size_t alt;
    enterOuterAlt(_localctx, 1);
    setState(368);
    eqExp(0);
    _ctx->stop = _input->LT(-1);
    setState(375);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 37, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        if (!_parseListeners.empty())
          triggerExitRuleEvent();
        previousContext = _localctx;
        _localctx = _tracker.createInstance<LAndExpContext>(parentContext, parentState);
        pushNewRecursionContext(_localctx, startState, RuleLAndExp);
        setState(370);

        if (!(precpred(_ctx, 1))) throw FailedPredicateException(this, "precpred(_ctx, 1)");
        setState(371);
        match(SysY2022Parser::AND);
        setState(372);
        eqExp(0); 
      }
      setState(377);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 37, _ctx);
    }
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }
  return _localctx;
}

//----------------- LOrExpContext ------------------------------------------------------------------

SysY2022Parser::LOrExpContext::LOrExpContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

SysY2022Parser::LAndExpContext* SysY2022Parser::LOrExpContext::lAndExp() {
  return getRuleContext<SysY2022Parser::LAndExpContext>(0);
}

SysY2022Parser::LOrExpContext* SysY2022Parser::LOrExpContext::lOrExp() {
  return getRuleContext<SysY2022Parser::LOrExpContext>(0);
}

tree::TerminalNode* SysY2022Parser::LOrExpContext::OR() {
  return getToken(SysY2022Parser::OR, 0);
}


size_t SysY2022Parser::LOrExpContext::getRuleIndex() const {
  return SysY2022Parser::RuleLOrExp;
}

void SysY2022Parser::LOrExpContext::enterRule(tree::ParseTreeListener *listener) {
  auto parserListener = dynamic_cast<SysY2022ParserListener *>(listener);
  if (parserListener != nullptr)
    parserListener->enterLOrExp(this);
}

void SysY2022Parser::LOrExpContext::exitRule(tree::ParseTreeListener *listener) {
  auto parserListener = dynamic_cast<SysY2022ParserListener *>(listener);
  if (parserListener != nullptr)
    parserListener->exitLOrExp(this);
}


std::any SysY2022Parser::LOrExpContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<SysY2022ParserVisitor*>(visitor))
    return parserVisitor->visitLOrExp(this);
  else
    return visitor->visitChildren(this);
}


SysY2022Parser::LOrExpContext* SysY2022Parser::lOrExp() {
   return lOrExp(0);
}

SysY2022Parser::LOrExpContext* SysY2022Parser::lOrExp(int precedence) {
  ParserRuleContext *parentContext = _ctx;
  size_t parentState = getState();
  SysY2022Parser::LOrExpContext *_localctx = _tracker.createInstance<LOrExpContext>(_ctx, parentState);
  SysY2022Parser::LOrExpContext *previousContext = _localctx;
  (void)previousContext; // Silence compiler, in case the context is not used by generated code.
  size_t startState = 62;
  enterRecursionRule(_localctx, 62, SysY2022Parser::RuleLOrExp, precedence);

    

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    unrollRecursionContexts(parentContext);
  });
  try {
    size_t alt;
    enterOuterAlt(_localctx, 1);
    setState(379);
    lAndExp(0);
    _ctx->stop = _input->LT(-1);
    setState(386);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 38, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        if (!_parseListeners.empty())
          triggerExitRuleEvent();
        previousContext = _localctx;
        _localctx = _tracker.createInstance<LOrExpContext>(parentContext, parentState);
        pushNewRecursionContext(_localctx, startState, RuleLOrExp);
        setState(381);

        if (!(precpred(_ctx, 1))) throw FailedPredicateException(this, "precpred(_ctx, 1)");
        setState(382);
        match(SysY2022Parser::OR);
        setState(383);
        lAndExp(0); 
      }
      setState(388);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 38, _ctx);
    }
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }
  return _localctx;
}

//----------------- ConstExpContext ------------------------------------------------------------------

SysY2022Parser::ConstExpContext::ConstExpContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

SysY2022Parser::AddExpContext* SysY2022Parser::ConstExpContext::addExp() {
  return getRuleContext<SysY2022Parser::AddExpContext>(0);
}


size_t SysY2022Parser::ConstExpContext::getRuleIndex() const {
  return SysY2022Parser::RuleConstExp;
}

void SysY2022Parser::ConstExpContext::enterRule(tree::ParseTreeListener *listener) {
  auto parserListener = dynamic_cast<SysY2022ParserListener *>(listener);
  if (parserListener != nullptr)
    parserListener->enterConstExp(this);
}

void SysY2022Parser::ConstExpContext::exitRule(tree::ParseTreeListener *listener) {
  auto parserListener = dynamic_cast<SysY2022ParserListener *>(listener);
  if (parserListener != nullptr)
    parserListener->exitConstExp(this);
}


std::any SysY2022Parser::ConstExpContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<SysY2022ParserVisitor*>(visitor))
    return parserVisitor->visitConstExp(this);
  else
    return visitor->visitChildren(this);
}

SysY2022Parser::ConstExpContext* SysY2022Parser::constExp() {
  ConstExpContext *_localctx = _tracker.createInstance<ConstExpContext>(_ctx, getState());
  enterRule(_localctx, 64, SysY2022Parser::RuleConstExp);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(389);
    addExp(0);
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

bool SysY2022Parser::sempred(RuleContext *context, size_t ruleIndex, size_t predicateIndex) {
  switch (ruleIndex) {
    case 26: return mulExpSempred(antlrcpp::downCast<MulExpContext *>(context), predicateIndex);
    case 27: return addExpSempred(antlrcpp::downCast<AddExpContext *>(context), predicateIndex);
    case 28: return relExpSempred(antlrcpp::downCast<RelExpContext *>(context), predicateIndex);
    case 29: return eqExpSempred(antlrcpp::downCast<EqExpContext *>(context), predicateIndex);
    case 30: return lAndExpSempred(antlrcpp::downCast<LAndExpContext *>(context), predicateIndex);
    case 31: return lOrExpSempred(antlrcpp::downCast<LOrExpContext *>(context), predicateIndex);

  default:
    break;
  }
  return true;
}

bool SysY2022Parser::mulExpSempred(MulExpContext *_localctx, size_t predicateIndex) {
  switch (predicateIndex) {
    case 0: return precpred(_ctx, 1);

  default:
    break;
  }
  return true;
}

bool SysY2022Parser::addExpSempred(AddExpContext *_localctx, size_t predicateIndex) {
  switch (predicateIndex) {
    case 1: return precpred(_ctx, 1);

  default:
    break;
  }
  return true;
}

bool SysY2022Parser::relExpSempred(RelExpContext *_localctx, size_t predicateIndex) {
  switch (predicateIndex) {
    case 2: return precpred(_ctx, 1);

  default:
    break;
  }
  return true;
}

bool SysY2022Parser::eqExpSempred(EqExpContext *_localctx, size_t predicateIndex) {
  switch (predicateIndex) {
    case 3: return precpred(_ctx, 1);

  default:
    break;
  }
  return true;
}

bool SysY2022Parser::lAndExpSempred(LAndExpContext *_localctx, size_t predicateIndex) {
  switch (predicateIndex) {
    case 4: return precpred(_ctx, 1);

  default:
    break;
  }
  return true;
}

bool SysY2022Parser::lOrExpSempred(LOrExpContext *_localctx, size_t predicateIndex) {
  switch (predicateIndex) {
    case 5: return precpred(_ctx, 1);

  default:
    break;
  }
  return true;
}

void SysY2022Parser::initialize() {
#if ANTLR4_USE_THREAD_LOCAL_CACHE
  sysy2022parserParserInitialize();
#else
  ::antlr4::internal::call_once(sysy2022parserParserOnceFlag, sysy2022parserParserInitialize);
#endif
}
