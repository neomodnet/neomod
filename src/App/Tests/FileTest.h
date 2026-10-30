// Copyright (c) 2026, WH, All rights reserved.
#pragma once
#include "App.h"
#include "Registration.h"
#include "types.h"

#include <optional>
#include <string>
#include <vector>

namespace Mc::Tests {

// the file layer's writing (ByteBufferedFile::Writer): replacing a file, large writes, the outcome of a write; and a file
// dialog's answer (which comes on a later frame)
class FileTest : public App {
    NOCOPY_NOMOVE(FileTest)
   public:
    FileTest();
    ~FileTest() override = default;

    void update() override;

   private:
    int m_passes{0};
    int m_failures{0};
    bool m_done{false};
    Mc::Registration m_dialog;
    f64 m_dialogAsked{0.0};  // when (0: not yet)
    std::optional<std::vector<std::string>> m_dialogAnswer;
};

}  // namespace Mc::Tests
