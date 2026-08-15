
// Generated from /mnt/d/VSCodeProjects/compiler/grammar/SysY2022Lexer.g4 by ANTLR 4.10.1

#pragma once


#include "antlr4-runtime.h"




class  SysY2022Lexer : public antlr4::Lexer {
public:
  enum {
    INT = 1, FLOAT = 2, VOID = 3, CONST = 4, VECTOR = 5, IF = 6, ELSE = 7, 
    WHILE = 8, BREAK = 9, CONTINUE = 10, RETURN = 11, L_PAREN = 12, R_PAREN = 13, 
    L_BRACKET = 14, R_BRACKET = 15, L_BRACE = 16, R_BRACE = 17, COMMA = 18, 
    SEMICOLON = 19, QUESTION = 20, COLON = 21, PLUS = 22, MINUS = 23, STAR = 24, 
    DIV = 25, MOD = 26, NOT = 27, AND = 28, OR = 29, LT = 30, GT = 31, LE = 32, 
    GE = 33, EQ = 34, NE = 35, ASSIGN = 36, IDENTIFIER = 37, INTCONST = 38, 
    FLOATCONST = 39, WHITESPACE = 40, LINE_COMMENT = 41, BLOCK_COMMENT = 42
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

