#pragma once
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

// Minimal, dependency-free argument parser: `stego_cli <command> --option value --flag`.
// Options may also be written --option=value (needed when a value itself starts with "--").
namespace cli {

class UsageError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

struct CommandSpec {
    std::set<std::string> values;  // options that take a value
    std::set<std::string> flags;   // options without a value
};

const std::map<std::string, CommandSpec>& commandSpecs();

class Args {
public:
    std::string command;

    bool has(const std::string& name) const { return values_.count(name) || flags_.count(name); }
    bool flag(const std::string& name) const { return flags_.count(name) != 0; }
    std::string get(const std::string& name, const std::string& fallback = {}) const;
    std::string require(const std::string& name) const;  // throws UsageError when missing
    int getInt(const std::string& name, int fallback) const;  // throws UsageError if not an integer

private:
    friend Args parseArgs(const std::vector<std::string>& argv);
    std::map<std::string, std::string> values_;
    std::set<std::string> flags_;
};

// argv excludes the program name. No arguments -> command "help".
// Throws UsageError on unknown commands/options, missing values, duplicates, stray arguments.
Args parseArgs(const std::vector<std::string>& argv);

}  // namespace cli
