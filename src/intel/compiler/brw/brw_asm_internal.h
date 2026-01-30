/*
 * Copyright © 2018 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#pragma once

/* Assembler internal state and definitions used by the brw_gram/brw_lex. */

#include <assert.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>

#include "brw_eu_defines.h"
#include "brw_reg_type.h"
#include "dev/intel_device_info.h"
#include "intel/compiler/gen/gen.h"
#include "util/hash_table.h"
#include "util/macros.h"
#include "util/u_dynarray.h"

/* glibc < 2.27 defines OVERFLOW in /usr/include/math.h. */
#undef OVERFLOW

static inline unsigned
reg_unit(const struct intel_device_info *devinfo)
{
   return devinfo->ver >= 20 ? 2 : 1;
}

#ifndef YY_TYPEDEF_YY_SCANNER_T
#define YY_TYPEDEF_YY_SCANNER_T
typedef void *yyscan_t;
#endif

typedef struct brw_asm_parser {
   const struct intel_device_info *devinfo;
   void *mem_ctx;
   const char *input_filename;
   int errors;
   bool compaction_warning_given;
   struct hash_table *labels;

   struct util_dynarray insts;

   /* Lexer state. */
   yyscan_t scanner;
   int saved_state;
} brw_asm_parser;

int yyparse(struct brw_asm_parser *parser);
char *brw_asm_get_text(yyscan_t scanner);

int brw_asm_lex_init_extra(struct brw_asm_parser *parser, yyscan_t *scanner);
int brw_asm_lex_destroy(yyscan_t scanner);
void brw_asm_restart(FILE *input_file, yyscan_t scanner);

struct condition {
   unsigned cond_modifier:4;
   unsigned flag_reg_nr:1;
   unsigned flag_subreg_nr:1;
};

struct predicate {
   unsigned pred_control:4;
   unsigned pred_inv:1;
   unsigned flag_reg_nr:1;
   unsigned flag_subreg_nr:1;
};

enum instoption_type {
   INSTOPTION_FLAG,
   INSTOPTION_DEP_INFO,
   INSTOPTION_CHAN_OFFSET,
};

struct instoption {
   enum instoption_type type;
   union {
      unsigned uint_value;
      struct tgl_swsb depinfo_value;
   };
};

struct options {
   uint8_t chan_offset;
   unsigned access_mode:1;
   unsigned compression_control:2;
   unsigned thread_control:2;
   unsigned branch_control:1;
   unsigned no_dd_check:1; // Dependency control
   unsigned no_dd_clear:1; // Dependency control
   unsigned mask_control:1;
   unsigned debug_control:1;
   unsigned acc_wr_control:1;
   unsigned end_of_thread:1;
   unsigned compaction:1;
   unsigned is_compr:1;
   struct tgl_swsb depinfo;
};

struct msgdesc {
   unsigned ex_bso:1;
   unsigned src1_len:5;
};

unsigned gen_asm_inst_count(const struct brw_asm_parser *parser);

gen_inst *gen_asm_next_inst(struct brw_asm_parser *parser, gen_opcode opcode);
void i965_asm_set_instruction_options(struct brw_asm_parser *parser, gen_inst *inst,
                                      const struct predicate *pred,
                                      const struct condition *cond,
                                      const struct options *options);


void brw_asm_label_set(struct brw_asm_parser *parser, const char *name);
void brw_asm_label_use_jip(struct brw_asm_parser *parser, const char *name);
void brw_asm_label_use_uip(struct brw_asm_parser *parser, const char *name);
