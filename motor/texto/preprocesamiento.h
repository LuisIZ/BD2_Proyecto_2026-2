#pragma once

#include <stdexcept>
#include <string>
#include <vector>

namespace motor {
namespace texto {

// minúsculas, sin puntuación, sin stopwords y con stemming
inline std::vector<std::string> tokenizar(const std::string& /*texto*/) {
    throw std::runtime_error("no implementado");
}

}  // namespace texto
}  // namespace motor
