%option yylineno
%option nounput
%option bison-bridge bison-locations reentrant noyywrap
%option extra-type="struct brw_asm_parser *"
%option prefix="brw_asm_"
%{
/*
 * Copyright © 2018 Intel Corporation
 * SPDX-License-Identifier: MIT
 */
#include <string.h>
#include "brw_asm_internal.h"
#undef ALIGN16
#include "brw_gram.tab.h"

#define YY_NO_INPUT

#define YY_USER_ACTION                                          \
        yylloc->first_line = yylloc->last_line = yylineno;      \
        yylloc->first_column = yycolumn;                        \
        yylloc->last_column = yycolumn + yyleng - 1;            \
        yycolumn += yyleng;

#define YY_USER_INIT                                            \
        do {                                                    \
           yylineno = 1;                                        \
           yycolumn = 1;                                        \
        } while (0)
%}

%x BLOCK_COMMENT
%x FILENAME
%x CHANNEL
%x REG
%x DOTSEL
%x LABEL
%x MSGDESC
%%

   struct brw_asm_parser *parser = yyextra;

 /* eat up single line comment */
\/\/.*[\r\n]    { yycolumn = 1; }

 /* eat up multiline comment */
\/\*            { parser->saved_state = YYSTATE; BEGIN(BLOCK_COMMENT); }

<BLOCK_COMMENT>\*\/     { BEGIN(parser->saved_state); }

<BLOCK_COMMENT>.        { }
<BLOCK_COMMENT>[\r\n]   { }

<FILENAME>\"[^\"]+\"    {
                           char *name = malloc(yyleng - 1);
                           memmove(name, yytext + 1, yyleng - 2);
                           name[yyleng-1] = '\0';
                           parser->input_filename = name;
                        }

 /* null register */
null            { BEGIN(REG); return NULL_TOKEN; }

 /* Opcodes */
add             { yylval->integer = GEN_OP_ADD; return ADD; }
add3            { yylval->integer = GEN_OP_ADD3; return ADD3; }
addc            { yylval->integer = GEN_OP_ADDC; return ADDC; }
and             { yylval->integer = GEN_OP_AND; return AND; }
asr             { yylval->integer = GEN_OP_ASR; return ASR; }
avg             { yylval->integer = GEN_OP_AVG; return AVG; }
bfe             { yylval->integer = GEN_OP_BFE; return BFE; }
bfi1            { yylval->integer = GEN_OP_BFI1; return BFI1; }
bfi2            { yylval->integer = GEN_OP_BFI2; return BFI2; }
bfrev           { yylval->integer = GEN_OP_BFREV; return BFREV; }
brc             { yylval->integer = GEN_OP_BRC; return BRC; }
brd             { yylval->integer = GEN_OP_BRD; return BRD; }
break           { yylval->integer = GEN_OP_BREAK; return BREAK; }
call            { yylval->integer = GEN_OP_CALL; return CALL; }
calla           { yylval->integer = GEN_OP_CALLA; return CALLA; }
cbit            { yylval->integer = GEN_OP_CBIT; return CBIT; }
cmp             { yylval->integer = GEN_OP_CMP; return CMP; }
cmpn            { yylval->integer = GEN_OP_CMPN; return CMPN; }
cont            { yylval->integer = GEN_OP_CONTINUE; return CONT; }
csel            { yylval->integer = GEN_OP_CSEL; return CSEL; }
do              { yylval->integer = GEN_OP_NOP; return DO; }
dp2             { yylval->integer = GEN_OP_DP2; return DP2; }
dp3             { yylval->integer = GEN_OP_DP3; return DP3; }
dp4             { yylval->integer = GEN_OP_DP4; return DP4; }
dp4a            { yylval->integer = GEN_OP_DP4A; return DP4A; }
dpas            { yylval->integer = GEN_OP_DPAS; return DPAS; }
dph             { yylval->integer = GEN_OP_DPH; return DPH; }
else            { yylval->integer = GEN_OP_ELSE; return ELSE; }
endif           { yylval->integer = GEN_OP_ENDIF; return ENDIF; }
fbh             { yylval->integer = GEN_OP_FBH; return FBH; }
fbl             { yylval->integer = GEN_OP_FBL; return FBL; }
frc             { yylval->integer = GEN_OP_FRC; return FRC; }
goto            { yylval->integer = GEN_OP_GOTO; return GOTO; }
halt            { yylval->integer = GEN_OP_HALT; return HALT; }
if              { yylval->integer = GEN_OP_IF; return IF; }
illegal         { yylval->integer = GEN_OP_ILLEGAL; return ILLEGAL; }
join            { yylval->integer = GEN_OP_JOIN; return JOIN; }
jmpi            { yylval->integer = GEN_OP_JMPI; return JMPI; }
line            { yylval->integer = GEN_OP_LINE; return LINE; }
lrp             { yylval->integer = GEN_OP_LRP; return LRP; }
lzd             { yylval->integer = GEN_OP_LZD; return LZD; }
mac             { yylval->integer = GEN_OP_MAC; return MAC; }
mach            { yylval->integer = GEN_OP_MACH; return MACH; }
mad             { yylval->integer = GEN_OP_MAD; return MAD; }
madm            { yylval->integer = GEN_OP_MADM; return MADM; }
mov             { yylval->integer = GEN_OP_MOV; return MOV; }
movi            { yylval->integer = GEN_OP_MOVI; return MOVI; }
mul             { yylval->integer = GEN_OP_MUL; return MUL; }
nop             { yylval->integer = GEN_OP_NOP; return NOP; }
not             { yylval->integer = GEN_OP_NOT; return NOT; }
or              { yylval->integer = GEN_OP_OR; return OR; }
pln             { yylval->integer = GEN_OP_PLN; return PLN; }
ret             { yylval->integer = GEN_OP_RET; return RET; }
rndd            { yylval->integer = GEN_OP_RNDD; return RNDD; }
rnde            { yylval->integer = GEN_OP_RNDE; return RNDE; }
rndu            { yylval->integer = GEN_OP_RNDU; return RNDU; }
rndz            { yylval->integer = GEN_OP_RNDZ; return RNDZ; }
rol             { yylval->integer = GEN_OP_ROL; return ROL; }
ror             { yylval->integer = GEN_OP_ROR; return ROR; }
sel             { yylval->integer = GEN_OP_SEL; return SEL; }
send            {
                   yylval->integer = GEN_OP_SEND;
                   return parser->devinfo->ver < 12 ? SEND_GFX4 : SEND_GFX12;
                }
