
// Generated from SysY2022Parser.g4 by ANTLR 4.13.1


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
      "compilationUnit", "decl", "constDecl", "bType", "tensorType", "constDef", 
      "constInitVal", "varDecl", "varDef", "initVal", "funcDef", "funcType", 
      "funcFParams", "funcFParam", "block", "blockItem", "stmt", "exp", 
      "cond", "lVal", "primaryExp", "number", "unaryExp", "unaryOp", "funcRParams", 
      "mulExp", "addExp", "relExp", "eqExp", "lAndExp", "lOrExp", "constExp"
    },
    std::vector<std::string>{
      "", "'int'", "'float'", "'tensor'", "'void'", "'const'", "'if'", "'else'", 
      "'while'", "'break'", "'continue'", "'return'", "'('", "')'", "'['", 
      "']'", "'{'", "'}'", "','", "';'", "'\\u003F'", "':'", "'+'", "'-'", 
      "'*'", "'/'", "'%'", "'@'", "'!'", "'&&'", "'||'", "'<'", "'>'", "'<='", 
      "'>='", "'=='", "'!='", "'='"
    },
    std::vector<std::string>{
      "", "INT", "FLOAT", "TENSOR", "VOID", "CONST", "IF", "ELSE", "WHILE", 
      "BREAK", "CONTINUE", "RETURN", "L_PAREN", "R_PAREN", "L_BRACKET", 
      "R_BRACKET", "L_BRACE", "R_BRACE", "COMMA", "SEMICOLON", "QUESTION", 
      "COLON", "PLUS", "MINUS", "STAR", "DIV", "MOD", "MATMUL", "NOT", "AND", 
      "OR", "LT", "GT", "LE", "GE", "EQ", "NE", "ASSIGN", "IDENTIFIER", 
      "INTCONST", "FLOATCONST", "WHITESPACE", "LINE_COMMENT", "BLOCK_COMMENT"
    }
  );
  static const int32_t serializedATNSegment[] = {
  	4,1,43,377,2,0,7,0,2,1,7,1,2,2,7,2,2,3,7,3,2,4,7,4,2,5,7,5,2,6,7,6,2,
  	7,7,7,2,8,7,8,2,9,7,9,2,10,7,10,2,11,7,11,2,12,7,12,2,13,7,13,2,14,7,
  	14,2,15,7,15,2,16,7,16,2,17,7,17,2,18,7,18,2,19,7,19,2,20,7,20,2,21,7,
  	21,2,22,7,22,2,23,7,23,2,24,7,24,2,25,7,25,2,26,7,26,2,27,7,27,2,28,7,
  	28,2,29,7,29,2,30,7,30,2,31,7,31,1,0,1,0,5,0,67,8,0,10,0,12,0,70,9,0,
  	1,0,1,0,1,1,1,1,3,1,76,8,1,1,2,1,2,1,2,1,2,1,2,5,2,83,8,2,10,2,12,2,86,
  	9,2,1,2,1,2,1,3,1,3,1,3,3,3,93,8,3,1,4,1,4,1,4,1,5,1,5,1,5,1,5,1,5,5,
  	5,103,8,5,10,5,12,5,106,9,5,1,5,1,5,1,5,1,6,1,6,1,6,1,6,1,6,5,6,116,8,
  	6,10,6,12,6,119,9,6,3,6,121,8,6,1,6,3,6,124,8,6,1,7,1,7,1,7,1,7,5,7,130,
  	8,7,10,7,12,7,133,9,7,1,7,1,7,1,8,1,8,1,8,1,8,1,8,5,8,142,8,8,10,8,12,
  	8,145,9,8,1,8,1,8,1,8,1,8,1,8,5,8,152,8,8,10,8,12,8,155,9,8,1,8,1,8,3,
  	8,159,8,8,1,9,1,9,1,9,1,9,1,9,5,9,166,8,9,10,9,12,9,169,9,9,3,9,171,8,
  	9,1,9,3,9,174,8,9,1,10,1,10,1,10,1,10,3,10,180,8,10,1,10,1,10,1,10,1,
  	11,1,11,1,11,1,11,3,11,189,8,11,1,12,1,12,1,12,5,12,194,8,12,10,12,12,
  	12,197,9,12,1,13,1,13,1,13,1,13,1,13,1,13,1,13,1,13,5,13,207,8,13,10,
  	13,12,13,210,9,13,3,13,212,8,13,1,14,1,14,5,14,216,8,14,10,14,12,14,219,
  	9,14,1,14,1,14,1,15,1,15,3,15,225,8,15,1,16,1,16,1,16,1,16,1,16,1,16,
  	3,16,233,8,16,1,16,1,16,1,16,1,16,1,16,1,16,1,16,1,16,1,16,3,16,244,8,
  	16,1,16,1,16,1,16,1,16,1,16,1,16,1,16,1,16,1,16,1,16,1,16,1,16,3,16,258,
  	8,16,1,16,3,16,261,8,16,1,17,1,17,1,18,1,18,1,19,1,19,1,19,1,19,1,19,
  	5,19,272,8,19,10,19,12,19,275,9,19,1,20,1,20,1,20,1,20,1,20,1,20,3,20,
  	283,8,20,1,21,1,21,1,22,1,22,1,22,1,22,3,22,291,8,22,1,22,1,22,1,22,1,
  	22,3,22,297,8,22,1,23,1,23,1,24,1,24,1,24,5,24,304,8,24,10,24,12,24,307,
  	9,24,1,25,1,25,1,25,1,25,1,25,1,25,5,25,315,8,25,10,25,12,25,318,9,25,
  	1,26,1,26,1,26,1,26,1,26,1,26,5,26,326,8,26,10,26,12,26,329,9,26,1,27,
  	1,27,1,27,1,27,1,27,1,27,5,27,337,8,27,10,27,12,27,340,9,27,1,28,1,28,
  	1,28,1,28,1,28,1,28,5,28,348,8,28,10,28,12,28,351,9,28,1,29,1,29,1,29,
  	1,29,1,29,1,29,5,29,359,8,29,10,29,12,29,362,9,29,1,30,1,30,1,30,1,30,
  	1,30,1,30,5,30,370,8,30,10,30,12,30,373,9,30,1,31,1,31,1,31,0,6,50,52,
  	54,56,58,60,32,0,2,4,6,8,10,12,14,16,18,20,22,24,26,28,30,32,34,36,38,
  	40,42,44,46,48,50,52,54,56,58,60,62,0,7,1,0,1,2,1,0,39,40,2,0,22,23,28,
  	28,1,0,24,27,1,0,22,23,1,0,31,34,1,0,35,36,393,0,68,1,0,0,0,2,75,1,0,
  	0,0,4,77,1,0,0,0,6,92,1,0,0,0,8,94,1,0,0,0,10,97,1,0,0,0,12,123,1,0,0,
  	0,14,125,1,0,0,0,16,158,1,0,0,0,18,173,1,0,0,0,20,175,1,0,0,0,22,188,
  	1,0,0,0,24,190,1,0,0,0,26,198,1,0,0,0,28,213,1,0,0,0,30,224,1,0,0,0,32,
  	260,1,0,0,0,34,262,1,0,0,0,36,264,1,0,0,0,38,266,1,0,0,0,40,282,1,0,0,
  	0,42,284,1,0,0,0,44,296,1,0,0,0,46,298,1,0,0,0,48,300,1,0,0,0,50,308,
  	1,0,0,0,52,319,1,0,0,0,54,330,1,0,0,0,56,341,1,0,0,0,58,352,1,0,0,0,60,
  	363,1,0,0,0,62,374,1,0,0,0,64,67,3,2,1,0,65,67,3,20,10,0,66,64,1,0,0,
  	0,66,65,1,0,0,0,67,70,1,0,0,0,68,66,1,0,0,0,68,69,1,0,0,0,69,71,1,0,0,
  	0,70,68,1,0,0,0,71,72,5,0,0,1,72,1,1,0,0,0,73,76,3,4,2,0,74,76,3,14,7,
  	0,75,73,1,0,0,0,75,74,1,0,0,0,76,3,1,0,0,0,77,78,5,5,0,0,78,79,3,6,3,
  	0,79,84,3,10,5,0,80,81,5,18,0,0,81,83,3,10,5,0,82,80,1,0,0,0,83,86,1,
  	0,0,0,84,82,1,0,0,0,84,85,1,0,0,0,85,87,1,0,0,0,86,84,1,0,0,0,87,88,5,
  	19,0,0,88,5,1,0,0,0,89,93,5,1,0,0,90,93,5,2,0,0,91,93,3,8,4,0,92,89,1,
  	0,0,0,92,90,1,0,0,0,92,91,1,0,0,0,93,7,1,0,0,0,94,95,5,3,0,0,95,96,7,
  	0,0,0,96,9,1,0,0,0,97,104,5,38,0,0,98,99,5,14,0,0,99,100,3,62,31,0,100,
  	101,5,15,0,0,101,103,1,0,0,0,102,98,1,0,0,0,103,106,1,0,0,0,104,102,1,
  	0,0,0,104,105,1,0,0,0,105,107,1,0,0,0,106,104,1,0,0,0,107,108,5,37,0,
  	0,108,109,3,12,6,0,109,11,1,0,0,0,110,124,3,62,31,0,111,120,5,16,0,0,
  	112,117,3,12,6,0,113,114,5,18,0,0,114,116,3,12,6,0,115,113,1,0,0,0,116,
  	119,1,0,0,0,117,115,1,0,0,0,117,118,1,0,0,0,118,121,1,0,0,0,119,117,1,
  	0,0,0,120,112,1,0,0,0,120,121,1,0,0,0,121,122,1,0,0,0,122,124,5,17,0,
  	0,123,110,1,0,0,0,123,111,1,0,0,0,124,13,1,0,0,0,125,126,3,6,3,0,126,
  	131,3,16,8,0,127,128,5,18,0,0,128,130,3,16,8,0,129,127,1,0,0,0,130,133,
  	1,0,0,0,131,129,1,0,0,0,131,132,1,0,0,0,132,134,1,0,0,0,133,131,1,0,0,
  	0,134,135,5,19,0,0,135,15,1,0,0,0,136,143,5,38,0,0,137,138,5,14,0,0,138,
  	139,3,62,31,0,139,140,5,15,0,0,140,142,1,0,0,0,141,137,1,0,0,0,142,145,
  	1,0,0,0,143,141,1,0,0,0,143,144,1,0,0,0,144,159,1,0,0,0,145,143,1,0,0,
  	0,146,153,5,38,0,0,147,148,5,14,0,0,148,149,3,62,31,0,149,150,5,15,0,
  	0,150,152,1,0,0,0,151,147,1,0,0,0,152,155,1,0,0,0,153,151,1,0,0,0,153,
  	154,1,0,0,0,154,156,1,0,0,0,155,153,1,0,0,0,156,157,5,37,0,0,157,159,
  	3,18,9,0,158,136,1,0,0,0,158,146,1,0,0,0,159,17,1,0,0,0,160,174,3,34,
  	17,0,161,170,5,16,0,0,162,167,3,18,9,0,163,164,5,18,0,0,164,166,3,18,
  	9,0,165,163,1,0,0,0,166,169,1,0,0,0,167,165,1,0,0,0,167,168,1,0,0,0,168,
  	171,1,0,0,0,169,167,1,0,0,0,170,162,1,0,0,0,170,171,1,0,0,0,171,172,1,
  	0,0,0,172,174,5,17,0,0,173,160,1,0,0,0,173,161,1,0,0,0,174,19,1,0,0,0,
  	175,176,3,22,11,0,176,177,5,38,0,0,177,179,5,12,0,0,178,180,3,24,12,0,
  	179,178,1,0,0,0,179,180,1,0,0,0,180,181,1,0,0,0,181,182,5,13,0,0,182,
  	183,3,28,14,0,183,21,1,0,0,0,184,189,5,4,0,0,185,189,5,1,0,0,186,189,
  	5,2,0,0,187,189,3,8,4,0,188,184,1,0,0,0,188,185,1,0,0,0,188,186,1,0,0,
  	0,188,187,1,0,0,0,189,23,1,0,0,0,190,195,3,26,13,0,191,192,5,18,0,0,192,
  	194,3,26,13,0,193,191,1,0,0,0,194,197,1,0,0,0,195,193,1,0,0,0,195,196,
  	1,0,0,0,196,25,1,0,0,0,197,195,1,0,0,0,198,199,3,6,3,0,199,211,5,38,0,
  	0,200,201,5,14,0,0,201,208,5,15,0,0,202,203,5,14,0,0,203,204,3,34,17,
  	0,204,205,5,15,0,0,205,207,1,0,0,0,206,202,1,0,0,0,207,210,1,0,0,0,208,
  	206,1,0,0,0,208,209,1,0,0,0,209,212,1,0,0,0,210,208,1,0,0,0,211,200,1,
  	0,0,0,211,212,1,0,0,0,212,27,1,0,0,0,213,217,5,16,0,0,214,216,3,30,15,
  	0,215,214,1,0,0,0,216,219,1,0,0,0,217,215,1,0,0,0,217,218,1,0,0,0,218,
  	220,1,0,0,0,219,217,1,0,0,0,220,221,5,17,0,0,221,29,1,0,0,0,222,225,3,
  	2,1,0,223,225,3,32,16,0,224,222,1,0,0,0,224,223,1,0,0,0,225,31,1,0,0,
  	0,226,227,3,38,19,0,227,228,5,37,0,0,228,229,3,34,17,0,229,230,5,19,0,
  	0,230,261,1,0,0,0,231,233,3,34,17,0,232,231,1,0,0,0,232,233,1,0,0,0,233,
  	234,1,0,0,0,234,261,5,19,0,0,235,261,3,28,14,0,236,237,5,6,0,0,237,238,
  	5,12,0,0,238,239,3,36,18,0,239,240,5,13,0,0,240,243,3,32,16,0,241,242,
  	5,7,0,0,242,244,3,32,16,0,243,241,1,0,0,0,243,244,1,0,0,0,244,261,1,0,
  	0,0,245,246,5,8,0,0,246,247,5,12,0,0,247,248,3,36,18,0,248,249,5,13,0,
  	0,249,250,3,32,16,0,250,261,1,0,0,0,251,252,5,9,0,0,252,261,5,19,0,0,
  	253,254,5,10,0,0,254,261,5,19,0,0,255,257,5,11,0,0,256,258,3,34,17,0,
  	257,256,1,0,0,0,257,258,1,0,0,0,258,259,1,0,0,0,259,261,5,19,0,0,260,
  	226,1,0,0,0,260,232,1,0,0,0,260,235,1,0,0,0,260,236,1,0,0,0,260,245,1,
  	0,0,0,260,251,1,0,0,0,260,253,1,0,0,0,260,255,1,0,0,0,261,33,1,0,0,0,
  	262,263,3,52,26,0,263,35,1,0,0,0,264,265,3,60,30,0,265,37,1,0,0,0,266,
  	273,5,38,0,0,267,268,5,14,0,0,268,269,3,34,17,0,269,270,5,15,0,0,270,
  	272,1,0,0,0,271,267,1,0,0,0,272,275,1,0,0,0,273,271,1,0,0,0,273,274,1,
  	0,0,0,274,39,1,0,0,0,275,273,1,0,0,0,276,277,5,12,0,0,277,278,3,34,17,
  	0,278,279,5,13,0,0,279,283,1,0,0,0,280,283,3,38,19,0,281,283,3,42,21,
  	0,282,276,1,0,0,0,282,280,1,0,0,0,282,281,1,0,0,0,283,41,1,0,0,0,284,
  	285,7,1,0,0,285,43,1,0,0,0,286,297,3,40,20,0,287,288,5,38,0,0,288,290,
  	5,12,0,0,289,291,3,48,24,0,290,289,1,0,0,0,290,291,1,0,0,0,291,292,1,
  	0,0,0,292,297,5,13,0,0,293,294,3,46,23,0,294,295,3,44,22,0,295,297,1,
  	0,0,0,296,286,1,0,0,0,296,287,1,0,0,0,296,293,1,0,0,0,297,45,1,0,0,0,
  	298,299,7,2,0,0,299,47,1,0,0,0,300,305,3,34,17,0,301,302,5,18,0,0,302,
  	304,3,34,17,0,303,301,1,0,0,0,304,307,1,0,0,0,305,303,1,0,0,0,305,306,
  	1,0,0,0,306,49,1,0,0,0,307,305,1,0,0,0,308,309,6,25,-1,0,309,310,3,44,
  	22,0,310,316,1,0,0,0,311,312,10,1,0,0,312,313,7,3,0,0,313,315,3,44,22,
  	0,314,311,1,0,0,0,315,318,1,0,0,0,316,314,1,0,0,0,316,317,1,0,0,0,317,
  	51,1,0,0,0,318,316,1,0,0,0,319,320,6,26,-1,0,320,321,3,50,25,0,321,327,
  	1,0,0,0,322,323,10,1,0,0,323,324,7,4,0,0,324,326,3,50,25,0,325,322,1,
  	0,0,0,326,329,1,0,0,0,327,325,1,0,0,0,327,328,1,0,0,0,328,53,1,0,0,0,
  	329,327,1,0,0,0,330,331,6,27,-1,0,331,332,3,52,26,0,332,338,1,0,0,0,333,
  	334,10,1,0,0,334,335,7,5,0,0,335,337,3,52,26,0,336,333,1,0,0,0,337,340,
  	1,0,0,0,338,336,1,0,0,0,338,339,1,0,0,0,339,55,1,0,0,0,340,338,1,0,0,
  	0,341,342,6,28,-1,0,342,343,3,54,27,0,343,349,1,0,0,0,344,345,10,1,0,
  	0,345,346,7,6,0,0,346,348,3,54,27,0,347,344,1,0,0,0,348,351,1,0,0,0,349,
  	347,1,0,0,0,349,350,1,0,0,0,350,57,1,0,0,0,351,349,1,0,0,0,352,353,6,
  	29,-1,0,353,354,3,56,28,0,354,360,1,0,0,0,355,356,10,1,0,0,356,357,5,
  	29,0,0,357,359,3,56,28,0,358,355,1,0,0,0,359,362,1,0,0,0,360,358,1,0,
  	0,0,360,361,1,0,0,0,361,59,1,0,0,0,362,360,1,0,0,0,363,364,6,30,-1,0,
  	364,365,3,58,29,0,365,371,1,0,0,0,366,367,10,1,0,0,367,368,5,30,0,0,368,
  	370,3,58,29,0,369,366,1,0,0,0,370,373,1,0,0,0,371,369,1,0,0,0,371,372,
  	1,0,0,0,372,61,1,0,0,0,373,371,1,0,0,0,374,375,3,52,26,0,375,63,1,0,0,
  	0,38,66,68,75,84,92,104,117,120,123,131,143,153,158,167,170,173,179,188,
  	195,208,211,217,224,232,243,257,260,273,282,290,296,305,316,327,338,349,
  	360,371
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
    setState(68);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 62) != 0)) {
      setState(66);
      _errHandler->sync(this);
      switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 0, _ctx)) {
      case 1: {
        setState(64);
        decl();
        break;
      }

      case 2: {
        setState(65);
        funcDef();
        break;
      }

      default:
        break;
      }
      setState(70);
      _errHandler->sync(this);
      _la = _input->LA(1);
    }
    setState(71);
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
    setState(75);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case SysY2022Parser::CONST: {
        enterOuterAlt(_localctx, 1);
        setState(73);
        constDecl();
        break;
      }

      case SysY2022Parser::INT:
      case SysY2022Parser::FLOAT:
      case SysY2022Parser::TENSOR: {
        enterOuterAlt(_localctx, 2);
        setState(74);
        varDecl();
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
  enterRule(_localctx, 4, SysY2022Parser::RuleConstDecl);
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
    setState(77);
    match(SysY2022Parser::CONST);
    setState(78);
    bType();
    setState(79);
    constDef();
    setState(84);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == SysY2022Parser::COMMA) {
      setState(80);
      match(SysY2022Parser::COMMA);
      setState(81);
      constDef();
      setState(86);
      _errHandler->sync(this);
      _la = _input->LA(1);
    }
    setState(87);
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

