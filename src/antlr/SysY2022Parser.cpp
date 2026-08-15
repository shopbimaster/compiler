
// Generated from /mnt/d/VSCodeProjects/compiler/grammar/SysY2022Parser.g4 by ANTLR 4.10.1


#include "SysY2022ParserListener.h"
#include "SysY2022ParserVisitor.h"
#include <mutex>

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

std::once_flag sysy2022parserParserOnceFlag;
SysY2022ParserStaticData *sysy2022parserParserStaticData = nullptr;

void sysy2022parserParserInitialize() {
  assert(sysy2022parserParserStaticData == nullptr);
  auto staticData = std::make_unique<SysY2022ParserStaticData>(
    std::vector<std::string>{
      "compilationUnit", "decl", "vectorDecl", "constDecl", "bType", "constDef", 
      "constInitVal", "varDecl", "varDef", "initVal", "funcDef", "funcType", 
      "funcFParams", "funcFParam", "block", "blockItem", "stmt", "exp", 
      "cond", "lVal", "primaryExp", "number", "unaryExp", "unaryOp", "funcRParams", 
      "mulExp", "addExp", "relExp", "eqExp", "lAndExp", "lOrExp", "constExp"
    },
    std::vector<std::string>{
      "", "'int'", "'float'", "'void'", "'const'", "'vector'", "'if'", "'else'", 
      "'while'", "'break'", "'continue'", "'return'", "'('", "')'", "'['", 
      "']'", "'{'", "'}'", "','", "';'", "'\\u003F'", "':'", "'+'", "'-'", 
      "'*'", "'/'", "'%'", "'!'", "'&&'", "'||'", "'<'", "'>'", "'<='", 
      "'>='", "'=='", "'!='", "'='"
    },
    std::vector<std::string>{
      "", "INT", "FLOAT", "VOID", "CONST", "VECTOR", "IF", "ELSE", "WHILE", 
      "BREAK", "CONTINUE", "RETURN", "L_PAREN", "R_PAREN", "L_BRACKET", 
      "R_BRACKET", "L_BRACE", "R_BRACE", "COMMA", "SEMICOLON", "QUESTION", 
      "COLON", "PLUS", "MINUS", "STAR", "DIV", "MOD", "NOT", "AND", "OR", 
      "LT", "GT", "LE", "GE", "EQ", "NE", "ASSIGN", "IDENTIFIER", "INTCONST", 
      "FLOATCONST", "WHITESPACE", "LINE_COMMENT", "BLOCK_COMMENT"
    }
  );
  static const int32_t serializedATNSegment[] = {
  	4,1,42,385,2,0,7,0,2,1,7,1,2,2,7,2,2,3,7,3,2,4,7,4,2,5,7,5,2,6,7,6,2,
  	7,7,7,2,8,7,8,2,9,7,9,2,10,7,10,2,11,7,11,2,12,7,12,2,13,7,13,2,14,7,
  	14,2,15,7,15,2,16,7,16,2,17,7,17,2,18,7,18,2,19,7,19,2,20,7,20,2,21,7,
  	21,2,22,7,22,2,23,7,23,2,24,7,24,2,25,7,25,2,26,7,26,2,27,7,27,2,28,7,
  	28,2,29,7,29,2,30,7,30,2,31,7,31,1,0,1,0,5,0,67,8,0,10,0,12,0,70,9,0,
  	1,0,1,0,1,1,1,1,1,1,3,1,77,8,1,1,2,1,2,1,2,1,2,1,2,1,2,1,2,1,2,1,2,1,
  	2,1,2,1,2,1,2,3,2,92,8,2,1,2,1,2,1,3,1,3,1,3,1,3,1,3,5,3,101,8,3,10,3,
  	12,3,104,9,3,1,3,1,3,1,4,1,4,1,5,1,5,1,5,1,5,1,5,5,5,115,8,5,10,5,12,
  	5,118,9,5,1,5,1,5,1,5,1,6,1,6,1,6,1,6,1,6,5,6,128,8,6,10,6,12,6,131,9,
  	6,3,6,133,8,6,1,6,3,6,136,8,6,1,7,1,7,1,7,1,7,5,7,142,8,7,10,7,12,7,145,
  	9,7,1,7,1,7,1,8,1,8,1,8,1,8,1,8,5,8,154,8,8,10,8,12,8,157,9,8,1,8,1,8,
  	1,8,1,8,1,8,5,8,164,8,8,10,8,12,8,167,9,8,1,8,1,8,3,8,171,8,8,1,9,1,9,
  	1,9,1,9,1,9,5,9,178,8,9,10,9,12,9,181,9,9,3,9,183,8,9,1,9,3,9,186,8,9,
  	1,10,1,10,1,10,1,10,3,10,192,8,10,1,10,1,10,1,10,1,11,1,11,1,12,1,12,
  	1,12,5,12,202,8,12,10,12,12,12,205,9,12,1,13,1,13,1,13,1,13,1,13,1,13,
  	1,13,1,13,5,13,215,8,13,10,13,12,13,218,9,13,3,13,220,8,13,1,14,1,14,
  	5,14,224,8,14,10,14,12,14,227,9,14,1,14,1,14,1,15,1,15,3,15,233,8,15,
  	1,16,1,16,1,16,1,16,1,16,1,16,3,16,241,8,16,1,16,1,16,1,16,1,16,1,16,
  	1,16,1,16,1,16,1,16,3,16,252,8,16,1,16,1,16,1,16,1,16,1,16,1,16,1,16,
  	1,16,1,16,1,16,1,16,1,16,3,16,266,8,16,1,16,3,16,269,8,16,1,17,1,17,1,
  	18,1,18,1,19,1,19,1,19,1,19,1,19,5,19,280,8,19,10,19,12,19,283,9,19,1,
  	20,1,20,1,20,1,20,1,20,1,20,3,20,291,8,20,1,21,1,21,1,22,1,22,1,22,1,
  	22,3,22,299,8,22,1,22,1,22,1,22,1,22,3,22,305,8,22,1,23,1,23,1,24,1,24,
  	1,24,5,24,312,8,24,10,24,12,24,315,9,24,1,25,1,25,1,25,1,25,1,25,1,25,
  	5,25,323,8,25,10,25,12,25,326,9,25,1,26,1,26,1,26,1,26,1,26,1,26,5,26,
  	334,8,26,10,26,12,26,337,9,26,1,27,1,27,1,27,1,27,1,27,1,27,5,27,345,
  	8,27,10,27,12,27,348,9,27,1,28,1,28,1,28,1,28,1,28,1,28,5,28,356,8,28,
  	10,28,12,28,359,9,28,1,29,1,29,1,29,1,29,1,29,1,29,5,29,367,8,29,10,29,
  	12,29,370,9,29,1,30,1,30,1,30,1,30,1,30,1,30,5,30,378,8,30,10,30,12,30,
  	381,9,30,1,31,1,31,1,31,0,6,50,52,54,56,58,60,32,0,2,4,6,8,10,12,14,16,
  	18,20,22,24,26,28,30,32,34,36,38,40,42,44,46,48,50,52,54,56,58,60,62,
  	0,8,1,0,1,2,1,0,1,3,1,0,38,39,2,0,22,23,27,27,1,0,24,26,1,0,22,23,1,0,
  	30,33,1,0,34,35,398,0,68,1,0,0,0,2,76,1,0,0,0,4,78,1,0,0,0,6,95,1,0,0,
  	0,8,107,1,0,0,0,10,109,1,0,0,0,12,135,1,0,0,0,14,137,1,0,0,0,16,170,1,
  	0,0,0,18,185,1,0,0,0,20,187,1,0,0,0,22,196,1,0,0,0,24,198,1,0,0,0,26,
  	206,1,0,0,0,28,221,1,0,0,0,30,232,1,0,0,0,32,268,1,0,0,0,34,270,1,0,0,
  	0,36,272,1,0,0,0,38,274,1,0,0,0,40,290,1,0,0,0,42,292,1,0,0,0,44,304,
  	1,0,0,0,46,306,1,0,0,0,48,308,1,0,0,0,50,316,1,0,0,0,52,327,1,0,0,0,54,
  	338,1,0,0,0,56,349,1,0,0,0,58,360,1,0,0,0,60,371,1,0,0,0,62,382,1,0,0,
  	0,64,67,3,2,1,0,65,67,3,20,10,0,66,64,1,0,0,0,66,65,1,0,0,0,67,70,1,0,
  	0,0,68,66,1,0,0,0,68,69,1,0,0,0,69,71,1,0,0,0,70,68,1,0,0,0,71,72,5,0,
  	0,1,72,1,1,0,0,0,73,77,3,6,3,0,74,77,3,14,7,0,75,77,3,4,2,0,76,73,1,0,
  	0,0,76,74,1,0,0,0,76,75,1,0,0,0,77,3,1,0,0,0,78,79,5,5,0,0,79,80,5,30,
  	0,0,80,81,3,8,4,0,81,82,5,31,0,0,82,91,5,37,0,0,83,84,5,14,0,0,84,85,
  	3,62,31,0,85,86,5,15,0,0,86,92,1,0,0,0,87,88,5,12,0,0,88,89,3,34,17,0,
  	89,90,5,13,0,0,90,92,1,0,0,0,91,83,1,0,0,0,91,87,1,0,0,0,92,93,1,0,0,
  	0,93,94,5,19,0,0,94,5,1,0,0,0,95,96,5,4,0,0,96,97,3,8,4,0,97,102,3,10,
  	5,0,98,99,5,18,0,0,99,101,3,10,5,0,100,98,1,0,0,0,101,104,1,0,0,0,102,
  	100,1,0,0,0,102,103,1,0,0,0,103,105,1,0,0,0,104,102,1,0,0,0,105,106,5,
  	19,0,0,106,7,1,0,0,0,107,108,7,0,0,0,108,9,1,0,0,0,109,116,5,37,0,0,110,
  	111,5,14,0,0,111,112,3,62,31,0,112,113,5,15,0,0,113,115,1,0,0,0,114,110,
  	1,0,0,0,115,118,1,0,0,0,116,114,1,0,0,0,116,117,1,0,0,0,117,119,1,0,0,
  	0,118,116,1,0,0,0,119,120,5,36,0,0,120,121,3,12,6,0,121,11,1,0,0,0,122,
  	136,3,62,31,0,123,132,5,16,0,0,124,129,3,12,6,0,125,126,5,18,0,0,126,
  	128,3,12,6,0,127,125,1,0,0,0,128,131,1,0,0,0,129,127,1,0,0,0,129,130,
  	1,0,0,0,130,133,1,0,0,0,131,129,1,0,0,0,132,124,1,0,0,0,132,133,1,0,0,
  	0,133,134,1,0,0,0,134,136,5,17,0,0,135,122,1,0,0,0,135,123,1,0,0,0,136,
  	13,1,0,0,0,137,138,3,8,4,0,138,143,3,16,8,0,139,140,5,18,0,0,140,142,
  	3,16,8,0,141,139,1,0,0,0,142,145,1,0,0,0,143,141,1,0,0,0,143,144,1,0,
  	0,0,144,146,1,0,0,0,145,143,1,0,0,0,146,147,5,19,0,0,147,15,1,0,0,0,148,
  	155,5,37,0,0,149,150,5,14,0,0,150,151,3,62,31,0,151,152,5,15,0,0,152,
  	154,1,0,0,0,153,149,1,0,0,0,154,157,1,0,0,0,155,153,1,0,0,0,155,156,1,
  	0,0,0,156,171,1,0,0,0,157,155,1,0,0,0,158,165,5,37,0,0,159,160,5,14,0,
  	0,160,161,3,62,31,0,161,162,5,15,0,0,162,164,1,0,0,0,163,159,1,0,0,0,
  	164,167,1,0,0,0,165,163,1,0,0,0,165,166,1,0,0,0,166,168,1,0,0,0,167,165,
  	1,0,0,0,168,169,5,36,0,0,169,171,3,18,9,0,170,148,1,0,0,0,170,158,1,0,
  	0,0,171,17,1,0,0,0,172,186,3,34,17,0,173,182,5,16,0,0,174,179,3,18,9,
  	0,175,176,5,18,0,0,176,178,3,18,9,0,177,175,1,0,0,0,178,181,1,0,0,0,179,
  	177,1,0,0,0,179,180,1,0,0,0,180,183,1,0,0,0,181,179,1,0,0,0,182,174,1,
  	0,0,0,182,183,1,0,0,0,183,184,1,0,0,0,184,186,5,17,0,0,185,172,1,0,0,
  	0,185,173,1,0,0,0,186,19,1,0,0,0,187,188,3,22,11,0,188,189,5,37,0,0,189,
  	191,5,12,0,0,190,192,3,24,12,0,191,190,1,0,0,0,191,192,1,0,0,0,192,193,
  	1,0,0,0,193,194,5,13,0,0,194,195,3,28,14,0,195,21,1,0,0,0,196,197,7,1,
  	0,0,197,23,1,0,0,0,198,203,3,26,13,0,199,200,5,18,0,0,200,202,3,26,13,
  	0,201,199,1,0,0,0,202,205,1,0,0,0,203,201,1,0,0,0,203,204,1,0,0,0,204,
  	25,1,0,0,0,205,203,1,0,0,0,206,207,3,8,4,0,207,219,5,37,0,0,208,209,5,
  	14,0,0,209,216,5,15,0,0,210,211,5,14,0,0,211,212,3,34,17,0,212,213,5,
  	15,0,0,213,215,1,0,0,0,214,210,1,0,0,0,215,218,1,0,0,0,216,214,1,0,0,
  	0,216,217,1,0,0,0,217,220,1,0,0,0,218,216,1,0,0,0,219,208,1,0,0,0,219,
  	220,1,0,0,0,220,27,1,0,0,0,221,225,5,16,0,0,222,224,3,30,15,0,223,222,
  	1,0,0,0,224,227,1,0,0,0,225,223,1,0,0,0,225,226,1,0,0,0,226,228,1,0,0,
  	0,227,225,1,0,0,0,228,229,5,17,0,0,229,29,1,0,0,0,230,233,3,2,1,0,231,
  	233,3,32,16,0,232,230,1,0,0,0,232,231,1,0,0,0,233,31,1,0,0,0,234,235,
  	3,38,19,0,235,236,5,36,0,0,236,237,3,34,17,0,237,238,5,19,0,0,238,269,
  	1,0,0,0,239,241,3,34,17,0,240,239,1,0,0,0,240,241,1,0,0,0,241,242,1,0,
  	0,0,242,269,5,19,0,0,243,269,3,28,14,0,244,245,5,6,0,0,245,246,5,12,0,
  	0,246,247,3,36,18,0,247,248,5,13,0,0,248,251,3,32,16,0,249,250,5,7,0,
  	0,250,252,3,32,16,0,251,249,1,0,0,0,251,252,1,0,0,0,252,269,1,0,0,0,253,
  	254,5,8,0,0,254,255,5,12,0,0,255,256,3,36,18,0,256,257,5,13,0,0,257,258,
  	3,32,16,0,258,269,1,0,0,0,259,260,5,9,0,0,260,269,5,19,0,0,261,262,5,
  	10,0,0,262,269,5,19,0,0,263,265,5,11,0,0,264,266,3,34,17,0,265,264,1,
  	0,0,0,265,266,1,0,0,0,266,267,1,0,0,0,267,269,5,19,0,0,268,234,1,0,0,
  	0,268,240,1,0,0,0,268,243,1,0,0,0,268,244,1,0,0,0,268,253,1,0,0,0,268,
  	259,1,0,0,0,268,261,1,0,0,0,268,263,1,0,0,0,269,33,1,0,0,0,270,271,3,
  	52,26,0,271,35,1,0,0,0,272,273,3,60,30,0,273,37,1,0,0,0,274,281,5,37,
  	0,0,275,276,5,14,0,0,276,277,3,34,17,0,277,278,5,15,0,0,278,280,1,0,0,
  	0,279,275,1,0,0,0,280,283,1,0,0,0,281,279,1,0,0,0,281,282,1,0,0,0,282,
  	39,1,0,0,0,283,281,1,0,0,0,284,285,5,12,0,0,285,286,3,34,17,0,286,287,
  	5,13,0,0,287,291,1,0,0,0,288,291,3,38,19,0,289,291,3,42,21,0,290,284,
  	1,0,0,0,290,288,1,0,0,0,290,289,1,0,0,0,291,41,1,0,0,0,292,293,7,2,0,
  	0,293,43,1,0,0,0,294,305,3,40,20,0,295,296,5,37,0,0,296,298,5,12,0,0,
  	297,299,3,48,24,0,298,297,1,0,0,0,298,299,1,0,0,0,299,300,1,0,0,0,300,
  	305,5,13,0,0,301,302,3,46,23,0,302,303,3,44,22,0,303,305,1,0,0,0,304,
  	294,1,0,0,0,304,295,1,0,0,0,304,301,1,0,0,0,305,45,1,0,0,0,306,307,7,
  	3,0,0,307,47,1,0,0,0,308,313,3,34,17,0,309,310,5,18,0,0,310,312,3,34,
  	17,0,311,309,1,0,0,0,312,315,1,0,0,0,313,311,1,0,0,0,313,314,1,0,0,0,
  	314,49,1,0,0,0,315,313,1,0,0,0,316,317,6,25,-1,0,317,318,3,44,22,0,318,
  	324,1,0,0,0,319,320,10,1,0,0,320,321,7,4,0,0,321,323,3,44,22,0,322,319,
  	1,0,0,0,323,326,1,0,0,0,324,322,1,0,0,0,324,325,1,0,0,0,325,51,1,0,0,
  	0,326,324,1,0,0,0,327,328,6,26,-1,0,328,329,3,50,25,0,329,335,1,0,0,0,
  	330,331,10,1,0,0,331,332,7,5,0,0,332,334,3,50,25,0,333,330,1,0,0,0,334,
  	337,1,0,0,0,335,333,1,0,0,0,335,336,1,0,0,0,336,53,1,0,0,0,337,335,1,
  	0,0,0,338,339,6,27,-1,0,339,340,3,52,26,0,340,346,1,0,0,0,341,342,10,
  	1,0,0,342,343,7,6,0,0,343,345,3,52,26,0,344,341,1,0,0,0,345,348,1,0,0,
  	0,346,344,1,0,0,0,346,347,1,0,0,0,347,55,1,0,0,0,348,346,1,0,0,0,349,
  	350,6,28,-1,0,350,351,3,54,27,0,351,357,1,0,0,0,352,353,10,1,0,0,353,
  	354,7,7,0,0,354,356,3,54,27,0,355,352,1,0,0,0,356,359,1,0,0,0,357,355,
  	1,0,0,0,357,358,1,0,0,0,358,57,1,0,0,0,359,357,1,0,0,0,360,361,6,29,-1,
  	0,361,362,3,56,28,0,362,368,1,0,0,0,363,364,10,1,0,0,364,365,5,28,0,0,
  	365,367,3,56,28,0,366,363,1,0,0,0,367,370,1,0,0,0,368,366,1,0,0,0,368,
  	369,1,0,0,0,369,59,1,0,0,0,370,368,1,0,0,0,371,372,6,30,-1,0,372,373,
  	3,58,29,0,373,379,1,0,0,0,374,375,10,1,0,0,375,376,5,29,0,0,376,378,3,
  	58,29,0,377,374,1,0,0,0,378,381,1,0,0,0,379,377,1,0,0,0,379,380,1,0,0,
  	0,380,61,1,0,0,0,381,379,1,0,0,0,382,383,3,52,26,0,383,63,1,0,0,0,37,
  	66,68,76,91,102,116,129,132,135,143,155,165,170,179,182,185,191,203,216,
  	219,225,232,240,251,265,268,281,290,298,304,313,324,335,346,357,368,379
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
      ((1ULL << _la) & ((1ULL << SysY2022Parser::INT)
      | (1ULL << SysY2022Parser::FLOAT)
      | (1ULL << SysY2022Parser::VOID)
      | (1ULL << SysY2022Parser::CONST)
      | (1ULL << SysY2022Parser::VECTOR))) != 0)) {
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

SysY2022Parser::VectorDeclContext* SysY2022Parser::DeclContext::vectorDecl() {
  return getRuleContext<SysY2022Parser::VectorDeclContext>(0);
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
    setState(76);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case SysY2022Parser::CONST: {
        enterOuterAlt(_localctx, 1);
        setState(73);
        constDecl();
        break;
      }

      case SysY2022Parser::INT:
      case SysY2022Parser::FLOAT: {
        enterOuterAlt(_localctx, 2);
        setState(74);
        varDecl();
        break;
      }

      case SysY2022Parser::VECTOR: {
        enterOuterAlt(_localctx, 3);
        setState(75);
        vectorDecl();
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

//----------------- VectorDeclContext ------------------------------------------------------------------

SysY2022Parser::VectorDeclContext::VectorDeclContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* SysY2022Parser::VectorDeclContext::VECTOR() {
  return getToken(SysY2022Parser::VECTOR, 0);
}

tree::TerminalNode* SysY2022Parser::VectorDeclContext::LT() {
  return getToken(SysY2022Parser::LT, 0);
}

SysY2022Parser::BTypeContext* SysY2022Parser::VectorDeclContext::bType() {
  return getRuleContext<SysY2022Parser::BTypeContext>(0);
}

tree::TerminalNode* SysY2022Parser::VectorDeclContext::GT() {
  return getToken(SysY2022Parser::GT, 0);
}

tree::TerminalNode* SysY2022Parser::VectorDeclContext::IDENTIFIER() {
  return getToken(SysY2022Parser::IDENTIFIER, 0);
}

tree::TerminalNode* SysY2022Parser::VectorDeclContext::SEMICOLON() {
  return getToken(SysY2022Parser::SEMICOLON, 0);
}

tree::TerminalNode* SysY2022Parser::VectorDeclContext::L_BRACKET() {
  return getToken(SysY2022Parser::L_BRACKET, 0);
}

SysY2022Parser::ConstExpContext* SysY2022Parser::VectorDeclContext::constExp() {
  return getRuleContext<SysY2022Parser::ConstExpContext>(0);
}

tree::TerminalNode* SysY2022Parser::VectorDeclContext::R_BRACKET() {
  return getToken(SysY2022Parser::R_BRACKET, 0);
}

tree::TerminalNode* SysY2022Parser::VectorDeclContext::L_PAREN() {
  return getToken(SysY2022Parser::L_PAREN, 0);
}

SysY2022Parser::ExpContext* SysY2022Parser::VectorDeclContext::exp() {
  return getRuleContext<SysY2022Parser::ExpContext>(0);
}

tree::TerminalNode* SysY2022Parser::VectorDeclContext::R_PAREN() {
  return getToken(SysY2022Parser::R_PAREN, 0);
}


size_t SysY2022Parser::VectorDeclContext::getRuleIndex() const {
  return SysY2022Parser::RuleVectorDecl;
}

void SysY2022Parser::VectorDeclContext::enterRule(tree::ParseTreeListener *listener) {
  auto parserListener = dynamic_cast<SysY2022ParserListener *>(listener);
  if (parserListener != nullptr)
    parserListener->enterVectorDecl(this);
}

void SysY2022Parser::VectorDeclContext::exitRule(tree::ParseTreeListener *listener) {
  auto parserListener = dynamic_cast<SysY2022ParserListener *>(listener);
  if (parserListener != nullptr)
    parserListener->exitVectorDecl(this);
}


std::any SysY2022Parser::VectorDeclContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<SysY2022ParserVisitor*>(visitor))
    return parserVisitor->visitVectorDecl(this);
  else
    return visitor->visitChildren(this);
}

SysY2022Parser::VectorDeclContext* SysY2022Parser::vectorDecl() {
  VectorDeclContext *_localctx = _tracker.createInstance<VectorDeclContext>(_ctx, getState());
  enterRule(_localctx, 4, SysY2022Parser::RuleVectorDecl);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(78);
    match(SysY2022Parser::VECTOR);
    setState(79);
    match(SysY2022Parser::LT);
    setState(80);
    bType();
    setState(81);
    match(SysY2022Parser::GT);
    setState(82);
    match(SysY2022Parser::IDENTIFIER);
    setState(91);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case SysY2022Parser::L_BRACKET: {
        setState(83);
        match(SysY2022Parser::L_BRACKET);
        setState(84);
        constExp();
        setState(85);
        match(SysY2022Parser::R_BRACKET);
        break;
      }

      case SysY2022Parser::L_PAREN: {
        setState(87);
        match(SysY2022Parser::L_PAREN);
        setState(88);
        exp();
        setState(89);
        match(SysY2022Parser::R_PAREN);
        break;
      }

    default:
      throw NoViableAltException(this);
    }
    setState(93);
    match(SysY2022Parser::SEMICOLON);
   
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
  enterRule(_localctx, 6, SysY2022Parser::RuleConstDecl);
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
    setState(95);
    match(SysY2022Parser::CONST);
    setState(96);
    bType();
    setState(97);
    constDef();
    setState(102);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == SysY2022Parser::COMMA) {
      setState(98);
      match(SysY2022Parser::COMMA);
      setState(99);
      constDef();
      setState(104);
      _errHandler->sync(this);
      _la = _input->LA(1);
    }
    setState(105);
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
  enterRule(_localctx, 8, SysY2022Parser::RuleBType);
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
    setState(107);
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
    setState(109);
    match(SysY2022Parser::IDENTIFIER);
    setState(116);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == SysY2022Parser::L_BRACKET) {
      setState(110);
      match(SysY2022Parser::L_BRACKET);
      setState(111);
      constExp();
      setState(112);
      match(SysY2022Parser::R_BRACKET);
      setState(118);
      _errHandler->sync(this);
      _la = _input->LA(1);
    }
    setState(119);
    match(SysY2022Parser::ASSIGN);
    setState(120);
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
    setState(135);
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
        setState(122);
        constExp();
        break;
      }

      case SysY2022Parser::L_BRACE: {
        enterOuterAlt(_localctx, 2);
        setState(123);
        match(SysY2022Parser::L_BRACE);
        setState(132);
        _errHandler->sync(this);

        _la = _input->LA(1);
        if ((((_la & ~ 0x3fULL) == 0) &&
          ((1ULL << _la) & ((1ULL << SysY2022Parser::L_PAREN)
          | (1ULL << SysY2022Parser::L_BRACE)
          | (1ULL << SysY2022Parser::PLUS)
          | (1ULL << SysY2022Parser::MINUS)
          | (1ULL << SysY2022Parser::NOT)
          | (1ULL << SysY2022Parser::IDENTIFIER)
          | (1ULL << SysY2022Parser::INTCONST)
          | (1ULL << SysY2022Parser::FLOATCONST))) != 0)) {
          setState(124);
          constInitVal();
          setState(129);
          _errHandler->sync(this);
          _la = _input->LA(1);
          while (_la == SysY2022Parser::COMMA) {
            setState(125);
            match(SysY2022Parser::COMMA);
            setState(126);
            constInitVal();
            setState(131);
            _errHandler->sync(this);
            _la = _input->LA(1);
          }
        }
        setState(134);
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
    setState(137);
    bType();
    setState(138);
    varDef();
    setState(143);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == SysY2022Parser::COMMA) {
      setState(139);
      match(SysY2022Parser::COMMA);
      setState(140);
      varDef();
      setState(145);
      _errHandler->sync(this);
      _la = _input->LA(1);
    }
    setState(146);
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
    setState(170);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 12, _ctx)) {
    case 1: {
      enterOuterAlt(_localctx, 1);
      setState(148);
      match(SysY2022Parser::IDENTIFIER);
      setState(155);
      _errHandler->sync(this);
      _la = _input->LA(1);
      while (_la == SysY2022Parser::L_BRACKET) {
        setState(149);
        match(SysY2022Parser::L_BRACKET);
        setState(150);
        constExp();
        setState(151);
        match(SysY2022Parser::R_BRACKET);
        setState(157);
        _errHandler->sync(this);
        _la = _input->LA(1);
      }
      break;
    }

    case 2: {
      enterOuterAlt(_localctx, 2);
      setState(158);
      match(SysY2022Parser::IDENTIFIER);
      setState(165);
      _errHandler->sync(this);
      _la = _input->LA(1);
      while (_la == SysY2022Parser::L_BRACKET) {
        setState(159);
        match(SysY2022Parser::L_BRACKET);
        setState(160);
        constExp();
        setState(161);
        match(SysY2022Parser::R_BRACKET);
        setState(167);
        _errHandler->sync(this);
        _la = _input->LA(1);
      }
      setState(168);
      match(SysY2022Parser::ASSIGN);
      setState(169);
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
    setState(185);
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
        setState(172);
        exp();
        break;
      }

      case SysY2022Parser::L_BRACE: {
        enterOuterAlt(_localctx, 2);
        setState(173);
        match(SysY2022Parser::L_BRACE);
        setState(182);
        _errHandler->sync(this);

        _la = _input->LA(1);
        if ((((_la & ~ 0x3fULL) == 0) &&
          ((1ULL << _la) & ((1ULL << SysY2022Parser::L_PAREN)
          | (1ULL << SysY2022Parser::L_BRACE)
          | (1ULL << SysY2022Parser::PLUS)
          | (1ULL << SysY2022Parser::MINUS)
          | (1ULL << SysY2022Parser::NOT)
          | (1ULL << SysY2022Parser::IDENTIFIER)
          | (1ULL << SysY2022Parser::INTCONST)
          | (1ULL << SysY2022Parser::FLOATCONST))) != 0)) {
          setState(174);
          initVal();
          setState(179);
          _errHandler->sync(this);
          _la = _input->LA(1);
          while (_la == SysY2022Parser::COMMA) {
            setState(175);
            match(SysY2022Parser::COMMA);
            setState(176);
            initVal();
            setState(181);
            _errHandler->sync(this);
            _la = _input->LA(1);
          }
        }
        setState(184);
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
    setState(187);
    funcType();
    setState(188);
    match(SysY2022Parser::IDENTIFIER);
    setState(189);
    match(SysY2022Parser::L_PAREN);
    setState(191);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == SysY2022Parser::INT

    || _la == SysY2022Parser::FLOAT) {
      setState(190);
      funcFParams();
    }
    setState(193);
    match(SysY2022Parser::R_PAREN);
    setState(194);
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
  enterRule(_localctx, 22, SysY2022Parser::RuleFuncType);
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
    setState(196);
    _la = _input->LA(1);
    if (!((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & ((1ULL << SysY2022Parser::INT)
      | (1ULL << SysY2022Parser::FLOAT)
      | (1ULL << SysY2022Parser::VOID))) != 0))) {
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
    setState(198);
    funcFParam();
    setState(203);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == SysY2022Parser::COMMA) {
      setState(199);
      match(SysY2022Parser::COMMA);
      setState(200);
      funcFParam();
      setState(205);
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
    setState(206);
    bType();
    setState(207);
    match(SysY2022Parser::IDENTIFIER);
    setState(219);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == SysY2022Parser::L_BRACKET) {
      setState(208);
      match(SysY2022Parser::L_BRACKET);
      setState(209);
      match(SysY2022Parser::R_BRACKET);
      setState(216);
      _errHandler->sync(this);
      _la = _input->LA(1);
      while (_la == SysY2022Parser::L_BRACKET) {
        setState(210);
        match(SysY2022Parser::L_BRACKET);
        setState(211);
        exp();
        setState(212);
        match(SysY2022Parser::R_BRACKET);
        setState(218);
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
    setState(221);
    match(SysY2022Parser::L_BRACE);
    setState(225);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & ((1ULL << SysY2022Parser::INT)
      | (1ULL << SysY2022Parser::FLOAT)
      | (1ULL << SysY2022Parser::CONST)
      | (1ULL << SysY2022Parser::VECTOR)
      | (1ULL << SysY2022Parser::IF)
      | (1ULL << SysY2022Parser::WHILE)
      | (1ULL << SysY2022Parser::BREAK)
      | (1ULL << SysY2022Parser::CONTINUE)
      | (1ULL << SysY2022Parser::RETURN)
      | (1ULL << SysY2022Parser::L_PAREN)
      | (1ULL << SysY2022Parser::L_BRACE)
      | (1ULL << SysY2022Parser::SEMICOLON)
      | (1ULL << SysY2022Parser::PLUS)
      | (1ULL << SysY2022Parser::MINUS)
      | (1ULL << SysY2022Parser::NOT)
      | (1ULL << SysY2022Parser::IDENTIFIER)
      | (1ULL << SysY2022Parser::INTCONST)
      | (1ULL << SysY2022Parser::FLOATCONST))) != 0)) {
      setState(222);
      blockItem();
      setState(227);
      _errHandler->sync(this);
      _la = _input->LA(1);
    }
    setState(228);
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
    setState(232);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case SysY2022Parser::INT:
      case SysY2022Parser::FLOAT:
      case SysY2022Parser::CONST:
      case SysY2022Parser::VECTOR: {
        enterOuterAlt(_localctx, 1);
        setState(230);
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
        setState(231);
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
    setState(268);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 25, _ctx)) {
    case 1: {
      enterOuterAlt(_localctx, 1);
      setState(234);
      lVal();
      setState(235);
      match(SysY2022Parser::ASSIGN);
      setState(236);
      exp();
      setState(237);
      match(SysY2022Parser::SEMICOLON);
      break;
    }

    case 2: {
      enterOuterAlt(_localctx, 2);
      setState(240);
      _errHandler->sync(this);

      _la = _input->LA(1);
      if ((((_la & ~ 0x3fULL) == 0) &&
        ((1ULL << _la) & ((1ULL << SysY2022Parser::L_PAREN)
        | (1ULL << SysY2022Parser::PLUS)
        | (1ULL << SysY2022Parser::MINUS)
        | (1ULL << SysY2022Parser::NOT)
        | (1ULL << SysY2022Parser::IDENTIFIER)
        | (1ULL << SysY2022Parser::INTCONST)
        | (1ULL << SysY2022Parser::FLOATCONST))) != 0)) {
        setState(239);
        exp();
      }
      setState(242);
      match(SysY2022Parser::SEMICOLON);
      break;
    }

    case 3: {
      enterOuterAlt(_localctx, 3);
      setState(243);
      block();
      break;
    }

    case 4: {
      enterOuterAlt(_localctx, 4);
      setState(244);
      match(SysY2022Parser::IF);
      setState(245);
      match(SysY2022Parser::L_PAREN);
      setState(246);
      cond();
      setState(247);
      match(SysY2022Parser::R_PAREN);
      setState(248);
      stmt();
      setState(251);
      _errHandler->sync(this);

      switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 23, _ctx)) {
      case 1: {
        setState(249);
        match(SysY2022Parser::ELSE);
        setState(250);
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
      setState(253);
      match(SysY2022Parser::WHILE);
      setState(254);
      match(SysY2022Parser::L_PAREN);
      setState(255);
      cond();
      setState(256);
      match(SysY2022Parser::R_PAREN);
      setState(257);
      stmt();
      break;
    }

    case 6: {
      enterOuterAlt(_localctx, 6);
      setState(259);
      match(SysY2022Parser::BREAK);
      setState(260);
      match(SysY2022Parser::SEMICOLON);
      break;
    }

    case 7: {
      enterOuterAlt(_localctx, 7);
      setState(261);
      match(SysY2022Parser::CONTINUE);
      setState(262);
      match(SysY2022Parser::SEMICOLON);
      break;
    }

    case 8: {
      enterOuterAlt(_localctx, 8);
      setState(263);
      match(SysY2022Parser::RETURN);
      setState(265);
      _errHandler->sync(this);

      _la = _input->LA(1);
      if ((((_la & ~ 0x3fULL) == 0) &&
        ((1ULL << _la) & ((1ULL << SysY2022Parser::L_PAREN)
        | (1ULL << SysY2022Parser::PLUS)
        | (1ULL << SysY2022Parser::MINUS)
        | (1ULL << SysY2022Parser::NOT)
        | (1ULL << SysY2022Parser::IDENTIFIER)
        | (1ULL << SysY2022Parser::INTCONST)
        | (1ULL << SysY2022Parser::FLOATCONST))) != 0)) {
        setState(264);
        exp();
      }
      setState(267);
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
    setState(270);
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
    setState(272);
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
    setState(274);
    match(SysY2022Parser::IDENTIFIER);
    setState(281);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 26, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(275);
        match(SysY2022Parser::L_BRACKET);
        setState(276);
        exp();
        setState(277);
        match(SysY2022Parser::R_BRACKET); 
      }
      setState(283);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 26, _ctx);
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
    setState(290);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case SysY2022Parser::L_PAREN: {
        enterOuterAlt(_localctx, 1);
        setState(284);
        match(SysY2022Parser::L_PAREN);
        setState(285);
        exp();
        setState(286);
        match(SysY2022Parser::R_PAREN);
        break;
      }

      case SysY2022Parser::IDENTIFIER: {
        enterOuterAlt(_localctx, 2);
        setState(288);
        lVal();
        break;
      }

      case SysY2022Parser::INTCONST:
      case SysY2022Parser::FLOATCONST: {
        enterOuterAlt(_localctx, 3);
        setState(289);
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
    setState(292);
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
    setState(304);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 29, _ctx)) {
    case 1: {
      enterOuterAlt(_localctx, 1);
      setState(294);
      primaryExp();
      break;
    }

    case 2: {
      enterOuterAlt(_localctx, 2);
      setState(295);
      match(SysY2022Parser::IDENTIFIER);
      setState(296);
      match(SysY2022Parser::L_PAREN);
      setState(298);
      _errHandler->sync(this);

      _la = _input->LA(1);
      if ((((_la & ~ 0x3fULL) == 0) &&
        ((1ULL << _la) & ((1ULL << SysY2022Parser::L_PAREN)
        | (1ULL << SysY2022Parser::PLUS)
        | (1ULL << SysY2022Parser::MINUS)
        | (1ULL << SysY2022Parser::NOT)
        | (1ULL << SysY2022Parser::IDENTIFIER)
        | (1ULL << SysY2022Parser::INTCONST)
        | (1ULL << SysY2022Parser::FLOATCONST))) != 0)) {
        setState(297);
        funcRParams();
      }
      setState(300);
      match(SysY2022Parser::R_PAREN);
      break;
    }

    case 3: {
      enterOuterAlt(_localctx, 3);
      setState(301);
      unaryOp();
      setState(302);
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
    setState(306);
    _la = _input->LA(1);
    if (!((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & ((1ULL << SysY2022Parser::PLUS)
      | (1ULL << SysY2022Parser::MINUS)
      | (1ULL << SysY2022Parser::NOT))) != 0))) {
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
    setState(308);
    exp();
    setState(313);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == SysY2022Parser::COMMA) {
      setState(309);
      match(SysY2022Parser::COMMA);
      setState(310);
      exp();
      setState(315);
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
    setState(317);
    unaryExp();
    _ctx->stop = _input->LT(-1);
    setState(324);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 31, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        if (!_parseListeners.empty())
          triggerExitRuleEvent();
        previousContext = _localctx;
        _localctx = _tracker.createInstance<MulExpContext>(parentContext, parentState);
        pushNewRecursionContext(_localctx, startState, RuleMulExp);
        setState(319);

        if (!(precpred(_ctx, 1))) throw FailedPredicateException(this, "precpred(_ctx, 1)");
        setState(320);
        _la = _input->LA(1);
        if (!((((_la & ~ 0x3fULL) == 0) &&
          ((1ULL << _la) & ((1ULL << SysY2022Parser::STAR)
          | (1ULL << SysY2022Parser::DIV)
          | (1ULL << SysY2022Parser::MOD))) != 0))) {
        _errHandler->recoverInline(this);
        }
        else {
          _errHandler->reportMatch(this);
          consume();
        }
        setState(321);
        unaryExp(); 
      }
      setState(326);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 31, _ctx);
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
    setState(328);
    mulExp(0);
    _ctx->stop = _input->LT(-1);
    setState(335);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 32, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        if (!_parseListeners.empty())
          triggerExitRuleEvent();
        previousContext = _localctx;
        _localctx = _tracker.createInstance<AddExpContext>(parentContext, parentState);
        pushNewRecursionContext(_localctx, startState, RuleAddExp);
        setState(330);

        if (!(precpred(_ctx, 1))) throw FailedPredicateException(this, "precpred(_ctx, 1)");
        setState(331);
        _la = _input->LA(1);
        if (!(_la == SysY2022Parser::PLUS

        || _la == SysY2022Parser::MINUS)) {
        _errHandler->recoverInline(this);
        }
        else {
          _errHandler->reportMatch(this);
          consume();
        }
        setState(332);
        mulExp(0); 
      }
      setState(337);
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
    setState(339);
    addExp(0);
    _ctx->stop = _input->LT(-1);
    setState(346);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 33, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        if (!_parseListeners.empty())
          triggerExitRuleEvent();
        previousContext = _localctx;
        _localctx = _tracker.createInstance<RelExpContext>(parentContext, parentState);
        pushNewRecursionContext(_localctx, startState, RuleRelExp);
        setState(341);

        if (!(precpred(_ctx, 1))) throw FailedPredicateException(this, "precpred(_ctx, 1)");
        setState(342);
        _la = _input->LA(1);
        if (!((((_la & ~ 0x3fULL) == 0) &&
          ((1ULL << _la) & ((1ULL << SysY2022Parser::LT)
          | (1ULL << SysY2022Parser::GT)
          | (1ULL << SysY2022Parser::LE)
          | (1ULL << SysY2022Parser::GE))) != 0))) {
        _errHandler->recoverInline(this);
        }
        else {
          _errHandler->reportMatch(this);
          consume();
        }
        setState(343);
        addExp(0); 
      }
      setState(348);
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
    setState(350);
    relExp(0);
    _ctx->stop = _input->LT(-1);
    setState(357);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 34, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        if (!_parseListeners.empty())
          triggerExitRuleEvent();
        previousContext = _localctx;
        _localctx = _tracker.createInstance<EqExpContext>(parentContext, parentState);
        pushNewRecursionContext(_localctx, startState, RuleEqExp);
        setState(352);

        if (!(precpred(_ctx, 1))) throw FailedPredicateException(this, "precpred(_ctx, 1)");
        setState(353);
        _la = _input->LA(1);
        if (!(_la == SysY2022Parser::EQ

        || _la == SysY2022Parser::NE)) {
        _errHandler->recoverInline(this);
        }
        else {
          _errHandler->reportMatch(this);
          consume();
        }
        setState(354);
        relExp(0); 
      }
      setState(359);
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
    setState(361);
    eqExp(0);
    _ctx->stop = _input->LT(-1);
    setState(368);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 35, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        if (!_parseListeners.empty())
          triggerExitRuleEvent();
        previousContext = _localctx;
        _localctx = _tracker.createInstance<LAndExpContext>(parentContext, parentState);
        pushNewRecursionContext(_localctx, startState, RuleLAndExp);
        setState(363);

        if (!(precpred(_ctx, 1))) throw FailedPredicateException(this, "precpred(_ctx, 1)");
        setState(364);
        match(SysY2022Parser::AND);
        setState(365);
        eqExp(0); 
      }
      setState(370);
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
    setState(372);
    lAndExp(0);
    _ctx->stop = _input->LT(-1);
    setState(379);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 36, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        if (!_parseListeners.empty())
          triggerExitRuleEvent();
        previousContext = _localctx;
        _localctx = _tracker.createInstance<LOrExpContext>(parentContext, parentState);
        pushNewRecursionContext(_localctx, startState, RuleLOrExp);
        setState(374);

        if (!(precpred(_ctx, 1))) throw FailedPredicateException(this, "precpred(_ctx, 1)");
        setState(375);
        match(SysY2022Parser::OR);
        setState(376);
        lAndExp(0); 
      }
      setState(381);
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
    setState(382);
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
  std::call_once(sysy2022parserParserOnceFlag, sysy2022parserParserInitialize);
}