sendc           {
                   yylval->integer = GEN_OP_SENDC;
                   return parser->devinfo->ver < 12 ? SENDC_GFX4 : SENDC_GFX12;
                }
sends           { yylval->integer = GEN_OP_SENDS; return SENDS; }
sendsc          { yylval->integer = GEN_OP_SENDSC; return SENDSC; }
shl             { yylval->integer = GEN_OP_SHL; return SHL; }
shr             { yylval->integer = GEN_OP_SHR; return SHR; }
smov            { yylval->integer = GEN_OP_SMOV; return SMOV; }
srnd            { yylval->integer = GEN_OP_SRND; return SRND; }
subb            { yylval->integer = GEN_OP_SUBB; return SUBB; }
wait            { yylval->integer = GEN_OP_WAIT; return WAIT; }
while           { yylval->integer = GEN_OP_WHILE; return WHILE; }
xor             { yylval->integer = GEN_OP_XOR; return XOR; }
sync            { yylval->integer = GEN_OP_SYNC; return SYNC; }
math            { yylval->integer = GEN_OP_MATH; return MATH; }

 /* extended math functions */
cos             { yylval->integer = BRW_MATH_FUNCTION_COS; return COS; }
exp             { yylval->integer = BRW_MATH_FUNCTION_EXP; return EXP; }
fdiv            { yylval->integer = BRW_MATH_FUNCTION_FDIV; return FDIV; }
inv             { yylval->integer = BRW_MATH_FUNCTION_INV; return INV; }
invm            { yylval->integer = GFX8_MATH_FUNCTION_INVM; return INVM; }
intdiv          {
                   yylval->integer = BRW_MATH_FUNCTION_INT_DIV_QUOTIENT;
                   return INTDIV;
                }
intdivmod       {
                   yylval->integer =
                      BRW_MATH_FUNCTION_INT_DIV_QUOTIENT_AND_REMAINDER;
                   return INTDIVMOD;
                }
intmod          {
                   yylval->integer = BRW_MATH_FUNCTION_INT_DIV_REMAINDER;
                   return INTMOD;
                }