SysY2022Parser::TensorTypeContext* SysY2022Parser::BTypeContext::tensorType() {
  return getRuleContext<SysY2022Parser::TensorTypeContext>(0);
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
  enterRule(_localctx, 6, SysY2022Parser::RuleBType);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(92);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case SysY2022Parser::INT: {
        enterOuterAlt(_localctx, 1);
        setState(89);
        match(SysY2022Parser::INT);
        break;
      }

      case SysY2022Parser::FLOAT: {
        enterOuterAlt(_localctx, 2);
        setState(90);
        match(SysY2022Parser::FLOAT);
        break;
      }

      case SysY2022Parser::TENSOR: {
        enterOuterAlt(_localctx, 3);
        setState(91);
        tensorType();
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

//----------------- TensorTypeContext ------------------------------------------------------------------

SysY2022Parser::TensorTypeContext::TensorTypeContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* SysY2022Parser::TensorTypeContext::TENSOR() {
  return getToken(SysY2022Parser::TENSOR, 0);
}

tree::TerminalNode* SysY2022Parser::TensorTypeContext::INT() {
  return getToken(SysY2022Parser::INT, 0);
}

tree::TerminalNode* SysY2022Parser::TensorTypeContext::FLOAT() {
  return getToken(SysY2022Parser::FLOAT, 0);
}


size_t SysY2022Parser::TensorTypeContext::getRuleIndex() const {
  return SysY2022Parser::RuleTensorType;
}

void SysY2022Parser::TensorTypeContext::enterRule(tree::ParseTreeListener *listener) {
  auto parserListener = dynamic_cast<SysY2022ParserListener *>(listener);
  if (parserListener != nullptr)
    parserListener->enterTensorType(this);
}

void SysY2022Parser::TensorTypeContext::exitRule(tree::ParseTreeListener *listener) {
  auto parserListener = dynamic_cast<SysY2022ParserListener *>(listener);
  if (parserListener != nullptr)
    parserListener->exitTensorType(this);
}


std::any SysY2022Parser::TensorTypeContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<SysY2022ParserVisitor*>(visitor))
    return parserVisitor->visitTensorType(this);
  else
    return visitor->visitChildren(this);
}

