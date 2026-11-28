// corun_shim/LittleFS.h -- an in-memory LittleFS for totalizer.h and queue.h.
//
// Files survive a simulated power cut (the test keeps this store and builds a
// new board on top of it), exactly as flash does. `capBytes` makes the
// partition fill: a write that does not fit writes nothing and returns 0, as a
// full LittleFS does. Each write lands whole; torn writes are
// test_queue_fault_injection.cpp's subject, not this one's.
//
// COPY-ON-WRITE, like LittleFS: a file opened with "w" keeps its old content
// until close(). The new content needs its OWN free space while the old copy
// still exists, and a rewrite that was refused anywhere is discarded at
// close(), leaving the old copy. So when the partition is full, a small file
// that is rewritten in place (the totalizer checkpoint, the ack cursor) stays
// at its last successful version -- stale, never empty.
//
// POSIX truncate() -- which LittleFsBackend calls with a "/littlefs" prefix --
// is defined by the test (one definition, one translation unit) and lands on
// shimTruncate() below.
#pragma once
#include <stdint.h>
#include <string.h>

#include <map>
#include <set>
#include <string>
#include <vector>

#include <Arduino.h>

#define FILE_READ "r"
#define FILE_WRITE "w"
#define FILE_APPEND "a"

struct ShimFs {
  std::map<std::string, std::vector<uint8_t> > files;
  std::set<std::string> dirs;
  size_t capBytes = 3 * 1024 * 1024 + 512 * 1024;  // default_16MB.csv: a 3.5 MB data partition
  uint64_t writes = 0;
  uint64_t refusedWrites = 0;
  size_t used() const {
    size_t n = 0;
    for (std::map<std::string, std::vector<uint8_t> >::const_iterator it = files.begin();
         it != files.end(); ++it)
      n += it->second.size();
    return n;
  }
};
inline ShimFs& shimFs() {
  static ShimFs fs;
  return fs;
}

inline bool shimTruncate(const char* path, size_t len) {
  std::map<std::string, std::vector<uint8_t> >::iterator it = shimFs().files.find(path);
  if (it == shimFs().files.end()) return false;
  if (len < it->second.size()) it->second.resize(len);
  return true;
}

class File {
 public:
  File() {}
  File(const std::string& path, bool dir, bool cow) : File(path, dir) { cow_ = cow; }
  File(const std::string& path, bool dir) : path_(path), open_(true), dir_(dir) {
    size_t slash = path.find_last_of('/');
    name_ = slash == std::string::npos ? path : path.substr(slash + 1);
    if (dir) {
      std::string prefix = path + "/";
      for (std::map<std::string, std::vector<uint8_t> >::iterator it = shimFs().files.begin();
           it != shimFs().files.end(); ++it)
        if (it->first.compare(0, prefix.size(), prefix) == 0 &&
            it->first.find('/', prefix.size()) == std::string::npos)
          listing_.push_back(it->first);
    }
  }
  explicit operator bool() const { return open_; }
  size_t size() const {
    std::map<std::string, std::vector<uint8_t> >::iterator it = shimFs().files.find(path_);
    return it == shimFs().files.end() ? 0 : it->second.size();
  }
  size_t read(uint8_t* out, size_t n) {
    std::vector<uint8_t>& v = shimFs().files[path_];
    size_t k = pos_ < v.size() ? v.size() - pos_ : 0;
    if (k > n) k = n;
    if (k) memcpy(out, v.data() + pos_, k);
    pos_ += k;
    return k;
  }
  size_t write(const uint8_t* data, size_t n) {
    if (cow_) {
      if (shimFs().used() + pending_.size() + n > shimFs().capBytes) {
        shimFs().refusedWrites++;
        failed_ = true;
        return 0;
      }
      pending_.insert(pending_.end(), data, data + n);
      shimFs().writes++;
      return n;
    }
    if (shimFs().used() + n > shimFs().capBytes) {
      shimFs().refusedWrites++;
      return 0;
    }
    std::vector<uint8_t>& v = shimFs().files[path_];
    v.insert(v.end(), data, data + n);
    shimFs().writes++;
    pos_ = v.size();
    return n;
  }
  bool seek(size_t pos) {
    if (pos > size()) return false;
    pos_ = pos;
    return true;
  }
  int available() const { return (int)(size() - (pos_ < size() ? pos_ : size())); }
  void flush() {}
  void close() {
    if (open_ && cow_ && !failed_) shimFs().files[path_] = pending_;
    open_ = false;
  }
  bool isDirectory() const { return dir_; }
  const char* name() const { return name_.c_str(); }
  File openNextFile() {
    if (li_ >= listing_.size()) return File();
    return File(listing_[li_++], false);
  }

 private:
  std::string path_;
  std::string name_;
  bool open_ = false;
  bool dir_ = false;
  size_t pos_ = 0;
  std::vector<std::string> listing_;
  size_t li_ = 0;
  bool cow_ = false;
  bool failed_ = false;
  std::vector<uint8_t> pending_;
};

struct LittleFSShim {
  File open(const char* path, const char* mode = FILE_READ) {
    std::string p(path);
    if (mode[0] == 'r' && shimFs().dirs.count(p)) return File(p, true);
    if (mode[0] == 'w') {
      shimFs().files[p];  // exists from now; its content changes only at close()
      return File(p, false, true);
    } else if (mode[0] == 'a') {
      shimFs().files[p];  // create if absent, never truncate
    } else if (!shimFs().files.count(p)) {
      return File();
    }
    return File(p, false);
  }
  File open(const String& path, const char* mode = FILE_READ) { return open(path.c_str(), mode); }
  bool remove(const String& path) { return remove(path.c_str()); }
  bool exists(const String& path) { return exists(path.c_str()); }
  bool exists(const char* path) {
    return shimFs().files.count(path) > 0 || shimFs().dirs.count(path) > 0;
  }
  bool remove(const char* path) { return shimFs().files.erase(path) > 0; }
  bool mkdir(const char* path) {
    shimFs().dirs.insert(path);
    return true;
  }
  bool rename(const char* from, const char* to) {
    std::map<std::string, std::vector<uint8_t> >::iterator it = shimFs().files.find(from);
    if (it == shimFs().files.end()) return false;
    shimFs().files[to] = it->second;
    shimFs().files.erase(from);
    return true;
  }
  size_t totalBytes() { return shimFs().capBytes; }
  size_t usedBytes() { return shimFs().used(); }
};
static LittleFSShim LittleFS;