log             { yylval->integer = BRW_MATH_FUNCTION_LOG; return LOG; }
pow             { yylval->integer = BRW_MATH_FUNCTION_POW; return POW; }
rsq             { yylval->integer = BRW_MATH_FUNCTION_RSQ; return RSQ; }
rsqrtm          { yylval->integer = GFX8_MATH_FUNCTION_RSQRTM; return RSQRTM; }
sin             { yylval->integer = BRW_MATH_FUNCTION_SIN; return SIN; }
sqrt            { yylval->integer = BRW_MATH_FUNCTION_SQRT; return SQRT; }

 /* sync instruction */
allrd           { yylval->integer = TGL_SYNC_ALLRD; return ALLRD; }
allwr           { yylval->integer = TGL_SYNC_ALLWR; return ALLWR; }
fence           { yylval->integer = TGL_SYNC_FENCE; return FENCE; }
bar             { yylval->integer = TGL_SYNC_BAR; return BAR; }
host            { yylval->integer = TGL_SYNC_HOST; return HOST; }

 /* shared functions for send instruction */
gateway                 { return GATEWAY; }
hdc0                    { return HDC0; }
hdc1                    { return HDC1; }
hdc2                    { return HDC2; }
"hdc:ro"                { return HDC_RO; }
pi                      { return PIXEL_INTERP; }
render                  { return RENDER; }
"rt accel"              { return RT_ACCEL; }
sampler                 { return SAMPLER; }
"ts/btd"                { return TS_BTD; }
urb                     { return URB; }
slm                     { return SLM; }
tgm                     { return TGM; }
ugm                     { return UGM; }

";"     { return SEMICOLON; }
":"     { return COLON; }
"("     { return LPAREN; }
")"     { return RPAREN; }
"{"     { return LCURLY; }
"}"     { return RCURLY; }
"["     { return LSQUARE; }
"]"     { return RSQUARE; }
"<"     { return LANGLE; }
">"     { return RANGLE; }
","     { return COMMA; }
"."     { return DOT; }
"+"     { return PLUS; }
"-"     { return MINUS; }
"~"     { return MINUS; }
"(abs)" { return ABS; }


"VxH"                   { return VxH; }
<REG>"<"                { return LANGLE; }
<REG>[0-9][0-9]*        {
                           yylval->integer = strtoul(yytext, NULL, 10);
                           return INTEGER;
                        }
<REG>">"                { return RANGLE; }
<REG>","                { return COMMA; }
<REG>"."                { BEGIN(DOTSEL); return DOT; }
<REG>";"                { return SEMICOLON; }

<DOTSEL>"x"             { yylval->integer = BRW_CHANNEL_X; return X; }
<DOTSEL>"y"             { yylval->integer = BRW_CHANNEL_Y; return Y; }
<DOTSEL>"z"             { yylval->integer = BRW_CHANNEL_Z; return Z; }
<DOTSEL>"w"             { yylval->integer = BRW_CHANNEL_W; return W; }
<DOTSEL>[0-9][0-9]*     {
                           yylval->integer = strtoul(yytext, NULL, 10);
                           BEGIN(REG);
                           return INTEGER;
                        }
<DOTSEL>.               { yyless(0); BEGIN(INITIAL); }
<REG>.                  { yyless(0); BEGIN(INITIAL); }

 /* Access mode */
"align1"        { return ALIGN1; }
"align16"       { return ALIGN16; }

 /* Accumulator write control */
AccWrEnable     { return ACCWREN; }

 /* Mask control (formerly WECtrl/Write Enable Control) */
"WE_all"        { return WECTRL; }

 /* Compaction control */
compacted       { return CMPTCTRL; }

 /* Debug control */
breakpoint      { return BREAKPOINT; }

 /* Dependency control */
NoDDClr         { return NODDCLR; }
NoDDChk         { return NODDCHK; }

 /* End of thread */
EOT             { return EOT; }

 /* Mask control */
nomask          { return MASK_DISABLE; }

 /* Channel */
<CHANNEL>"x"            { yylval->integer = BRW_CHANNEL_X; return X; }
<CHANNEL>"y"            { yylval->integer = BRW_CHANNEL_Y; return Y; }
<CHANNEL>"z"            { yylval->integer = BRW_CHANNEL_Z; return Z; }
<CHANNEL>"w"            { yylval->integer = BRW_CHANNEL_W; return W; }
<CHANNEL>[0-9][0-9]*    {
                           yylval->integer = strtoul(yytext, NULL, 10);
                           return INTEGER;
                        }
<CHANNEL>"."            { return DOT; }
<CHANNEL>.              { yyless(0); BEGIN(INITIAL); }


 /* Predicate Control */
