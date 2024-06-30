#ifndef DATABLOCK_HPP
#define DATABLOCK_HPP

#include <vector>
#include <string>
#include <fstream>
#include <stdexcept>
#include <algorithm>
#include <cstring>
#include <cctype>
#include <sstream>
#include <iterator>

namespace RedatamLib {

class DataBlock {
public:
  std::vector<uint8_t> data;
  int n = 0;

  DataBlock(const std::string &path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
      throw std::runtime_error("Cannot open file: " + path);
    }

    file.seekg(0, std::ios::end);
    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);

    data.resize(size);
    if (!file.read(reinterpret_cast<char *>(data.data()), size)) {
      throw std::runtime_error("Error reading file: " + path);
    }
  }

  DataBlock(const std::vector<uint8_t> &bytes) : data(bytes) {}

  DataBlock() = default;

  DataBlock getPart(int prevStart, int iStart) const {
    if (prevStart < 0 || static_cast<size_t>(iStart) > data.size() ||
        prevStart >= iStart) {
      throw std::out_of_range("Invalid range for getPart");
    }
    return DataBlock(
        std::vector<uint8_t>(data.begin() + prevStart, data.begin() + iStart));
  }

  std::string toString() const { return std::string(data.begin(), data.end()); }

  std::vector<uint8_t> makeStringBlock(const std::string &str) const {
    std::vector<uint8_t> block;
    auto intSize = calcSize16(str);
    auto text = makeString(str);
    block.insert(block.end(), intSize.begin(), intSize.end());
    block.insert(block.end(), text.begin(), text.end());
    return block;
  }

  bool moveTo(const std::vector<uint8_t> &pattern) {
    auto it = std::search(data.begin() + n, data.end(), pattern.begin(),
                          pattern.end());
    if (it == data.end())
      return false;
    n = std::distance(data.begin(), it);
    return true;
  }

  std::string eatShortString() {
    int length = eat16int();
    if (length == 0xFFFF) {
      return eatString();
    }
    return eatChars(length);
  }

  std::string eatChars(int length) {
    std::string cad(data.begin() + n, data.begin() + n + length);
    n += length;
    return cad;
  }

  int eat32intInv() { return eat16int() + 0x10000 * eat16int(); }

  int eat32int() { return 0x10000 * eat16int() + eat16int(); }

  int eat16int() { return 0x1 * eatByte() + 0x100 * eatByte(); }

  uint8_t eatByte() { return data[n++]; }

  std::vector<int> GetAllMatches(const std::vector<uint8_t> &block) {
    std::vector<int> ret;
    int keepN = n;
    while (moveTo(block)) {
      ret.push_back(n);
      n++;
    }
    n = keepN;
    return ret;
  }

  bool IsText(const std::string &cad) const {
    for (char c : cad) {
      if ((c < 'a' || c > 'z') && c != ' ' && c != '-' && c != '_' &&
          (c < '0' || c > '9'))
        return false;
    }
    return true;
  }

  bool PlausibleString(std::string &cad, bool filterByContent = true) {
    int keepN = n;
    cad = "";
    if (n + 2 >= static_cast<int>(data.size()))
      return false;
    int length = eat16int();
    if (length < 0 || length > 128 || n + length > static_cast<int>(data.size())) {
      n = keepN;
      return false;
    }
    move(-2);
    cad = eatShortString();
    n = keepN;

    if (filterByContent == true && IsText(cad) == false)
      return false;
    return true;
  }

  bool eatPlausibleString(std::string &cad, bool filterByContent = true) {
    if (PlausibleString(cad, filterByContent) == false)
      return false;

    cad = eatShortString();
    return true;
  }

  void move(int i) { n += i; }

  int moveBackString(int maxLength = 65536) {
    move(-2);
    int offset = 0;
    while (offset < n) {
      std::vector<uint8_t> bytes(offset + 2);
      std::copy(data.begin() + n - offset,
                data.begin() + n - offset + offset + 2, bytes.begin());
      std::string text = std::string(bytes.begin(), bytes.end());
      if (static_cast<int>(text.length()) > maxLength)
        return -1;
      if (IsText(text))
        return offset;
      offset++;
    }
    return -1;
  }

  std::vector<uint8_t> addArrays(const std::vector<uint8_t> &a,
                                 const std::vector<uint8_t> &b) const {
    std::vector<uint8_t> result(a);
    result.insert(result.end(), b.begin(), b.end());
    return result;
  }

  std::vector<uint8_t> addArrays(const std::vector<uint8_t> &a,
                                 const std::vector<uint8_t> &b,
                                 const std::vector<uint8_t> &c) const {
    std::vector<uint8_t> result(a);
    result.insert(result.end(), b.begin(), b.end());
    result.insert(result.end(), c.begin(), c.end());
    return result;
  }

private:
  std::vector<uint8_t> calcSize16(int n) const {
    uint8_t n1 = static_cast<uint8_t>(n % 256);
    uint8_t n2 = static_cast<uint8_t>(n / 256);
    return {n1, n2};
  }

  std::vector<uint8_t> calcSize16(const std::string &cad) const {
    return calcSize16(cad.size());
  }

  std::vector<uint8_t> calcSize(const std::string &cad) const {
    int i = cad.size();
    return addArrays(calcSize16(i / 65536), calcSize16(i % 65536));
  }

  std::vector<uint8_t> makeString(const std::string &entity) const {
    return std::vector<uint8_t>(entity.begin(), entity.end());
  }

  std::string eatString() {
    int length = eat32intInv();
    return eatChars(length);
  }

  bool matches(const std::vector<uint8_t> &haystack,
               const std::vector<uint8_t> &needle, int offset) const {
    for (size_t i = 0; i < needle.size(); ++i) {
      if (needle[i] != haystack[offset + i])
        return false;
    }
    return true;
  }

  int FindPattern(const std::vector<uint8_t> &data,
                  const std::vector<uint8_t> &pattern, int start) const {
    for (size_t i = start; i <= data.size() - pattern.size(); ++i) {
      if (matches(data, pattern, i))
        return i;
    }
    return -1;
  }
};

} // namespace RedatamLib

#endif // DATABLOCK_HPP
