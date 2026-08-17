
// Generated from /mnt/d/VSCodeProjects/compiler/grammar/SysY2022Lexer.g4 by ANTLR 4.13.1

#pragma once


#include "antlr4-runtime.h"




class  SysY2022Lexer : public antlr4::Lexer {
public:
  enum {
    INT = 1, FLOAT = 2, VOID = 3, CONST = 4, VEC = 5, VECF = 6, IF = 7, 
    ELSE = 8, WHILE = 9, BREAK = 10, CONTINUE = 11, RETURN = 12, L_PAREN = 13, 
    R_PAREN = 14, L_BRACKET = 15, R_BRACKET = 16, L_BRACE = 17, R_BRACE = 18, 
    COMMA = 19, SEMICOLON = 20, QUESTION = 21, COLON = 22, PLUS = 23, MINUS = 24, 
    STAR = 25, DIV = 26, MOD = 27, NOT = 28, AND = 29, OR = 30, LT = 31, 
    GT = 32, LE = 33, GE = 34, EQ = 35, NE = 36, ASSIGN = 37, IDENTIFIER = 38, 
    INTCONST = 39, FLOATCONST = 40, WHITESPACE = 41, LINE_COMMENT = 42, 
    BLOCK_COMMENT = 43
  };

  explicit SysY2022Lexer(antlr4::CharStream *input);

  ~SysY2022Lexer() override;


  std::string getGrammarFileName() const override;

  const std::vector<std::string>& getRuleNames() const override;

  const std::vector<std::string>& getChannelNames() const override;

  const std::vector<std::string>& getModeNames() const override;

  const antlr4::dfa::Vocabulary& getVocabulary() const override;

  antlr4::atn::SerializedATNView getSerializedATN() const override;

  const antlr4::atn::ATN& getATN() const override;

  // By default the static state used to implement the lexer is lazily initialized during the first
  // call to the constructor. You can call this function if you wish to initialize the static state
  // ahead of time.
  static void initialize();

private:

  // Individual action functions triggered by action() above.

  // Individual semantic predicate functions triggered by sempred() above.

};

