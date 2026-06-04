
// Generated from grammar/SysY2022Lexer.g4 by ANTLR 4.13.1

#pragma once


#include "antlr4-runtime.h"




class  SysY2022Lexer : public antlr4::Lexer {
public:
  enum {
    INT = 1, FLOAT = 2, VOID = 3, CONST = 4, IF = 5, ELSE = 6, WHILE = 7, 
    BREAK = 8, CONTINUE = 9, RETURN = 10, L_PAREN = 11, R_PAREN = 12, L_BRACKET = 13, 
    R_BRACKET = 14, L_BRACE = 15, R_BRACE = 16, COMMA = 17, SEMICOLON = 18, 
    QUESTION = 19, COLON = 20, PLUS = 21, MINUS = 22, STAR = 23, DIV = 24, 
    MOD = 25, NOT = 26, AND = 27, OR = 28, LT = 29, GT = 30, LE = 31, GE = 32, 
    EQ = 33, NE = 34, ASSIGN = 35, IDENTIFIER = 36, INTCONST = 37, FLOATCONST = 38, 
    WHITESPACE = 39, LINE_COMMENT = 40, BLOCK_COMMENT = 41
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

