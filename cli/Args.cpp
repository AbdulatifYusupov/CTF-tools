#include "Args.hpp"

#include <optional>

namespace cli {

const std::map<std::string, CommandSpec>& commandSpecs() {
    static const std::map<std::string, CommandSpec> specs = {
        {"help", {{}, {}}},
        {"keygen", {{"out"}, {"force"}}},
        {"capacity", {{"algo", "in", "mode", "bits", "threshold", "pairs", "parity"}, {}}},
        {"embed",
         {{"algo", "in", "out", "pub", "password", "password-file", "msg", "file", "bits", "threshold", "strength",
           "pairs", "parity"},
          {"progress", "ask-password"}}},
        {"extract",
         {{"algo", "in", "key", "password", "password-file", "out", "bits", "threshold", "pairs", "parity"},
          {"progress", "ask-password"}}},
        {"analyze", {{"cover", "stego", "lsb-out", "diff-out"}, {}}},
    };
    return specs;
}

std::string Args::get(const std::string& name, const std::string& fallback) const {
    const auto it = values_.find(name);
    return it == values_.end() ? fallback : it->second;
}

std::string Args::require(const std::string& name) const {
    const auto it = values_.find(name);
    if (it == values_.end()) throw UsageError("missing required option --" + name);
    return it->second;
}

int Args::getInt(const std::string& name, int fallback) const {
    const auto it = values_.find(name);
    if (it == values_.end()) return fallback;
    const std::string& s = it->second;
    size_t used = 0;
    int v = 0;
    try {
        v = std::stoi(s, &used);
    } catch (...) {
        throw UsageError("--" + name + " must be an integer, got '" + s + "'");
    }
    if (used != s.size()) throw UsageError("--" + name + " must be an integer, got '" + s + "'");
    return v;
}

Args parseArgs(const std::vector<std::string>& argv) {
    Args a;
    if (argv.empty()) {
        a.command = "help";
        return a;
    }
    a.command = argv[0];
    if (a.command == "--help" || a.command == "-h") a.command = "help";

    const auto& specs = commandSpecs();
    const auto it     = specs.find(a.command);
    if (it == specs.end()) throw UsageError("unknown command '" + a.command + "'");
    const CommandSpec& spec = it->second;

    for (size_t i = 1; i < argv.size(); ++i) {
        const std::string& tok = argv[i];
        if (tok.rfind("--", 0) != 0 || tok.size() == 2) throw UsageError("unexpected argument '" + tok + "'");

        std::string name = tok.substr(2);
        std::optional<std::string> inlineValue;
        if (const auto eq = name.find('='); eq != std::string::npos) {
            inlineValue = name.substr(eq + 1);
            name.resize(eq);
        }

        const bool isFlag  = spec.flags.count(name) != 0;
        const bool isValue = spec.values.count(name) != 0;
        if (!isFlag && !isValue) throw UsageError("unknown option --" + name + " for '" + a.command + "'");
        if (a.has(name)) throw UsageError("option --" + name + " given twice");

        if (isFlag) {
            if (inlineValue) throw UsageError("option --" + name + " takes no value");
            a.flags_.insert(name);
        } else if (inlineValue) {
            a.values_[name] = *inlineValue;
        } else {
            if (i + 1 >= argv.size() || argv[i + 1].rfind("--", 0) == 0)
                throw UsageError("option --" + name + " needs a value (write --" + name +
                                 "=VALUE if the value starts with --)");
            a.values_[name] = argv[++i];
        }
    }
    return a;
}

}  // namespace cli
