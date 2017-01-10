/* A Bison parser, made by GNU Bison 3.0.4.  */

/* Bison interface for Yacc-like parsers in C

   Copyright (C) 1984, 1989-1990, 2000-2015 Free Software Foundation, Inc.

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.  */

/* As a special exception, you may create a larger work that contains
   part or all of the Bison parser skeleton and distribute that work
   under terms of your choice, so long as that work isn't itself a
   parser generator using the skeleton or a modified version thereof
   as a parser skeleton.  Alternatively, if you modify or redistribute
   the parser skeleton itself, you may (at your option) remove this
   special exception, which will cause the skeleton and the resulting
   Bison output files to be licensed under the GNU General Public
   License without this special exception.

   This special exception was added by the Free Software Foundation in
   version 2.2 of Bison.  */

#ifndef YY_YY_GRAMMAR_H_INCLUDED
# define YY_YY_GRAMMAR_H_INCLUDED
/* Debug traces.  */
#ifndef YYDEBUG
# define YYDEBUG 0
#endif
#if YYDEBUG
extern int yydebug;
#endif

/* Token type.  */
#ifndef YYTOKENTYPE
# define YYTOKENTYPE
  enum yytokentype
  {
    NAME = 258,
    FUNC = 259,
    BEGINOBJ = 260,
    ENDOBJ = 261,
    BEGIN_ACTION = 262,
    BEGIN_PROPERTY = 263,
    BEGIN_CONSTANT = 264,
    BEGIN_TPL = 265,
    BEGIN_RULESET = 266,
    STOP = 267,
    SET = 268,
    RESET = 269,
    UNSET = 270,
    CONTINUE = 271,
    CALL = 272,
    CALL_INDIRECT = 273,
    LEGACY_ACTION = 274,
    LEGACY_RULESET = 275,
    PRIFILT = 276,
    PROPFILT = 277,
    BSD_TAG_SELECTOR = 278,
    BSD_HOST_SELECTOR = 279,
    RELOAD_LOOKUP_TABLE_PROCEDURE = 280,
    IF = 281,
    THEN = 282,
    ELSE = 283,
    FOREACH = 284,
    ITERATOR_ASSIGNMENT = 285,
    DO = 286,
    OR = 287,
    AND = 288,
    NOT = 289,
    VAR = 290,
    STRING = 291,
    NUMBER = 292,
    CMP_EQ = 293,
    CMP_NE = 294,
    CMP_LE = 295,
    CMP_GE = 296,
    CMP_LT = 297,
    CMP_GT = 298,
    CMP_CONTAINS = 299,
    CMP_CONTAINSI = 300,
    CMP_STARTSWITH = 301,
    CMP_STARTSWITHI = 302,
    UMINUS = 303
  };
#endif
/* Tokens.  */
#define NAME 258
#define FUNC 259
#define BEGINOBJ 260
#define ENDOBJ 261
#define BEGIN_ACTION 262
#define BEGIN_PROPERTY 263
#define BEGIN_CONSTANT 264
#define BEGIN_TPL 265
#define BEGIN_RULESET 266
#define STOP 267
#define SET 268
#define RESET 269
#define UNSET 270
#define CONTINUE 271
#define CALL 272
#define CALL_INDIRECT 273
#define LEGACY_ACTION 274
#define LEGACY_RULESET 275
#define PRIFILT 276
#define PROPFILT 277
#define BSD_TAG_SELECTOR 278
#define BSD_HOST_SELECTOR 279
#define RELOAD_LOOKUP_TABLE_PROCEDURE 280
#define IF 281
#define THEN 282
#define ELSE 283
#define FOREACH 284
#define ITERATOR_ASSIGNMENT 285
#define DO 286
#define OR 287
#define AND 288
#define NOT 289
#define VAR 290
#define STRING 291
#define NUMBER 292
#define CMP_EQ 293
#define CMP_NE 294
#define CMP_LE 295
#define CMP_GE 296
#define CMP_LT 297
#define CMP_GT 298
#define CMP_CONTAINS 299
#define CMP_CONTAINSI 300
#define CMP_STARTSWITH 301
#define CMP_STARTSWITHI 302
#define UMINUS 303

/* Value type.  */
#if ! defined YYSTYPE && ! defined YYSTYPE_IS_DECLARED

union YYSTYPE
{
#line 53 "grammar.y" /* yacc.c:1909  */

	char *s;
	long long n;
	es_str_t *estr;
	enum cnfobjType objType;
	struct cnfobj *obj;
	struct cnfstmt *stmt;
	struct nvlst *nvlst;
	struct objlst *objlst;
	struct cnfexpr *expr;
	struct cnfarray *arr;
	struct cnffunc *func;
	struct cnffparamlst *fparams;
	struct cnfitr *itr;

#line 166 "grammar.h" /* yacc.c:1909  */
};

typedef union YYSTYPE YYSTYPE;
# define YYSTYPE_IS_TRIVIAL 1
# define YYSTYPE_IS_DECLARED 1
#endif


extern YYSTYPE yylval;

int yyparse (void);

#endif /* !YY_YY_GRAMMAR_H_INCLUDED  */