SysY2022Parser::TensorTypeContext* SysY2022Parser::tensorType() {
  TensorTypeContext *_localctx = _tracker.createInstance<TensorTypeContext>(_ctx, getState());
  enterRule(_localctx, 8, SysY2022Parser::RuleTensorType);
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
    setState(94);
    match(SysY2022Parser::TENSOR);
    setState(95);
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
  enterRule(_localctx, 10, SysY2022Parser::RuleConstDef);
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
    setState(97);
    match(SysY2022Parser::IDENTIFIER);
    setState(104);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == SysY2022Parser::L_BRACKET) {
      setState(98);
      match(SysY2022Parser::L_BRACKET);
      setState(99);
      constExp();
      setState(100);
      match(SysY2022Parser::R_BRACKET);
      setState(106);
      _errHandler->sync(this);
      _la = _input->LA(1);
    }
    setState(107);
    match(SysY2022Parser::ASSIGN);
    setState(108);
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
  enterRule(_localctx, 12, SysY2022Parser::RuleConstInitVal);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(123);
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
        setState(110);
        constExp();
        break;
      }

      case SysY2022Parser::L_BRACE: {
        enterOuterAlt(_localctx, 2);
        setState(111);
        match(SysY2022Parser::L_BRACE);
        setState(120);
        _errHandler->sync(this);

        _la = _input->LA(1);
        if ((((_la & ~ 0x3fULL) == 0) &&
          ((1ULL << _la) & 1924426436608) != 0)) {
          setState(112);
          constInitVal();
          setState(117);
          _errHandler->sync(this);
          _la = _input->LA(1);
          while (_la == SysY2022Parser::COMMA) {
            setState(113);
            match(SysY2022Parser::COMMA);
            setState(114);
            constInitVal();
            setState(119);
            _errHandler->sync(this);
            _la = _input->LA(1);
          }
        }
        setState(122);
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
  enterRule(_localctx, 14, SysY2022Parser::RuleVarDecl);
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
    setState(125);
    bType();
    setState(126);
    varDef();
    setState(131);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == SysY2022Parser::COMMA) {
      setState(127);
      match(SysY2022Parser::COMMA);
      setState(128);
      varDef();
      setState(133);
      _errHandler->sync(this);
      _la = _input->LA(1);
    }
    setState(134);
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
  enterRule(_localctx, 16, SysY2022Parser::RuleVarDef);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(158);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 12, _ctx)) {
    case 1: {
      enterOuterAlt(_localctx, 1);
      setState(136);
      match(SysY2022Parser::IDENTIFIER);
      setState(143);
      _errHandler->sync(this);
      _la = _input->LA(1);
      while (_la == SysY2022Parser::L_BRACKET) {
        setState(137);
        match(SysY2022Parser::L_BRACKET);
        setState(138);
        constExp();
        setState(139);
        match(SysY2022Parser::R_BRACKET);
        setState(145);
        _errHandler->sync(this);
        _la = _input->LA(1);
      }
      break;
    }

    case 2: {
      enterOuterAlt(_localctx, 2);
      setState(146);
      match(SysY2022Parser::IDENTIFIER);
      setState(153);
      _errHandler->sync(this);
      _la = _input->LA(1);
      while (_la == SysY2022Parser::L_BRACKET) {
        setState(147);
        match(SysY2022Parser::L_BRACKET);
        setState(148);
        constExp();
        setState(149);
        match(SysY2022Parser::R_BRACKET);
        setState(155);
        _errHandler->sync(this);
        _la = _input->LA(1);
      }
      setState(156);
      match(SysY2022Parser::ASSIGN);
      setState(157);
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
  enterRule(_localctx, 18, SysY2022Parser::RuleInitVal);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(173);
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
        setState(160);
        exp();
        break;
      }

      case SysY2022Parser::L_BRACE: {
        enterOuterAlt(_localctx, 2);
        setState(161);
        match(SysY2022Parser::L_BRACE);
        setState(170);
        _errHandler->sync(this);

        _la = _input->LA(1);
        if ((((_la & ~ 0x3fULL) == 0) &&
          ((1ULL << _la) & 1924426436608) != 0)) {
          setState(162);
          initVal();
          setState(167);
          _errHandler->sync(this);
          _la = _input->LA(1);
          while (_la == SysY2022Parser::COMMA) {
            setState(163);
            match(SysY2022Parser::COMMA);
            setState(164);
            initVal();
            setState(169);
            _errHandler->sync(this);
            _la = _input->LA(1);
          }
        }
        setState(172);
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
  enterRule(_localctx, 20, SysY2022Parser::RuleFuncDef);
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
    setState(175);
    funcType();
    setState(176);
    match(SysY2022Parser::IDENTIFIER);
    setState(177);
    match(SysY2022Parser::L_PAREN);
    setState(179);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 14) != 0)) {
      setState(178);
      funcFParams();
    }
    setState(181);
    match(SysY2022Parser::R_PAREN);
    setState(182);
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

