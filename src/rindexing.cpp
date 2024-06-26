
#include <algorithm>

#include "primitives.h"
#include "util.h"
#include "rindexing.h"

// translate integer values from `dst` into
// the indices of their corresponding matches in `levelkeys`
// this is necessary because REDATAM can
// store factors as sparse, 0-based integers
// while R requires factors to be contiguous and 1-based
void translate_factor(SEXP dst, SEXP levelkeys) {
  const int *const first = INTEGER(levelkeys);
  const int *const last = first + length(levelkeys);
  for (int i = 0; i < length(dst); ++i) {
    int v = INTEGER_ELT(dst, i);
    const int *const pos = std::lower_bound(first, last, v);
    SET_INTEGER_ELT(dst, i,
                    pos != last && *pos == v ? pos - first + 1 : NA_INTEGER);
  }
}

// if `src` has `levels` and `levelkeys` attributes, then
// translate the values in `dst` to the factors in `src`
// using `translate_factor` and set the `levels` and `class`
// attributes of `dst` to turn it into a proper factor object
void configure_factor(SEXP dst, SEXP src) {
  SEXP levels = getAttrib(src, R_LevelsSymbol);
  SEXP levelkeys = getAttrib(src, mkString("levelkeys"));
  if (!isNull(levels) and !isNull(levelkeys)) {
    translate_factor(dst, levelkeys);
    setAttrib(dst, R_LevelsSymbol, levels);
    classgets(dst, mkString("factor"));
  }
}

// reads a variable-bit object using the provided function in `fread_elt`
// fread_elt should return a word
template <typename F>
SEXP variable_bit_get_rvector(SEXP robj, SEXP indices, F &&fread_elt) {
  std::string path =
      CHAR(asChar(STRING_ELT(getAttrib(robj, mkString("data.path")), 0)));
  const unsigned int instance_len =
      INTEGER_ELT(getAttrib(robj, mkString("instance.len")), 0);
  const int instance_num =
      INTEGER_ELT(getAttrib(robj, mkString("instance.num")), 0);

  std::fstream f(path, std::ios_base::in | std::ios_base::binary);
  if (!f) {
    error_return("could not open data file for reading");
  }

  int protect_count = 0;

  SEXP ans = allocVector(INTSXP, length(indices));
  PROTECT(ans);
  ++protect_count;

  const uint32_t bits_per_word = sizeof(uint32_t) * 8;
  const uint32_t mask = (1 << instance_len) - 1;

  uint32_t element = fread_elt(f);
  size_t current_offset = 0;
  for (int dst_index = 0; dst_index < length(indices); ++dst_index) {
    int src_index = INTEGER_ELT(indices, dst_index) - 1;

    if (src_index < 0 or src_index >= instance_num) {
      SET_INTEGER_ELT(ans, dst_index, NA_INTEGER);
      continue;
    }

    size_t target_offset =
        src_index * instance_len / bits_per_word * sizeof(uint32_t);
    // only use seek if we really need to (can be really slow, and contiguous
    // indices don't need to seek)
    if (target_offset != current_offset) {
      f.seekg(target_offset);
      element = fread_elt(f);
      current_offset = target_offset;
    }
    const int shifted_bits = src_index * instance_len % bits_per_word;

    if (bits_per_word - shifted_bits >= instance_len) {
      uint32_t v =
          element >> (sizeof(uint32_t) * 8 - instance_len - shifted_bits);
      v &= mask;
      SET_INTEGER_ELT(ans, dst_index, v);
    } else {
      uint32_t next_element = fread_elt(f);
      const int bits_in_first = instance_len + shifted_bits - bits_per_word;
      uint32_t v = (element << bits_in_first) +
                   (next_element >> (bits_per_word - bits_in_first));
      SET_INTEGER_ELT(ans, dst_index, v & mask);
      element = next_element;
      ++current_offset;
    }
  }

  configure_factor(ans, robj);

  UNPROTECT(protect_count);

  return ans;
}

// reads objects from the file using `fread_elt` and writes them to the result
// array using `write_elt` `fread_elt` receives the file stream and the number
// of bytes per element `write_elt` receives the destination array SEXP, the
// destination index and the value returned by `fread_elt`
template <typename F, typename G, typename NAT>
SEXP variable_byte_get_rvector(SEXP robj, SEXP indices, F &&fread_elt,
                               G &&write_elt, NAT naval, int anstype) {
  std::string path =
      CHAR(asChar(STRING_ELT(getAttrib(robj, mkString("data.path")), 0)));
  const int instance_len =
      INTEGER_ELT(getAttrib(robj, mkString("instance.len")), 0);
  const int instance_num =
      INTEGER_ELT(getAttrib(robj, mkString("instance.num")), 0);

  std::fstream f(path, std::ios_base::in | std::ios_base::binary);
  if (!f) {
    error_return("could not open data file for reading");
  }

  int protect_count = 0;

  SEXP ans = allocVector(anstype, length(indices));
  PROTECT(ans);
  ++protect_count;

  size_t current_offset = 0;
  for (int dst_index = 0; dst_index < length(indices); ++dst_index) {
    int src_index = INTEGER_ELT(indices, dst_index) - 1;

    if (src_index < 0 || src_index >= instance_num) {
      write_elt(ans, dst_index, naval);
      continue;
    }

    size_t target_offset = src_index * instance_len;
    if (target_offset != current_offset) {
      f.seekg(target_offset);
      current_offset = target_offset;
    }
    auto v = fread_elt(f, instance_len);
    write_elt(ans, dst_index, v);
    current_offset += instance_len;
  }

  UNPROTECT(protect_count);

  return ans;
}

extern "C" {
SEXP _redatam_bin_get_rvector(SEXP bin, SEXP indices) {
  return variable_bit_get_rvector(bin, indices, fread_BIN);
}

SEXP _redatam_pck_get_rvector(SEXP pck, SEXP indices) {
  return variable_bit_get_rvector(pck, indices, fread_PCK);
}

SEXP _redatam_chr_get_rvector(SEXP chr, SEXP indices) {
  return variable_byte_get_rvector(
      chr, indices,
      [](std::fstream &f, size_t instance_len) {
        std::string s = fread_fixed_string(f, instance_len);
        rtrim(s);
        return mkChar(s.c_str());
      },
      [](SEXP dst, int dst_index, SEXP s) {
        SET_STRING_ELT(dst, dst_index, s);
      },
      NA_STRING, STRSXP);
}

SEXP _redatam_int_get_rvector(SEXP intg, SEXP indices) {
  SEXP ans = variable_byte_get_rvector(
      intg, indices,
      [](std::fstream &f, int unused) { return fread_uint16_t(f); },
      SET_INTEGER_ELT, NA_INTEGER, INTSXP);
  configure_factor(ans, intg);
  return ans;
}

SEXP _redatam_lng_get_rvector(SEXP lng, SEXP indices) {
  SEXP ans = variable_byte_get_rvector(
      lng, indices,
      [](std::fstream &f, int unused) { return fread_uint32_t(f); },
      SET_INTEGER_ELT, NA_INTEGER, INTSXP);
  configure_factor(ans, lng);
  return ans;
}

SEXP _redatam_real_get_rvector(SEXP real, SEXP indices) {
  return variable_byte_get_rvector(
      real, indices,
      [](std::fstream &f, int unused) { return fread_double(f); }, SET_REAL_ELT,
      NA_REAL, REALSXP);
}
}