<CHANNEL>".anyv"        { yylval->integer = BRW_PREDICATE_ALIGN1_ANYV; return ANYV; }
<CHANNEL>".allv"        { yylval->integer = BRW_PREDICATE_ALIGN1_ALLV; return ALLV; }
<CHANNEL>".any2h"       { yylval->integer = BRW_PREDICATE_ALIGN1_ANY2H; return ANY2H; }
<CHANNEL>".all2h"       { yylval->integer = BRW_PREDICATE_ALIGN1_ALL2H; return ALL2H; }
<CHANNEL>".any4h"       { yylval->integer = BRW_PREDICATE_ALIGN16_ANY4H; return ANY4H; }
<CHANNEL>".all4h"       { yylval->integer = BRW_PREDICATE_ALIGN16_ALL4H; return ALL4H; }
<CHANNEL>".any8h"       { yylval->integer = BRW_PREDICATE_ALIGN1_ANY8H; return ANY8H; }
<CHANNEL>".all8h"       { yylval->integer = BRW_PREDICATE_ALIGN1_ALL8H; return ALL8H; }
<CHANNEL>".any16h"      { yylval->integer = BRW_PREDICATE_ALIGN1_ANY16H; return ANY16H; }
<CHANNEL>".all16h"      { yylval->integer = BRW_PREDICATE_ALIGN1_ALL16H; return ALL16H; }
<CHANNEL>".any32h"      { yylval->integer = BRW_PREDICATE_ALIGN1_ANY32H; return ANY32H; }
<CHANNEL>".all32h"      { yylval->integer = BRW_PREDICATE_ALIGN1_ALL32H; return ALL32H; }

 /* Saturation */
".sat"          { return SATURATE; }

 /* Thread control */
atomic          { return ATOMIC; }
switch          { return SWITCH; }

 /* Branch control */
BranchCtrl      { return BRANCH_CTRL; }

 /* Quarter Control */
1[HNQ]          { }
"2Q"            { return QTR_2Q; }
"3Q"            { return QTR_3Q; }
"4Q"            { return QTR_4Q; }
"2H"            { return QTR_2H; }
"2N"            { return QTR_2N; }
"3N"            { return QTR_3N; }
"4N"            { return QTR_4N; }
"5N"            { return QTR_5N; }
"6N"            { return QTR_6N; }
"7N"            { return QTR_7N; }
"8N"            { return QTR_8N; }

 /* data types */
:?B     { return TYPE_B; }
:?BF    { return TYPE_BF; }
:?BF8   { return TYPE_BF8; }
:?D     { return TYPE_D; }
:?DF    { return TYPE_DF; }
:?F     { return TYPE_F; }
:?HF    { return TYPE_HF; }
:?HF8   { return TYPE_HF8; }
:?Q     { return TYPE_Q; }
:?UB    { return TYPE_UB; }
:?UD    { return TYPE_UD; }
:?UW    { return TYPE_UW; }
:?UQ    { return TYPE_UQ; }
:?UV    { return TYPE_UV; }
:?V     { return TYPE_V; }
:?VF    { return TYPE_VF; }
:?W     { return TYPE_W; }

 /* Address registers */
"a0"            { return ADDRREG; }

 /* accumulator registers */
"acc"[0-9]+     { yylval->integer = atoi(yytext + 3); return ACCREG; }

 /* channel enable registers */
"ce0"           { return CHANNELENABLEREG; }

 /* control registers */
"cr0"           { return CONTROLREG; }

 /* flag registers */
"f"[0|1]        { BEGIN(CHANNEL); yylval->integer = atoi(yytext + 1); return FLAGREG; }

 /* scalar register */
"s0"            { return SCALARREG; }

 /* state register */
sr[0-9]+        { yylval->integer = atoi(yytext + 2); return STATEREG; }

 /* notification registers */
"n0"            { BEGIN(REG); return NOTIFYREG; }

 /* IP register */
"ip"            { return IPREG; }

 /* Thread control register */
"tdr0"          { return THREADREG; }

 /* performance register */
"tm0"           { BEGIN(REG); return PERFORMANCEREG; }

[gr][0-9]+      {
                   yylval->integer = atoi(yytext + 1);
                   BEGIN(REG); return GENREG;
                }
[gr]            { return GENREGFILE; }
"mask"[0-9]+    { yylval->integer = atoi(yytext + 4); return MASKREG; }

 /* Conditional modifiers */
