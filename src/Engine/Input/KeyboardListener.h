// Copyright (c) 2015, PG, All rights reserved.
#ifndef KEYBOARDLISTENER_H
#define KEYBOARDLISTENER_H

#include "KeyboardEvent.h"

class KeyboardListener {
   public:
    KeyboardListener() = default;
    virtual ~KeyboardListener() = default;

    KeyboardListener(const KeyboardListener &) = default;
    KeyboardListener &operator=(const KeyboardListener &) = default;
    KeyboardListener(KeyboardListener &&) = default;
    KeyboardListener &operator=(KeyboardListener &&) = default;

    virtual void onKeyDown(KeyboardEvent &e) = 0;
    virtual void onKeyUp(KeyboardEvent &e) = 0;
};

#endif
