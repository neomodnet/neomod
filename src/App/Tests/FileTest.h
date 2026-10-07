// Copyright (c) 2026, WH, All rights reserved.
#pragma once
#include "App.h"

namespace Mc::Tests {

// the file layer's writing (ByteBufferedFile::Writer): replacing a file, large writes, the outcome of a write
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
};

}  // namespace Mc::Tests
