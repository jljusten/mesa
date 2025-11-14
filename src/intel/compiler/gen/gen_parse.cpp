/*
 * Copyright © 2026 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include <ctype.h>
#include <stdarg.h>
#include <string.h>

#include <charconv>
#include <limits>
#include <map>
#include <string_view>
#include <vector>

#include "util/ralloc.h"

#include "gen_private.h"

namespace {

#define SV_FMT(s) static_cast<int>((s).size()), (s).data()

enum {
   GEN_CHANNEL_X = 0,
   GEN_CHANNEL_Y = 1,
   GEN_CHANNEL_Z = 2,
   GEN_CHANNEL_W = 3,
};

static inline uint32_t
gen_message_desc(const intel_device_info *devinfo,
                 unsigned msg_length,
                 unsigned response_length,
                 bool header_present)
{
   (void)devinfo;
   return SET_BITS(msg_length, 28, 25) |
          SET_BITS(response_length, 24, 20) |
          SET_BITS(header_present, 19, 19);
}

static inline unsigned
gen_lsc_msg_dest_len(const struct intel_device_info *devinfo,
                     enum lsc_data_size data_sz,
                     unsigned n)
{
   return DIV_ROUND_UP(lsc_data_size_bytes(data_sz) * n,
                       devinfo->grf_size);
}

static inline unsigned
gen_lsc_msg_addr_len(const struct intel_device_info *devinfo,
                     enum lsc_addr_size addr_sz,
                     unsigned n)
{
   return DIV_ROUND_UP(lsc_addr_size_bytes(addr_sz) * n,
                       devinfo->grf_size);
}

static inline uint32_t
gen_lsc_bti_ex_desc(const struct intel_device_info *devinfo,
                    unsigned index,
                    unsigned base_offset)
{
   gen_lsc_ex_desc ex_desc = {};
   ex_desc.addr_type = LSC_ADDR_SURFTYPE_BTI;
   ex_desc.bti.index = index;
   ex_desc.bti.base_offset = base_offset;
   return gen_lsc_ex_desc_encode(devinfo, &ex_desc);
}

constexpr unsigned DEFAULT_WRITEMASK_XYZW = 0xF;
constexpr unsigned SWIZZLE_XYZW =
   (GEN_CHANNEL_X << 0) |
   (GEN_CHANNEL_Y << 2) |
   (GEN_CHANNEL_Z << 4) |
   (GEN_CHANNEL_W << 6);

static bool
is_ident_start(char c)
{
   return isalpha((unsigned char)c) || c == '_';
}

static bool
is_ident_char(char c)
{
   return isalnum((unsigned char)c) || c == '_';
}

static bool
is_opcode_char(char c)
{
   return isalnum((unsigned char)c) || c == '_' || c == '.' || c == '/';
}

static bool
is_lsc_symbolic_opcode_char(char c)
{
   return is_opcode_char(c) || c == '[' || c == ']';
}

static unsigned
brw_implied_width_for_3src_a1(unsigned v, unsigned h)
{
   if (v == 0)
      return 1;
   if (h == 0)
      return v;
   return v / h;
}

static bool
view_starts_with(std::string_view s, std::string_view prefix)
{
   return s.size() >= prefix.size() && s.substr(0, prefix.size()) == prefix;
}

static bool
split_integer_token(std::string_view token,
                    bool &negative,
                    int &base,
                    std::string_view &digits)
{
   if (token.empty())
      return false;

   negative = false;
   if (token.front() == '+' || token.front() == '-') {
      negative = token.front() == '-';
      token.remove_prefix(1);
   }

   base = 10;
   if (token.size() >= 2 && token[0] == '0' &&
       (token[1] == 'x' || token[1] == 'X')) {
      base = 16;
      token.remove_prefix(2);
   }

   if (token.empty())
      return false;

   digits = token;
   return true;
}

static bool
parse_unsigned_token(std::string_view token, unsigned long long &value)
{
   bool negative = false;
   int base = 10;
   std::string_view digits;
   if (!split_integer_token(token, negative, base, digits))
      return false;

   unsigned long long magnitude = 0;
   const auto result = std::from_chars(digits.data(),
                                       digits.data() + digits.size(),
                                       magnitude, base);
   if (result.ec != std::errc() || result.ptr != digits.data() + digits.size())
      return false;

   value = negative ? 0ull - magnitude : magnitude;
   return true;
}

static bool
parse_signed_token(std::string_view token, long long &value)
{
   bool negative = false;
   int base = 10;
   std::string_view digits;
   if (!split_integer_token(token, negative, base, digits))
      return false;

   unsigned long long magnitude = 0;
   const auto result = std::from_chars(digits.data(),
                                       digits.data() + digits.size(),
                                       magnitude, base);
   if (result.ec != std::errc() || result.ptr != digits.data() + digits.size())
      return false;

   if (negative) {
      const unsigned long long min_magnitude =
         static_cast<unsigned long long>(std::numeric_limits<long long>::max()) + 1ull;
      if (magnitude > min_magnitude)
         return false;

      value = magnitude == min_magnitude ? std::numeric_limits<long long>::min()
                                         : -static_cast<long long>(magnitude);
   } else {
      if (magnitude > static_cast<unsigned long long>(std::numeric_limits<long long>::max()))
         return false;

      value = static_cast<long long>(magnitude);
   }

   return true;
}

static gen_opcode
opcode_from_name(std::string_view name)
{
   for (int op = GEN_OP_ILLEGAL; op <= GEN_OP_XOR; op++) {
      if (name == gen_opcode_to_string((gen_opcode)op))
         return (gen_opcode)op;
   }

   return GEN_OP_ILLEGAL;
}

static gen_reg_type
type_from_name(std::string_view name)
{
   if (name == "ub") return GEN_TYPE_UB;
   if (name == "b")  return GEN_TYPE_B;
   if (name == "uw") return GEN_TYPE_UW;
   if (name == "w")  return GEN_TYPE_W;
   if (name == "ud") return GEN_TYPE_UD;
   if (name == "d")  return GEN_TYPE_D;
   if (name == "uq") return GEN_TYPE_UQ;
   if (name == "q")  return GEN_TYPE_Q;
   if (name == "hf") return GEN_TYPE_HF;
   if (name == "bf") return GEN_TYPE_BF;
   if (name == "f")  return GEN_TYPE_F;
   if (name == "df") return GEN_TYPE_DF;
   if (name == "v")  return GEN_TYPE_V;
   if (name == "vf") return GEN_TYPE_VF;
   if (name == "uv") return GEN_TYPE_UV;
   return GEN_TYPE_INVALID;
}

static gen_condition
cond_modifier_from_name(std::string_view name)
{
   if (name == "eq") return GEN_CONDITION_Z;
   if (name == "ne") return GEN_CONDITION_NZ;
   if (name == "g" || name == "gt")  return GEN_CONDITION_G;
   if (name == "ge") return GEN_CONDITION_GE;
   if (name == "l" || name == "lt")  return GEN_CONDITION_L;
   if (name == "le") return GEN_CONDITION_LE;
   if (name == "r")  return (gen_condition)7;
   if (name == "o")  return GEN_CONDITION_O;
   if (name == "u")  return GEN_CONDITION_U;
   return GEN_CONDITION_NONE;
}

static int
math_function_from_name(std::string_view name)
{
   if (name == "inv")       return GEN_MATH_INV;
   if (name == "log")       return GEN_MATH_LOG;
   if (name == "exp")       return GEN_MATH_EXP;
   if (name == "sqrt" || name == "sqt")
      return GEN_MATH_SQRT;
   if (name == "rsq")       return GEN_MATH_RSQ;
   if (name == "sin")       return GEN_MATH_SIN;
   if (name == "cos")       return GEN_MATH_COS;
   if (name == "fdiv")      return GEN_MATH_FDIV;
   if (name == "pow")       return GEN_MATH_POW;
   if (name == "intdiv_qr") return GEN_MATH_INT_DIV_BOTH;
   if (name == "intdiv_q")  return GEN_MATH_INT_DIV_QUOTIENT;
   if (name == "intdiv_r")  return GEN_MATH_INT_DIV_REMAINDER;
   if (name == "invm")      return GEN_MATH_INVM;
   if (name == "rsqrtm")    return GEN_MATH_RSQRTM;
   return -1;
}

static int
sync_function_from_name(std::string_view name)
{
   if (name == "nop")   return GEN_SYNC_NOP;
   if (name == "allrd") return GEN_SYNC_ALLRD;
   if (name == "allwr") return GEN_SYNC_ALLWR;
   if (name == "fence") return GEN_SYNC_FENCE;
   if (name == "bar")   return GEN_SYNC_BAR;
   if (name == "host")  return GEN_SYNC_HOST;
   return -1;
}

static int
sfid_from_name(const intel_device_info *devinfo, std::string_view name)
{
   if (name == "null")    return GEN_SFID_NULL;
   if (name == "sampler") return GEN_SFID_SAMPLER;
   if (name == "gtwy")
      return GEN_SFID_MESSAGE_GATEWAY;
   if (name == "hdc2")    return GEN_SFID_HDC2;
   if (name == "render")  return GEN_SFID_RENDER_CACHE;
   if (name == "urb")     return GEN_SFID_URB;
   if (name == "ts")
      return devinfo->verx10 <= 120 ? GEN_SFID_THREAD_SPAWNER : -1;
   if (name == "btd")
      return devinfo->verx10 >= 125 ? GEN_SFID_BINDLESS_THREAD_DISPATCH : -1;
   if (name == "rtaccel") return GEN_SFID_RAY_TRACE_ACCELERATOR;
   if (name == "hdc_ro")  return GEN_SFID_HDC_READ_ONLY;
   if (name == "hdc0")    return GEN_SFID_HDC0;
   if (name == "pi")      return GEN_SFID_PIXEL_INTERPOLATOR;
   if (name == "hdc1")    return GEN_SFID_HDC1;
   if (name == "slm")     return GEN_SFID_SLM;
   if (name == "tgm")     return GEN_SFID_TGM;
   if (name == "ugm")     return GEN_SFID_UGM;
   return -1;
}

struct lsc_symbolic_parse_info {
   enum lsc_opcode op = LSC_OP_LOAD;
   enum lsc_addr_surface_type addr_type = LSC_ADDR_SURFTYPE_FLAT;
   enum lsc_addr_size addr_size = LSC_ADDR_SIZE_A32;
   enum lsc_data_size data_size = LSC_DATA_SIZE_D32;
   enum lsc_cmask cmask = LSC_CMASK_X;
   enum lsc_fence_scope fence_scope = LSC_FENCE_LOCAL;
   enum lsc_flush_type flush_type = LSC_FLUSH_TYPE_NONE;

   unsigned sfid = 0;
   unsigned num_values = 1;
   unsigned cache_ctrl = 0;
   bool transpose = false;
   bool route_to_lsc = false;

   bool ex_desc_is_reg = false;
   uint8_t ex_desc_subnr = 0;
   uint32_t ex_desc_imm = 0;
};

static bool
lsc_opcode_is_symbolic_name(std::string_view name)
{
   return name == "load" ||
          name == "load_cmask" ||
          name == "store" ||
          name == "store_cmask" ||
          name == "load_cmask_msrt" ||
          name == "store_cmask_msrt" ||
          name == "fence" ||
          name == "atomic_inc" ||
          name == "atomic_dec" ||
          name == "atomic_load" ||
          name == "atomic_store" ||
          name == "atomic_add" ||
          name == "atomic_sub" ||
          name == "atomic_min" ||
          name == "atomic_max" ||
          name == "atomic_umin" ||
          name == "atomic_umax" ||
          name == "atomic_cmpxchg" ||
          name == "atomic_fadd" ||
          name == "atomic_fsub" ||
          name == "atomic_fmin" ||
          name == "atomic_fmax" ||
          name == "atomic_fcmpxchg" ||
          name == "atomic_and" ||
          name == "atomic_or" ||
          name == "atomic_xor";
}

static enum lsc_opcode
lsc_opcode_from_name(std::string_view name)
{
   if (name == "load")             return LSC_OP_LOAD;
   if (name == "load_cmask")       return LSC_OP_LOAD_CMASK;
   if (name == "store")            return LSC_OP_STORE;
   if (name == "store_cmask")      return LSC_OP_STORE_CMASK;
   if (name == "load_cmask_msrt")  return LSC_OP_LOAD_CMASK_MSRT;
   if (name == "store_cmask_msrt") return LSC_OP_STORE_CMASK_MSRT;
   if (name == "fence")            return LSC_OP_FENCE;
   if (name == "atomic_inc")       return LSC_OP_ATOMIC_INC;
   if (name == "atomic_dec")       return LSC_OP_ATOMIC_DEC;
   if (name == "atomic_load")      return LSC_OP_ATOMIC_LOAD;
   if (name == "atomic_store")     return LSC_OP_ATOMIC_STORE;
   if (name == "atomic_add")       return LSC_OP_ATOMIC_ADD;
   if (name == "atomic_sub")       return LSC_OP_ATOMIC_SUB;
   if (name == "atomic_min")       return LSC_OP_ATOMIC_MIN;
   if (name == "atomic_max")       return LSC_OP_ATOMIC_MAX;
   if (name == "atomic_umin")      return LSC_OP_ATOMIC_UMIN;
   if (name == "atomic_umax")      return LSC_OP_ATOMIC_UMAX;
   if (name == "atomic_cmpxchg")   return LSC_OP_ATOMIC_CMPXCHG;
   if (name == "atomic_fadd")      return LSC_OP_ATOMIC_FADD;
   if (name == "atomic_fsub")      return LSC_OP_ATOMIC_FSUB;
   if (name == "atomic_fmin")      return LSC_OP_ATOMIC_FMIN;
   if (name == "atomic_fmax")      return LSC_OP_ATOMIC_FMAX;
   if (name == "atomic_fcmpxchg")  return LSC_OP_ATOMIC_FCMPXCHG;
   if (name == "atomic_and")       return LSC_OP_ATOMIC_AND;
   if (name == "atomic_or")        return LSC_OP_ATOMIC_OR;
   if (name == "atomic_xor")       return LSC_OP_ATOMIC_XOR;
   return (enum lsc_opcode)-1;
}

static enum lsc_data_size
lsc_data_size_from_name(std::string_view name)
{
   if (name == "d8")      return LSC_DATA_SIZE_D8;
   if (name == "d16")     return LSC_DATA_SIZE_D16;
   if (name == "d32")     return LSC_DATA_SIZE_D32;
   if (name == "d64")     return LSC_DATA_SIZE_D64;
   if (name == "d8u32")   return LSC_DATA_SIZE_D8U32;
   if (name == "d16u32")  return LSC_DATA_SIZE_D16U32;
   if (name == "d16bf32") return LSC_DATA_SIZE_D16BF32;
   return (enum lsc_data_size)-1;
}

static enum lsc_addr_size
lsc_addr_size_from_name(std::string_view name)
{
   if (name == "a16") return LSC_ADDR_SIZE_A16;
   if (name == "a32") return LSC_ADDR_SIZE_A32;
   if (name == "a64") return LSC_ADDR_SIZE_A64;
   return (enum lsc_addr_size)-1;
}

static enum lsc_cmask
lsc_cmask_from_name(std::string_view name)
{
   if (name == "x")    return LSC_CMASK_X;
   if (name == "y")    return LSC_CMASK_Y;
   if (name == "xy")   return LSC_CMASK_XY;
   if (name == "z")    return LSC_CMASK_Z;
   if (name == "xz")   return LSC_CMASK_XZ;
   if (name == "yz")   return LSC_CMASK_YZ;
   if (name == "xyz")  return LSC_CMASK_XYZ;
   if (name == "w")    return LSC_CMASK_W;
   if (name == "xw")   return LSC_CMASK_XW;
   if (name == "yw")   return LSC_CMASK_YW;
   if (name == "xyw")  return LSC_CMASK_XYW;
   if (name == "zw")   return LSC_CMASK_ZW;
   if (name == "xzw")  return LSC_CMASK_XZW;
   if (name == "yzw")  return LSC_CMASK_YZW;
   if (name == "xyzw") return LSC_CMASK_XYZW;
   return (enum lsc_cmask)-1;
}

static int
lsc_fence_scope_from_name(std::string_view name)
{
   if (name == "threadgroup")    return LSC_FENCE_THREADGROUP;
   if (name == "local")          return LSC_FENCE_LOCAL;
   if (name == "tile")           return LSC_FENCE_TILE;
   if (name == "gpu")            return LSC_FENCE_GPU;
   if (name == "all_gpu")        return LSC_FENCE_ALL_GPU;
   if (name == "system_release") return LSC_FENCE_SYSTEM_RELEASE;
   if (name == "system_acquire") return LSC_FENCE_SYSTEM_ACQUIRE;
   return -1;
}

static int
lsc_flush_type_from_name(std::string_view name)
{
   if (name == "none")       return LSC_FLUSH_TYPE_NONE;
   if (name == "evict")      return LSC_FLUSH_TYPE_EVICT;
   if (name == "invalidate") return LSC_FLUSH_TYPE_INVALIDATE;
   if (name == "discard")    return LSC_FLUSH_TYPE_DISCARD;
   if (name == "clean")      return LSC_FLUSH_TYPE_CLEAN;
   if (name == "l3only")     return LSC_FLUSH_TYPE_L3ONLY;
   if (name == "none_6")     return LSC_FLUSH_TYPE_NONE_6;
   return -1;
}

static bool
lsc_opcode_uses_load_cache(enum lsc_opcode opcode)
{
   return opcode == LSC_OP_LOAD ||
          opcode == LSC_OP_LOAD_CMASK ||
          opcode == LSC_OP_LOAD_CMASK_MSRT;
}

static bool
lsc_send_has_symbolic_src1(enum lsc_opcode op)
{
   return lsc_opcode_is_store(op) ||
          (lsc_opcode_is_atomic(op) && lsc_op_num_data_values(op) > 0);
}

static bool
lsc_cache_ctrl_from_name(const intel_device_info *devinfo,
                         enum lsc_opcode op,
                         std::string_view l1,
                         std::string_view l3,
                         unsigned &cache_ctrl)
{
   cache_ctrl = 0;
   if (l1.empty() || l3.empty())
      return true;

   if (lsc_opcode_uses_load_cache(op)) {
      if (devinfo->ver >= 20) {
         if (l1 == "uc" && l3 == "uc") { cache_ctrl = XE2_LSC_CACHE_LOAD_L1UC_L3UC; return true; }
         if (l1 == "uc" && l3 == "ca") { cache_ctrl = XE2_LSC_CACHE_LOAD_L1UC_L3C; return true; }
         if (l1 == "uc" && l3 == "cc") { cache_ctrl = XE2_LSC_CACHE_LOAD_L1UC_L3CC; return true; }
         if (l1 == "ca" && l3 == "uc") { cache_ctrl = XE2_LSC_CACHE_LOAD_L1C_L3UC; return true; }
         if (l1 == "ca" && l3 == "ca") { cache_ctrl = XE2_LSC_CACHE_LOAD_L1C_L3C; return true; }
         if (l1 == "ca" && l3 == "cc") { cache_ctrl = XE2_LSC_CACHE_LOAD_L1C_L3CC; return true; }
         if (l1 == "st" && l3 == "uc") { cache_ctrl = XE2_LSC_CACHE_LOAD_L1S_L3UC; return true; }
         if (l1 == "st" && l3 == "ca") { cache_ctrl = XE2_LSC_CACHE_LOAD_L1S_L3C; return true; }
         if (l1 == "ri" && l3 == "ri") { cache_ctrl = XE2_LSC_CACHE_LOAD_L1IAR_L3IAR; return true; }
         return false;
      }

      if (l1 == "uc" && l3 == "uc") { cache_ctrl = LSC_CACHE_LOAD_L1UC_L3UC; return true; }
      if (l1 == "uc" && l3 == "ca") { cache_ctrl = LSC_CACHE_LOAD_L1UC_L3C; return true; }
      if (l1 == "ca" && l3 == "uc") { cache_ctrl = LSC_CACHE_LOAD_L1C_L3UC; return true; }
      if (l1 == "ca" && l3 == "ca") { cache_ctrl = LSC_CACHE_LOAD_L1C_L3C; return true; }
      if (l1 == "st" && l3 == "uc") { cache_ctrl = LSC_CACHE_LOAD_L1S_L3UC; return true; }
      if (l1 == "st" && l3 == "ca") { cache_ctrl = LSC_CACHE_LOAD_L1S_L3C; return true; }
      if (l1 == "ri" && l3 == "ca") { cache_ctrl = LSC_CACHE_LOAD_L1IAR_L3C; return true; }
      return false;
   }

   if (devinfo->ver >= 20) {
      if (l1 == "uc" && l3 == "uc") { cache_ctrl = XE2_LSC_CACHE_STORE_L1UC_L3UC; return true; }
      if (l1 == "uc" && l3 == "wb") { cache_ctrl = XE2_LSC_CACHE_STORE_L1UC_L3WB; return true; }
      if (l1 == "wt" && l3 == "uc") { cache_ctrl = XE2_LSC_CACHE_STORE_L1WT_L3UC; return true; }
      if (l1 == "wt" && l3 == "wb") { cache_ctrl = XE2_LSC_CACHE_STORE_L1WT_L3WB; return true; }
      if (l1 == "st" && l3 == "uc") { cache_ctrl = XE2_LSC_CACHE_STORE_L1S_L3UC; return true; }
      if (l1 == "st" && l3 == "wb") { cache_ctrl = XE2_LSC_CACHE_STORE_L1S_L3WB; return true; }
      if (l1 == "wb" && l3 == "wb") { cache_ctrl = XE2_LSC_CACHE_STORE_L1WB_L3WB; return true; }
      return false;
   }

   if (l1 == "uc" && l3 == "uc") { cache_ctrl = LSC_CACHE_STORE_L1UC_L3UC; return true; }
   if (l1 == "uc" && l3 == "wb") { cache_ctrl = LSC_CACHE_STORE_L1UC_L3WB; return true; }
   if (l1 == "wt" && l3 == "uc") { cache_ctrl = LSC_CACHE_STORE_L1WT_L3UC; return true; }
   if (l1 == "wt" && l3 == "wb") { cache_ctrl = LSC_CACHE_STORE_L1WT_L3WB; return true; }
   if (l1 == "st" && l3 == "uc") { cache_ctrl = LSC_CACHE_STORE_L1S_L3UC; return true; }
   if (l1 == "st" && l3 == "wb") { cache_ctrl = LSC_CACHE_STORE_L1S_L3WB; return true; }
   if (l1 == "wb" && l3 == "wb") { cache_ctrl = LSC_CACHE_STORE_L1WB_L3WB; return true; }
   return false;
}

static bool
lsc_parse_data_token(std::string_view tok,
                     bool allow_vector,
                     enum lsc_data_size &data_size,
                     unsigned &num_values,
                     bool &transpose)
{
   data_size = (enum lsc_data_size)-1;
   num_values = 1;
   transpose = false;

   const size_t x = tok.find('x');
   const std::string_view data_name = x == std::string_view::npos ? tok : tok.substr(0, x);
   data_size = lsc_data_size_from_name(data_name);
   if ((int)data_size < 0)
      return false;

   if (x == std::string_view::npos)
      return true;

   if (!allow_vector)
      return false;

   std::string_view tail = tok.substr(x + 1);
   if (!tail.empty() && tail.back() == 't') {
      transpose = true;
      tail.remove_suffix(1);
   }

   if (tail.empty())
      return false;

   unsigned long long n = 0;
   const auto result = std::from_chars(tail.data(), tail.data() + tail.size(), n, 10);
   if (result.ec != std::errc() || result.ptr != tail.data() + tail.size())
      return false;

   num_values = n;
   return true;
}

static bool
split_dotted_lsc_word(std::string_view word, std::vector<std::string_view> &parts)
{
   parts.clear();
   size_t start = 0;
   int bracket_depth = 0;

   for (size_t i = 0; i < word.size(); i++) {
      if (word[i] == '[') {
         bracket_depth++;
      } else if (word[i] == ']') {
         if (bracket_depth == 0)
            return false;
         bracket_depth--;
      } else if (word[i] == '.' && bracket_depth == 0) {
         parts.push_back(word.substr(start, i - start));
         start = i + 1;
      }
   }

   if (bracket_depth != 0)
      return false;

   parts.push_back(word.substr(start));
   return !parts.empty();
}

static unsigned
lsc_symbolic_num_data_values(const lsc_symbolic_parse_info &info)
{
   if (info.op == LSC_OP_FENCE)
      return 0;

   if (lsc_opcode_has_cmask(info.op))
      return __builtin_popcount((unsigned)info.cmask);

   if (lsc_opcode_is_atomic(info.op))
      return lsc_op_num_data_values(info.op);

   return info.num_values;
}

static unsigned
lsc_symbolic_coord_components(const lsc_symbolic_parse_info &info)
{
   if (info.op == LSC_OP_FENCE)
      return 1;

   return info.sfid == GEN_SFID_TGM ? 4 : 1;
}

static unsigned
lsc_symbolic_src0_length(const intel_device_info *devinfo,
                         const lsc_symbolic_parse_info &info,
                         unsigned exec_size)
{
   if (info.op == LSC_OP_FENCE)
      return 1;

   return gen_lsc_msg_addr_len(devinfo, info.addr_size,
                           exec_size * lsc_symbolic_coord_components(info));
}

static unsigned
lsc_symbolic_src1_length(const intel_device_info *devinfo,
                         const lsc_symbolic_parse_info &info,
                         unsigned exec_size)
{
   const unsigned values = lsc_symbolic_num_data_values(info);
   if (values == 0)
      return 0;

   return gen_lsc_msg_dest_len(devinfo, info.data_size, exec_size * values);
}

static unsigned
lsc_symbolic_response_length(const intel_device_info *devinfo,
                             const lsc_symbolic_parse_info &info,
                             unsigned exec_size,
                             bool has_dst)
{
   if (!has_dst)
      return 0;

   if (info.op == LSC_OP_FENCE)
      return 1;

   if (lsc_opcode_is_store(info.op))
      return 0;

   if (lsc_opcode_is_atomic(info.op))
      return gen_lsc_msg_dest_len(devinfo, info.data_size, exec_size);

   return gen_lsc_msg_dest_len(devinfo, info.data_size,
                           exec_size * lsc_symbolic_num_data_values(info));
}

struct line_cursor {
   std::string_view line;
   size_t pos = 0;

   explicit line_cursor(std::string_view line) : line(line) {}

   bool eof() const
   {
      return pos >= line.size();
   }

   char peek() const
   {
      return eof() ? '\0' : line[pos];
   }

   void skip_ws()
   {
      while (!eof() && isspace((unsigned char)line[pos]))
         pos++;
   }

   bool consume(char c)
   {
      if (peek() != c)
         return false;
      pos++;
      return true;
   }

   bool consume(const char *s)
   {
      const size_t len = strlen(s);
      if (line.compare(pos, len, s, len) != 0)
         return false;
      pos += len;
      return true;
   }

   bool next_is(char c) const
   {
      return peek() == c;
   }

   bool next_is_digit() const
   {
      return isdigit((unsigned char)peek());
   }

   template <typename Pred>
   std::string_view take_while(Pred pred)
   {
      const size_t start = pos;
      while (!eof() && pred(line[pos]))
         pos++;
      return line.substr(start, pos - start);
   }

   std::string_view take_until_space_or(char terminal)
   {
      const size_t start = pos;
      while (!eof() && !isspace((unsigned char)line[pos]) && line[pos] != terminal)
         pos++;
      return line.substr(start, pos - start);
   }

   bool take_uint(unsigned &value)
   {
      if (!next_is_digit())
         return false;

      unsigned long long v = 0;
      const char *begin = line.data() + pos;
      const char *end = line.data() + line.size();
      const auto result = std::from_chars(begin, end, v, 10);
      if (result.ptr == begin || result.ec != std::errc())
         return false;

      pos = result.ptr - line.data();
      value = v;
      return true;
   }

   bool take_number_token(std::string_view &tok)
   {
      const size_t start = pos;

      if (!eof() && (line[pos] == '+' || line[pos] == '-'))
         pos++;

      if (pos + 2 <= line.size() && line[pos] == '0' &&
          (line[pos + 1] == 'x' || line[pos + 1] == 'X')) {
         pos += 2;
         const size_t hex_start = pos;
         while (!eof() && isxdigit((unsigned char)line[pos]))
            pos++;
         if (pos == hex_start) {
            pos = start;
            return false;
         }
      } else {
         const size_t dec_start = pos;
         while (!eof() && isdigit((unsigned char)line[pos]))
            pos++;
         if (pos == dec_start) {
            pos = start;
            return false;
         }
      }

      tok = line.substr(start, pos - start);
      return true;
   }
};

struct gen_parser {
   explicit gen_parser(gen_parse_params *params)
      : params(params),
        devinfo(params->devinfo)
   {}

   bool parse()
   {
      assert(devinfo);
      assert(params->mem_ctx);
      assert(params->insts == NULL);
      assert(params->errors == NULL);
      assert(params->text || params->text_size == 0);
      assert(params->text_size >= 0);

      text = params->text ? std::string_view(params->text, params->text_size)
                          : std::string_view();

      size_t start = 0;
      unsigned line_no = 1;
      while (start <= text.size()) {
         const size_t end = text.find('\n', start);
         const size_t len = end == std::string_view::npos ? text.size() - start : end - start;

         std::string_view line = text.substr(start, len);
         if (!line.empty() && line.back() == '\r')
            line.remove_suffix(1);

         if (!parse_line(line, line_no))
            break;

         if (end == std::string_view::npos)
            break;

         start = end + 1;
         line_no++;
      }

      if (errors.empty())
         resolve_labels();

      if (errors.empty())
         emit_insts();

      emit_errors();
      return errors.empty();
   }

private:
   struct pending_label_use {
      int inst_idx;
      unsigned src_index;
      unsigned line;
      std::string_view label;
   };

   struct operand_state {
      bool has_grf_subreg = false;
      unsigned grf_subreg_index = 0;
   };

   struct inst_state {
      bool flag_ref_seen = false;
      unsigned flag_nr = 0;
      unsigned flag_subnr = 0;
      bool dst_has_writemask = false;
      bool src_has_swizzle[3] = {};
   };

   gen_parse_params *params;
   const intel_device_info *devinfo;
   std::string_view text;
   std::vector<gen_inst> insts;
   std::map<std::string_view, int> labels;
   std::vector<pending_label_use> label_uses;
   std::vector<gen_error> errors;

   void error(unsigned line, const char *msg)
   {
      errors.push_back({ line, msg });
   }

   void errorf(unsigned line, const char *fmt, ...)
   {
      va_list args;
      va_start(args, fmt);
      errors.push_back({ line, ralloc_vasprintf(params->mem_ctx, fmt, args) });
      va_end(args);
   }

   static std::string_view trim(std::string_view s)
   {
      size_t begin = 0;
      while (begin < s.size() && isspace((unsigned char)s[begin]))
         begin++;

      size_t end = s.size();
      while (end > begin && isspace((unsigned char)s[end - 1]))
         end--;

      return s.substr(begin, end - begin);
   }

   static std::string_view strip_line_comment(std::string_view s)
   {
      const size_t pos = s.find("//");
      if (pos == std::string_view::npos)
         return s;

      return s.substr(0, pos);
   }

   bool parse_line(std::string_view line, unsigned line_no)
   {
      const std::string_view t = trim(strip_line_comment(line));
      if (t.empty())
         return true;

      if (t.rfind("ERROR:", 0) == 0)
         return true;

      if (is_label_line(t, line_no))
         return errors.empty();

      parse_instruction(t, line_no);
      return errors.empty();
   }

   bool is_label_line(std::string_view line, unsigned line_no)
   {
      line_cursor c(line);
      if (!is_ident_start(c.peek()))
         return false;

      const std::string_view name = c.take_while(is_ident_char);
      if (!c.consume(':'))
         return false;

      c.skip_ws();
      if (!c.eof())
         return false;

      if (labels.find(name) != labels.end()) {
         errorf(line_no, "duplicate label '%.*s'", SV_FMT(name));
         return true;
      }

      labels[name] = insts.size();
      return true;
   }

   bool parse_instruction(std::string_view line, unsigned line_no)
   {
      line_cursor c(line);
      gen_inst inst = {};
      inst_state st;
      bool has_lsc_symbolic = false;
      lsc_symbolic_parse_info lsc = {};

      c.skip_ws();
      if (looks_like_prefix(c)) {
         if (!parse_prefix(c, inst, st, line_no))
            return false;
         c.skip_ws();
      }

      if (devinfo->has_lsc && looks_like_lsc_symbolic_opcode(c)) {
         if (!parse_lsc_symbolic_opcode(c, inst, lsc, line_no))
            return false;
         has_lsc_symbolic = true;
      } else if (!parse_opcode(c, inst, line_no)) {
         return false;
      }

      c.skip_ws();
      if (looks_like_exec_control(c)) {
         if (!parse_exec_control(c, inst, line_no))
            return false;
         c.skip_ws();
      }

      if (looks_like_cond_modifier(c)) {
         if (!parse_cond_modifier(c, inst, st, line_no))
            return false;
         c.skip_ws();
      }

      const gen_format format = gen_inst_format(inst.opcode);
      switch (format) {
      case GEN_FORMAT_BASIC_ONE_SRC:
      case GEN_FORMAT_BASIC_TWO_SRC:
      case GEN_FORMAT_DPAS_THREE_SRC: {
         const unsigned num_sources = gen_inst_num_sources(devinfo, &inst);
         const bool has_dst = gen_inst_has_dst(format, inst.opcode);

         if (has_dst) {
            if (!require_operand_separator(c, line_no))
               return false;
            if (!parse_dst(c, inst, st, line_no))
               return false;
         }

         for (unsigned i = 0; i < num_sources; i++) {
            if (!require_operand_separator(c, line_no))
               return false;
            if (!parse_src(c, inst.src[i], st.src_has_swizzle[i], false, i,
                           inst, line_no))
               return false;
         }
         break;
      }

      case GEN_FORMAT_BASIC_THREE_SRC: {
         const unsigned num_sources = gen_inst_num_sources(devinfo, &inst);
         const bool has_dst = gen_inst_has_dst(format, inst.opcode);

         if (has_dst) {
            if (!require_operand_separator(c, line_no))
               return false;
            if (!parse_dst(c, inst, st, line_no))
               return false;
         }

         for (unsigned i = 0; i < num_sources; i++) {
            if (!require_operand_separator(c, line_no))
               return false;
            if (!parse_src(c, inst.src[i], st.src_has_swizzle[i], true, i,
                           inst, line_no))
               return false;
         }
         break;
      }

      case GEN_FORMAT_SEND:
         if (!require_operand_separator(c, line_no))
            return false;
         if (has_lsc_symbolic) {
            if (!parse_lsc_symbolic_send(c, inst, lsc, line_no))
               return false;
         } else {
            if (!parse_send(c, inst, line_no))
               return false;
         }
         break;

      case GEN_FORMAT_BRANCH_ONE_SRC:
         if (!require_operand_separator(c, line_no))
            return false;
         c.skip_ws();
         c.consume("jip:");
         if (!parse_branch_operand(c, inst, 0, st, line_no))
            return false;
         break;

      case GEN_FORMAT_BRANCH_TWO_SRC:
         if (!require_operand_separator(c, line_no))
            return false;
         c.skip_ws();
         if (c.consume("jip:")) {
            if (!parse_branch_operand(c, inst, 0, st, line_no))
               return false;
            c.skip_ws();
            if (!c.consume("uip:")) {
               error(line_no, "expected 'uip:'");
               return false;
            }
            if (!parse_branch_operand(c, inst, 1, st, line_no))
               return false;
         } else {
            /* Register operand without jip:/uip: prefix. */
            if (!parse_src(c, inst.src[0], st.src_has_swizzle[0], false, 0,
                           inst, line_no))
               return false;
         }
         break;

      case GEN_FORMAT_NOP:
      case GEN_FORMAT_ILLEGAL:
         break;
      }

      c.skip_ws();
      if (c.consume('{')) {
         if (!parse_annotation(c, inst, line_no))
            return false;
         c.skip_ws();
      }

      if (!c.eof()) {
         error(line_no, "unexpected trailing text");
         return false;
      }

      apply_align16_defaults(inst, st);
      insts.push_back(inst);
      return true;
   }

   static bool looks_like_prefix(const line_cursor &c)
   {
      if (!c.next_is('(') || c.pos + 1 >= c.line.size())
         return false;

      const char next = c.line[c.pos + 1];
      return next == 'W' || next == '~' || next == 'f';
   }

   static bool looks_like_exec_control(const line_cursor &c)
   {
      return c.next_is('(') && c.pos + 1 < c.line.size() &&
             isdigit((unsigned char)c.line[c.pos + 1]);
   }

   static bool looks_like_cond_modifier(const line_cursor &c)
   {
      return c.next_is('(') && c.pos + 1 < c.line.size() &&
             isalpha((unsigned char)c.line[c.pos + 1]);
   }

   static bool looks_like_lsc_symbolic_opcode(const line_cursor &c)
   {
      const size_t start = c.pos;
      size_t end = start;
      while (end < c.line.size() && is_lsc_symbolic_opcode_char(c.line[end]))
         end++;

      std::vector<std::string_view> parts;
      if (!split_dotted_lsc_word(c.line.substr(start, end - start), parts) ||
          parts.empty())
         return false;

      return lsc_opcode_is_symbolic_name(parts[0]);
   }

   bool parse_lsc_symbolic_opcode(line_cursor &c, gen_inst &inst,
                                  lsc_symbolic_parse_info &info,
                                  unsigned line_no)
   {
      const std::string_view word = c.take_while(is_lsc_symbolic_opcode_char);
      std::vector<std::string_view> parts;
      if (!split_dotted_lsc_word(word, parts) || parts.empty()) {
         error(line_no, "malformed LSC symbolic opcode");
         return false;
      }

      info = {};
      info.op = lsc_opcode_from_name(parts[0]);
      if ((int)info.op < 0) {
         errorf(line_no, "unknown LSC symbolic opcode '%.*s'", SV_FMT(parts[0]));
         return false;
      }

      if (parts.size() < 2) {
         error(line_no, "missing LSC SFID suffix");
         return false;
      }

      const int sfid = sfid_from_name(devinfo, parts[1]);
      if (sfid != GEN_SFID_SLM && sfid != GEN_SFID_TGM && sfid != GEN_SFID_UGM) {
         errorf(line_no, "unsupported LSC symbolic SFID '%.*s'", SV_FMT(parts[1]));
         return false;
      }

      info.sfid = sfid;
      inst.opcode = GEN_OP_SEND;
      inst.send.sfid = (gen_sfid)sfid;

      size_t idx = 2;
      if (info.op == LSC_OP_FENCE) {
         if (parts.size() < 4 || parts.size() > 5) {
            error(line_no, "malformed LSC fence symbolic opcode");
            return false;
         }

         const int scope = lsc_fence_scope_from_name(parts[idx++]);
         if (scope < 0) {
            errorf(line_no, "unknown LSC fence scope '%.*s'", SV_FMT(parts[idx - 1]));
            return false;
         }
         const int flush = lsc_flush_type_from_name(parts[idx++]);
         if (flush < 0) {
            errorf(line_no, "unknown LSC fence flush type '%.*s'", SV_FMT(parts[idx - 1]));
            return false;
         }

         info.fence_scope = (enum lsc_fence_scope)scope;
         info.flush_type = (enum lsc_flush_type)flush;
         info.addr_type = LSC_ADDR_SURFTYPE_FLAT;

         if (idx < parts.size()) {
            if (parts[idx] != "route_to_lsc") {
               errorf(line_no, "unexpected LSC fence suffix '%.*s'", SV_FMT(parts[idx]));
               return false;
            }
            info.route_to_lsc = true;
            idx++;
         }

         if (idx != parts.size()) {
            error(line_no, "unexpected trailing LSC fence suffix");
            return false;
         }

         return true;
      }

      if (parts.size() < 4) {
         error(line_no, "malformed LSC symbolic opcode");
         return false;
      }

      const bool allow_vector = !lsc_opcode_has_cmask(info.op) &&
                                !lsc_opcode_is_atomic(info.op);
      if (!lsc_parse_data_token(parts[idx++], allow_vector,
                                info.data_size, info.num_values,
                                info.transpose)) {
         error(line_no, "malformed LSC data descriptor");
         return false;
      }

      if (lsc_opcode_has_cmask(info.op)) {
         info.cmask = lsc_cmask_from_name(parts[idx++]);
         if ((int)info.cmask < 0) {
            errorf(line_no, "unknown LSC component mask '%.*s'", SV_FMT(parts[idx - 1]));
            return false;
         }
      }

      info.addr_size = lsc_addr_size_from_name(parts[idx++]);
      if ((int)info.addr_size < 0) {
         errorf(line_no, "unknown LSC address size '%.*s'", SV_FMT(parts[idx - 1]));
         return false;
      }

      if (idx + 1 < parts.size()) {
         unsigned cache_ctrl = 0;
         if (lsc_cache_ctrl_from_name(devinfo, info.op, parts[idx], parts[idx + 1],
                                      cache_ctrl)) {
            info.cache_ctrl = cache_ctrl;
            idx += 2;
         }
      }

      info.addr_type = LSC_ADDR_SURFTYPE_FLAT;
      if (idx < parts.size()) {
         const std::string_view surface = parts[idx++];
         if (surface == "flat") {
            info.addr_type = LSC_ADDR_SURFTYPE_FLAT;
         } else if (view_starts_with(surface, "bss[a0.") && surface.back() == ']') {
            unsigned subreg = 0;
            unsigned long long value = 0;
            if (!parse_unsigned_token(surface.substr(7, surface.size() - 8), value)) {
               error(line_no, "malformed bindless surface reference");
               return false;
            }
            subreg = value;
            info.addr_type = LSC_ADDR_SURFTYPE_BSS;
            info.ex_desc_is_reg = true;
            info.ex_desc_subnr = subreg * 2;
         } else if (view_starts_with(surface, "ss[a0.") && surface.back() == ']') {
            unsigned long long value = 0;
            if (!parse_unsigned_token(surface.substr(6, surface.size() - 7), value)) {
               error(line_no, "malformed surface-state reference");
               return false;
            }
            const unsigned subreg = value;
            info.addr_type = LSC_ADDR_SURFTYPE_SS;
            info.ex_desc_is_reg = true;
            info.ex_desc_subnr = subreg * 2;
         } else if (view_starts_with(surface, "bti[") && surface.back() == ']') {
            unsigned long long value = 0;
            if (!parse_unsigned_token(surface.substr(4, surface.size() - 5), value)) {
               error(line_no, "malformed BTI surface reference");
               return false;
            }
            const unsigned bti = value;
            info.addr_type = LSC_ADDR_SURFTYPE_BTI;
            info.ex_desc_imm = gen_lsc_bti_ex_desc(devinfo, bti, 0);
         } else {
            errorf(line_no, "unknown LSC surface selector '%.*s'", SV_FMT(surface));
            return false;
         }
      }

      if (idx != parts.size()) {
         error(line_no, "unexpected trailing LSC symbolic suffix");
         return false;
      }

      if (info.sfid == GEN_SFID_SLM && info.addr_type != LSC_ADDR_SURFTYPE_FLAT) {
         error(line_no, "SLM symbolic syntax does not take a surface selector");
         return false;
      }

      return true;
   }

   bool parse_lsc_symbolic_send(line_cursor &c, gen_inst &inst,
                                const lsc_symbolic_parse_info &info,
                                unsigned line_no)
   {
      int src0_length = -1;
      int src1_length = -1;

      if (!parse_send_reg(c, inst.dst, NULL, line_no))
         return false;
      if (!require_operand_separator(c, line_no))
         return false;
      if (!parse_send_reg(c, inst.src[0], &src0_length, line_no))
         return false;

      inst.src[1].file = GEN_ARF;
      inst.src[1].nr = GEN_ARF_NULL;

      if (lsc_send_has_symbolic_src1(info.op)) {
         if (!require_operand_separator(c, line_no))
            return false;
         if (!parse_send_reg(c, inst.src[1], &src1_length, line_no))
            return false;
      } else {
         c.skip_ws();
         if (!c.eof() && c.peek() != '{') {
            if (c.consume("null:0") || c.consume("null")) {
               /* Accept transitional raw-style null src1 spelling. */
            } else {
               error(line_no, "unexpected extra operand");
               return false;
            }
         }
      }

      const unsigned exec_size = inst.exec_size ? inst.exec_size : 1;
      const unsigned mlen = src0_length >= 0 ? src0_length :
         lsc_symbolic_src0_length(devinfo, info, exec_size);
      const unsigned ex_mlen = src1_length >= 0 ? src1_length :
         lsc_symbolic_src1_length(devinfo, info, exec_size);
      const unsigned rlen =
         lsc_symbolic_response_length(devinfo, info, exec_size, !is_null(inst.dst));

      inst.send.desc_is_reg = false;
      inst.send.ex_desc_is_reg = info.ex_desc_is_reg;
      inst.send.ex_desc_subnr = info.ex_desc_subnr;
      inst.send.ex_desc_imm = info.ex_desc_imm;
      inst.send.ex_desc_imm_extra = 0;
      inst.send.src1_len = ex_mlen;

      const uint32_t payload_desc =
         info.op == LSC_OP_FENCE ?
         lsc_fence_msg_desc(devinfo, info.fence_scope, info.flush_type,
                            info.route_to_lsc) :
         lsc_msg_desc(devinfo, info.op, info.addr_type, info.addr_size,
                      info.data_size,
                      lsc_opcode_has_cmask(info.op) ? (unsigned)info.cmask : info.num_values,
                      info.transpose, info.cache_ctrl);

      inst.send.desc_imm = gen_message_desc(devinfo, mlen, rlen, false) |
                           payload_desc;
      return true;
   }

   bool require_operand_separator(line_cursor &c, unsigned line_no)
   {
      c.skip_ws();
      if (c.eof() || c.peek() == '{') {
         error(line_no, "expected operand");
         return false;
      }
      return true;
   }

   bool parse_prefix(line_cursor &c, gen_inst &inst, inst_state &st,
                     unsigned line_no)
   {
      if (!c.consume('(')) {
         error(line_no, "expected '('");
         return false;
      }

      if (c.consume('W')) {
         inst.no_mask = true;
         if (c.consume('&')) {
            if (!parse_predicate_ref(c, inst, st, line_no))
               return false;
         }
      } else {
         if (!parse_predicate_ref(c, inst, st, line_no))
            return false;
      }

      if (!c.consume(')')) {
         error(line_no, "expected ')' after prefix");
         return false;
      }

      return true;
   }

   bool parse_opcode(line_cursor &c, gen_inst &inst, unsigned line_no)
   {
      const std::string_view word = c.take_while(is_opcode_char);
      if (word.empty()) {
         error(line_no, "expected opcode");
         return false;
      }

      size_t dot = word.find('.');
      const std::string_view base = dot == std::string_view::npos ? word : word.substr(0, dot);
      inst.opcode = opcode_from_name(base);
      if (inst.opcode == GEN_OP_ILLEGAL && base != "illegal") {
         errorf(line_no, "unknown opcode '%.*s'", SV_FMT(base));
         return false;
      }

      size_t start = dot == std::string_view::npos ? word.size() : dot + 1;
      while (start < word.size()) {
         const size_t next = word.find('.', start);
         const std::string_view suffix = word.substr(start, next - start);

         if (suffix == "sat") {
            inst.saturate = true;
         } else if (inst.opcode == GEN_OP_MATH) {
            const int func = math_function_from_name(suffix);
            if (func < 0) {
               errorf(line_no, "unknown math function '%.*s'", SV_FMT(suffix));
               return false;
            }
            inst.math.func = (gen_math)func;
         } else if (inst.opcode == GEN_OP_SYNC) {
            const int func = sync_function_from_name(suffix);
            if (func < 0) {
               errorf(line_no, "unknown sync function '%.*s'", SV_FMT(suffix));
               return false;
            }
            inst.sync.func = (gen_sync_func)func;
         } else if (gen_inst_is_send(&inst)) {
            const int sfid = sfid_from_name(devinfo, suffix);
            if (sfid < 0) {
               errorf(line_no, "unknown SFID '%.*s'", SV_FMT(suffix));
               return false;
            }
            inst.send.sfid = (gen_sfid)sfid;
         } else if (inst.opcode == GEN_OP_BFN) {
            unsigned long long v = 0;
            if (!parse_unsigned_token(suffix, v) || v > 0xff) {
               errorf(line_no, "invalid BFN function control '%.*s'", SV_FMT(suffix));
               return false;
            }
            inst.boolean_func_ctrl = v;
         } else if (inst.opcode == GEN_OP_DPAS) {
            size_t x = suffix.find('x');
            if (x == std::string_view::npos) {
               errorf(line_no, "invalid DPAS function control '%.*s'", SV_FMT(suffix));
               return false;
            }

            const std::string_view lhs = suffix.substr(0, x);
            const std::string_view rhs = suffix.substr(x + 1);
            unsigned long long sdepth = 0;
            if (!parse_unsigned_token(lhs, sdepth)) {
               errorf(line_no, "invalid DPAS systolic depth '%.*s'", SV_FMT(lhs));
               return false;
            }
            unsigned long long rcount = 0;
            if (!parse_unsigned_token(rhs, rcount)) {
               errorf(line_no, "invalid DPAS repeat count '%.*s'", SV_FMT(rhs));
               return false;
            }
            inst.dpas.sdepth = sdepth;
            inst.dpas.rcount = rcount;
         } else if (gen_has_branch_ctrl(inst.opcode) && suffix == "b") {
            inst.branch_control = true;
         } else {
            errorf(line_no, "unexpected opcode suffix '%.*s'", SV_FMT(suffix));
            return false;
         }

         if (next == std::string_view::npos)
            break;
         start = next + 1;
      }

      return true;
   }

   bool parse_exec_control(line_cursor &c, gen_inst &inst, unsigned line_no)
   {
      if (!c.consume('(')) {
         error(line_no, "expected '(' for execution control");
         return false;
      }

      unsigned exec_size = 0;
      if (!c.take_uint(exec_size)) {
         error(line_no, "expected execution size");
         return false;
      }
      inst.exec_size = exec_size;

      if (c.consume('|')) {
         if (!c.consume('M')) {
            error(line_no, "expected 'M' in execution control");
            return false;
         }
         unsigned chan_offset = 0;
         if (!c.take_uint(chan_offset)) {
            error(line_no, "expected channel offset");
            return false;
         }
         inst.chan_offset = chan_offset;
      }

      if (!c.consume(')')) {
         error(line_no, "expected ')' after execution control");
         return false;
      }

      return true;
   }

   bool parse_cond_modifier(line_cursor &c, gen_inst &inst, inst_state &st,
                            unsigned line_no)
   {
      if (!c.consume('(')) {
         error(line_no, "expected '('");
         return false;
      }

      const std::string_view name = c.take_while([](char ch) {
         return isalpha((unsigned char)ch);
      });
      if (!c.consume(')')) {
         error(line_no, "expected ')' after conditional modifier");
         return false;
      }

      const gen_condition mod = cond_modifier_from_name(name);
      if (mod == GEN_CONDITION_NONE) {
         errorf(line_no, "unknown conditional modifier '%.*s'", SV_FMT(name));
         return false;
      }

      inst.cmod = mod;
      return parse_flag_ref(c, inst, st, false, line_no);
   }

   bool parse_predicate_ref(line_cursor &c, gen_inst &inst, inst_state &st,
                            unsigned line_no)
   {
      if (c.consume('~'))
         inst.pred_inv = true;

      if (!parse_flag_ref(c, inst, st, true, line_no))
         return false;

      if (!inst.pred_control)
         inst.pred_control = GEN_PREDICATE_NORMAL;

      return true;
   }

   bool parse_flag_ref(line_cursor &c, gen_inst &inst, inst_state &st,
                       bool include_pred_suffix, unsigned line_no)
   {
      if (!c.consume('f')) {
         error(line_no, "expected flag register reference");
         return false;
      }

      unsigned nr = 0, subnr = 0;
      if (!c.take_uint(nr) || !c.consume('.') || !c.take_uint(subnr)) {
         error(line_no, "malformed flag register reference");
         return false;
      }

      if (st.flag_ref_seen && (st.flag_nr != nr || st.flag_subnr != subnr)) {
         error(line_no, "predicate and conditional modifier must use the same flag register");
         return false;
      }

      st.flag_ref_seen = true;
      st.flag_nr = nr;
      st.flag_subnr = subnr;
      inst.flag_nr = nr;
      inst.flag_subnr = subnr;

      if (!include_pred_suffix)
         return true;

      if (!c.consume('.'))
         return true;

      const std::string_view suffix = c.take_while([](char ch) {
         return isalpha((unsigned char)ch) || isdigit((unsigned char)ch);
      });

      if (suffix == "x") {
         inst.pred_control = GEN_PREDICATE_A16_REPLICATE_X;
         inst.align16 = true;
      } else if (suffix == "y") {
         inst.pred_control = GEN_PREDICATE_A16_REPLICATE_Y;
         inst.align16 = true;
      } else if (suffix == "z") {
         inst.pred_control = GEN_PREDICATE_A16_REPLICATE_Z;
         inst.align16 = true;
      } else if (suffix == "w") {
         inst.pred_control = GEN_PREDICATE_A16_REPLICATE_W;
         inst.align16 = true;
      } else if (suffix == "any") {
         inst.pred_control = GEN_PREDICATE_ANYV;
      } else if (suffix == "all") {
         inst.pred_control = GEN_PREDICATE_ALLV;
      } else if (suffix == "anyv") {
         inst.pred_control = GEN_PREDICATE_ANYV;
      } else if (suffix == "allv") {
         inst.pred_control = GEN_PREDICATE_ALLV;
      } else if (suffix == "any2h") {
         inst.pred_control = GEN_PREDICATE_ANY2H;
      } else if (suffix == "all2h") {
         inst.pred_control = GEN_PREDICATE_ALL2H;
      } else if (suffix == "any4h") {
         inst.pred_control = GEN_PREDICATE_ANY4H;
      } else if (suffix == "all4h") {
         inst.pred_control = GEN_PREDICATE_ALL4H;
      } else if (suffix == "any8h") {
         inst.pred_control = GEN_PREDICATE_ANY8H;
      } else if (suffix == "all8h") {
         inst.pred_control = GEN_PREDICATE_ALL8H;
      } else if (suffix == "any16h") {
         inst.pred_control = GEN_PREDICATE_ANY16H;
      } else if (suffix == "all16h") {
         inst.pred_control = GEN_PREDICATE_ALL16H;
      } else if (suffix == "any32h") {
         inst.pred_control = GEN_PREDICATE_ANY32H;
      } else if (suffix == "all32h") {
         inst.pred_control = GEN_PREDICATE_ALL32H;
      } else {
         errorf(line_no, "unknown predicate control '%.*s'", SV_FMT(suffix));
         return false;
      }

      return true;
   }

   bool parse_dst(line_cursor &c, gen_inst &inst, inst_state &st,
                  unsigned line_no)
   {
      operand_state op_state;
      if (!parse_register_base(c, inst.dst, op_state, false, line_no))
         return false;

      if (inst.dst.file == GEN_IMM) {
         error(line_no, "destination cannot be immediate");
         return false;
      }

      if (c.consume('<')) {
         unsigned hstride = 0;
         if (!c.take_uint(hstride) || !c.consume('>')) {
            error(line_no, "malformed destination region");
            return false;
         }
         inst.dst.region.hstride = hstride;
      } else {
         inst.dst.region.hstride = 1;
      }

      if (c.consume('.')) {
         inst.align16 = true;
         st.dst_has_writemask = true;
         unsigned writemask = 0;
         if (!parse_writemask(c, writemask, line_no))
            return false;
         inst.dst.writemask = writemask;
      }

      bool has_type = false;
      if (!parse_type_if_present(c, inst.dst.type, has_type, line_no))
         return false;
      if (!has_type)
         inst.dst.type = GEN_TYPE_UD;

      if (!finalize_grf_subreg(inst.dst, op_state, false)) {
         error(line_no, "missing destination type for GRF subregister");
         return false;
      }

      return true;
   }

   bool parse_src(line_cursor &c, gen_operand &src, bool &has_explicit_swizzle,
                  bool is_3src, unsigned src_idx, gen_inst &inst,
                  unsigned line_no)
   {
      if (c.consume('-'))
         src.negate = true;
      if (c.consume('|'))
         src.abs = true;

      operand_state op_state;
      if (looks_like_number(c)) {
         if (!parse_immediate(c, src, line_no))
            return false;
      } else {
         if (!parse_register_base(c, src, op_state, false, line_no))
            return false;

         if (is_null(src)) {
            src.region = { 1, 1, 0 };
            return true;
         }

         bool has_explicit_region = false;
         if (c.consume('<')) {
            has_explicit_region = true;
            if (is_3src) {
               if (!parse_src_region_3src(c, src.region, src_idx, line_no))
                  return false;
            } else {
               if (!parse_src_region(c, src.region, line_no))
                  return false;
            }
         } else {
            src.region = { 1, 1, 0 };
         }

         if (c.consume('.')) {
            if (is_3src) {
               if (!c.consume('r') || is_ident_char(c.peek())) {
                  error(line_no, "expected 3-src replicate-control suffix '.r'");
                  return false;
               }

               inst.align16 = true;
               src.rep_ctrl = true;
               if (!has_explicit_region)
                  src.region = { 0, 1, 0 };
            } else {
               inst.align16 = true;
               has_explicit_swizzle = true;
               unsigned swizzle = 0;
               if (!parse_swizzle(c, swizzle, line_no))
                  return false;
               src.swizzle = swizzle;
            }
         }

         bool has_type = false;
         if (!parse_type_if_present(c, src.type, has_type, line_no))
            return false;
         if (!has_type)
            src.type = GEN_TYPE_UD;

         if (!finalize_grf_subreg(src, op_state, false)) {
            error(line_no, "missing source type for GRF subregister");
            return false;
         }
      }

      if (src.abs && !c.consume('|')) {
         error(line_no, "expected closing '|' for absolute source");
         return false;
      }

      return true;
   }

   bool looks_like_number(const line_cursor &c) const
   {
      if (c.eof())
         return false;

      const char ch = c.peek();
      return isdigit((unsigned char)ch) || ch == '+' || ch == '-';
   }

   bool parse_register_base(line_cursor &c, gen_operand &op, operand_state &st,
                            bool send_grf_subreg, unsigned line_no)
   {
      if (c.consume("r[")) {
         op.file = GEN_GRF;
         op.indirect = true;
         if (!c.consume("a0")) {
            error(line_no, "expected a0 in indirect register");
            return false;
         }

         if (!c.consume('.')) {
            error(line_no, "expected address subregister");
            return false;
         }

         unsigned subnr = 0;
         if (!parse_chan_subreg(c, subnr)) {
            error(line_no, "malformed address subregister");
            return false;
         }
         op.subnr = subnr;

         c.skip_ws();
         if (c.consume('+')) {
            c.skip_ws();
            unsigned offset = 0;
            if (!c.take_uint(offset)) {
               error(line_no, "expected indirect register offset");
               return false;
            }
            op.addr_imm = offset;
         } else if (c.consume('-')) {
            c.skip_ws();
            unsigned offset = 0;
            if (!c.take_uint(offset)) {
               error(line_no, "expected indirect register offset");
               return false;
            }
            op.addr_imm = -(int)offset;
         }

         if (!c.consume(']')) {
            error(line_no, "expected ']'");
            return false;
         }

         return true;
      }

      if (c.next_is('r') && c.pos + 1 < c.line.size() &&
          isdigit((unsigned char)c.line[c.pos + 1])) {
         c.consume('r');
         op.file = GEN_GRF;
         if (!c.take_uint(op.nr)) {
            error(line_no, "expected GRF number");
            return false;
         }

         if (c.next_is('.') && c.pos + 1 < c.line.size() &&
             isdigit((unsigned char)c.line[c.pos + 1])) {
            c.consume('.');
            st.has_grf_subreg = true;
            if (!c.take_uint(st.grf_subreg_index)) {
               error(line_no, "expected GRF subregister");
               return false;
            }
            if (send_grf_subreg)
               op.subnr = st.grf_subreg_index * 16;
         }

         return true;
      }

      if (c.consume("bad")) {
         op.file = GEN_BAD_FILE;
         return true;
      }

      return parse_arf_base(c, op, line_no);
   }

   bool parse_arf_base(line_cursor &c, gen_operand &op, unsigned line_no)
   {
      op.file = GEN_ARF;

      if (c.consume("null")) {
         op.nr = GEN_ARF_NULL;
         return true;
      }

      if (c.consume("ip")) {
         op.nr = GEN_ARF_IP;
         return true;
      }

      if (c.consume("tdr")) {
         unsigned idx = 0;
         if (!c.take_uint(idx)) {
            error(line_no, "expected TDR index");
            return false;
         }
         op.nr = GEN_ARF_TDR + idx;
         return true;
      }

      if (c.consume("mask")) {
         unsigned idx = 0;
         if (!c.take_uint(idx)) {
            error(line_no, "expected mask register index");
            return false;
         }
         op.nr = GEN_ARF_MASK + idx;
         return true;
      }

      if (c.consume("acc")) {
         unsigned idx = 0;
         if (!c.take_uint(idx)) {
            error(line_no, "expected accumulator index");
            return false;
         }
         op.nr = GEN_ARF_ACCUMULATOR + idx;
         if (c.consume('.')) {
            unsigned subnr = 0;
            if (!parse_chan_subreg(c, subnr)) {
               error(line_no, "malformed accumulator subregister");
               return false;
            }
            op.subnr = subnr;
         }
         return true;
      }

      if (c.consume("arf")) {
         unsigned nr = 0;
         if (!c.take_uint(nr)) {
            error(line_no, "expected ARF number");
            return false;
         }
         op.nr = nr;
         return true;
      }

      if (c.consume("sr")) {
         unsigned idx = 0;
         if (!c.take_uint(idx)) {
            error(line_no, "expected state register index");
            return false;
         }
         op.nr = GEN_ARF_STATE + idx;
         return true;
      }

      if (c.consume("tm")) {
         unsigned idx = 0;
         if (!c.take_uint(idx)) {
            error(line_no, "expected timestamp register index");
            return false;
         }
         op.nr = GEN_ARF_TIMESTAMP + idx;
         return true;
      }

      if (c.consume("cr")) {
         unsigned idx = 0;
         if (!c.take_uint(idx)) {
            error(line_no, "expected control register index");
            return false;
         }
         op.nr = GEN_ARF_CONTROL + idx;
         if (c.consume('.')) {
            unsigned subnr = 0;
            if (!parse_chan_subreg(c, subnr)) {
               error(line_no, "malformed control subregister");
               return false;
            }
            op.subnr = subnr;
         }
         return true;
      }

      if (c.consume('a')) {
         unsigned idx = 0;
         if (!c.take_uint(idx)) {
            error(line_no, "expected address register index");
            return false;
         }
         op.nr = GEN_ARF_ADDRESS + idx;
         if (c.consume('.')) {
            unsigned subnr = 0;
            if (!parse_chan_subreg(c, subnr)) {
               error(line_no, "malformed address subregister");
               return false;
            }
            op.subnr = subnr;
         }
         return true;
      }

      if (c.consume('f')) {
         unsigned idx = 0;
         if (!c.take_uint(idx)) {
            error(line_no, "expected flag register index");
            return false;
         }
         op.nr = GEN_ARF_FLAG | idx;
         if (c.consume('.')) {
            unsigned subnr = 0;
            if (!c.take_uint(subnr)) {
               error(line_no, "expected flag subregister");
               return false;
            }
            op.subnr = subnr;
         }
         return true;
      }

      if (c.consume('s')) {
         unsigned idx = 0;
         if (!c.take_uint(idx)) {
            error(line_no, "expected scalar register index");
            return false;
         }
         op.nr = GEN_ARF_SCALAR + idx;
         if (c.consume('.')) {
            unsigned subnr = 0;
            if (!parse_chan_subreg(c, subnr)) {
               error(line_no, "malformed scalar subregister");
               return false;
            }
            op.subnr = subnr;
         }
         return true;
      }

      if (c.consume('n')) {
         unsigned idx = 0;
         if (!c.take_uint(idx)) {
            error(line_no, "expected notification count register index");
            return false;
         }
         op.nr = GEN_ARF_NOTIFICATION_COUNT + idx;
         return true;
      }

      error(line_no, "unknown register");
      return false;
   }

   bool parse_chan_subreg(line_cursor &c, unsigned &subnr)
   {
      if (c.consume('x')) {
         subnr = 0;
         return true;
      }
      if (c.consume('y')) {
         subnr = 2;
         return true;
      }
      if (c.consume('z')) {
         subnr = 4;
         return true;
      }
      if (c.consume('w')) {
         subnr = 6;
         return true;
      }
      unsigned n = 0;
      if (!c.take_uint(n))
         return false;
      subnr = n;
      return true;
   }

   bool parse_type_if_present(line_cursor &c, gen_reg_type &type,
                             bool &present, unsigned line_no)
   {
      present = false;
      if (!c.consume(':'))
         return true;

      present = true;
      const std::string_view name = c.take_while([](char ch) {
         return isalpha((unsigned char)ch) || isdigit((unsigned char)ch);
      });
      type = type_from_name(name);
      if (type == GEN_TYPE_INVALID) {
         errorf(line_no, "unknown type '%.*s'", SV_FMT(name));
         return false;
      }

      return true;
   }

   bool finalize_grf_subreg(gen_operand &op, const operand_state &st,
                            bool send_grf_subreg) const
   {
      if (op.file != GEN_GRF || op.indirect || !st.has_grf_subreg)
         return true;

      if (send_grf_subreg)
         return true;

      const unsigned type_size = MAX2(gen_type_size_bytes(op.type), 1u);
      op.subnr = st.grf_subreg_index * type_size;
      return true;
   }

   bool parse_writemask(line_cursor &c, unsigned &writemask, unsigned line_no)
   {
      writemask = 0;
      while (!c.eof()) {
         const char ch = c.peek();
         unsigned bit = 0;
         switch (ch) {
         case 'x': bit = 1u << GEN_CHANNEL_X; break;
         case 'y': bit = 1u << GEN_CHANNEL_Y; break;
         case 'z': bit = 1u << GEN_CHANNEL_Z; break;
         case 'w': bit = 1u << GEN_CHANNEL_W; break;
         default:
            return true;
         }
         writemask |= bit;
         c.pos++;
      }

      if (writemask == 0 && c.peek() != ':' && c.peek() != '\0' && c.peek() != '<') {
         error(line_no, "malformed writemask");
         return false;
      }

      return true;
   }

   bool parse_swizzle(line_cursor &c, unsigned &swizzle, unsigned line_no)
   {
      char fields[4] = {};
      unsigned num_fields = 0;
      while (!c.eof()) {
         const char ch = c.peek();
         if (ch != 'x' && ch != 'y' && ch != 'z' && ch != 'w')
            break;
         if (num_fields < ARRAY_SIZE(fields))
            fields[num_fields] = ch;
         num_fields++;
         c.pos++;
      }

      if (num_fields == 0) {
         error(line_no, "malformed swizzle");
         return false;
      }

      if (num_fields != 1 && num_fields != 4) {
         error(line_no, "swizzle must have 1 or 4 components");
         return false;
      }

      auto chan = [](char ch) {
         switch (ch) {
         case 'x': return GEN_CHANNEL_X;
         case 'y': return GEN_CHANNEL_Y;
         case 'z': return GEN_CHANNEL_Z;
         default:  return GEN_CHANNEL_W;
         }
      };

      const char x = fields[0];
      const char y = num_fields == 1 ? fields[0] : fields[1];
      const char z = num_fields == 1 ? fields[0] : fields[2];
      const char w = num_fields == 1 ? fields[0] : fields[3];

      swizzle = (chan(x) << 0) |
                (chan(y) << 2) |
                (chan(z) << 4) |
                (chan(w) << 6);
      return true;
   }

   bool parse_src_region(line_cursor &c, gen_region &region, unsigned line_no)
   {
      if (c.next_is('0') && c.pos + 1 < c.line.size() && c.line[c.pos + 1] == '>') {
         c.consume('0');
         region = { 0, 1, 0 };
      } else {
         if (c.consume("VxH")) {
            region.vstride = GEN_VSTRIDE_ONE_DIMENSIONAL;
         } else {
            unsigned vstride = 0;
            if (!c.take_uint(vstride)) {
               error(line_no, "expected source vertical stride");
               return false;
            }
            region.vstride = vstride;
         }

         unsigned width = 0, hstride = 0;
         if (!c.consume(';') || !c.take_uint(width) ||
             !c.consume(',') || !c.take_uint(hstride)) {
            error(line_no, "malformed source region");
            return false;
         }
         region.width = width;
         region.hstride = hstride;
      }

      if (!c.consume('>')) {
         error(line_no, "expected '>' after source region");
         return false;
      }

      return true;
   }

   bool parse_src_region_3src(line_cursor &c, gen_region &region,
                              unsigned src_idx, unsigned line_no)
   {
      if (c.next_is('0') && c.pos + 1 < c.line.size() && c.line[c.pos + 1] == '>') {
         c.consume('0');
         region = { 0, 1, 0 };
         if (!c.consume('>')) {
            error(line_no, "expected '>' after source region");
            return false;
         }
         return true;
      }

      unsigned a = 0, b = 0;
      if (!c.take_uint(a)) {
         error(line_no, "expected 3-src region");
         return false;
      }

      if (src_idx < 2) {
         if (!c.consume(';') || !c.take_uint(b)) {
            error(line_no, "malformed 3-src region");
            return false;
         }

         region.vstride = a;
         region.hstride = b;
         region.width = brw_implied_width_for_3src_a1(a, b);
      } else {
         region.vstride = 0;
         region.width = 1;
         region.hstride = a;
      }

      if (!c.consume('>')) {
         error(line_no, "expected '>' after source region");
         return false;
      }

      return true;
   }

   bool parse_immediate(line_cursor &c, gen_operand &op, unsigned line_no)
   {
      std::string_view tok;
      if (!c.take_number_token(tok)) {
         error(line_no, "expected immediate");
         return false;
      }

      op.file = GEN_IMM;
      bool has_type = false;
      if (!parse_type_if_present(c, op.type, has_type, line_no))
         return false;
      if (!has_type)
         op.type = GEN_TYPE_UD;

      switch (op.type) {
      case GEN_TYPE_D:
      case GEN_TYPE_W:
      case GEN_TYPE_B: {
         long long v = 0;
         if (!parse_signed_token(tok, v)) {
            error(line_no, "malformed signed immediate");
            return false;
         }
         op.imm = (uint64_t)v;
         break;
      }
      default: {
         unsigned long long v = 0;
         if (!parse_unsigned_token(tok, v)) {
            error(line_no, "malformed immediate");
            return false;
         }
         op.imm = v;
         break;
      }
      }

      return true;
   }

   bool parse_send(line_cursor &c, gen_inst &inst, unsigned line_no)
   {
      if (!parse_send_reg(c, inst.dst, NULL, line_no))
         return false;
      if (!require_operand_separator(c, line_no))
         return false;

      int src0_length = -1;
      if (!parse_send_reg(c, inst.src[0], &src0_length, line_no))
         return false;
      if (!require_operand_separator(c, line_no))
         return false;

      int src1_length = -1;
      if (c.consume("null:0")) {
         inst.src[1].file = GEN_ARF;
         inst.src[1].nr = GEN_ARF_NULL;
      } else {
         const char next = c.peek();
         if (next == 'a' || next == '+' || next == '-' ||
             isdigit((unsigned char)next)) {
            /* Raw SEND syntax may omit a null src1 when the next operand is
             * already the extended descriptor.
             */
            inst.src[1].file = GEN_ARF;
            inst.src[1].nr = GEN_ARF_NULL;
         } else if (!parse_send_reg(c, inst.src[1], &src1_length, line_no)) {
            return false;
         }
      }

      if (src1_length >= 0)
         inst.send.src1_len = src1_length;

      (void)src0_length;

      if (!require_operand_separator(c, line_no))
         return false;
      if (!parse_send_ex_desc(c, inst, line_no))
         return false;

      if (!require_operand_separator(c, line_no))
         return false;
      if (!parse_send_desc(c, inst, line_no))
         return false;

      return true;
   }

   bool parse_send_reg(line_cursor &c, gen_operand &op, int *length,
                       unsigned line_no)
   {
      operand_state st;
      if (!parse_register_base(c, op, st, true, line_no))
         return false;

      op.region = { 1, 1, 0 };

      if (length)
         *length = -1;

      if (c.consume(':')) {
         unsigned len = 0;
         if (!c.take_uint(len)) {
            error(line_no, "expected send payload length");
            return false;
         }
         if (length)
            *length = len;
      }
      return true;
   }

   bool parse_send_ex_desc(line_cursor &c, gen_inst &inst, unsigned line_no)
   {
      unsigned imm_extra = 0;
      bool has_imm_extra = false;
      const size_t saved = c.pos;
      std::string_view tok;
      if (c.take_number_token(tok) && c.consume(':')) {
         unsigned long long value = 0;
         if (!parse_unsigned_token(tok, value)) {
            error(line_no, "malformed send extended descriptor immediate offset");
            return false;
         }
         imm_extra = value;
         has_imm_extra = true;
      } else {
         c.pos = saved;
      }

      if (c.consume("a0.")) {
         unsigned subreg = 0;
         if (!c.take_uint(subreg)) {
            error(line_no, "expected send extended descriptor subregister");
            return false;
         }

         inst.send.ex_desc_is_reg = true;
         inst.send.ex_desc_subnr = subreg * 2;
         if (has_imm_extra)
            inst.send.ex_desc_imm_extra = imm_extra;
         return true;
      }

      if (has_imm_extra) {
         error(line_no, "immediate offset prefix requires register extended descriptor");
         return false;
      }

      if (!c.take_number_token(tok)) {
         error(line_no, "expected send extended descriptor");
         return false;
      }

      unsigned long long value = 0;
      if (!parse_unsigned_token(tok, value)) {
         error(line_no, "malformed send extended descriptor");
         return false;
      }
      inst.send.ex_desc_imm = value;
      return true;
   }

   bool parse_send_desc(line_cursor &c, gen_inst &inst, unsigned line_no)
   {
      if (c.consume("a0.0")) {
         inst.send.desc_is_reg = true;
         return true;
      }

      std::string_view tok;
      if (!c.take_number_token(tok)) {
         error(line_no, "expected send descriptor");
         return false;
      }

      unsigned long long value = 0;
      if (!parse_unsigned_token(tok, value)) {
         error(line_no, "malformed send descriptor");
         return false;
      }
      inst.send.desc_imm = value;

      /* Accept legacy raw SEND syntax that leaves the EOT bit embedded in the
       * descriptor immediate.
       */
      inst.send.eot |= inst.send.desc_imm >> 31;
      inst.send.desc_imm &= ~(1u << 31);
      return true;
   }

   bool parse_branch_operand(line_cursor &c, gen_inst &inst, unsigned src_index,
                             inst_state &st, unsigned line_no)
   {
      c.skip_ws();

      if (looks_like_number(c)) {
         const std::string_view tok = c.take_until_space_or('{');
         if (tok.empty()) {
            error(line_no, "expected branch target value");
            return false;
         }

         unsigned long long v = 0;
         if (!parse_unsigned_token(tok, v)) {
            error(line_no, "malformed branch target");
            return false;
         }

         inst.src[src_index].file = GEN_IMM;
         inst.src[src_index].type = GEN_TYPE_D;
         inst.src[src_index].imm = (int32_t)(uint32_t)v;
         return true;
      }

      /* Check if the token contains register syntax characters (e.g. '<',
       * ':', '.', '[') before the next space, indicating a register source
       * rather than a bare label identifier.
       */
      bool has_register_syntax = false;
      for (size_t i = c.pos; i < c.line.size(); i++) {
         const char ch = c.line[i];
         if (isspace((unsigned char)ch) || ch == '{')
            break;
         if (ch == '<' || ch == ':' || ch == '.' || ch == '[') {
            has_register_syntax = true;
            break;
         }
      }

      if (has_register_syntax) {
         return parse_src(c, inst.src[src_index], st.src_has_swizzle[src_index],
                          false, src_index, inst, line_no);
      }

      /* Bare identifier — treat as label for deferred resolution. */
      const std::string_view tok = c.take_until_space_or('{');
      if (tok.empty() || !is_ident_start(tok[0])) {
         error(line_no, "expected branch target or register source");
         return false;
      }

      inst.src[src_index].file = GEN_IMM;
      inst.src[src_index].type = GEN_TYPE_D;
      inst.src[src_index].imm = 0;
      label_uses.push_back({ (int)insts.size(), src_index, line_no, tok });
      return true;
   }

   bool parse_annotation(line_cursor &c, gen_inst &inst, unsigned line_no)
   {
      while (true) {
         const size_t start = c.pos;
         while (!c.eof() && c.peek() != ',' && c.peek() != '}')
            c.pos++;

         const std::string_view note = c.line.substr(start, c.pos - start);
         if (note.empty()) {
            error(line_no, "empty annotation note");
            return false;
         }

         if (!parse_annotation_note(note, inst, line_no))
            return false;

         if (c.consume('}'))
            return true;
         if (!c.consume(',')) {
            error(line_no, "expected ',' or '}' in annotation");
            return false;
         }
      }
   }

   bool parse_annotation_note(std::string_view note, gen_inst &inst,
                              unsigned line_no)
   {
      if (note == "AccWrEn") {
         inst.acc_wr_control = true;
         return true;
      }

      if (note == "Atomic") {
         if (devinfo->ver >= 12)
            inst.atomic_control = true;
         else
            inst.thread_control = GEN_THREAD_ATOMIC;
         return true;
      }

      if (note == "Breakpoint") {
         inst.debug_control = true;
         return true;
      }

      if (note == "Align16") {
         inst.align16 = true;
         return true;
      }

      if (note == "EOT") {
         inst.send.eot = true;
         return true;
      }

      if (note == "NoDDChk") {
         inst.no_dd_check = true;
         return true;
      }

      if (note == "NoDDClr") {
         inst.no_dd_clear = true;
         return true;
      }

      if (note == "Switch") {
         inst.thread_control = GEN_THREAD_SWITCH;
         return true;
      }

      if (note == "Serialize") {
         inst.fusion_control = true;
         return true;
      }

      if (note == "ExBSO") {
         inst.send.ex_bso = true;
         return true;
      }

      if (!note.empty() && note[0] == '$') {
         const size_t dot = note.find('.');
         unsigned long long sbid = 0;
         if (!parse_unsigned_token(note.substr(1, dot == std::string_view::npos ? note.size() - 1
                                                                             : dot - 1), sbid)) {
            error(line_no, "malformed SBID annotation");
            return false;
         }
         inst.swsb.sbid = sbid;

         if (dot == std::string_view::npos) {
            inst.swsb.mode = GEN_SBID_SET;
         } else if (note.substr(dot) == ".dst") {
            inst.swsb.mode = GEN_SBID_DST;
         } else if (note.substr(dot) == ".src") {
            inst.swsb.mode = GEN_SBID_SRC;
         } else {
            errorf(line_no, "unknown SBID annotation '%.*s'", SV_FMT(note));
            return false;
         }
         return true;
      }

      if (note.size() >= 3 && note[1] == '@') {
         switch (note[0]) {
         case 'F': inst.swsb.pipe = GEN_PIPE_FLOAT; break;
         case 'I': inst.swsb.pipe = GEN_PIPE_INT; break;
         case 'L': inst.swsb.pipe = GEN_PIPE_LONG; break;
         case 'A': inst.swsb.pipe = GEN_PIPE_ALL; break;
         case 'M': inst.swsb.pipe = GEN_PIPE_MATH; break;
         case 'S': inst.swsb.pipe = GEN_PIPE_SCALAR; break;
         default:
            errorf(line_no, "unknown SWSB pipe annotation '%.*s'", SV_FMT(note));
            return false;
         }

         unsigned long long regdist = 0;
         if (!parse_unsigned_token(note.substr(2), regdist)) {
            error(line_no, "malformed SWSB pipe annotation");
            return false;
         }
         inst.swsb.regdist = regdist;
         return true;
      }

      errorf(line_no, "unknown annotation note '%.*s'", SV_FMT(note));
      return false;
   }

   void apply_align16_defaults(gen_inst &inst, const inst_state &st) const
   {
      if (!inst.align16)
         return;

      const gen_format format = gen_inst_format(inst.opcode);
      const bool has_dst = gen_inst_has_dst(format, inst.opcode);
      if (has_dst && !st.dst_has_writemask)
         inst.dst.writemask = DEFAULT_WRITEMASK_XYZW;

      const unsigned num_sources = gen_inst_num_sources(devinfo, &inst);
      for (unsigned i = 0; i < num_sources; i++) {
         if (inst.src[i].file != GEN_IMM && !st.src_has_swizzle[i])
            inst.src[i].swizzle = SWIZZLE_XYZW;
      }
   }

   void resolve_labels()
   {
      for (const pending_label_use &use : label_uses) {
         auto it = labels.find(use.label);
         if (it == labels.end()) {
            errorf(use.line, "undefined label '%.*s'", SV_FMT(use.label));
            continue;
         }

         const int rel = (it->second - use.inst_idx) * 16;
         gen_inst &inst = insts[use.inst_idx];
         inst.src[use.src_index].imm = rel;
      }
   }

   void emit_insts()
   {
      params->num_insts = insts.size();
      if (insts.empty())
         return;

      params->insts = ralloc_array(params->mem_ctx, gen_inst *, insts.size());
      gen_inst *values = rzalloc_array(params->mem_ctx, gen_inst, insts.size());

      for (unsigned i = 0; i < insts.size(); i++) {
         values[i] = insts[i];
         params->insts[i] = &values[i];
      }
   }

   void emit_errors()
   {
      if (errors.empty())
         return;

      params->errors = ralloc_array(params->mem_ctx, gen_error, errors.size());
      params->num_errors = errors.size();

      for (unsigned i = 0; i < errors.size(); i++) {
         params->errors[i] = errors[i];
      }
   }
};

} /* namespace */

extern "C" bool
gen_parse(gen_parse_params *params)
{
   gen_parser p(params);
   return p.parse();
}