".e"    { yylval->integer = BRW_CONDITIONAL_Z; return EQUAL; }
".g"    { yylval->integer = BRW_CONDITIONAL_G; return GREATER; }
".ge"   { yylval->integer = BRW_CONDITIONAL_GE; return GREATER_EQUAL; }
".l"    { yylval->integer = BRW_CONDITIONAL_L; return LESS; }
".le"   { yylval->integer = BRW_CONDITIONAL_LE; return LESS_EQUAL; }
".ne"   { yylval->integer = BRW_CONDITIONAL_NZ; return NOT_EQUAL; }
".nz"   { yylval->integer = BRW_CONDITIONAL_NZ; return NOT_ZERO; }
".o"    { yylval->integer = BRW_CONDITIONAL_O; return OVERFLOW; }
".r"    { yylval->integer = BRW_CONDITIONAL_R; return ROUND_INCREMENT; }
".u"    { yylval->integer = BRW_CONDITIONAL_U; return UNORDERED; }
".z"    { yylval->integer = BRW_CONDITIONAL_Z; return ZERO; }

"JIP: "         { BEGIN(LABEL); return JIP; }
"UIP: "         { BEGIN(LABEL); return UIP; }
[ \t]+          { }

"MsgDesc: "             { BEGIN(MSGDESC); return MSGDESC_BEGIN; }
<MSGDESC>ex_bso         { return EX_BSO; }
<MSGDESC>src1_len       { return SRC1_LEN; }
<MSGDESC>"="            { return ASSIGN; }
<MSGDESC>[0-9][0-9]*    {
                           yylval->integer = strtoul(yytext, NULL, 10);
                           return INTEGER;
                        }
<MSGDESC>"{"            { yyless(0); BEGIN(INITIAL); return MSGDESC_END; }
<MSGDESC>.              { }
<MSGDESC>\n             { yycolumn = 1; }

"0x"[0-9a-f][0-9a-f]*   {
                           yylval->llint = strtoull(yytext + 2, NULL, 16);
                           return LONG;
                        }
[0-9][0-9]*             {
                           yylval->llint = strtoll(yytext, NULL, 10);
                           return LONG;
                        }

 /* jump label target */
[a-zA-Z_][0-9a-zA-Z_]*":" {
        yylval->string = ralloc_strdup(parser->mem_ctx, yytext);
        /* Stomp the trailing ':' */
        yylval->string[yyleng - 1] = '\0';
        return JUMP_LABEL_TARGET;
}

 /* jump label */
<LABEL>[a-zA-Z_][0-9a-zA-Z_]* {
        yylval->string = ralloc_strdup(parser->mem_ctx, yytext);
        BEGIN(INITIAL);
        return JUMP_LABEL;
}

 /* SWSB */
"@"[1-7]        { yylval->integer = atoi(yytext + 1); return REG_DIST_CURRENT; }
"F@"[1-7]       { yylval->integer = atoi(yytext + 2); return REG_DIST_FLOAT; }
"I@"[1-7]       { yylval->integer = atoi(yytext + 2); return REG_DIST_INT; }
"L@"[1-7]       { yylval->integer = atoi(yytext + 2); return REG_DIST_LONG; }
"A@"[1-7]       { yylval->integer = atoi(yytext + 2); return REG_DIST_ALL; }
"M@"[1-7]       { yylval->integer = atoi(yytext + 2); return REG_DIST_MATH; }
"S@"[1-7]       { yylval->integer = atoi(yytext + 2); return REG_DIST_SCALAR; }

"$"[0-9]*       { yylval->integer = atoi(yytext + 1); return SBID_ALLOC; }
"$"[0-9]*".src" { yylval->integer = atoi(yytext + 1); return SBID_WAIT_SRC; }
"$"[0-9]*".dst" { yylval->integer = atoi(yytext + 1); return SBID_WAIT_DST; }

 /* DPAS params. */
"."[1-9][0-9]*x[1-9][0-9]* {
        yylval->dpas_params.sdepth = atoi(yytext + 1);
        yylval->dpas_params.rcount = atoi(strchr(yytext, 'x') + 1);
        return DPAS_PARAMS;
}

\n      { yycolumn = 1; }

.       {
           fprintf(stderr, "%s: %d: %s: at \"%s\"\n",
                   parser->input_filename, brw_asm_get_lineno(yyscanner),
                   "unexpected token", brw_asm_get_text(yyscanner));
        }
%%