SysY2022Parser::TensorTypeContext* SysY2022Parser::FuncTypeContext::tensorType() {
  return getRuleContext<SysY2022Parser::TensorTypeContext>(0);
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
  enterRule(_localctx, 22, SysY2022Parser::RuleFuncType);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(188);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case SysY2022Parser::VOID: {
        enterOuterAlt(_localctx, 1);
        setState(184);
        match(SysY2022Parser::VOID);
        break;
      }

      case SysY2022Parser::INT: {
        enterOuterAlt(_localctx, 2);
        setState(185);
        match(SysY2022Parser::INT);
        break;
      }

      case SysY2022Parser::FLOAT: {
        enterOuterAlt(_localctx, 3);
        setState(186);
        match(SysY2022Parser::FLOAT);
        break;
      }

      case SysY2022Parser::TENSOR: {
        enterOuterAlt(_localctx, 4);
        setState(187);
        tensorType();
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
  enterRule(_localctx, 24, SysY2022Parser::RuleFuncFParams);
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
    setState(190);
    funcFParam();
    setState(195);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == SysY2022Parser::COMMA) {
      setState(191);
      match(SysY2022Parser::COMMA);
      setState(192);
      funcFParam();
      setState(197);
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
  enterRule(_localctx, 26, SysY2022Parser::RuleFuncFParam);
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
    setState(198);
    bType();
    setState(199);
    match(SysY2022Parser::IDENTIFIER);
    setState(211);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == SysY2022Parser::L_BRACKET) {
      setState(200);
      match(SysY2022Parser::L_BRACKET);
      setState(201);
      match(SysY2022Parser::R_BRACKET);
      setState(208);
      _errHandler->sync(this);
      _la = _input->LA(1);
      while (_la == SysY2022Parser::L_BRACKET) {
        setState(202);
        match(SysY2022Parser::L_BRACKET);
        setState(203);
        exp();
        setState(204);
        match(SysY2022Parser::R_BRACKET);
        setState(210);
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
  enterRule(_localctx, 28, SysY2022Parser::RuleBlock);
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
    match(SysY2022Parser::L_BRACE);
    setState(217);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 1924426964846) != 0)) {
      setState(214);
      blockItem();
      setState(219);
      _errHandler->sync(this);
      _la = _input->LA(1);
    }
    setState(220);
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
  enterRule(_localctx, 30, SysY2022Parser::RuleBlockItem);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(224);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case SysY2022Parser::INT:
      case SysY2022Parser::FLOAT:
      case SysY2022Parser::TENSOR:
      case SysY2022Parser::CONST: {
        enterOuterAlt(_localctx, 1);
        setState(222);
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
        setState(223);
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
  enterRule(_localctx, 32, SysY2022Parser::RuleStmt);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(260);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 26, _ctx)) {
    case 1: {
      enterOuterAlt(_localctx, 1);
      setState(226);
      lVal();
      setState(227);
      match(SysY2022Parser::ASSIGN);
      setState(228);
      exp();
      setState(229);
      match(SysY2022Parser::SEMICOLON);
      break;
    }

    case 2: {
      enterOuterAlt(_localctx, 2);
      setState(232);
      _errHandler->sync(this);

      _la = _input->LA(1);
      if ((((_la & ~ 0x3fULL) == 0) &&
        ((1ULL << _la) & 1924426371072) != 0)) {
        setState(231);
        exp();
      }
      setState(234);
      match(SysY2022Parser::SEMICOLON);
      break;
    }

    case 3: {
      enterOuterAlt(_localctx, 3);
      setState(235);
      block();
      break;
    }

    case 4: {
      enterOuterAlt(_localctx, 4);
      setState(236);
      match(SysY2022Parser::IF);
      setState(237);
      match(SysY2022Parser::L_PAREN);
      setState(238);
      cond();
      setState(239);
      match(SysY2022Parser::R_PAREN);
      setState(240);
      stmt();
      setState(243);
      _errHandler->sync(this);

      switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 24, _ctx)) {
      case 1: {
        setState(241);
        match(SysY2022Parser::ELSE);
        setState(242);
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
      setState(245);
      match(SysY2022Parser::WHILE);
      setState(246);
      match(SysY2022Parser::L_PAREN);
      setState(247);
      cond();
      setState(248);
      match(SysY2022Parser::R_PAREN);
      setState(249);
      stmt();
      break;
    }

    case 6: {
      enterOuterAlt(_localctx, 6);
      setState(251);
      match(SysY2022Parser::BREAK);
      setState(252);
      match(SysY2022Parser::SEMICOLON);
      break;
    }

    case 7: {
      enterOuterAlt(_localctx, 7);
      setState(253);
      match(SysY2022Parser::CONTINUE);
      setState(254);
      match(SysY2022Parser::SEMICOLON);
      break;
    }

    case 8: {
      enterOuterAlt(_localctx, 8);
      setState(255);
      match(SysY2022Parser::RETURN);
      setState(257);
      _errHandler->sync(this);

      _la = _input->LA(1);
      if ((((_la & ~ 0x3fULL) == 0) &&
        ((1ULL << _la) & 1924426371072) != 0)) {
        setState(256);
        exp();
      }
      setState(259);
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
  enterRule(_localctx, 34, SysY2022Parser::RuleExp);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(262);
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
  enterRule(_localctx, 36, SysY2022Parser::RuleCond);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(264);
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
  enterRule(_localctx, 38, SysY2022Parser::RuleLVal);

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
    setState(266);
    match(SysY2022Parser::IDENTIFIER);
    setState(273);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 27, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(267);
        match(SysY2022Parser::L_BRACKET);
        setState(268);
        exp();
        setState(269);
        match(SysY2022Parser::R_BRACKET); 
      }
      setState(275);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 27, _ctx);
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
  enterRule(_localctx, 40, SysY2022Parser::RulePrimaryExp);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(282);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case SysY2022Parser::L_PAREN: {
        enterOuterAlt(_localctx, 1);
        setState(276);
        match(SysY2022Parser::L_PAREN);
        setState(277);
        exp();
        setState(278);
        match(SysY2022Parser::R_PAREN);
        break;
      }

      case SysY2022Parser::IDENTIFIER: {
        enterOuterAlt(_localctx, 2);
        setState(280);
        lVal();
        break;
      }

      case SysY2022Parser::INTCONST:
      case SysY2022Parser::FLOATCONST: {
        enterOuterAlt(_localctx, 3);
        setState(281);
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
  enterRule(_localctx, 42, SysY2022Parser::RuleNumber);
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
    setState(284);
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
  enterRule(_localctx, 44, SysY2022Parser::RuleUnaryExp);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(296);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 30, _ctx)) {
    case 1: {
      enterOuterAlt(_localctx, 1);
      setState(286);
      primaryExp();
      break;
    }

    case 2: {
      enterOuterAlt(_localctx, 2);
      setState(287);
      match(SysY2022Parser::IDENTIFIER);
      setState(288);
      match(SysY2022Parser::L_PAREN);
      setState(290);
      _errHandler->sync(this);

      _la = _input->LA(1);
      if ((((_la & ~ 0x3fULL) == 0) &&
        ((1ULL << _la) & 1924426371072) != 0)) {
        setState(289);
        funcRParams();
      }
      setState(292);
      match(SysY2022Parser::R_PAREN);
      break;
    }

    case 3: {
      enterOuterAlt(_localctx, 3);
      setState(293);
      unaryOp();
      setState(294);
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
  enterRule(_localctx, 46, SysY2022Parser::RuleUnaryOp);
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
    setState(298);
    _la = _input->LA(1);
    if (!((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 281018368) != 0))) {
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
  enterRule(_localctx, 48, SysY2022Parser::RuleFuncRParams);
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
    setState(300);
    exp();
    setState(305);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == SysY2022Parser::COMMA) {
      setState(301);
      match(SysY2022Parser::COMMA);
      setState(302);
      exp();
      setState(307);
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

tree::TerminalNode* SysY2022Parser::MulExpContext::MATMUL() {
  return getToken(SysY2022Parser::MATMUL, 0);
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
  size_t startState = 50;
  enterRecursionRule(_localctx, 50, SysY2022Parser::RuleMulExp, precedence);

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
    setState(309);
    unaryExp();
    _ctx->stop = _input->LT(-1);
    setState(316);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 32, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        if (!_parseListeners.empty())
          triggerExitRuleEvent();
        previousContext = _localctx;
        _localctx = _tracker.createInstance<MulExpContext>(parentContext, parentState);
        pushNewRecursionContext(_localctx, startState, RuleMulExp);
        setState(311);

        if (!(precpred(_ctx, 1))) throw FailedPredicateException(this, "precpred(_ctx, 1)");
        setState(312);
        _la = _input->LA(1);
        if (!((((_la & ~ 0x3fULL) == 0) &&
          ((1ULL << _la) & 251658240) != 0))) {
        _errHandler->recoverInline(this);
        }
        else {
          _errHandler->reportMatch(this);
          consume();
        }
        setState(313);
        unaryExp(); 
      }
      setState(318);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 32, _ctx);
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
  size_t startState = 52;
  enterRecursionRule(_localctx, 52, SysY2022Parser::RuleAddExp, precedence);

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
    setState(320);
    mulExp(0);
    _ctx->stop = _input->LT(-1);
    setState(327);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 33, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        if (!_parseListeners.empty())
          triggerExitRuleEvent();
        previousContext = _localctx;
        _localctx = _tracker.createInstance<AddExpContext>(parentContext, parentState);
        pushNewRecursionContext(_localctx, startState, RuleAddExp);
        setState(322);

        if (!(precpred(_ctx, 1))) throw FailedPredicateException(this, "precpred(_ctx, 1)");
        setState(323);
        _la = _input->LA(1);
        if (!(_la == SysY2022Parser::PLUS

        || _la == SysY2022Parser::MINUS)) {
        _errHandler->recoverInline(this);
        }
        else {
          _errHandler->reportMatch(this);
          consume();
        }
        setState(324);
        mulExp(0); 
      }
      setState(329);
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
  size_t startState = 54;
  enterRecursionRule(_localctx, 54, SysY2022Parser::RuleRelExp, precedence);

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
    setState(331);
    addExp(0);
    _ctx->stop = _input->LT(-1);
    setState(338);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 34, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        if (!_parseListeners.empty())
          triggerExitRuleEvent();
        previousContext = _localctx;
        _localctx = _tracker.createInstance<RelExpContext>(parentContext, parentState);
        pushNewRecursionContext(_localctx, startState, RuleRelExp);
        setState(333);

        if (!(precpred(_ctx, 1))) throw FailedPredicateException(this, "precpred(_ctx, 1)");
        setState(334);
        _la = _input->LA(1);
        if (!((((_la & ~ 0x3fULL) == 0) &&
          ((1ULL << _la) & 32212254720) != 0))) {
        _errHandler->recoverInline(this);
        }
        else {
          _errHandler->reportMatch(this);
          consume();
        }
        setState(335);
        addExp(0); 
      }
      setState(340);
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
  size_t startState = 56;
  enterRecursionRule(_localctx, 56, SysY2022Parser::RuleEqExp, precedence);

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
    setState(342);
    relExp(0);
    _ctx->stop = _input->LT(-1);
    setState(349);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 35, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        if (!_parseListeners.empty())
          triggerExitRuleEvent();
        previousContext = _localctx;
        _localctx = _tracker.createInstance<EqExpContext>(parentContext, parentState);
        pushNewRecursionContext(_localctx, startState, RuleEqExp);
        setState(344);

        if (!(precpred(_ctx, 1))) throw FailedPredicateException(this, "precpred(_ctx, 1)");
        setState(345);
        _la = _input->LA(1);
        if (!(_la == SysY2022Parser::EQ

        || _la == SysY2022Parser::NE)) {
        _errHandler->recoverInline(this);
        }
        else {
          _errHandler->reportMatch(this);
          consume();
        }
        setState(346);
        relExp(0); 
      }
      setState(351);
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
  size_t startState = 58;
  enterRecursionRule(_localctx, 58, SysY2022Parser::RuleLAndExp, precedence);

    

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
    setState(353);
    eqExp(0);
    _ctx->stop = _input->LT(-1);
    setState(360);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 36, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        if (!_parseListeners.empty())
          triggerExitRuleEvent();
        previousContext = _localctx;
        _localctx = _tracker.createInstance<LAndExpContext>(parentContext, parentState);
        pushNewRecursionContext(_localctx, startState, RuleLAndExp);
        setState(355);

        if (!(precpred(_ctx, 1))) throw FailedPredicateException(this, "precpred(_ctx, 1)");
        setState(356);
        match(SysY2022Parser::AND);
        setState(357);
        eqExp(0); 
      }
      setState(362);
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
  size_t startState = 60;
  enterRecursionRule(_localctx, 60, SysY2022Parser::RuleLOrExp, precedence);

    

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
    setState(364);
    lAndExp(0);
    _ctx->stop = _input->LT(-1);
    setState(371);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 37, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        if (!_parseListeners.empty())
          triggerExitRuleEvent();
        previousContext = _localctx;
        _localctx = _tracker.createInstance<LOrExpContext>(parentContext, parentState);
        pushNewRecursionContext(_localctx, startState, RuleLOrExp);
        setState(366);

        if (!(precpred(_ctx, 1))) throw FailedPredicateException(this, "precpred(_ctx, 1)");
        setState(367);
        match(SysY2022Parser::OR);
        setState(368);
        lAndExp(0); 
      }
      setState(373);
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
  enterRule(_localctx, 62, SysY2022Parser::RuleConstExp);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(374);
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
    case 25: return mulExpSempred(antlrcpp::downCast<MulExpContext *>(context), predicateIndex);
    case 26: return addExpSempred(antlrcpp::downCast<AddExpContext *>(context), predicateIndex);
    case 27: return relExpSempred(antlrcpp::downCast<RelExpContext *>(context), predicateIndex);
    case 28: return eqExpSempred(antlrcpp::downCast<EqExpContext *>(context), predicateIndex);
    case 29: return lAndExpSempred(antlrcpp::downCast<LAndExpContext *>(context), predicateIndex);
    case 30: return lOrExpSempred(antlrcpp::downCast<LOrExpContext *>(context), predicateIndex);

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
